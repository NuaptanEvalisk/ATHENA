/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "decision_feed.hpp"
#include "control_http.hpp"
#include "sqlite_internal.hpp"
#include <QCryptographicHash>
#include <QThread>
#include <QTimer>
#include <nlohmann/json.hpp>
#include <sodium.h>
#include <set>
#include <limits>

namespace athena::hodarium {
namespace {
using json= nlohmann::json;
using namespace detail;
constexpr std::size_t page_budget= 512*1024;
void require (bool value, const char* diagnostic) {
  if (!value) throw std::invalid_argument (diagnostic);
}
std::string base64 (const QByteArray& bytes) {
  return bytes.toBase64 (QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals).toStdString ();
}
QByteArray decode (const std::string& value) {
  require (value.size () <= page_budget, "Decision feed encoding exceeds budget");
  auto bytes= QByteArray::fromBase64 (QByteArray::fromStdString (value),
    QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
  require (base64 (bytes) == value, "Invalid decision feed base64");
  return bytes;
}
void identifier (const std::string& value) {
  require (value.size () == 43 && decode (value).size () == 32, "Invalid decision feed identity");
}
json parse (const std::string& bytes) {
  require (bytes.size () <= page_budget, "Decision feed exceeds budget");
  std::vector<std::set<std::string>> keys;
  return json::parse (bytes, [&] (int depth, json::parse_event_t event, json& item) {
    require (depth <= 12, "Decision feed exceeds depth budget");
    if (event == json::parse_event_t::object_start) keys.emplace_back ();
    else if (event == json::parse_event_t::object_end) keys.pop_back ();
    else if (event == json::parse_event_t::key)
      require (keys.back ().insert (item.get<std::string> ()).second, "Duplicate decision feed field");
    return true;
  });
}
std::int64_t counter (const json& value) {
  require (value.is_number_integer () && value >= 0 && value <= INT64_MAX,
           "Invalid decision feed counter");
  return value.get<std::int64_t> ();
}
json evidence_json (const decision_evidence& value) {
  return {{"envelope",value.envelope},{"epoch",value.epoch},{"nonce",value.nonce},{"subject",value.subject}};
}
decision_feed_record read_record (const authority_pin& pin, std::int64_t sequence,
                                  decision_evidence evidence) {
  require (evidence.envelope.size () <= 16384, "Decision feed record exceeds budget");
  auto envelope= parse (evidence.envelope);
  auto payload= parse (decode (envelope.at ("payload")).toStdString ());
  const auto& record= payload.at ("decision");
  auto verified= verify_decision (pin,evidence.envelope,evidence.epoch,evidence.nonce,evidence.subject,
                                  record.at ("vault"),record.at ("conflict"));
  require (verified.has_value (), "Decision feed contains absent decision");
  require (verified->version == 1,"Unsupported historical re-adjudication in decision feed; explicit repair required");
  return {sequence,*verified,std::move (evidence)};
}
decision_feed_record persisted_record (const authority_pin& pin, statement& row) {
  auto value= parse (row.bytes (1));
  return read_record (pin,sqlite3_column_int64 (row.value,0),
    {value.at ("envelope"),value.at ("epoch"),value.at ("nonce"),value.at ("subject")});
}
struct feed_page {
  std::int64_t after,next,watermark;
  bool more;
  std::vector<decision_feed_record> entries;
};
feed_page verify_page (const authority_pin& pin,const decision_evidence& evidence,
                      std::int64_t after,std::uint32_t limit) {
  identifier (evidence.epoch); identifier (evidence.nonce); identifier (evidence.subject);
  auto envelope= parse (evidence.envelope);
  auto payload= decode (envelope.at ("payload"));
  auto signature= decode (envelope.at ("signature")); auto key= decode (pin.public_key);
  require (signature.size () == crypto_sign_BYTES && key.size () == crypto_sign_PUBLICKEYBYTES,
           "Invalid decision feed signature size");
  constexpr char domain[]= "ATHENA-HODARIUM-DECISION-FEED-v1";
  auto signed_bytes= std::string (domain,sizeof domain) + payload.toStdString ();
  require (crypto_sign_verify_detached (reinterpret_cast<const unsigned char*> (signature.constData ()),
    reinterpret_cast<const unsigned char*> (signed_bytes.data ()),signed_bytes.size (),
    reinterpret_cast<const unsigned char*> (key.constData ())) == 0,"Invalid decision feed signature");
  auto page= parse (payload.toStdString ());
  require (page.at ("protocol") == 1 && page.at ("group") == pin.group &&
    page.at ("generation") == pin.generation && page.at ("epoch") == evidence.epoch &&
    page.at ("challenge") == evidence.nonce && page.at ("subject") == evidence.subject,
    "Decision feed does not match current membership request");
  feed_page result{counter (page.at ("after")),counter (page.at ("next")),counter (page.at ("watermark")),
                   page.at ("more").get<bool> (),{}};
  require (result.after == after && result.next >= after && result.watermark >= result.next &&
           result.more == (result.next < result.watermark),"Invalid decision feed cursor transition");
  const auto& entries= page.at ("entries");
  require (entries.is_array () && entries.size () <= limit,"Invalid decision feed page length");
  auto previous= after;
  for (const auto& item: entries) {
    auto sequence= counter (item.at ("sequence"));
    require (sequence > previous && sequence <= result.next,"Decision feed sequence is not increasing");
    result.entries.push_back (read_record (pin,sequence,
      {item.at ("receipt").dump (),evidence.epoch,evidence.nonce,evidence.subject}));
    previous= sequence;
  }
  require (previous == result.next && (!entries.empty () || !result.more),"Decision feed skipped records");
  return result;
}
QByteArray encode (const json& value) { return QByteArray::fromStdString (value.dump ()); }
}

decision_feed_store::decision_feed_store (const std::filesystem::path& path,authority_pin pin): pin_ (std::move (pin)) {
  identifier (pin_.group);identifier (pin_.generation);identifier (pin_.public_key);
  if (sodium_init () < 0) throw std::runtime_error ("Cannot initialize decision feed verifier");
  try {
    check (db_,sqlite3_open_v2 (path.string ().c_str (),&db_,SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX,nullptr));
    check (db_,sqlite3_busy_timeout (db_,5000));
    { statement version (db_,"PRAGMA user_version");
      require (version.row () && sqlite3_column_int (version.value,0) <= 1,"Unsupported decision feed database"); }
    sql (db_,"PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL;");
    transaction tx (db_);
    sql (db_,"CREATE TABLE IF NOT EXISTS feed_state(singleton INTEGER PRIMARY KEY CHECK(singleton=1),"
      "pin TEXT NOT NULL,cursor INTEGER NOT NULL,watermark INTEGER NOT NULL,initialized INTEGER NOT NULL);"
      "CREATE TABLE IF NOT EXISTS feed_latest(vault TEXT NOT NULL,conflict TEXT NOT NULL,version INTEGER NOT NULL,"
      "sequence INTEGER NOT NULL UNIQUE,evidence TEXT NOT NULL,handled INTEGER NOT NULL,PRIMARY KEY(vault,conflict));"
      "PRAGMA user_version=1;");
    auto binding= json::array ({pin_.group,pin_.generation,pin_.public_key}).dump ();
    statement existing (db_,"SELECT pin FROM feed_state WHERE singleton=1");
    if (existing.row ()) require (existing.bytes (0) == binding,"Decision feed authority changed");
    else { statement insert (db_,"INSERT INTO feed_state VALUES(1,?,0,0,0)");insert.text (1,binding);insert.row (); }
    tx.commit ();
  } catch (...) { sqlite3_close (db_);db_= nullptr;throw; }
}
decision_feed_store::~decision_feed_store () { sqlite3_close (db_); }
std::int64_t decision_feed_store::cursor () const {
  statement row (db_,"SELECT cursor FROM feed_state WHERE singleton=1");row.row ();
  return sqlite3_column_int64 (row.value,0);
}
bool decision_feed_store::caught_up () const {
  statement row (db_,"SELECT initialized AND cursor=watermark FROM feed_state WHERE singleton=1");row.row ();
  return sqlite3_column_int (row.value,0) != 0;
}
std::optional<decision_feed_record> decision_feed_store::latest (const std::string& vault,const std::string& conflict) const {
  statement row (db_,"SELECT sequence,evidence FROM feed_latest WHERE vault=? AND conflict=?");
  row.text (1,vault);row.text (2,conflict);
  if (!row.row ()) return {};
  return persisted_record (pin_,row);
}
bool decision_feed_store::accept (const decision_evidence& evidence,std::int64_t after,std::uint32_t limit) {
  require (after >= 0 && limit > 0 && limit <= 64,"Invalid decision feed page request");
  auto page= verify_page (pin_,evidence,after,limit);
  transaction tx (db_);
  require (cursor () == after,"Decision feed cursor changed during request");
  { statement previous (db_,"SELECT watermark FROM feed_state WHERE singleton=1");previous.row ();
    require (page.watermark >= sqlite3_column_int64 (previous.value,0),"Decision feed authority rolled back"); }
  std::int64_t records= 0;
  { statement count (db_,"SELECT COUNT(*) FROM feed_latest");count.row ();records= sqlite3_column_int64 (count.value,0); }
  for (const auto& entry: page.entries) {
    const auto& d= entry.decision;
    auto previous= latest (d.vault,d.conflict);
    if (previous) {
      throw std::invalid_argument ("Immutable decision scope appeared again in the feed");
    }
    else require (++records <= 100000,"Decision feed persistent record budget exceeded");
    statement update (db_,"INSERT INTO feed_latest VALUES(?,?,?,?,?,0) ON CONFLICT(vault,conflict) DO UPDATE SET "
      "version=excluded.version,sequence=excluded.sequence,evidence=excluded.evidence,handled=0");
    update.text (1,d.vault);update.text (2,d.conflict);
    check (db_,sqlite3_bind_int64 (update.value,3,d.version));
    check (db_,sqlite3_bind_int64 (update.value,4,entry.sequence));
    update.text (5,evidence_json (entry.evidence).dump ());update.row ();
  }
  statement update (db_,"UPDATE feed_state SET cursor=?,watermark=?,initialized=1 WHERE singleton=1");
  check (db_,sqlite3_bind_int64 (update.value,1,page.next));
  check (db_,sqlite3_bind_int64 (update.value,2,page.watermark));update.row ();
  tx.commit ();return page.more;
}
std::vector<decision_feed_record> decision_feed_store::pending (std::int64_t after,std::uint32_t limit) const {
  require (after >= 0 && limit > 0 && limit <= 256,"Invalid decision feed pending page");
  statement row (db_,"SELECT sequence,evidence FROM feed_latest WHERE handled=0 AND sequence>? ORDER BY sequence LIMIT ?");
  check (db_,sqlite3_bind_int64 (row.value,1,after));check (db_,sqlite3_bind_int (row.value,2,limit));
  std::vector<decision_feed_record> result;
  while (row.row ()) result.push_back (persisted_record (pin_,row));
  return result;
}
void decision_feed_store::acknowledge (const std::string& vault,const std::string& conflict,std::int64_t version) {
  statement update (db_,"UPDATE feed_latest SET handled=1 WHERE vault=? AND conflict=? AND version=?");
  update.text (1,vault);update.text (2,conflict);check (db_,sqlite3_bind_int64 (update.value,3,version));update.row ();
}

decision_feed_client::decision_feed_client (QUrl origin,authority_pin pin,device_identity device,
  std::string member,decision_feed_store& store): pin_ (std::move (pin)),device_ (std::move (device)),
  member_ (std::move (member)),store_ (store),http_ (new control_http (std::move (origin),this)),deadline_ (new QTimer (this)) {
  identifier (pin_.group);identifier (pin_.generation);identifier (pin_.public_key);identifier (member_);
  deadline_->setSingleShot (true);
  connect (deadline_,&QTimer::timeout,this,[this] {
    http_->cancel ();finish ({control_failure::transport,"Decision feed timed out; cursor retained"});
  });
}
decision_feed_client::~decision_feed_client () { http_->cancel (); }
void decision_feed_client::finish (control_result result,bool more) {
  deadline_->stop ();auto callback= std::move (completed_);completed_= {};authorized_= {};
  if (callback) callback (std::move (result),more);
}
void decision_feed_client::cancel () {
  if (QThread::currentThread () != thread ()) throw std::logic_error ("Decision feed accessed outside its owner");
  http_->cancel ();finish ({control_failure::cancelled,"Decision feed cancelled; cursor retained"});
}
void decision_feed_client::post (const QString& path,const QByteArray& bytes,std::function<void (QByteArray)> success) {
  if (!authorized_ (epoch_)) { finish ({control_failure::denied,"Decision feed membership changed"});return; }
  http_->request (path,bytes,[this,success= std::move (success)] (control_response response) {
    if (!response.error.empty ()) {
      auto failure= response.status == 401 || response.status == 403 ? control_failure::denied :
        response.status == 409 || response.oversized ? control_failure::invalid_state : control_failure::transport;
      finish ({failure,std::move (response.error)});return;
    }
    try {
      if (!authorized_ (epoch_)) { finish ({control_failure::denied,"Decision feed membership changed"});return; }
      success (std::move (response.body));
    } catch (const key_store_error& e) { finish ({control_failure::key_store,e.what ()}); }
      catch (const std::exception& e) { finish ({control_failure::invalid_state,e.what ()}); }
  });
}
void decision_feed_client::refresh (std::string epoch,authorization authorized,completion completed) {
  if (QThread::currentThread () != thread () || completed_)
    throw std::logic_error ("Decision feed owner or request state violation");
  if (!authorized || !completed) throw std::invalid_argument ("Decision feed requires owner callbacks");
  identifier (epoch);
  auto after= store_.cursor ();
  auto payload= encode ({{"member",member_},{"generation",pin_.generation},{"epoch",epoch},{"after",after},{"limit",64}});
  auto subject= base64 (QCryptographicHash::hash (payload,QCryptographicHash::Sha256));
  epoch_= std::move (epoch);authorized_= std::move (authorized);completed_= std::move (completed);deadline_->start (30000);
  post ("/api/device/challenge",encode ({{"purpose","decision-feed"},{"subject",subject}}),
    [this,payload,subject,after] (QByteArray bytes) {
      require (bytes.size () <= 1024,"Decision feed challenge exceeds budget");
      auto challenge= parse (bytes.toStdString ());std::string nonce= challenge.at ("challenge");identifier (nonce);
      auto signature= sign_device_proof (device_,pin_.group,"decision-feed",subject,nonce);
      post ("/api/device/decision-feed",encode ({{"payload",base64 (payload)},{"challenge",nonce},{"signature",signature}}),
        [this,nonce,subject,after] (QByteArray bytes) {
          bool more= store_.accept ({bytes.toStdString (),epoch_,nonce,subject},after);
          finish ({},more);
        });
    });
}
} // namespace athena::hodarium

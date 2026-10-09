/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "membership.hpp"
#include "sqlite_internal.hpp"

#include <nlohmann/json.hpp>
#include <sodium.h>
#include <algorithm>
#include <set>
#include <stdexcept>
#include <string_view>

namespace athena::hodarium {
namespace {
using json= nlohmann::json;
constexpr std::size_t budget= 8*1024*1024;
constexpr auto maximum_offline= std::chrono::hours (24);

void require (bool condition, const char* message) {
  if (!condition) throw std::invalid_argument (message);
}
std::string decode (const std::string& text) {
  require (text.size () <= budget, "Hodarium base64 exceeds budget");
  std::string bytes (text.size (), '\0');
  std::size_t size= 0;
  require (sodium_base642bin (reinterpret_cast<unsigned char*> (bytes.data ()),
    bytes.size (), text.data (), text.size (), nullptr, &size, nullptr,
    sodium_base64_VARIANT_URLSAFE_NO_PADDING) == 0, "Invalid Hodarium base64");
  bytes.resize (size);
  std::string canonical (sodium_base64_ENCODED_LEN (size,
    sodium_base64_VARIANT_URLSAFE_NO_PADDING), '\0');
  sodium_bin2base64 (canonical.data (), canonical.size (),
    reinterpret_cast<const unsigned char*> (bytes.data ()), size,
    sodium_base64_VARIANT_URLSAFE_NO_PADDING);
  canonical.resize (canonical.size () - 1);
  require (text == canonical, "Noncanonical Hodarium base64");
  return bytes;
}
void identity (const std::string& text) {
  require (text.size () == 43 && decode (text).size () == 32,
           "Invalid Hodarium identity");
}
json parse (const std::string& text) {
  require (text.size () <= budget, "Hodarium control message exceeds budget");
  // Reject duplicate keys and deep input, rather than interpreting ambiguous
  // signed control data differently from the authority or another client.
  std::vector<std::set<std::string>> keys;
  return json::parse (text, [&] (int depth, json::parse_event_t event, json& value) {
    require (depth <= 16, "Hodarium control message exceeds depth budget");
    if (event == json::parse_event_t::object_start) keys.emplace_back ();
    else if (event == json::parse_event_t::object_end) keys.pop_back ();
    else if (event == json::parse_event_t::key)
      require (keys.back ().insert (value.get<std::string> ()).second,
               "Duplicate Hodarium control field");
    return true;
  });
}
void verify (const authority_pin& pin, const std::string& message,
             const std::string& signature) {
  static const int initialized= sodium_init ();
  require (initialized >= 0, "Cannot initialize libsodium");
  auto key= decode (pin.public_key), sig= decode (signature);
  require (key.size () == crypto_sign_PUBLICKEYBYTES &&
           sig.size () == crypto_sign_BYTES, "Invalid Hodarium signature size");
  require (crypto_sign_verify_detached (
    reinterpret_cast<const unsigned char*> (sig.data ()),
    reinterpret_cast<const unsigned char*> (message.data ()), message.size (),
    reinterpret_cast<const unsigned char*> (key.data ())) == 0,
    "Invalid Hodarium authority signature");
}
membership_state state_from (const authority_pin& pin, const json& envelope) {
  auto payload= decode (envelope.at ("payload").get<std::string> ());
  constexpr char domain[]= "ATHENA-HODARIUM-STATE-v1";
  verify (pin, std::string (domain, sizeof domain) + payload,
          envelope.at ("signature").get<std::string> ());
  auto value= parse (payload);
  require (value.at ("protocol") == 1, "Unsupported Hodarium protocol");
  membership_state state;
  state.group= value.at ("group"); state.generation= value.at ("generation");
  state.epoch= value.at ("epoch");
  identity (state.group); identity (state.generation); identity (state.epoch);
  require (state.group == pin.group && state.generation == pin.generation,
           "Hodarium authority requires explicit trust re-establishment");
  const auto& revision= value.at ("revision");
  require (revision.is_number_integer () && revision > 0 &&
    revision <= INT64_MAX, "Invalid Hodarium membership revision");
  state.revision= revision.get<std::int64_t> ();
  const auto& members= value.at ("members");
  require (members.is_array () && members.size () <= 4096,
           "Invalid Hodarium member list");
  std::set<std::string> ids, public_keys;
  for (const auto& item: members) {
    member_identity member{item.at ("id"), item.at ("name"), item.at ("public_key")};
    identity (member.id); identity (member.public_key);
    require (!member.name.empty () && member.name.size () <= 128 &&
      ids.insert (member.id).second && public_keys.insert (member.public_key).second,
      "Invalid or duplicate Hodarium member");
    state.members.push_back (std::move (member));
  }
  return state;
}
}

membership_state verify_membership (const authority_pin& pin,
                                    const std::string& envelope) {
  return state_from (pin, parse (envelope));
}

membership_validation verify_validation (const authority_pin& pin,
  const std::string& response, const std::string& nonce,
  const std::string& member, const std::string& public_key) {
  identity (nonce); identity (member); identity (public_key);
  auto value= parse (response);
  require (value.at ("challenge") == nonce && value.at ("member") == member,
           "Hodarium validation does not match the pending request");
  const auto& seconds= value.at ("max_offline_seconds");
  require (seconds.is_number_integer () && seconds > 0 && seconds <= 86400,
           "Invalid Hodarium offline interval");
  const auto& envelope= value.at ("state");
  // Go's SignedState encodes these fields in this order. The signed array has
  // only ASCII strings/numbers; no escaping or Unicode canonicalization varies.
  json signed_envelope{{"payload", envelope.at ("payload")},
                       {"signature", envelope.at ("signature")}};
  auto state= state_from (pin, signed_envelope);
  auto message= json::array ({"ATHENA-HODARIUM-VALIDATION-v1", pin.group,
    signed_envelope, member, nonce, seconds}).dump ();
  verify (pin, message, value.at ("signature"));
  auto own= std::find_if (state.members.begin (), state.members.end (),
    [&] (const member_identity& m) { return m.id == member && m.public_key == public_key; });
  require (own != state.members.end (), "Device is not an active Hodarium member");
  return {std::move (state), std::chrono::seconds (seconds.get<int> ())};
}

std::optional<conflict_decision> verify_decision (const authority_pin& pin,
  const std::string& response, const std::string& epoch,
  const std::string& nonce, const std::string& subject,
  const std::string& vault, const std::string& conflict) {
  identity (epoch); identity (nonce); identity (subject);
  identity (vault); identity (conflict);
  require (response.size () <= 16384, "Hodarium decision exceeds budget");
  auto envelope= parse (response);
  auto payload= decode (envelope.at ("payload").get<std::string> ());
  constexpr char domain[]= "ATHENA-HODARIUM-DECISION-v1";
  verify (pin, std::string (domain, sizeof domain) + payload,
          envelope.at ("signature").get<std::string> ());
  auto value= parse (payload);
  require (value.at ("protocol") == 1 && value.at ("group") == pin.group &&
    value.at ("generation") == pin.generation && value.at ("epoch") == epoch &&
    value.at ("challenge") == nonce && value.at ("subject") == subject,
    "Hodarium decision does not match current request and membership");
  const auto& record= value.at ("decision");
  if (record.is_null ()) return std::nullopt;
  conflict_decision result;
  result.vault= record.at ("vault"); result.conflict= record.at ("conflict");
  result.request= record.at ("request_id"); result.branches= record.at ("branches");
  result.resolution= record.at ("resolution"); result.member= record.at ("member");
  result.generation= record.at ("generation"); result.epoch= record.at ("epoch");
  for (const auto* id: {&result.vault, &result.conflict, &result.request,
      &result.branches, &result.resolution, &result.member, &result.generation, &result.epoch})
    identity (*id);
  require (result.vault == vault && result.conflict == conflict,
    "Hodarium decision refers to a different conflict");
  for (const auto* field: {"version", "created"})
    require (record.at (field).is_number_integer () && record.at (field) > 0 &&
      record.at (field) <= INT64_MAX, "Invalid Hodarium decision counter or timestamp");
  result.version= record.at ("version").get<std::int64_t> ();
  result.created= record.at ("created").get<std::int64_t> ();
  // Record provenance may predate the current epoch or explicit recovery.
  // Freshness is attested by the enclosing challenge-bound statement.
  return result;
}

std::optional<vault_secret_registration> verify_vault_registration (const authority_pin& pin,
  const std::string& response, const std::string& epoch, const std::string& nonce,
  const std::string& subject, const std::string& slot) {
  identity (epoch); identity (nonce); identity (subject); identity (slot);
  require (response.size () <= 16384, "Hodarium Vault registration exceeds budget");
  auto envelope= parse (response);
  auto payload= decode (envelope.at ("payload").get<std::string> ());
  constexpr char domain[]= "ATHENA-HODARIUM-VAULT-SECRET-v1";
  verify (pin, std::string (domain, sizeof domain) + payload, envelope.at ("signature"));
  auto value= parse (payload);
  require (value.at ("protocol") == 1 && value.at ("group") == pin.group &&
    value.at ("generation") == pin.generation && value.at ("epoch") == epoch &&
    value.at ("challenge") == nonce && value.at ("subject") == subject && value.at ("slot") == slot,
    "Hodarium Vault registration does not match current request and membership");
  const auto& record= value.at ("registration");
  if (record.is_null ()) return std::nullopt;
  vault_secret_registration result{record.at ("commitment"), record.at ("member"), 0};
  identity (result.commitment); identity (result.member);
  const auto& created= record.at ("created");
  require (created.is_number_integer () && created > 0 && created <= INT64_MAX,
    "Invalid Hodarium Vault registration timestamp");
  result.created= created.get<std::int64_t> ();
  return result;
}

membership_store::membership_store (const std::filesystem::path& database,
  authority_pin pin): pin_ (std::move (pin)) {
  using namespace detail;
  identity (pin_.group); identity (pin_.generation); identity (pin_.public_key);
  try {
    check (db_, sqlite3_open_v2 (database.string ().c_str (), &db_,
      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, nullptr));
    check (db_, sqlite3_busy_timeout (db_, 5000));
    int version;
    {
      statement st (db_, "PRAGMA user_version"); st.row ();
      version= sqlite3_column_int (st.value, 0);
    }
    require (version <= 1, "Unsupported Hodarium membership store version");
    sql (db_, "PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL;");
    transaction tx (db_);
    if (version == 0) {
      sql (db_, "CREATE TABLE trust(singleton INTEGER PRIMARY KEY CHECK(singleton=1),"
        "hodarium TEXT NOT NULL,authority TEXT NOT NULL,generation TEXT NOT NULL);"
        "CREATE TABLE publications(revision INTEGER PRIMARY KEY,epoch TEXT UNIQUE NOT NULL,"
        "payload BLOB NOT NULL,envelope TEXT NOT NULL); PRAGMA user_version=1;");
      statement insert (db_, "INSERT INTO trust VALUES(1,?,?,?)");
      insert.text (1, pin_.group); insert.text (2, pin_.public_key);
      insert.text (3, pin_.generation); insert.row ();
    }
    statement stored (db_, "SELECT hodarium,authority,generation FROM trust WHERE singleton=1");
    require (stored.row () && stored.bytes (0) == pin_.group &&
      stored.bytes (1) == pin_.public_key && stored.bytes (2) == pin_.generation,
      "Hodarium stored trust does not match enrollment");
    tx.commit ();
  }
  catch (...) { sqlite3_close (db_); db_= nullptr; throw; }
}
membership_store::~membership_store () { sqlite3_close (db_); }

membership_state membership_store::accept (const std::string& envelope) {
  using namespace detail;
  auto state= verify_membership (pin_, envelope);
  auto payload= decode (parse (envelope).at ("payload").get<std::string> ());
  transaction tx (db_);
  {
    statement previous (db_, "SELECT revision,payload FROM publications ORDER BY revision DESC LIMIT 1");
    if (previous.row ()) {
      auto revision= sqlite3_column_int64 (previous.value, 0);
      require (state.revision >= revision, "Hodarium membership rollback rejected");
      if (state.revision == revision) {
        require (payload == previous.bytes (1), "Conflicting Hodarium membership publication");
        tx.commit ();
        return state;
      }
    }
  }
  {
    statement epoch (db_, "SELECT 1 FROM publications WHERE epoch=?");
    epoch.text (1, state.epoch);
    require (!epoch.row (), "Hodarium epoch reuse rejected");
  }
  statement insert (db_, "INSERT INTO publications VALUES(?,?,?,?)");
  check (db_, sqlite3_bind_int64 (insert.value, 1, state.revision));
  insert.text (2, state.epoch); insert.blob (3, payload);
  insert.text (4, envelope); insert.row ();
  tx.commit ();
  return state;
}
std::optional<membership_state> membership_store::current () const {
  detail::statement st (db_, "SELECT envelope FROM publications ORDER BY revision DESC LIMIT 1");
  if (!st.row ()) return std::nullopt;
  return verify_membership (pin_, st.bytes (0));
}

void membership_lease::renew (std::chrono::seconds lifetime,
  steady::time_point sent, wall::time_point sent_wall) {
  require (lifetime.count () > 0 && lifetime <= maximum_offline,
           "Invalid Hodarium lease duration");
  start_= last_= sent; wall_start_= wall_last_= sent_wall;
  lifetime_= lifetime; active_= true;
}
bool membership_lease::valid (steady::time_point now, wall::time_point wall_now) {
  if (!active_) return false;
  auto elapsed= now - start_;
  auto wall_elapsed= wall_now - wall_start_;
  // A suspend or clock change can make the clocks disagree. Do not turn that
  // uncertainty into additional offline authorization time.
  auto skew= std::chrono::duration_cast<std::chrono::milliseconds> (elapsed) -
    std::chrono::duration_cast<std::chrono::milliseconds> (wall_elapsed);
  if (now < last_ || wall_now < wall_last_ || elapsed >= lifetime_ ||
      wall_elapsed >= lifetime_ || skew > std::chrono::seconds (2) ||
      skew < -std::chrono::seconds (2)) revoke ();
  last_= now; wall_last_= wall_now;
  return active_;
}
void membership_lease::revoke () { active_= false; }

} // namespace athena::hodarium

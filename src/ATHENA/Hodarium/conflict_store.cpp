/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "conflict_store.hpp"
#include "sqlite_internal.hpp"
#include <nlohmann/json.hpp>
#include <sodium.h>
#include <limits>

namespace athena::hodarium {
namespace {
using json= nlohmann::json;
using namespace detail;
void token (const std::string& value) {
  unsigned char bytes[32]; std::size_t size= 0;
  if (value.size () != 43 || sodium_base642bin (bytes, sizeof bytes, value.data (),
      value.size (), nullptr, &size, nullptr, sodium_base64_VARIANT_URLSAFE_NO_PADDING) != 0 || size != 32)
    throw std::invalid_argument ("Invalid Hodarium conflict token");
}
json descriptor (const conflict_proposal& value) {
  const auto& q= value.request; const auto& r= value.resolution;
  return {{"request", {{"vault", q.vault}, {"conflict", q.conflict}, {"request_id", q.request_id},
    {"branches", q.branches}, {"resolution", q.resolution}, {"expected", q.expected}}},
    {"revision", {{"id", r.id}, {"vault", r.vault}, {"object", r.object},
      {"origin_member", r.origin_member}, {"format", r.format}, {"semantic_version", r.semantic_version},
      {"path", r.relative_path}, {"deleted", r.deleted}, {"parents", r.parents}}}};
}
conflict_proposal read_proposal (const std::string& header, std::string payload) {
  auto value= json::parse (header); const auto& q= value.at ("request"); const auto& r= value.at ("revision");
  conflict_proposal result;
  result.request= {q.at ("vault"), q.at ("conflict"), q.at ("request_id"),
    q.at ("branches"), q.at ("resolution"), q.at ("expected")};
  result.resolution= {r.at ("id"), r.at ("vault"), r.at ("object"), r.at ("origin_member"),
    r.at ("format"), r.at ("semantic_version"), r.at ("path"), r.at ("deleted"),
    r.at ("parents").get<std::vector<std::string>> (), std::move (payload)};
  return result;
}
json decision_json (const conflict_decision& r) {
  return {{"vault", r.vault}, {"conflict", r.conflict}, {"request_id", r.request},
    {"branches", r.branches}, {"resolution", r.resolution}, {"member", r.member},
    {"generation", r.generation}, {"epoch", r.epoch}, {"version", r.version}, {"created", r.created}};
}
conflict_decision read_decision (const std::string& bytes) {
  auto r= json::parse (bytes);
  return {r.at ("vault"), r.at ("conflict"), r.at ("request_id"), r.at ("branches"),
    r.at ("resolution"), r.at ("member"), r.at ("generation"), r.at ("epoch"),
    r.at ("version"), r.at ("created")};
}
}
conflict_store::conflict_store (const std::filesystem::path& path, authority_pin pin): pin_ (std::move (pin)) {
  token (pin_.group); token (pin_.generation); token (pin_.public_key);
  try {
    check (db_, sqlite3_open_v2 (path.string ().c_str (), &db_,
      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, nullptr));
    check (db_, sqlite3_busy_timeout (db_, 5000));
    {
      statement version (db_, "PRAGMA user_version");
      if (!version.row () || sqlite3_column_int (version.value, 0) > 2)
        throw std::invalid_argument ("Unsupported Hodarium conflict store version");
    }
    sql (db_, "PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL;");
    transaction tx (db_);
    sql (db_, "CREATE TABLE IF NOT EXISTS trust(singleton INTEGER PRIMARY KEY CHECK(singleton=1),pin TEXT NOT NULL);"
      "CREATE TABLE IF NOT EXISTS proposals(operation TEXT PRIMARY KEY,descriptor TEXT NOT NULL,payload BLOB NOT NULL,"
      "settled INTEGER NOT NULL CHECK(settled IN(0,1)));"
      "CREATE TABLE IF NOT EXISTS decisions(vault TEXT NOT NULL,conflict TEXT NOT NULL,version INTEGER NOT NULL,"
      "record TEXT NOT NULL,envelope TEXT NOT NULL,PRIMARY KEY(vault,conflict));"
      "CREATE TABLE IF NOT EXISTS publications(operation TEXT PRIMARY KEY); PRAGMA user_version=2;");
    auto binding= json::array ({pin_.group, pin_.generation, pin_.public_key}).dump ();
    statement existing (db_, "SELECT pin FROM trust WHERE singleton=1");
    if (existing.row ()) {
      if (existing.bytes (0) != binding) throw std::invalid_argument ("Hodarium conflict store authority changed");
    }
    else {
      statement insert (db_, "INSERT INTO trust VALUES(1,?)"); insert.text (1, binding); insert.row ();
    }
    tx.commit ();
  }
  catch (...) { sqlite3_close (db_); db_= nullptr; throw; }
}
conflict_store::~conflict_store () { sqlite3_close (db_); }
void conflict_store::prepare (const conflict_proposal& value, const revision_store& revisions) {
  const auto& q= value.request; const auto& r= value.resolution;
  for (const auto* id: {&q.vault, &q.conflict, &q.request_id, &q.branches, &q.resolution}) token (*id);
  if (q.expected < 0 || q.expected == std::numeric_limits<std::int64_t>::max () ||
      r.payload.size () > 512ULL*1024*1024)
    throw std::invalid_argument ("Invalid Hodarium conflict proposal budget or version");
  auto sealed= seal_revision (r);
  if (sealed.id != r.id || sealed.parents != r.parents || r.parents.size () < 2)
    throw std::invalid_argument ("Conflict proposal must preserve its exact revision and branch identities");
  auto header= descriptor (value).dump ();
  transaction tx (db_);
  if (auto existing= proposal (q.request_id)) {
    if (descriptor (*existing).dump () != header || existing->resolution.payload != r.payload)
      throw std::invalid_argument ("Hodarium conflict operation reused for different content");
    tx.commit (); return;
  }
  if (revisions.heads (r.vault, r.object) != r.parents)
    throw std::invalid_argument ("Hodarium conflict branches changed before preparation");
  auto prior= latest (q.vault, q.conflict);
  if ((prior ? prior->version : 0) != q.expected || (prior && prior->branches != q.branches))
    throw std::invalid_argument ("Hodarium conflict decision changed before preparation");
  statement insert (db_, "INSERT INTO proposals VALUES(?,?,?,0)");
  insert.text (1, q.request_id); insert.text (2, header); insert.blob (3, r.payload); insert.row ();
  tx.commit ();
}
std::optional<conflict_proposal> conflict_store::proposal (const std::string& operation) const {
  statement st (db_, "SELECT descriptor,payload FROM proposals WHERE operation=?"); st.text (1, operation);
  if (!st.row ()) return std::nullopt;
  return read_proposal (st.bytes (0), st.bytes (1));
}
std::vector<std::string> conflict_store::pending (const std::string& after, std::uint32_t limit) const {
  if (limit == 0 || limit > 256) throw std::invalid_argument ("Invalid Hodarium proposal page size");
  statement st (db_, "SELECT operation FROM proposals p WHERE operation>? "
    "AND NOT EXISTS(SELECT 1 FROM publications WHERE operation=p.operation) "
    "AND (settled=0 OR operation IN(SELECT json_extract(record,'$.request_id') FROM decisions)) "
    "ORDER BY operation LIMIT ?");
  st.text (1, after); check (db_, sqlite3_bind_int (st.value, 2, limit));
  std::vector<std::string> result;
  while (st.row ()) result.push_back (st.bytes (0));
  return result;
}
void conflict_store::published (const std::string& operation) {
  auto value= proposal (operation);
  if (!value) throw std::invalid_argument ("Unknown conflict publication");
  auto decision= latest (value->request.vault, value->request.conflict);
  if (!decision || decision->request != operation)
    throw std::invalid_argument ("Cannot publish a losing conflict proposal");
  statement st (db_, "INSERT OR IGNORE INTO publications VALUES(?)"); st.text (1, operation); st.row ();
}
std::vector<conflict_proposal_summary> conflict_store::proposals (
  const std::string& vault, const std::string& after, std::uint32_t limit) const {
  if (limit == 0 || limit > 256) throw std::invalid_argument ("Invalid Hodarium proposal page size");
  statement st (db_, "SELECT p.operation,json_extract(p.descriptor,'$.revision.path'),"
    "json_extract(p.descriptor,'$.revision.format'),json_extract(p.descriptor,'$.revision.deleted'),"
    "CASE WHEN published.operation IS NOT NULL THEN 1 "
    "WHEN d.version>json_extract(p.descriptor,'$.request.expected') THEN "
    "CASE WHEN json_extract(d.record,'$.request_id')=p.operation THEN 1 ELSE 2 END "
    "ELSE 0 END FROM proposals p "
    "LEFT JOIN publications published ON published.operation=p.operation "
    "LEFT JOIN decisions d ON d.vault=json_extract(p.descriptor,'$.request.vault') "
    "AND d.conflict=json_extract(p.descriptor,'$.request.conflict') "
    "WHERE p.operation>? AND json_extract(p.descriptor,'$.revision.vault')=? "
    "ORDER BY p.operation LIMIT ?");
  st.text (1, after); st.text (2, vault); check (db_, sqlite3_bind_int (st.value, 3, limit));
  std::vector<conflict_proposal_summary> result;
  while (st.row ()) result.push_back ({st.bytes (0), st.bytes (1), st.bytes (2),
    sqlite3_column_int (st.value, 3) != 0,
    static_cast<conflict_proposal_state> (sqlite3_column_int (st.value, 4))});
  return result;
}
std::optional<conflict_decision> conflict_store::latest (const std::string& vault, const std::string& conflict) const {
  statement st (db_, "SELECT record FROM decisions WHERE vault=? AND conflict=?");
  st.text (1, vault); st.text (2, conflict);
  if (!st.row ()) return std::nullopt;
  return read_decision (st.bytes (0));
}
std::optional<conflict_decision> conflict_store::accept (const std::string& operation,
  const std::string& envelope, const std::string& epoch,
  const std::string& nonce, const std::string& subject) {
  transaction tx (db_);
  auto value= proposal (operation);
  if (!value) throw std::invalid_argument ("Unknown Hodarium conflict operation");
  const auto& q= value->request;
  auto result= verify_decision (pin_, envelope, epoch, nonce, subject, q.vault, q.conflict);
  auto previous= latest (q.vault, q.conflict);
  if (!result) {
    if (previous) throw std::invalid_argument ("Hodarium conflict decision rolled back to absence");
    tx.commit (); return result;
  }
  auto record= decision_json (*result).dump ();
  if (result->branches != q.branches ||
      (previous && (result->version < previous->version ||
        (result->version == previous->version && record != decision_json (*previous).dump ()))))
    throw std::invalid_argument ("Hodarium conflict decision rollback or equivocation");
  if (result->request == operation && (result->version != q.expected+1 ||
      result->resolution != q.resolution || result->member != value->resolution.origin_member))
    throw std::invalid_argument ("Hodarium decision does not match prepared content");
  statement update (db_, "INSERT INTO decisions VALUES(?,?,?,?,?) ON CONFLICT(vault,conflict) DO UPDATE SET "
    "version=excluded.version,record=excluded.record,envelope=excluded.envelope");
  update.text (1, q.vault); update.text (2, q.conflict); check (db_, sqlite3_bind_int64 (update.value, 3, result->version));
  update.text (4, record); update.text (5, envelope); update.row ();
  if (result->version > q.expected) {
    statement settled (db_, "UPDATE proposals SET settled=1 WHERE operation=?");
    settled.text (1, operation); settled.row ();
  }
  tx.commit (); return result;
}
} // namespace athena::hodarium

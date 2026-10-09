/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "revisions.hpp"
#include "sqlite_internal.hpp"

#include <nlohmann/json.hpp>
#include <sodium.h>
#include <sqlite3.h>

#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <array>

namespace athena::hodarium {
namespace {
using json= nlohmann::json;
using namespace detail;

bool digest_id (const std::string& id) {
  return id.size () == 64 && std::all_of (id.begin (), id.end (), [] (char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  });
}
void validate (const revision& r) {
  if (r.vault.empty () || r.object.empty () || r.origin_member.empty () ||
      r.format.empty () || r.semantic_version == 0 || r.parents.size () > 256)
    throw std::invalid_argument ("Invalid Hodarium revision descriptor");
  if (r.deleted && !r.payload.empty ())
    throw std::invalid_argument ("Tombstone carries content");
  for (const auto& parent: r.parents)
    if (!digest_id (parent)) throw std::invalid_argument ("Invalid parent ID");
}

json descriptor (const revision& r) {
  return json{{"protocol", 1}, {"vault", r.vault}, {"object", r.object},
    {"origin_member", r.origin_member}, {"format", r.format},
    {"semantic_version", r.semantic_version}, {"path", r.relative_path},
    {"deleted", r.deleted}, {"parents", r.parents}};
}

void hash_header (crypto_hash_sha256_state& hash, const revision& r) {
  static const int initialized= sodium_init ();
  if (initialized < 0) throw std::runtime_error ("Cannot initialize libsodium");
  auto header= json::to_cbor (descriptor (r));
  crypto_hash_sha256_init (&hash);
  constexpr std::string_view domain= "ATHENA-HODARIUM-REVISION-v1";
  crypto_hash_sha256_update (&hash,
    reinterpret_cast<const unsigned char*> (domain.data ()), domain.size ());
  // CBOR is self-delimiting; payload is the remainder of the hashed message.
  crypto_hash_sha256_update (&hash, header.data (), header.size ());
}
std::string hash_finish (crypto_hash_sha256_state& hash) {
  unsigned char digest[crypto_hash_sha256_BYTES];
  crypto_hash_sha256_final (&hash, digest);
  char hex[crypto_hash_sha256_BYTES * 2 + 1];
  sodium_bin2hex (hex, sizeof hex, digest, sizeof digest);
  return hex;
}
std::string identity (const revision& r) {
  crypto_hash_sha256_state hash;
  hash_header (hash, r);
  crypto_hash_sha256_update (&hash,
    reinterpret_cast<const unsigned char*> (r.payload.data ()), r.payload.size ());
  return hash_finish (hash);
}
std::string raw_fingerprint (std::string_view bytes) {
  crypto_hash_sha256_state hash; crypto_hash_sha256_init (&hash);
  crypto_hash_sha256_update (&hash, reinterpret_cast<const unsigned char*> (bytes.data ()), bytes.size ());
  return hash_finish (hash);
}
void cache_fingerprint (sqlite3* db, const std::string& id, const std::string& hash) {
  statement st (db, "INSERT OR IGNORE INTO payload_fingerprints VALUES(?,?)");
  st.text (1, id); st.text (2, hash); st.row ();
}
revision from_descriptor (const std::string& id, const std::string& bytes) {
  auto d= json::parse (bytes);
  revision r;
  r.id= id; r.vault= d.at ("vault"); r.object= d.at ("object");
  r.origin_member= d.at ("origin_member"); r.format= d.at ("format");
  r.semantic_version= d.at ("semantic_version"); r.relative_path= d.at ("path");
  r.deleted= d.at ("deleted"); r.parents= d.at ("parents").get<std::vector<std::string>> ();
  return r;
}
struct revision_blob {
  sqlite3_blob* value= nullptr;
  revision_blob (sqlite3* db, sqlite3_int64 row, bool write, const char* table= "incoming") {
    check (db, sqlite3_blob_open (db, "main", table, "payload", row, write ? 1 : 0, &value));
  }
  ~revision_blob () { sqlite3_blob_close (value); }
  revision_blob (const revision_blob&)= delete;
  revision_blob& operator= (const revision_blob&)= delete;
};
void validate_parents (sqlite3* db, const revision& r) {
  for (const auto& parent: r.parents) {
    statement st (db, "SELECT vault,object FROM revisions WHERE id=?");
    st.text (1, parent);
    if (!st.row ()) throw std::invalid_argument ("Missing revision parent");
    if (st.bytes (0) != r.vault || st.bytes (1) != r.object)
      throw std::invalid_argument ("Revision parent belongs to another object");
  }
}
void insert_parents (sqlite3* db, const revision& r) {
  for (const auto& parent: r.parents) {
    statement edge (db, "INSERT INTO parents VALUES(?,?)");
    edge.text (1, r.id); edge.text (2, parent); edge.row ();
  }
}
void admit_revision (sqlite3* db, const std::string& id) {
  statement st (db, "INSERT OR IGNORE INTO eligible SELECT id FROM revisions r WHERE id=? "
    "AND ((SELECT COUNT(*) FROM parents WHERE child=r.id)<2 OR EXISTS("
    "SELECT 1 FROM resolution_approvals WHERE revision=r.id)) AND NOT EXISTS("
    "SELECT 1 FROM parents p LEFT JOIN eligible e ON e.id=p.parent WHERE p.child=r.id AND e.id IS NULL)");
  st.text (1, id); st.row ();
}
json intent_descriptor (const apply_intent& value) {
  return {{"revision", value.revision_id},
    {"expected", value.expected_revision ? json (*value.expected_revision) : json (nullptr)},
    {"source_path", value.source_path},
    {"source_fingerprint", value.source_fingerprint ? json (*value.source_fingerprint) : json (nullptr)},
    {"protected_history_version", value.protected_history_version}};
}
apply_intent read_intent (const std::string& operation, const std::string& bytes, bool completed) {
  auto value= json::parse (bytes);
  apply_intent result;
  result.operation= operation; result.revision_id= value.at ("revision");
  if (!value.at ("expected").is_null ()) result.expected_revision= value.at ("expected").get<std::string> ();
  result.source_path= value.at ("source_path");
  if (!value.at ("source_fingerprint").is_null ())
    result.source_fingerprint= value.at ("source_fingerprint").get<std::string> ();
  result.protected_history_version= value.at ("protected_history_version");
  result.completed= completed; return result;
}
} // namespace

revision seal_revision (revision value) {
  std::sort (value.parents.begin (), value.parents.end ());
  value.parents.erase (std::unique (value.parents.begin (), value.parents.end ()),
                       value.parents.end ());
  validate (value);
  value.id= identity (value);
  return value;
}

revision_store::revision_store (const std::filesystem::path& database) {
  try {
    check (db_, sqlite3_open_v2 (database.string ().c_str (), &db_,
      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, nullptr));
    check (db_, sqlite3_busy_timeout (db_, 5000));
    {
      statement version (db_, "PRAGMA user_version");
      if (!version.row () || sqlite3_column_int (version.value, 0) > 5)
        throw std::runtime_error ("Unsupported Hodarium revision store version");
    }
    sql (db_, "PRAGMA foreign_keys=ON; PRAGMA journal_mode=WAL;"
              "PRAGMA synchronous=FULL;");
    transaction tx (db_);
    sql (db_,
      "CREATE TABLE IF NOT EXISTS revisions("
      "id TEXT PRIMARY KEY,vault TEXT NOT NULL,object TEXT NOT NULL,"
      "descriptor TEXT NOT NULL,payload BLOB NOT NULL);"
      "CREATE TABLE IF NOT EXISTS parents("
      "child TEXT NOT NULL REFERENCES revisions(id),"
      "parent TEXT NOT NULL REFERENCES revisions(id),PRIMARY KEY(child,parent));"
      "CREATE INDEX IF NOT EXISTS parents_reverse ON parents(parent);"
      "CREATE INDEX IF NOT EXISTS revisions_object ON revisions(vault,object);"
      "CREATE INDEX IF NOT EXISTS revisions_vault ON revisions(vault);"
      "CREATE TABLE IF NOT EXISTS payload_fingerprints("
      "revision TEXT PRIMARY KEY REFERENCES revisions(id),hash TEXT NOT NULL);"
      "CREATE TABLE IF NOT EXISTS applied("
      "vault TEXT NOT NULL,object TEXT NOT NULL,"
      "revision TEXT NOT NULL REFERENCES revisions(id),PRIMARY KEY(vault,object));"
      "CREATE TABLE IF NOT EXISTS incoming("
      "id TEXT PRIMARY KEY,descriptor TEXT NOT NULL,payload BLOB NOT NULL,"
      "received INTEGER NOT NULL CHECK(received>=0 AND received<=length(payload)));"
      "CREATE TABLE IF NOT EXISTS apply_intents("
      "operation TEXT PRIMARY KEY,vault TEXT NOT NULL,object TEXT NOT NULL,"
      "target TEXT NOT NULL REFERENCES revisions(id),descriptor TEXT NOT NULL,"
      "completed INTEGER NOT NULL CHECK(completed IN(0,1)));"
      "CREATE UNIQUE INDEX IF NOT EXISTS apply_intents_pending ON apply_intents(vault,object) WHERE completed=0;"
      "CREATE INDEX IF NOT EXISTS apply_intents_recovery ON apply_intents(vault,completed,operation);"
      "CREATE TABLE IF NOT EXISTS resolution_approvals("
      "revision TEXT PRIMARY KEY REFERENCES revisions(id),envelope TEXT NOT NULL);"
      "CREATE TABLE IF NOT EXISTS eligible(id TEXT PRIMARY KEY REFERENCES revisions(id));");
    {
      statement version (db_, "PRAGMA user_version"); version.row ();
      if (sqlite3_column_int (version.value, 0) < 5) {
        // Receipt order is topological: parents must exist before their child.
        statement entries (db_, "SELECT id FROM revisions ORDER BY rowid");
        while (entries.row ()) admit_revision (db_, entries.bytes (0));
      }
    }
    sql (db_, "PRAGMA user_version=5;");
    tx.commit ();
  }
  catch (...) { sqlite3_close (db_); db_= nullptr; throw; }
}
revision_store::~revision_store () { sqlite3_close (db_); }

std::string revision_store::payload_fingerprint (const std::string& id) {
  {
    statement st (db_, "SELECT hash FROM payload_fingerprints WHERE revision=?");
    st.text (1, id); if (st.row ()) return st.bytes (0);
  }
  auto value= offer (id);
  if (!value) throw std::invalid_argument ("Unknown revision fingerprint");
  crypto_hash_sha256_state hash; crypto_hash_sha256_init (&hash);
  for (std::uint64_t offset= 0; offset < value->size;) {
    auto bytes= payload_chunk (id, offset, 64*1024);
    crypto_hash_sha256_update (&hash, reinterpret_cast<const unsigned char*> (bytes.data ()), bytes.size ());
    offset+= bytes.size ();
  }
  auto result= hash_finish (hash); cache_fingerprint (db_, id, result); return result;
}

bool revision_store::contains (const std::string& vault, const std::string& id) const {
  if (!digest_id (id)) throw std::invalid_argument ("Invalid revision ID");
  statement st (db_, "SELECT 1 FROM revisions WHERE vault=? AND id=?");
  st.text (1, vault); st.text (2, id); return st.row ();
}
std::int64_t revision_store::inventory_tip (const std::string& vault) const {
  statement st (db_, "SELECT COALESCE(MAX(rowid),0) FROM revisions WHERE vault=?");
  st.text (1, vault); st.row (); return sqlite3_column_int64 (st.value, 0);
}
std::vector<revision_head> revision_store::inventory_heads (const std::string& vault,
  std::int64_t after, std::int64_t through, std::uint32_t limit) const {
  if (after < 0 || through < after || limit == 0 || limit > 256)
    throw std::invalid_argument ("Invalid revision inventory cursor");
  statement st (db_, "SELECT r.rowid,r.id FROM revisions r WHERE r.vault=? "
    "AND r.rowid>? AND r.rowid<=? AND NOT EXISTS("
    "SELECT 1 FROM parents p JOIN revisions c ON c.id=p.child "
    "WHERE p.parent=r.id AND c.rowid<=?) ORDER BY r.rowid LIMIT ?");
  st.text (1, vault);
  check (db_, sqlite3_bind_int64 (st.value, 2, after));
  check (db_, sqlite3_bind_int64 (st.value, 3, through));
  check (db_, sqlite3_bind_int64 (st.value, 4, through));
  check (db_, sqlite3_bind_int (st.value, 5, int (limit)));
  std::vector<revision_head> result;
  while (st.row ()) result.push_back ({sqlite3_column_int64 (st.value, 0), st.bytes (1)});
  return result;
}

std::optional<revision> revision_store::get (const std::string& id) const {
  statement st (db_, "SELECT descriptor,payload FROM revisions WHERE id=?");
  st.text (1, id);
  if (!st.row ()) return std::nullopt;
  revision r= from_descriptor (id, st.bytes (0));
  r.payload= st.bytes (1);
  return r;
}

bool revision_store::receive (const revision& r) {
  auto sealed= seal_revision (r);
  if (r.id != sealed.id || r.parents != sealed.parents)
    throw std::invalid_argument ("Revision identity or parent order mismatch");
  transaction tx (db_);
  {
    statement known (db_, "SELECT 1 FROM revisions WHERE id=?");
    known.text (1, r.id);
    if (known.row ()) { tx.commit (); return false; }
  }
  validate_parents (db_, r);
  statement insert (db_, "INSERT INTO revisions VALUES(?,?,?,?,?)");
  insert.text (1, r.id); insert.text (2, r.vault); insert.text (3, r.object);
  insert.text (4, descriptor (r).dump ()); insert.blob (5, r.payload);
  insert.row ();
  insert_parents (db_, r);
  admit_revision (db_, r.id);
  cache_fingerprint (db_, r.id, raw_fingerprint (r.payload));
  tx.commit ();
  return true;
}

std::optional<std::string> revision_store::capture_saved (revision snapshot,
  const std::optional<std::string>& expected,
  const std::optional<std::string>& predecessor_fingerprint) {
  if (!snapshot.id.empty () || !snapshot.parents.empty ())
    throw std::invalid_argument ("Saved snapshot must not supply a revision ID or parents");
  validate (snapshot);
  if (predecessor_fingerprint && !digest_id (*predecessor_fingerprint))
    throw std::invalid_argument ("Invalid saved predecessor fingerprint");
  transaction tx (db_);
  if (applied (snapshot.vault, snapshot.object) != expected) { tx.commit (); return std::nullopt; }
  {
    statement pending (db_, "SELECT 1 FROM apply_intents WHERE vault=? AND object=? AND completed=0");
    pending.text (1, snapshot.vault); pending.text (2, snapshot.object);
    if (pending.row ()) throw std::invalid_argument ("Saved object has an unfinished remote application");
  }
  if (expected) {
    if (!eligible (*expected)) throw std::invalid_argument ("Saved base requires an authoritative conflict decision");
    auto prior= offer (*expected);
    if (predecessor_fingerprint) {
      if (prior->metadata.deleted) { tx.commit (); return std::nullopt; }
      if (payload_fingerprint (*expected) != *predecessor_fingerprint) { tx.commit (); return std::nullopt; }
    }
    // Compare storage content without loading the previous payload or treating
    // the saving device and new causal edge as document modifications.
    auto origin= snapshot.origin_member;
    snapshot.origin_member= prior->metadata.origin_member;
    snapshot.parents= prior->metadata.parents;
    bool unchanged= identity (snapshot) == *expected;
    snapshot.origin_member= std::move (origin);
    if (unchanged) { tx.commit (); return expected; }
    snapshot.parents= {*expected};
  }
  snapshot.id= identity (snapshot);
  if (!contains (snapshot.vault, snapshot.id)) {
    statement st (db_, "INSERT INTO revisions VALUES(?,?,?,?,?)");
    st.text (1, snapshot.id); st.text (2, snapshot.vault); st.text (3, snapshot.object);
    st.text (4, descriptor (snapshot).dump ()); st.blob (5, snapshot.payload); st.row ();
    insert_parents (db_, snapshot);
    admit_revision (db_, snapshot.id);
    cache_fingerprint (db_, snapshot.id, raw_fingerprint (snapshot.payload));
  }
  statement st (db_, "INSERT INTO applied VALUES(?,?,?) "
    "ON CONFLICT(vault,object) DO UPDATE SET revision=excluded.revision");
  st.text (1, snapshot.vault); st.text (2, snapshot.object); st.text (3, snapshot.id); st.row ();
  tx.commit (); return snapshot.id;
}

std::uint64_t revision_store::begin_receive (const revision& metadata, std::uint64_t size) {
  validate (metadata);
  if (!metadata.payload.empty () || !digest_id (metadata.id) ||
      !std::is_sorted (metadata.parents.begin (), metadata.parents.end ()) ||
      std::adjacent_find (metadata.parents.begin (), metadata.parents.end ()) != metadata.parents.end () ||
      (metadata.deleted && size != 0) || size > 512ULL*1024*1024)
    throw std::invalid_argument ("Invalid incoming revision metadata or size");
  auto encoded= descriptor (metadata).dump ();
  if (encoded.size () > 64*1024) throw std::invalid_argument ("Incoming revision descriptor exceeds budget");
  transaction tx (db_);
  {
    statement known (db_, "SELECT descriptor,length(payload) FROM revisions WHERE id=?");
    known.text (1, metadata.id);
    if (known.row ()) {
      if (known.bytes (0) != encoded || std::uint64_t (sqlite3_column_int64 (known.value, 1)) != size)
        throw std::invalid_argument ("Incoming revision conflicts with stored descriptor");
      discard_receive (metadata.id); tx.commit (); return size;
    }
  }
  {
    statement pending (db_, "SELECT descriptor,length(payload),received FROM incoming WHERE id=?");
    pending.text (1, metadata.id);
    if (pending.row ()) {
      if (pending.bytes (0) != encoded || std::uint64_t (sqlite3_column_int64 (pending.value, 1)) != size)
        throw std::invalid_argument ("Incoming revision conflicts with pending descriptor");
      auto received= std::uint64_t (sqlite3_column_int64 (pending.value, 2));
      tx.commit (); return received;
    }
  }
  {
    statement budget (db_, "SELECT count(*),coalesce(sum(length(payload)),0) FROM incoming");
    budget.row ();
    if (sqlite3_column_int (budget.value, 0) >= 32 ||
        std::uint64_t (sqlite3_column_int64 (budget.value, 1)) + size > 2ULL*1024*1024*1024)
      throw std::invalid_argument ("Incoming revisions exceed staging budget");
  }
  statement insert (db_, "INSERT INTO incoming VALUES(?,?,?,0)");
  insert.text (1, metadata.id); insert.text (2, encoded);
  check (db_, sqlite3_bind_zeroblob64 (insert.value, 3, size)); insert.row ();
  tx.commit (); return 0;
}
std::uint64_t revision_store::receive_chunk (const std::string& id, std::uint64_t offset,
                                            const std::string& bytes) {
  if (bytes.empty () || bytes.size () > 256*1024)
    throw std::invalid_argument ("Invalid incoming revision chunk size");
  transaction tx (db_);
  sqlite3_int64 row;
  {
    statement st (db_, "SELECT rowid,length(payload),received FROM incoming WHERE id=?");
    st.text (1, id);
    if (!st.row ()) throw std::invalid_argument ("Unknown incoming revision");
    row= sqlite3_column_int64 (st.value, 0);
    auto total= std::uint64_t (sqlite3_column_int64 (st.value, 1));
    if (offset != std::uint64_t (sqlite3_column_int64 (st.value, 2)) ||
        offset > total || bytes.size () > total - offset)
      throw std::invalid_argument ("Incoming revision chunk offset mismatch");
  }
  {
    revision_blob blob (db_, row, true);
    check (db_, sqlite3_blob_write (blob.value, bytes.data (), int (bytes.size ()), int (offset)));
  }
  statement advance (db_, "UPDATE incoming SET received=? WHERE id=?");
  check (db_, sqlite3_bind_int64 (advance.value, 1, offset + bytes.size ()));
  advance.text (2, id); advance.row (); tx.commit ();
  return offset + bytes.size ();
}
bool revision_store::finish_receive (const std::string& id) {
  transaction tx (db_);
  {
    statement known (db_, "SELECT 1 FROM revisions WHERE id=?"); known.text (1, id);
    if (known.row ()) { discard_receive (id); tx.commit (); return false; }
  }
  revision metadata;
  sqlite3_int64 row, total;
  {
    statement pending (db_, "SELECT rowid,descriptor,length(payload),received FROM incoming WHERE id=?");
    pending.text (1, id);
    if (!pending.row ()) throw std::invalid_argument ("Unknown incoming revision");
    row= sqlite3_column_int64 (pending.value, 0); total= sqlite3_column_int64 (pending.value, 2);
    if (total != sqlite3_column_int64 (pending.value, 3))
      throw std::invalid_argument ("Incoming revision is incomplete");
    metadata= from_descriptor (id, pending.bytes (1));
  }
  validate_parents (db_, metadata);
  crypto_hash_sha256_state hash, raw; hash_header (hash, metadata); crypto_hash_sha256_init (&raw);
  {
    revision_blob blob (db_, row, false);
    std::array<unsigned char, 64*1024> bytes;
    for (sqlite3_int64 offset= 0; offset < total;) {
      auto count= int (std::min<sqlite3_int64> (bytes.size (), total - offset));
      check (db_, sqlite3_blob_read (blob.value, bytes.data (), count, int (offset)));
      crypto_hash_sha256_update (&hash, bytes.data (), count);
      crypto_hash_sha256_update (&raw, bytes.data (), count); offset+= count;
    }
  }
  if (hash_finish (hash) != id) throw std::invalid_argument ("Incoming revision content hash mismatch");
  statement insert (db_, "INSERT INTO revisions SELECT id,?,?,descriptor,payload FROM incoming WHERE id=?");
  insert.text (1, metadata.vault); insert.text (2, metadata.object); insert.text (3, id); insert.row ();
  cache_fingerprint (db_, id, hash_finish (raw));
  insert_parents (db_, metadata); admit_revision (db_, id);
  discard_receive (id); tx.commit (); return true;
}
void revision_store::discard_receive (const std::string& id) {
  statement st (db_, "DELETE FROM incoming WHERE id=?"); st.text (1, id); st.row ();
}
std::optional<revision_offer> revision_store::offer (const std::string& id) const {
  statement st (db_, "SELECT descriptor,length(payload) FROM revisions WHERE id=?");
  st.text (1, id);
  if (!st.row ()) return std::nullopt;
  return revision_offer{from_descriptor (id, st.bytes (0)),
    std::uint64_t (sqlite3_column_int64 (st.value, 1))};
}
std::string revision_store::payload_chunk (const std::string& id, std::uint64_t offset,
                                         std::uint32_t limit) const {
  if (limit == 0 || limit > 256*1024) throw std::invalid_argument ("Invalid outgoing chunk budget");
  statement st (db_, "SELECT rowid,length(payload) FROM revisions WHERE id=?");
  st.text (1, id);
  if (!st.row ()) throw std::invalid_argument ("Unknown outgoing revision");
  auto size= std::uint64_t (sqlite3_column_int64 (st.value, 1));
  if (offset > size) throw std::invalid_argument ("Invalid outgoing revision offset");
  auto count= int (std::min<std::uint64_t> (limit, size - offset));
  std::string bytes (count, '\0');
  if (count != 0) {
    revision_blob blob (db_, sqlite3_column_int64 (st.value, 0), false, "revisions");
    check (db_, sqlite3_blob_read (blob.value, bytes.data (), count, int (offset)));
  }
  return bytes;
}

std::vector<std::string> revision_store::heads (
    const std::string& vault, const std::string& object) const {
  statement st (db_, "SELECT id FROM revisions WHERE vault=? AND object=? "
    "AND id IN(SELECT id FROM eligible) AND NOT EXISTS(SELECT 1 FROM parents "
    "JOIN eligible e ON e.id=parents.child WHERE parent=revisions.id) ORDER BY id");
  st.text (1, vault); st.text (2, object);
  std::vector<std::string> result;
  while (st.row ()) result.push_back (st.bytes (0));
  return result;
}

bool revision_store::eligible (const std::string& id) const {
  statement st (db_, "SELECT 1 FROM eligible WHERE id=?"); st.text (1, id); return st.row ();
}
std::vector<revision> revision_store::pending_resolutions (const std::string& vault,
    const std::string& after, std::uint32_t limit) const {
  if (limit == 0 || limit > 256) throw std::invalid_argument ("Invalid resolution page budget");
  statement st (db_, "SELECT r.id,r.descriptor FROM revisions r WHERE r.vault=? AND r.id>? "
    "AND (SELECT COUNT(*) FROM parents WHERE child=r.id)>1 AND NOT EXISTS("
    "SELECT 1 FROM resolution_approvals WHERE revision=r.id) "
    "AND NOT EXISTS(SELECT 1 FROM parents p LEFT JOIN eligible e ON e.id=p.parent "
    "WHERE p.child=r.id AND e.id IS NULL) ORDER BY r.id LIMIT ?");
  st.text (1, vault); st.text (2, after); check (db_, sqlite3_bind_int (st.value, 3, limit));
  std::vector<revision> result;
  while (st.row ()) result.push_back (from_descriptor (st.bytes (0), st.bytes (1)));
  return result;
}
bool revision_store::accept_resolution (const std::string& id, const authority_pin& pin,
    const vault_secret& secret, const decision_evidence& evidence) {
  auto value= offer (id);
  if (!value || value->metadata.parents.size () < 2 || secret.group != pin.group ||
      secret.vault != value->metadata.vault)
    throw std::invalid_argument ("Resolution does not match its protected Vault scope");
  const auto& r= value->metadata;
  auto tokens= derive_conflict_tokens (secret, r.object, r.parents, r.id);
  auto decision= verify_decision (pin, evidence.envelope, evidence.epoch, evidence.nonce,
                                 evidence.subject, tokens.vault, tokens.conflict);
  if (!decision || decision->branches != tokens.branches || decision->resolution != tokens.resolution ||
      decision->member != r.origin_member) return false;
  transaction tx (db_);
  statement insert (db_, "INSERT OR IGNORE INTO resolution_approvals VALUES(?,?)");
  insert.text (1, id); insert.text (2, evidence.envelope); insert.row ();
  // A waiting descendant may already have arrived over another peer. Only
  // reconsider this object's descendants, in their topological receipt order.
  statement descendants (db_, "WITH RECURSIVE descendants(id) AS (VALUES(?) UNION "
    "SELECT child FROM parents JOIN descendants ON parent=descendants.id) "
    "SELECT r.id FROM revisions r JOIN descendants d ON d.id=r.id ORDER BY r.rowid");
  descendants.text (1, id);
  while (descendants.row ()) admit_revision (db_, descendants.bytes (0));
  tx.commit (); return true;
}

bool revision_store::ancestor_of (const std::string& first,
                                 const std::string& second) const {
  statement st (db_, "WITH RECURSIVE ancestors(id) AS (VALUES(?) UNION "
    "SELECT parent FROM parents JOIN ancestors ON child=ancestors.id) "
    "SELECT 1 FROM ancestors WHERE id=? LIMIT 1");
  st.text (1, second); st.text (2, first);
  return st.row ();
}

ancestry revision_store::compare (const std::string& first,
                                  const std::string& second) const {
  statement st (db_, "SELECT a.vault,a.object,b.vault,b.object "
    "FROM revisions a,revisions b WHERE a.id=? AND b.id=?");
  st.text (1, first); st.text (2, second);
  if (!st.row () || st.bytes (0) != st.bytes (2) || st.bytes (1) != st.bytes (3))
    throw std::invalid_argument ("Cannot compare unknown or unrelated objects");
  if (first == second) return ancestry::same;
  if (ancestor_of (first, second)) return ancestry::ancestor;
  if (ancestor_of (second, first)) return ancestry::descendant;
  return ancestry::concurrent;
}

std::vector<std::string> revision_store::merge_bases (
    const std::string& first, const std::string& second) const {
  (void) compare (first, second);
  statement st (db_,
    "WITH RECURSIVE a(id) AS (VALUES(?) UNION "
    "SELECT parent FROM parents JOIN a ON child=a.id),"
    "b(id) AS (VALUES(?) UNION "
    "SELECT parent FROM parents JOIN b ON child=b.id),"
    "common(id) AS (SELECT id FROM a INTERSECT SELECT id FROM b) "
    "SELECT id FROM common WHERE NOT EXISTS(SELECT 1 FROM parents "
    "JOIN common newer ON newer.id=parents.child WHERE parents.parent=common.id) "
    "ORDER BY id");
  st.text (1, first); st.text (2, second);
  std::vector<std::string> result;
  while (st.row ()) result.push_back (st.bytes (0));
  return result;
}

std::optional<std::string> revision_store::applied (
    const std::string& vault, const std::string& object) const {
  statement st (db_, "SELECT revision FROM applied WHERE vault=? AND object=?");
  st.text (1, vault); st.text (2, object);
  if (!st.row ()) return std::nullopt;
  return st.bytes (0);
}

std::vector<revision> revision_store::applied_page (const std::string& vault,
    const std::string& after_object, std::uint32_t limit) const {
  if (limit == 0 || limit > 256)
    throw std::invalid_argument ("Invalid applied inventory budget");
  statement st (db_, "SELECT r.id,r.descriptor FROM applied a "
    "JOIN revisions r ON r.id=a.revision WHERE a.vault=? AND a.object>? "
    "ORDER BY a.object LIMIT ?");
  st.text (1, vault); st.text (2, after_object);
  check (db_, sqlite3_bind_int (st.value, 3, int (limit)));
  std::vector<revision> result;
  while (st.row ()) result.push_back (from_descriptor (st.bytes (0), st.bytes (1)));
  return result;
}

std::vector<revision> revision_store::application_candidates (const std::string& vault,
    const std::string& after_object, std::uint32_t limit) const {
  if (limit == 0 || limit > 256)
    throw std::invalid_argument ("Invalid application inventory budget");
  statement st (db_, "WITH heads AS (SELECT r.id,r.object FROM revisions r WHERE r.vault=? "
    "AND r.object>? AND r.id IN(SELECT id FROM eligible) AND NOT EXISTS(SELECT 1 FROM parents "
    "JOIN eligible e ON e.id=parents.child WHERE parent=r.id)),"
    "single AS (SELECT object,MIN(id) AS id FROM heads GROUP BY object HAVING COUNT(*)=1) "
    "SELECT r.id,r.descriptor FROM single s JOIN revisions r ON r.id=s.id "
    "LEFT JOIN applied a ON a.vault=r.vault AND a.object=r.object "
    "WHERE (a.revision IS NULL OR a.revision<>r.id) AND NOT EXISTS("
    "SELECT 1 FROM apply_intents i WHERE i.vault=r.vault AND i.object=r.object AND i.completed=0) "
    "ORDER BY s.object LIMIT ?");
  st.text (1, vault); st.text (2, after_object);
  check (db_, sqlite3_bind_int (st.value, 3, int (limit)));
  std::vector<revision> result;
  while (st.row ()) result.push_back (from_descriptor (st.bytes (0), st.bytes (1)));
  return result;
}

std::vector<revision_conflict> revision_store::conflicts (const std::string& vault,
    const std::string& after_object, std::uint32_t limit) const {
  if (limit == 0 || limit > 256)
    throw std::invalid_argument ("Invalid Hodarium conflict page size");
  statement st (db_, "WITH heads AS (SELECT r.id,r.object FROM revisions r WHERE r.vault=? "
    "AND r.object>? AND r.id IN(SELECT id FROM eligible) AND NOT EXISTS(SELECT 1 FROM parents "
    "JOIN eligible e ON e.id=parents.child WHERE parent=r.id)),"
    "conflicts AS (SELECT object,MIN(id) AS id,COUNT(*) AS count FROM heads GROUP BY object HAVING COUNT(*)>1) "
    "SELECT c.object,r.descriptor,c.count FROM conflicts c JOIN revisions r ON r.id=c.id "
    "ORDER BY c.object LIMIT ?");
  st.text (1, vault); st.text (2, after_object);
  check (db_, sqlite3_bind_int (st.value, 3, int (limit)));
  std::vector<revision_conflict> result;
  while (st.row ()) result.push_back ({st.bytes (0), from_descriptor ({}, st.bytes (1)).relative_path,
                                      sqlite3_column_int64 (st.value, 2)});
  return result;
}

std::vector<revision_path_collision> revision_store::path_collisions (
    const std::string& vault, const std::string& after, std::uint32_t limit) const {
  if (limit == 0 || limit > 256)
    throw std::invalid_argument ("Invalid Hodarium path collision page size");
  statement st (db_, "WITH heads AS (SELECT r.id,r.object,r.descriptor,"
    "json_extract(r.descriptor,'$.path') AS path FROM revisions r "
    "JOIN eligible e ON e.id=r.id WHERE r.vault=? "
    "AND json_extract(r.descriptor,'$.deleted')=0 "
    "AND json_extract(r.descriptor,'$.format') IN ('ath-xml-v2','ath-resource-avd-v1',"
    "'ath-resource-blob-v1','ath-resource-style-v1','ath-resource-sorter-v1') "
    "AND NOT EXISTS(SELECT 1 FROM parents p JOIN eligible child ON child.id=p.child WHERE p.parent=r.id)),"
    "collisions AS (SELECT path FROM heads WHERE path>? GROUP BY path "
    "HAVING COUNT(DISTINCT object)>1 ORDER BY path LIMIT ?) "
    "SELECT h.path,h.id,h.descriptor FROM collisions c JOIN heads h ON h.path=c.path "
    "ORDER BY h.path,h.object,h.id");
  st.text (1, vault); st.text (2, after);
  check (db_, sqlite3_bind_int (st.value, 3, int (limit)));
  std::vector<revision_path_collision> result;
  while (st.row ()) {
    auto path= st.bytes (0);
    if (result.empty () || result.back ().path != path) result.push_back ({path, {}});
    result.back ().heads.push_back (from_descriptor (st.bytes (1), st.bytes (2)));
  }
  return result;
}

bool revision_store::record_applied (
    const std::string& id, const std::optional<std::string>& expected) {
  transaction tx (db_);
  if (!eligible (id)) throw std::invalid_argument ("Revision requires an authoritative conflict decision");
  statement object (db_, "SELECT vault,object FROM revisions WHERE id=?");
  object.text (1, id);
  if (!object.row ()) throw std::invalid_argument ("Unknown applied revision");
  auto vault= object.bytes (0), key= object.bytes (1);
  {
    statement pending (db_, "SELECT 1 FROM apply_intents WHERE vault=? AND object=? AND completed=0");
    pending.text (1, vault); pending.text (2, key);
    if (pending.row ()) throw std::invalid_argument ("Object has an unfinished remote application");
  }
  if (applied (vault, key) != expected) { tx.commit (); return false; }
  if (expected && !ancestor_of (*expected, id))
    throw std::invalid_argument ("Application must advance causal history");
  statement st (db_, "INSERT INTO applied VALUES(?,?,?) "
    "ON CONFLICT(vault,object) DO UPDATE SET revision=excluded.revision");
  st.text (1, vault); st.text (2, key); st.text (3, id); st.row ();
  tx.commit ();
  return true;
}

std::optional<apply_intent> revision_store::application (const std::string& operation) const {
  statement st (db_, "SELECT descriptor,completed FROM apply_intents WHERE operation=?");
  st.text (1, operation);
  if (!st.row ()) return std::nullopt;
  return read_intent (operation, st.bytes (0), sqlite3_column_int (st.value, 1) != 0);
}
bool revision_store::prepare_apply (const apply_intent& intent) {
  if (intent.operation.empty () || intent.operation.size () > 256 ||
      intent.operation.find ('\0') != std::string::npos || intent.completed ||
      !digest_id (intent.revision_id) ||
      (intent.expected_revision && !digest_id (*intent.expected_revision)) ||
      intent.source_path.empty () || intent.source_path.size () > 64*1024 ||
      (intent.source_fingerprint && (!digest_id (*intent.source_fingerprint) || intent.protected_history_version <= 0)) ||
      (!intent.source_fingerprint && intent.protected_history_version != 0))
    throw std::invalid_argument ("Invalid protected application intent");
  auto descriptor= intent_descriptor (intent).dump ();
  transaction tx (db_);
  if (auto previous= application (intent.operation)) {
    if (intent_descriptor (*previous).dump () != descriptor)
      throw std::invalid_argument ("Application operation is bound to another transition");
    tx.commit (); return false;
  }
  auto target= offer (intent.revision_id);
  if (!target) throw std::invalid_argument ("Unknown application target revision");
  if (!eligible (intent.revision_id)) throw std::invalid_argument ("Application requires an authoritative conflict decision");
  const auto& r= target->metadata;
  if (applied (r.vault, r.object) != intent.expected_revision)
    throw std::invalid_argument ("Application base revision changed");
  if (intent.expected_revision) {
    auto previous= offer (*intent.expected_revision);
    if (!previous || previous->metadata.relative_path != intent.source_path ||
        compare (*intent.expected_revision, r.id) != ancestry::ancestor)
      throw std::invalid_argument ("Application must advance its object's causal history");
  }
  else if (intent.source_path != r.relative_path)
    throw std::invalid_argument ("Initial application source path differs from target");
  {
    statement pending (db_, "SELECT 1 FROM apply_intents WHERE vault=? AND object=? AND completed=0");
    pending.text (1, r.vault); pending.text (2, r.object);
    if (pending.row ()) throw std::invalid_argument ("Object already has an unfinished application");
  }
  statement st (db_, "INSERT INTO apply_intents VALUES(?,?,?,?,?,0)");
  st.text (1, intent.operation); st.text (2, r.vault); st.text (3, r.object);
  st.text (4, r.id); st.text (5, descriptor); st.row (); tx.commit (); return true;
}
bool revision_store::abandon_unpublished_apply (const std::string& operation) {
  transaction tx (db_);
  auto intent= application (operation);
  if (!intent) { tx.commit (); return false; }
  if (intent->completed) throw std::invalid_argument ("Cannot abandon a completed application");
  auto target= offer (intent->revision_id);
  if (applied (target->metadata.vault, target->metadata.object) != intent->expected_revision)
    throw std::invalid_argument ("Cannot abandon application after its base changed");
  statement remove (db_, "DELETE FROM apply_intents WHERE operation=?");
  remove.text (1, operation); remove.row (); tx.commit (); return true;
}

std::vector<apply_intent> revision_store::pending_applications (
    const std::string& vault, const std::string& after, std::uint32_t limit) const {
  if (limit == 0 || limit > 256) throw std::invalid_argument ("Invalid application recovery page size");
  statement st (db_, "SELECT operation,descriptor FROM apply_intents "
    "WHERE vault=? AND completed=0 AND operation>? ORDER BY operation LIMIT ?");
  st.text (1, vault); st.text (2, after); check (db_, sqlite3_bind_int (st.value, 3, int (limit)));
  std::vector<apply_intent> result;
  while (st.row ()) result.push_back (read_intent (st.bytes (0), st.bytes (1), false));
  return result;
}
bool revision_store::finish_apply (const std::string& operation,
  const std::optional<std::string>& observed_fingerprint) {
  transaction tx (db_);
  auto intent= application (operation);
  if (!intent) throw std::invalid_argument ("Unknown application operation");
  if (intent->completed) { tx.commit (); return false; }
  auto target= offer (intent->revision_id);
  if (!target) throw std::invalid_argument ("Unknown application target");
  if (!eligible (intent->revision_id)) throw std::invalid_argument ("Application target lacks an authoritative conflict decision");
  const auto& r= target->metadata;
  if (applied (r.vault, r.object) != intent->expected_revision)
    throw std::invalid_argument ("Application base changed before completion");
  if (r.deleted) {
    if (observed_fingerprint) throw std::invalid_argument ("Deleted application target still exists");
  }
  else {
    if (!observed_fingerprint || !digest_id (*observed_fingerprint))
      throw std::invalid_argument ("Application target fingerprint is missing");
    if (payload_fingerprint (r.id) != *observed_fingerprint)
      throw std::invalid_argument ("Application target fingerprint does not match received content");
  }
  statement applied (db_, "INSERT INTO applied VALUES(?,?,?) "
    "ON CONFLICT(vault,object) DO UPDATE SET revision=excluded.revision");
  applied.text (1, r.vault); applied.text (2, r.object); applied.text (3, r.id); applied.row ();
  statement complete (db_, "UPDATE apply_intents SET completed=1 WHERE operation=?");
  complete.text (1, operation); complete.row (); tx.commit (); return true;
}
} // namespace athena::hodarium

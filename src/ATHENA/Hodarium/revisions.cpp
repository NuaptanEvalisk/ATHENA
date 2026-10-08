/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "revisions.hpp"
#include "sqlite_internal.hpp"

#include <nlohmann/json.hpp>
#include <sodium.h>
#include <sqlite3.h>

#include <algorithm>
#include <stdexcept>
#include <string_view>

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

std::string identity (const revision& r) {
  static const int initialized= sodium_init ();
  if (initialized < 0) throw std::runtime_error ("Cannot initialize libsodium");
  auto header= json::to_cbor (descriptor (r));
  crypto_hash_sha256_state hash;
  crypto_hash_sha256_init (&hash);
  constexpr std::string_view domain= "ATHENA-HODARIUM-REVISION-v1";
  crypto_hash_sha256_update (&hash,
    reinterpret_cast<const unsigned char*> (domain.data ()), domain.size ());
  // CBOR is self-delimiting; payload is the remainder of the hashed message.
  crypto_hash_sha256_update (&hash, header.data (), header.size ());
  crypto_hash_sha256_update (&hash,
    reinterpret_cast<const unsigned char*> (r.payload.data ()), r.payload.size ());
  unsigned char digest[crypto_hash_sha256_BYTES];
  crypto_hash_sha256_final (&hash, digest);
  char hex[crypto_hash_sha256_BYTES * 2 + 1];
  sodium_bin2hex (hex, sizeof hex, digest, sizeof digest);
  return hex;
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
      if (!version.row () || sqlite3_column_int (version.value, 0) > 1)
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
      "CREATE TABLE IF NOT EXISTS applied("
      "vault TEXT NOT NULL,object TEXT NOT NULL,"
      "revision TEXT NOT NULL REFERENCES revisions(id),PRIMARY KEY(vault,object));"
      "PRAGMA user_version=1;");
    tx.commit ();
  }
  catch (...) { sqlite3_close (db_); db_= nullptr; throw; }
}
revision_store::~revision_store () { sqlite3_close (db_); }

std::optional<revision> revision_store::get (const std::string& id) const {
  statement st (db_, "SELECT descriptor,payload FROM revisions WHERE id=?");
  st.text (1, id);
  if (!st.row ()) return std::nullopt;
  auto d= json::parse (st.bytes (0));
  revision r;
  r.id= id;
  r.vault= d.at ("vault"); r.object= d.at ("object");
  r.origin_member= d.at ("origin_member"); r.format= d.at ("format");
  r.semantic_version= d.at ("semantic_version"); r.relative_path= d.at ("path");
  r.deleted= d.at ("deleted");
  r.parents= d.at ("parents").get<std::vector<std::string>> ();
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
  for (const auto& parent: r.parents) {
    statement st (db_, "SELECT vault,object FROM revisions WHERE id=?");
    st.text (1, parent);
    if (!st.row ()) throw std::invalid_argument ("Missing revision parent");
    if (st.bytes (0) != r.vault || st.bytes (1) != r.object)
      throw std::invalid_argument ("Revision parent belongs to another object");
  }
  statement insert (db_, "INSERT INTO revisions VALUES(?,?,?,?,?)");
  insert.text (1, r.id); insert.text (2, r.vault); insert.text (3, r.object);
  insert.text (4, descriptor (r).dump ()); insert.blob (5, r.payload);
  insert.row ();
  for (const auto& parent: r.parents) {
    statement edge (db_, "INSERT INTO parents VALUES(?,?)");
    edge.text (1, r.id); edge.text (2, parent); edge.row ();
  }
  tx.commit ();
  return true;
}

std::vector<std::string> revision_store::heads (
    const std::string& vault, const std::string& object) const {
  statement st (db_, "SELECT id FROM revisions WHERE vault=? AND object=? "
    "AND NOT EXISTS(SELECT 1 FROM parents WHERE parent=revisions.id) ORDER BY id");
  st.text (1, vault); st.text (2, object);
  std::vector<std::string> result;
  while (st.row ()) result.push_back (st.bytes (0));
  return result;
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

bool revision_store::record_applied (
    const std::string& id, const std::optional<std::string>& expected) {
  transaction tx (db_);
  statement object (db_, "SELECT vault,object FROM revisions WHERE id=?");
  object.text (1, id);
  if (!object.row ()) throw std::invalid_argument ("Unknown applied revision");
  auto vault= object.bytes (0), key= object.bytes (1);
  if (applied (vault, key) != expected) { tx.commit (); return false; }
  if (expected && !ancestor_of (*expected, id))
    throw std::invalid_argument ("Application must advance causal history");
  statement st (db_, "INSERT INTO applied VALUES(?,?,?) "
    "ON CONFLICT(vault,object) DO UPDATE SET revision=excluded.revision");
  st.text (1, vault); st.text (2, key); st.text (3, id); st.row ();
  tx.commit ();
  return true;
}
} // namespace athena::hodarium

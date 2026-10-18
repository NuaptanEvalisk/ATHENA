/******************************************************************************
* MODULE     : vault_xml_v3_history.cpp
* DESCRIPTION: Complete detached XML v3 document-history/Fossil migration
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include "vault_xml_v3_history.hpp"
#include "athena_document_xml.hpp"
#include "math_v2_to_v3.hpp"
#include "document_upgrade_file.hpp"
#include "ATHENA/Data/fossil_delta.hpp"
#include <QUrl>
#include <QUrlQuery>

#include <sqlite3.h>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace athena::document {
namespace fs= std::filesystem;
namespace {

class database {
  sqlite3* db_= nullptr;
public:
  database (const fs::path& file, bool read_only) {
    const int flags= read_only ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE;
    std::string filename= file.string ();
    int open_flags= flags | SQLITE_OPEN_NOFOLLOW;
    if (read_only) {
      // Immutable SQLite reads neither initialize a WAL shared-memory file
      // nor checkpoint the source. A nonempty WAL is a hard offline blocker:
      // silently ignoring its uncheckpointed revisions would lose history.
      const fs::path wal (filename + "-wal");
      if (fs::exists (wal) && fs::file_size (wal) != 0)
        throw std::runtime_error (
          "History database needs an offline WAL checkpoint before v3 migration");
      QUrl uri= QUrl::fromLocalFile (QString::fromStdString (filename));
      QUrlQuery query; query.addQueryItem ("immutable", "1");
      uri.setQuery (query);
      filename= uri.toEncoded (QUrl::FullyEncoded).toStdString ();
      open_flags|= SQLITE_OPEN_URI;
    }
    if (sqlite3_open_v2 (filename.c_str (), &db_, open_flags,
                         nullptr) != SQLITE_OK) {
      const std::string error= db_ ? sqlite3_errmsg (db_) : "Cannot open history database";
      if (db_) sqlite3_close (db_);
      db_= nullptr;
      throw std::runtime_error (file.string () + ": " + error);
    }
    if (sqlite3_busy_timeout (db_, 10000) != SQLITE_OK)
      throw std::runtime_error ("Cannot set history database busy timeout");
  }
  ~database () { if (db_) sqlite3_close (db_); }
  database (const database&)= delete;
  sqlite3* get () const { return db_; }
  void exec (const char* sql) const {
    char* message= nullptr;
    const int code= sqlite3_exec (db_, sql, nullptr, nullptr, &message);
    if (code != SQLITE_OK) {
      const std::string error= message ? message : sqlite3_errmsg (db_);
      sqlite3_free (message);
      throw std::runtime_error (error);
    }
  }
};

class statement {
  sqlite3_stmt* st_= nullptr;
public:
  statement (const database& db, const char* sql) {
    if (sqlite3_prepare_v2 (db.get (), sql, -1, &st_, nullptr) != SQLITE_OK)
      throw std::runtime_error (sqlite3_errmsg (db.get ()));
  }
  ~statement () { if (st_) sqlite3_finalize (st_); }
  statement (const statement&)= delete;
  sqlite3_stmt* get () const { return st_; }
  bool row () {
    const int code= sqlite3_step (st_);
    if (code == SQLITE_ROW) return true;
    if (code == SQLITE_DONE) return false;
    throw std::runtime_error (sqlite3_errmsg (sqlite3_db_handle (st_)));
  }
  void reset () {
    if (sqlite3_reset (st_) != SQLITE_OK)
      throw std::runtime_error (sqlite3_errmsg (sqlite3_db_handle (st_)));
    sqlite3_clear_bindings (st_);
  }
  void integer (int index, std::int64_t value) {
    if (sqlite3_bind_int64 (st_, index, value) != SQLITE_OK)
      throw std::runtime_error (sqlite3_errmsg (sqlite3_db_handle (st_)));
  }
  void text (int index, const std::string& value) {
    if (value.size () > std::size_t (std::numeric_limits<int>::max ()) ||
        sqlite3_bind_text (st_, index, value.data (), int (value.size ()),
                           SQLITE_TRANSIENT) != SQLITE_OK)
      throw std::runtime_error ("Could not bind v3 history string");
  }
  void blob (int index, const std::string& value) {
    if (value.size () > std::size_t (std::numeric_limits<int>::max ()) ||
        sqlite3_bind_blob (st_, index, value.data (), int (value.size ()),
                           SQLITE_TRANSIENT) != SQLITE_OK)
      throw std::runtime_error ("Could not bind v3 history content");
  }
};

std::string column_text (sqlite3_stmt* st, int col) {
  const char* text= reinterpret_cast<const char*> (sqlite3_column_text (st, col));
  const int size= sqlite3_column_bytes (st, col);
  return text ? std::string (text, std::size_t (size)) : std::string ();
}

std::string column_blob (sqlite3_stmt* st, int col) {
  const char* source= static_cast<const char*> (sqlite3_column_blob (st, col));
  const int size= sqlite3_column_bytes (st, col);
  return source && size > 0 ? std::string (source, std::size_t (size)) : std::string ();
}

void validate_database (const database& db) {
  statement pragma (db, "PRAGMA quick_check");
  if (!pragma.row () || column_text (pragma.get (), 0) != "ok")
    throw std::runtime_error ("Invalid document history database");
}

bool history_document (const std::string& path) {
  return fs::path (path).extension () == ".ath";
}

struct original_version {
  std::int64_t id= 0, base= 0;
  std::string path;
  bool protected_snapshot= false;
};

std::vector<original_version> all_versions (const database& db) {
  statement query (db,
    "SELECT v.id,v.path,COALESCE(v.base_id,0),"
    "EXISTS(SELECT 1 FROM protected_versions p WHERE p.version_id=v.id) "
    "FROM versions v ORDER BY v.id");
  std::vector<original_version> result;
  while (query.row ()) {
    if (result.size () >= 1000000)
      throw std::length_error ("Too many historical versions for an offline migration");
    result.push_back ({sqlite3_column_int64 (query.get (), 0),
                       sqlite3_column_int64 (query.get (), 2),
                       column_text (query.get (), 1),
                       sqlite3_column_int (query.get (), 3) != 0});
  }
  return result;
}

std::string reconstruct (const database& db, std::int64_t version_id) {
  struct delta_step {
    std::string bytes, hash;
    std::int64_t length= 0;
  };
  statement read (db,
    "SELECT COALESCE(base_id,0),encoding,payload,content_size,content_hash "
    "FROM versions WHERE id=?1");
  std::vector<delta_step> deltas;
  std::set<std::int64_t> visited;
  std::int64_t current= version_id;
  for (int depth= 0; depth<64; ++depth) {
    if (!visited.insert (current).second)
      throw std::runtime_error ("Document history delta cycle");
    read.reset ();
    read.integer (1, current);
    if (!read.row ())
      throw std::runtime_error ("Missing historical version " + std::to_string (current));
    const std::int64_t base= sqlite3_column_int64 (read.get (), 0);
    const int encoding= sqlite3_column_int (read.get (), 1);
    std::string payload= column_blob (read.get (), 2);
    const auto expected_size= sqlite3_column_int64 (read.get (), 3);
    std::string hash= column_text (read.get (), 4);
    if (expected_size < 0 || expected_size >
        static_cast<std::int64_t> (codec_limits ().input_bytes))
      throw std::runtime_error ("Historical document exceeds XML input budget");
    if (encoding == 0) {
      if (base != 0 || std::int64_t (payload.size ()) != expected_size ||
          storage_bytes_fingerprint (payload) != hash)
        throw std::runtime_error ("Historical full checkpoint checksum mismatch");
      for (auto it= deltas.rbegin (); it != deltas.rend (); ++it) {
        const auto next= history::fossil_delta_apply (payload, it->bytes);
        if (!next || std::int64_t (next->size ()) != it->length ||
            storage_bytes_fingerprint (*next) != it->hash)
          throw std::runtime_error ("Historical Fossil delta checksum mismatch");
        payload= *next;
      }
      return payload;
    }
    if (encoding != 1 || base <= 0)
      throw std::runtime_error ("Invalid historical delta encoding");
    deltas.push_back ({std::move (payload), std::move (hash), expected_size});
    current= base;
  }
  throw std::runtime_error ("Historical delta chain is too deep");
}

struct migrated_version {
  original_version original;
  std::string original_bytes, v3_bytes;
  std::int64_t new_base= 0;
  int encoding= 0, chain_depth= 0;
  std::string encoded_payload;
};

} // namespace

v3_history_migration_stats migrate_vault_history_v3 (
  const fs::path& original_vault, const fs::path* staged_vault,
  bool strict,
  const std::function<void(std::size_t,std::size_t,const std::string&)>& progress) {
  const fs::path relative= fs::path (".athena") / "document-history.sqlite";
  const fs::path original_file= original_vault / relative;
  v3_history_migration_stats stats;
  if (!fs::exists (original_file)) return stats;
  if (!fs::is_regular_file (fs::symlink_status (original_file)))
    throw std::runtime_error ("History source cannot be a symlink or non-file");
  database original (original_file, true);
  original.exec ("PRAGMA query_only=ON");
  original.exec ("BEGIN");
  validate_database (original);
  const auto versions= all_versions (original);
  std::vector<migrated_version> converted;
  converted.reserve (versions.size ());
  std::map<std::int64_t,std::pair<std::string,int>> earlier;

  for (const auto& version: versions) {
    if (progress) progress (converted.size (), versions.size (), version.path);
    if (!history_document (version.path)) continue;
    migrated_version next;
    next.original= version;
    try {
      next.original_bytes= reconstruct (original, version.id);
      const tree old= read_xml_v2 (next.original_bytes);
      const auto result= migrate_math_v2_tree (old, strict);
      validate_cold_v3_migration (result.document);
      stats.warnings += result.warnings.size ();
      next.v3_bytes= write_xml_v3 (result.document);
      if (read_xml_v3 (next.v3_bytes) != result.document)
        throw std::runtime_error ("History v3 codec round trip differs from migration result");
      if (version.base > 0) {
        auto parent= earlier.find (version.base);
        if (parent != earlier.end () && parent->second.second < 19) {
          auto delta= history::fossil_delta_create (parent->second.first, next.v3_bytes);
          if (!delta.empty () && delta.size () + 16 < next.v3_bytes.size ()) {
            next.encoded_payload= std::move (delta);
            next.new_base= version.base;
            next.encoding= 1;
            next.chain_depth= parent->second.second + 1;
          }
        }
      }
      if (next.encoding == 0) next.encoded_payload= next.v3_bytes;
      earlier[version.id]= {next.v3_bytes, next.chain_depth};
    }
    catch (const std::exception& error) {
      throw std::runtime_error (
        "Cannot migrate historical version " + std::to_string (version.id) +
        " of " + version.path + ": " + error.what ());
    }
    ++stats.versions_converted;
    if (version.protected_snapshot) ++stats.protected_evidence;
    converted.push_back (std::move (next));
  }
  if (progress) progress (versions.size (), versions.size (), {});
  if (!staged_vault || converted.empty ()) return stats;
  const fs::path staged_file= *staged_vault / relative;
  if (!fs::is_regular_file (fs::symlink_status (staged_file)))
    throw std::runtime_error ("Staged history database missing or symlinked");

  database target (staged_file, false);
  target.exec ("PRAGMA foreign_keys=ON");
  validate_database (target);
  target.exec ("BEGIN IMMEDIATE");
  try {
    target.exec (
      "CREATE TABLE IF NOT EXISTS xml_v3_protected_evidence("
      "version_id INTEGER PRIMARY KEY REFERENCES versions(id) ON DELETE RESTRICT,"
      "original_payload BLOB NOT NULL,"
      "original_content_hash TEXT NOT NULL,"
      "original_size INTEGER NOT NULL)");
    statement evidence (target,
      "INSERT INTO xml_v3_protected_evidence"
      "(version_id,original_payload,original_content_hash,original_size)"
      " VALUES(?1,?2,?3,?4)");
    statement update (target,
      "UPDATE versions SET base_id=?1,encoding=?2,payload=?3,content_size=?4,"
      "content_hash=?5,chain_depth=?6 WHERE id=?7");
    for (const auto& entry: converted) {
      if (entry.original.protected_snapshot) {
        evidence.reset ();
        evidence.integer (1, entry.original.id);
        evidence.blob (2, entry.original_bytes);
        evidence.text (3, storage_bytes_fingerprint (entry.original_bytes));
        evidence.integer (4, std::int64_t (entry.original_bytes.size ()));
        if (evidence.row ()) throw std::runtime_error ("Unexpected protected evidence row");
      }
      update.reset ();
      if (entry.new_base)
        update.integer (1, entry.new_base);
      else if (sqlite3_bind_null (update.get (), 1) != SQLITE_OK)
        throw std::runtime_error ("Cannot clear old Fossil delta reference");
      update.integer (2, entry.encoding);
      update.blob (3, entry.encoded_payload);
      update.integer (4, std::int64_t (entry.v3_bytes.size ()));
      update.text (5, storage_bytes_fingerprint (entry.v3_bytes));
      update.integer (6, entry.chain_depth);
      update.integer (7, entry.original.id);
      if (update.row ()) throw std::runtime_error ("Unexpected historical update row");
      if (sqlite3_changes (target.get ()) != 1)
        throw std::runtime_error ("Historical source version vanished during migration");
    }
    target.exec ("COMMIT");
  }
  catch (...) {
    try { target.exec ("ROLLBACK"); } catch (...) {}
    throw;
  }
  validate_database (target);
  for (const auto& entry: converted) {
    if (reconstruct (target, entry.original.id) != entry.v3_bytes)
      throw std::runtime_error (
        "Post-migration historical reconstruction mismatch: " +
        std::to_string (entry.original.id));
    const tree reread= read_xml_v3 (entry.v3_bytes);
    (void) reread;
  }
  target.exec ("PRAGMA wal_checkpoint(TRUNCATE)");
  return stats;
}

} // namespace athena::document

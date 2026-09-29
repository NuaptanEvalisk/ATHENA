/******************************************************************************
* MODULE     : document_history_store.cpp
* DESCRIPTION: SQLite + Fossil-delta file-scoped document history store
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include "document_history_store.hpp"

#include "Data/Convert/Xml/document_upgrade_file.hpp"
#include "fossil_delta.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <limits>
#include <unordered_set>

namespace athena::history {
namespace {

struct statement {
  sqlite3_stmt* value= nullptr;
  ~statement () { if (value != nullptr) sqlite3_finalize (value); }
};

bool
sql (sqlite3* db, const char* query, std::string& error) {
  char* message= nullptr;
  int rc= sqlite3_exec (db, query, nullptr, nullptr, &message);
  if (rc == SQLITE_OK) return true;
  error= message != nullptr ? message : sqlite3_errmsg (db);
  sqlite3_free (message);
  return false;
}

bool
prepare (sqlite3* db, statement& st, const char* query, std::string& error) {
  if (sqlite3_prepare_v2 (db, query, -1, &st.value, nullptr) == SQLITE_OK)
    return true;
  error= sqlite3_errmsg (db);
  return false;
}

bool
bind_text (sqlite3_stmt* st, int index, const std::string& value) {
  return sqlite3_bind_text (st, index, value.data (),
                            static_cast<int> (value.size ()),
                            SQLITE_TRANSIENT) == SQLITE_OK;
}

std::string
column_text (sqlite3_stmt* st, int column) {
  const auto* value= sqlite3_column_text (st, column);
  return value == nullptr ? std::string () :
    std::string (reinterpret_cast<const char*> (value),
                 static_cast<std::size_t> (sqlite3_column_bytes (st, column)));
}

std::string
column_blob (sqlite3_stmt* st, int column) {
  const auto* value= static_cast<const char*> (sqlite3_column_blob (st, column));
  int size= sqlite3_column_bytes (st, column);
  return value == nullptr || size <= 0 ? std::string () :
    std::string (value, static_cast<std::size_t> (size));
}

std::int64_t
now_ms () {
  using namespace std::chrono;
  return duration_cast<milliseconds> (
    system_clock::now ().time_since_epoch ()).count ();
}

std::string
normalize_relative (const std::string& path) {
  return std::filesystem::path (path).lexically_normal ().generic_string ();
}

} // namespace

bool
valid_relative_document_path (const std::string& path) {
  if (path.empty () || path.find ('\0') != std::string::npos) return false;
  std::filesystem::path value (path);
  if (value.is_absolute ()) return false;
  value= value.lexically_normal ();
  if (value.empty () || value == ".") return false;
  for (const auto& part: value) {
    if (part == ".." || part == ".") return false;
  }
  auto first= *value.begin ();
  return first != ".athena" && first != ".backup" && first != ".git" &&
         value != "Vaultfile.json";
}

document_history_store::~document_history_store () {
  if (db_ != nullptr) sqlite3_close (db_);
}

bool
document_history_store::open (const std::filesystem::path& vault_root,
                              std::string& error) {
  namespace fs= std::filesystem;
  std::error_code ec;
  root_= fs::weakly_canonical (vault_root, ec);
  if (ec) root_= fs::absolute (vault_root, ec).lexically_normal ();
  if (root_.empty () || !fs::is_directory (root_)) {
    error= "Document history vault root is not a directory";
    return false;
  }
  fs::path directory= root_ / ".athena";
  database_= directory / "document-history.sqlite";
  if (fs::is_symlink (directory, ec) || fs::is_symlink (database_, ec)) {
    error= "Document history database cannot be a symbolic link";
    return false;
  }
  fs::create_directories (directory, ec);
  if (ec) { error= ec.message (); return false; }
  if (db_ != nullptr) { sqlite3_close (db_); db_= nullptr; }
  if (sqlite3_open_v2 (database_.string ().c_str (), &db_,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
    error= db_ != nullptr ? sqlite3_errmsg (db_) :
      "Could not open document history database";
    return false;
  }
  sqlite3_busy_timeout (db_, 5000);
  statement version;
  if (!prepare (db_, version, "PRAGMA user_version;", error)) return false;
  if (sqlite3_step (version.value) != SQLITE_ROW ||
      sqlite3_column_int (version.value, 0) > 1) {
    error= "Unsupported document history database version";
    return false;
  }
  sqlite3_finalize (version.value); version.value= nullptr;
  return sql (db_,
    "PRAGMA foreign_keys=ON;"
    "PRAGMA journal_mode=WAL;"
    "PRAGMA synchronous=NORMAL;"
    "CREATE TABLE IF NOT EXISTS versions("
      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "path TEXT NOT NULL,"
      "created_at_ms INTEGER NOT NULL,"
      "trigger TEXT NOT NULL,"
      "base_id INTEGER REFERENCES versions(id),"
      "encoding INTEGER NOT NULL CHECK(encoding IN(0,1)),"
      "payload BLOB NOT NULL,"
      "content_size INTEGER NOT NULL,"
      "content_hash TEXT NOT NULL,"
      "chain_depth INTEGER NOT NULL);"
    "CREATE INDEX IF NOT EXISTS versions_path_id ON versions(path,id DESC);"
    "PRAGMA user_version=1;", error);
}

bool
document_history_store::reconstruct (
    std::int64_t version_id, std::string& content, std::string& error) const {
  if (db_ == nullptr || version_id <= 0) {
    error= "Document history store is not open";
    return false;
  }
  struct pending_delta {
    std::string payload;
    std::int64_t expected_size= 0;
    std::string expected_hash;
  };
  std::vector<pending_delta> deltas;
  std::unordered_set<std::int64_t> visited;
  std::int64_t current= version_id;
  for (int depth=0; depth<64; ++depth) {
    if (!visited.insert (current).second) {
      error= "Document history delta cycle detected";
      return false;
    }
    statement query;
    if (!prepare (db_, query,
          "SELECT base_id,encoding,payload,content_size,content_hash "
          "FROM versions WHERE id=?1;", error))
      return false;
    sqlite3_bind_int64 (query.value, 1, current);
    if (sqlite3_step (query.value) != SQLITE_ROW) {
      error= "Document history version does not exist";
      return false;
    }
    const std::int64_t base= sqlite3_column_type (query.value, 0) == SQLITE_NULL ?
      0 : sqlite3_column_int64 (query.value, 0);
    const int encoding= sqlite3_column_int (query.value, 1);
    std::string payload= column_blob (query.value, 2);
    const std::int64_t expected_size= sqlite3_column_int64 (query.value, 3);
    const std::string expected_hash= column_text (query.value, 4);
    if (encoding == 0) {
      content= std::move (payload);
      if (static_cast<std::int64_t> (content.size ()) != expected_size ||
          athena::document::storage_bytes_fingerprint (content) != expected_hash) {
        error= "Document history full checkpoint is damaged";
        return false;
      }
      for (auto it= deltas.rbegin (); it != deltas.rend (); ++it) {
        auto rebuilt= fossil_delta_apply (content, it->payload);
        if (!rebuilt) {
          error= "Document history Fossil delta is damaged";
          return false;
        }
        content= std::move (*rebuilt);
        if (static_cast<std::int64_t> (content.size ()) != it->expected_size ||
            athena::document::storage_bytes_fingerprint (content) !=
              it->expected_hash) {
          error= "Document history Fossil delta checksum mismatch";
          return false;
        }
      }
      return true;
    }
    if (encoding != 1 || base <= 0) {
      error= "Invalid document history encoding";
      return false;
    }
    deltas.push_back ({std::move (payload), expected_size, expected_hash});
    current= base;
  }
  error= "Document history delta chain is too deep";
  return false;
}

bool
document_history_store::capture (
    const std::string& relative_path, std::string_view content,
    const std::string& trigger, std::optional<std::int64_t> retention_seconds,
    bool& inserted, std::int64_t& version_id, std::string& error) {
  inserted= false;
  version_id= 0;
  if (db_ == nullptr || !valid_relative_document_path (relative_path)) {
    error= "Invalid document history path";
    return false;
  }
  const std::string path= normalize_relative (relative_path);
  const std::string hash=
    athena::document::storage_bytes_fingerprint (content);

  std::int64_t previous_id= 0;
  std::string previous_hash;
  int previous_depth= -1;
  {
    statement latest;
    if (!prepare (db_, latest,
          "SELECT id,content_hash,chain_depth FROM versions "
          "WHERE path=?1 ORDER BY id DESC LIMIT 1;", error) ||
        !bind_text (latest.value, 1, path))
      return false;
    int rc= sqlite3_step (latest.value);
    if (rc == SQLITE_ROW) {
      previous_id= sqlite3_column_int64 (latest.value, 0);
      previous_hash= column_text (latest.value, 1);
      previous_depth= sqlite3_column_int (latest.value, 2);
    }
    else if (rc != SQLITE_DONE) {
      error= sqlite3_errmsg (db_);
      return false;
    }
  }
  if (previous_id != 0 && previous_hash == hash) {
    version_id= previous_id;
    return true;
  }

  std::string payload (content);
  std::int64_t base_id= 0;
  int encoding= 0;
  int chain_depth= 0;
  if (previous_id != 0 && previous_depth < 19) {
    std::string previous;
    if (!reconstruct (previous_id, previous, error)) return false;
    std::string delta= fossil_delta_create (previous, content);
    if (!delta.empty () && delta.size () + 16 < content.size ()) {
      payload= std::move (delta);
      base_id= previous_id;
      encoding= 1;
      chain_depth= previous_depth + 1;
    }
  }

  if (!sql (db_, "BEGIN IMMEDIATE;", error)) return false;
  bool committed= false;
  auto rollback= [&] {
    if (!committed) {
      std::string ignored;
      (void) sql (db_, "ROLLBACK;", ignored);
    }
  };
  statement insert;
  if (!prepare (db_, insert,
        "INSERT INTO versions(path,created_at_ms,trigger,base_id,encoding,payload,"
        "content_size,content_hash,chain_depth) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9);",
        error) ||
      !bind_text (insert.value, 1, path) ||
      sqlite3_bind_int64 (insert.value, 2, now_ms ()) != SQLITE_OK ||
      !bind_text (insert.value, 3, trigger) ||
      (base_id == 0 ? sqlite3_bind_null (insert.value, 4) :
       sqlite3_bind_int64 (insert.value, 4, base_id)) != SQLITE_OK ||
      sqlite3_bind_int (insert.value, 5, encoding) != SQLITE_OK ||
      sqlite3_bind_blob (insert.value, 6, payload.data (),
                         static_cast<int> (payload.size ()), SQLITE_TRANSIENT) != SQLITE_OK ||
      sqlite3_bind_int64 (insert.value, 7,
                          static_cast<sqlite3_int64> (content.size ())) != SQLITE_OK ||
      !bind_text (insert.value, 8, hash) ||
      sqlite3_bind_int (insert.value, 9, chain_depth) != SQLITE_OK ||
      sqlite3_step (insert.value) != SQLITE_DONE) {
    if (error.empty ()) error= sqlite3_errmsg (db_);
    rollback ();
    return false;
  }
  version_id= sqlite3_last_insert_rowid (db_);
  if (!sql (db_, "COMMIT;", error)) {
    rollback ();
    return false;
  }
  committed= true;
  inserted= true;

  if (retention_seconds && *retention_seconds > 0) {
    const std::int64_t cutoff= now_ms () - *retention_seconds * 1000;
    if (!prune (path, cutoff, error)) return false;
  }
  return true;
}

bool
document_history_store::list (
    const std::string& relative_path, std::vector<version_entry>& versions,
    std::string& error) const {
  versions.clear ();
  if (db_ == nullptr || !valid_relative_document_path (relative_path)) {
    error= "Invalid document history path";
    return false;
  }
  const std::string path= normalize_relative (relative_path);
  statement query;
  if (!prepare (db_, query,
        "SELECT id,path,created_at_ms,trigger,COALESCE(base_id,0),encoding,"
        "content_size,length(payload),content_hash,chain_depth FROM versions "
        "WHERE path=?1 ORDER BY id DESC;", error) ||
      !bind_text (query.value, 1, path))
    return false;
  int rc;
  while ((rc= sqlite3_step (query.value)) == SQLITE_ROW) {
    versions.push_back ({
      sqlite3_column_int64 (query.value, 0), column_text (query.value, 1),
      sqlite3_column_int64 (query.value, 2), column_text (query.value, 3),
      sqlite3_column_int64 (query.value, 4),
      sqlite3_column_int (query.value, 5) == 1,
      sqlite3_column_int64 (query.value, 6),
      sqlite3_column_int64 (query.value, 7), column_text (query.value, 8),
      sqlite3_column_int (query.value, 9)});
  }
  if (rc == SQLITE_DONE) return true;
  error= sqlite3_errmsg (db_);
  return false;
}

bool
document_history_store::prune (
    const std::string& relative_path, std::int64_t cutoff_ms,
    std::string& error) {
  statement first_kept;
  if (!prepare (db_, first_kept,
        "SELECT id FROM versions WHERE path=?1 AND created_at_ms>=?2 "
        "ORDER BY id LIMIT 1;", error) ||
      !bind_text (first_kept.value, 1, relative_path) ||
      sqlite3_bind_int64 (first_kept.value, 2, cutoff_ms) != SQLITE_OK)
    return false;
  int rc= sqlite3_step (first_kept.value);
  if (rc == SQLITE_DONE) {
    // Keep the newest version even when every version is older than retention.
    statement newest;
    if (!prepare (db_, newest,
          "SELECT id FROM versions WHERE path=?1 ORDER BY id DESC LIMIT 1;",
          error) || !bind_text (newest.value, 1, relative_path))
      return false;
    rc= sqlite3_step (newest.value);
    if (rc != SQLITE_ROW) return rc == SQLITE_DONE;
    sqlite3_finalize (first_kept.value); first_kept.value= newest.value;
    newest.value= nullptr;
  }
  else if (rc != SQLITE_ROW) {
    error= sqlite3_errmsg (db_);
    return false;
  }
  std::int64_t anchor= sqlite3_column_int64 (first_kept.value, 0);
  for (int depth=0; depth<64; ++depth) {
    statement base;
    if (!prepare (db_, base, "SELECT base_id FROM versions WHERE id=?1;", error))
      return false;
    sqlite3_bind_int64 (base.value, 1, anchor);
    if (sqlite3_step (base.value) != SQLITE_ROW) return false;
    if (sqlite3_column_type (base.value, 0) == SQLITE_NULL) break;
    std::int64_t next= sqlite3_column_int64 (base.value, 0);
    if (next <= 0 || next >= anchor) {
      error= "Invalid document history dependency chain";
      return false;
    }
    anchor= next;
  }
  statement remove_old;
  if (!prepare (db_, remove_old,
        "DELETE FROM versions WHERE path=?1 AND id<?2;", error) ||
      !bind_text (remove_old.value, 1, relative_path) ||
      sqlite3_bind_int64 (remove_old.value, 2, anchor) != SQLITE_OK ||
      sqlite3_step (remove_old.value) != SQLITE_DONE) {
    if (error.empty ()) error= sqlite3_errmsg (db_);
    return false;
  }
  return true;
}

bool
document_history_store::rename_path (
    const std::string& old_relative_path, const std::string& new_relative_path,
    bool directory, std::string& error) {
  if (db_ == nullptr || !valid_relative_document_path (old_relative_path) ||
      !valid_relative_document_path (new_relative_path)) {
    error= "Invalid document history rename path";
    return false;
  }
  const std::string old_path= normalize_relative (old_relative_path);
  const std::string new_path= normalize_relative (new_relative_path);
  if (!directory) {
    statement update;
    if (!prepare (db_, update,
          "UPDATE versions SET path=?1 WHERE path=?2;", error) ||
        !bind_text (update.value, 1, new_path) ||
        !bind_text (update.value, 2, old_path) ||
        sqlite3_step (update.value) != SQLITE_DONE) {
      if (error.empty ()) error= sqlite3_errmsg (db_);
      return false;
    }
    return true;
  }
  const std::string prefix= old_path + "/";
  statement rows;
  if (!prepare (db_, rows, "SELECT DISTINCT path FROM versions;", error))
    return false;
  std::vector<std::string> paths;
  int rc;
  while ((rc= sqlite3_step (rows.value)) == SQLITE_ROW) {
    std::string path= column_text (rows.value, 0);
    if (path == old_path || path.rfind (prefix, 0) == 0)
      paths.push_back (std::move (path));
  }
  if (rc != SQLITE_DONE) { error= sqlite3_errmsg (db_); return false; }
  if (!sql (db_, "BEGIN IMMEDIATE;", error)) return false;
  for (const auto& path: paths) {
    const std::string suffix= path == old_path ? std::string () :
      path.substr (old_path.size ());
    statement update;
    if (!prepare (db_, update,
          "UPDATE versions SET path=?1 WHERE path=?2;", error) ||
        !bind_text (update.value, 1, new_path + suffix) ||
        !bind_text (update.value, 2, path) ||
        sqlite3_step (update.value) != SQLITE_DONE) {
      std::string ignored; (void) sql (db_, "ROLLBACK;", ignored);
      if (error.empty ()) error= sqlite3_errmsg (db_);
      return false;
    }
  }
  return sql (db_, "COMMIT;", error);
}

} // namespace athena::history

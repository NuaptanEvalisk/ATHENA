/******************************************************************************
* MODULE     : rag_storage.cpp
* DESCRIPTION: Shared Continuous RAG SQLite schema and reset policy
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "rag_storage.hpp"

#include "tm_ostream.hpp"

#include <sqlite3.h>

#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace athena::rag::storage {
namespace {

std::mutex database_reset_mutex;

bool
exec_sql (sqlite3* db, const char* sql, std::string& error) {
  char* message= nullptr;
  const int rc= sqlite3_exec (db, sql, nullptr, nullptr, &message);
  if (rc == SQLITE_OK) return true;
  error= message == nullptr ? sqlite3_errmsg (db) : message;
  sqlite3_free (message);
  return false;
}

std::string
read_version (const fs::path& path) {
  sqlite3* db= nullptr;
  if (sqlite3_open_v2 (
        path.string ().c_str (), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
    if (db != nullptr) sqlite3_close (db);
    return {};
  }
  sqlite3_stmt* statement= nullptr;
  std::string result;
  if (sqlite3_prepare_v2 (
        db, "SELECT value FROM meta WHERE key='schema-version'", -1,
        &statement, nullptr) == SQLITE_OK &&
      sqlite3_step (statement) == SQLITE_ROW) {
    const unsigned char* text= sqlite3_column_text (statement, 0);
    if (text != nullptr) result= reinterpret_cast<const char*> (text);
  }
  if (statement != nullptr) sqlite3_finalize (statement);
  sqlite3_close (db);
  return result;
}

bool
current_chunks_layout (const fs::path& path) {
  sqlite3* db= nullptr;
  if (sqlite3_open_v2 (
        path.string ().c_str (), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
    if (db != nullptr) sqlite3_close (db);
    return false;
  }
  sqlite3_stmt* statement= nullptr;
  bool have_input_hash= false;
  bool have_space= false;
  bool have_legacy_blob= false;
  if (sqlite3_prepare_v2 (
        db, "PRAGMA table_info(chunks)", -1, &statement, nullptr) == SQLITE_OK) {
    while (sqlite3_step (statement) == SQLITE_ROW) {
      const unsigned char* text= sqlite3_column_text (statement, 1);
      if (text == nullptr) continue;
      const std::string name (reinterpret_cast<const char*> (text));
      if (name == "embedding_input_hash") have_input_hash= true;
      else if (name == "embedding_space") have_space= true;
      else if (name == "embedding" || name == "embedding_dim")
        have_legacy_blob= true;
    }
  }
  if (statement != nullptr) sqlite3_finalize (statement);
  sqlite3_close (db);
  return have_input_hash && have_space && !have_legacy_blob;
}

} // namespace

bool
prepare_database_path (const fs::path& path, std::string& error) {
  // Realtime prepares/commits may open the same derived DB from several worker
  // threads at once. Serialize the destructive pre-v3 cutover so a second
  // opener cannot observe v2, wait, and then unlink a freshly created v3 DB.
  std::lock_guard<std::mutex> guard (database_reset_mutex);
  if (!fs::exists (path)) return true;
  if (read_version (path) == schema_version && current_chunks_layout (path))
    return true;
  athena_spdlog_info (
    "rag storage: discarding incompatible database before clean rebuild: " +
    path.generic_string ());
  std::error_code ec;
  for (const fs::path& candidate:
       std::vector<fs::path> {path, fs::path (path.string () + "-wal"),
                              fs::path (path.string () + "-shm")}) {
    fs::remove (candidate, ec);
    if (ec && fs::exists (candidate)) {
      error= "failed to remove old RAG database " + candidate.generic_string () +
             ": " + ec.message ();
      return false;
    }
    ec.clear ();
  }
  return true;
}

bool
ensure_schema (sqlite3* db, std::string& error) {
  if (db == nullptr) {
    error= "RAG database handle is null";
    return false;
  }
  sqlite3_busy_timeout (db, 5000);
#ifdef SQLITE_DBCONFIG_NO_CKPT_ON_CLOSE
  (void) sqlite3_db_config (db, SQLITE_DBCONFIG_NO_CKPT_ON_CLOSE, 1, nullptr);
#endif
  const char* schema=
    "PRAGMA journal_mode=WAL;"
    "PRAGMA synchronous=FULL;"
    "CREATE TABLE IF NOT EXISTS meta ("
    "  key TEXT PRIMARY KEY, value TEXT NOT NULL);"
    "CREATE TABLE IF NOT EXISTS documents ("
    "  rel_path TEXT PRIMARY KEY, abs_path TEXT NOT NULL,"
    "  size INTEGER NOT NULL, mtime_ns INTEGER NOT NULL,"
    "  storage_revision TEXT NOT NULL, semantic_revision TEXT NOT NULL,"
    "  indexed_at INTEGER NOT NULL, status TEXT NOT NULL, error TEXT NOT NULL);"
    "CREATE TABLE IF NOT EXISTS chunks ("
    "  chunk_id TEXT PRIMARY KEY, rel_path TEXT NOT NULL,"
    "  kind TEXT NOT NULL, tree_path TEXT NOT NULL, anchor TEXT,"
    "  title TEXT, heading_path TEXT, text TEXT NOT NULL, source TEXT,"
    "  embedding_input_hash TEXT NOT NULL DEFAULT '',"
    "  embedding_space TEXT NOT NULL DEFAULT '');"
    "CREATE INDEX IF NOT EXISTS chunks_rel_path_idx ON chunks(rel_path);"
    "CREATE INDEX IF NOT EXISTS chunks_input_hash_idx "
    "  ON chunks(embedding_input_hash);"
    "CREATE INDEX IF NOT EXISTS chunks_embedding_ref_idx "
    "  ON chunks(embedding_space,embedding_input_hash);"
    "CREATE TABLE IF NOT EXISTS embeddings ("
    "  space_id TEXT NOT NULL, input_hash TEXT NOT NULL,"
    "  embedding BLOB NOT NULL, embedding_dim INTEGER NOT NULL,"
    "  PRIMARY KEY(space_id,input_hash));"
    "CREATE TABLE IF NOT EXISTS embedding_spaces ("
    "  space_id TEXT PRIMARY KEY, dimension INTEGER NOT NULL DEFAULT 0,"
    "  backend TEXT NOT NULL DEFAULT '', model TEXT NOT NULL DEFAULT '',"
    "  contract TEXT NOT NULL DEFAULT '');"
    "CREATE TABLE IF NOT EXISTS edges ("
    "  src_chunk TEXT NOT NULL, relation TEXT NOT NULL,"
    "  target TEXT NOT NULL, label TEXT);"
    "CREATE VIRTUAL TABLE IF NOT EXISTS chunks_fts USING fts5("
    "  chunk_id UNINDEXED, rel_path, title, heading_path, text);";
  if (!exec_sql (db, schema, error)) return false;

  sqlite3_stmt* version= nullptr;
  if (sqlite3_prepare_v2 (
        db, "SELECT value FROM meta WHERE key='schema-version'", -1,
        &version, nullptr) != SQLITE_OK) {
    error= sqlite3_errmsg (db);
    return false;
  }
  std::string value;
  if (sqlite3_step (version) == SQLITE_ROW) {
    const unsigned char* text= sqlite3_column_text (version, 0);
    if (text != nullptr) value= reinterpret_cast<const char*> (text);
  }
  sqlite3_finalize (version);
  if (!value.empty () && value != schema_version) {
    error= "unsupported RAG database schema version " + value;
    return false;
  }
  sqlite3_stmt* insert= nullptr;
  if (sqlite3_prepare_v2 (
        db, "INSERT INTO meta(key,value) VALUES('schema-version','3') "
            "ON CONFLICT(key) DO NOTHING", -1, &insert, nullptr) != SQLITE_OK) {
    error= sqlite3_errmsg (db);
    return false;
  }
  const bool ok= sqlite3_step (insert) == SQLITE_DONE;
  if (!ok) error= sqlite3_errmsg (db);
  sqlite3_finalize (insert);
  return ok;
}

} // namespace athena::rag::storage

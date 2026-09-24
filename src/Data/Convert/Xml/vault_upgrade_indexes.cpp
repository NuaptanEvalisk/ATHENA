/******************************************************************************
* MODULE     : vault_upgrade_indexes.cpp
* DESCRIPTION: Preserve cached semantic revisions before offline document rewrites
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "vault_format_upgrade.hpp"
#include "document_file_codec.hpp"
#include "legacy_document_reader.hpp"
#include "convert.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"
#include "confined_filesystem.hpp"
#include <QByteArray>
#include <sqlite3.h>
#include <stdexcept>

namespace athena::document {
namespace {
struct database {
  sqlite3* value= nullptr;
  explicit database (const std::filesystem::path& file) {
    int code= sqlite3_open_v2 (file.c_str (), &value,
      SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOFOLLOW, nullptr);
    if (code != SQLITE_OK) {
      std::string error= value ? sqlite3_errmsg (value) : "Cannot open snapshot database";
      sqlite3_close (value); value= nullptr;
      throw std::runtime_error (file.string () + ": " + error);
    }
  }
  ~database () { sqlite3_close (value); }
  void exec (const std::string& sql) {
    if (sqlite3_exec (value, sql.c_str (), nullptr, nullptr, nullptr) != SQLITE_OK)
      throw std::runtime_error (sqlite3_errmsg (value));
  }
};
struct statement {
  sqlite3_stmt* value= nullptr;
  database& db;
  statement (database& database, const std::string& sql): db (database) {
    if (sqlite3_prepare_v2 (db.value, sql.c_str (), -1, &value, nullptr) != SQLITE_OK)
      throw std::runtime_error (sqlite3_errmsg (db.value));
  }
  ~statement () { sqlite3_finalize (value); }
  void text (int column, const std::string& s) {
    if (sqlite3_bind_text (value, column, s.c_str (), (int) s.size (), SQLITE_TRANSIENT) != SQLITE_OK)
      throw std::runtime_error (sqlite3_errmsg (db.value));
  }
  void number (int column, long long n) {
    if (sqlite3_bind_int64 (value, column, n) != SQLITE_OK)
      throw std::runtime_error (sqlite3_errmsg (db.value));
  }
  std::string column (int i) {
    const auto* p= sqlite3_column_text (value, i);
    return p ? std::string ((const char*) p, sqlite3_column_bytes (value, i)) : "";
  }
  int step () {
    int code= sqlite3_step (value);
    if (code != SQLITE_ROW && code != SQLITE_DONE)
      throw std::runtime_error (sqlite3_errmsg (db.value));
    return code;
  }
};
bool table_exists (database& db, const std::string& table) {
  statement query (db, "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?1");
  query.text (1, table);
  return query.step () == SQLITE_ROW;
}
bool column_exists (database& db, const std::string& table, const std::string& name) {
  statement query (db, "SELECT 1 FROM pragma_table_info(?1) WHERE name=?2");
  query.text (1, table); query.text (2, name);
  return query.step () == SQLITE_ROW;
}
void verify (database& db) {
  statement check (db, "PRAGMA quick_check");
  if (check.step () != SQLITE_ROW || check.column (0) != "ok")
    throw std::runtime_error ("Invalid snapshot SQLite database");
}

bool native_trees (database& db, const std::string& metadata) {
  if (!table_exists (db, metadata)) return false;
  statement query (db, "SELECT value FROM " + metadata + " WHERE key='tree-format'");
  if (query.step () != SQLITE_ROW) return false;
  if (query.column (0) != "utf8-xml-v1")
    throw std::runtime_error ("Unsupported index tree text model: " + query.column (0));
  return true;
}

void mark_native_trees (database& db, const std::string& metadata) {
  db.exec ("CREATE TABLE IF NOT EXISTS " + metadata +
           "(key TEXT PRIMARY KEY,value TEXT NOT NULL)");
  db.exec ("INSERT INTO " + metadata +
           "(key,value) VALUES('tree-format','utf8-xml-v1') "
           "ON CONFLICT(key) DO UPDATE SET value='utf8-xml-v1'");
}

std::string migrate_fragment (const std::string& source) {
  if (source.empty ()) return source;
  // Parse in the same root context as the old index consumer, then wrap the
  // tree for role-aware import. Wrapping the source would change whitespace.
  legacy_import_limits limits;
  if (source.size () > limits.codec.input_bytes)
    throw std::runtime_error ("Index tree exceeds import size budget");
  const auto parsed= read_legacy_markup_fragment (source, limits.codec);
  const auto imported= import_legacy_document (
    tree (DOCUMENT, compound ("body", parsed)), standard_legacy_cork_table (), limits);
  for (int i=0; i<N(imported.document); ++i) {
    if (!is_compound (imported.document[i], "body", 1)) continue;
    tree result= imported.document[i][0];
    if (is_func (result, DOCUMENT, 1)) result= result[0];
    const auto bytes= write_xml (result, xml_kind::fragment);
    if (read_xml (bytes, xml_kind::fragment) != result)
      throw std::runtime_error ("Index tree failed UTF-8 round trip");
    return bytes;
  }
  throw std::runtime_error ("Missing imported index tree");
}

void migrate_names (database& db, const vault_upgrade_progress& progress) {
  if (native_trees (db, "artifact_metadata")) return;
  if (table_exists (db, "artifact_names") && column_exists (db, "artifact_names", "name_tree")) {
    statement names (db, "SELECT artifact_uuid,ordinal,name_tree FROM artifact_names WHERE name_tree<>''");
    statement count (db, "SELECT count(*) FROM artifact_names WHERE name_tree<>''");
    count.step ();
    const auto total= sqlite3_column_int64 (count.value, 0);
    std::size_t done= 0;
    while (names.step () == SQLITE_ROW) {
      try {
        const auto migrated= migrate_fragment (names.column (2));
        statement update (db, "UPDATE artifact_names SET name_tree=?1 WHERE artifact_uuid=?2 AND ordinal=?3");
        update.text (1, migrated); update.text (2, names.column (0));
        update.number (3, sqlite3_column_int64 (names.value, 1)); update.step ();
        ++done;
        if (progress && (done % 128 == 0 || done == (std::size_t) total))
          progress ("Convert artifact names", done, total, names.column (0));
      }
      catch (const std::exception& error) {
        throw std::runtime_error ("artifact_names " + names.column (0) + ": " + error.what ());
      }
    }
  }
  mark_native_trees (db, "artifact_metadata");
}

void migrate_bold_trees (const std::filesystem::path& file, const vault_upgrade_progress& progress) {
  database db (file);
  verify (db);
  if (native_trees (db, "bold_text_metadata")) return;
  if (!table_exists (db, "entries") || !column_exists (db, "entries", "keyword_tree"))
    throw std::runtime_error ("Unknown bold-text index schema: " + file.string ());
  db.exec ("BEGIN IMMEDIATE");
  std::size_t done= 0, total= 0;
  {
    statement count (db, "SELECT count(*) FROM entries");
    count.step (); total= sqlite3_column_int64 (count.value, 0);
  }
  statement keywords (db, "SELECT uuid,keyword_tree FROM entries");
  while (keywords.step () == SQLITE_ROW) {
    try {
      std::string source= keywords.column (1);
      if (source.rfind ("base64-v1:", 0) == 0) {
        const auto decoded= QByteArray::fromBase64Encoding (
          QByteArray (source.data ()+10, source.size ()-10), QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded) throw std::runtime_error ("Invalid Base64 keyword tree");
        source.assign (decoded.decoded.constData (), decoded.decoded.size ());
      }
      const auto migrated= migrate_fragment (source);
      const auto encoded= QByteArray (migrated.data (), migrated.size ()).toBase64 ();
      statement update (db, "UPDATE entries SET keyword_tree=?1 WHERE uuid=?2");
      update.text (1, "base64-v1:" + encoded.toStdString ());
      update.text (2, keywords.column (0)); update.step ();
      ++done;
      if (progress && (done % 128 == 0 || done == total))
        progress ("Convert keyword trees", done, total, keywords.column (0));
    }
    catch (const std::exception& error) {
      throw std::runtime_error ("bold-text.entries " + keywords.column (0) + ": " + error.what ());
    }
  }
  mark_native_trees (db, "bold_text_metadata");
  db.exec ("COMMIT");
  verify (db);
  db.exec ("PRAGMA wal_checkpoint(TRUNCATE)");
}

void seed (const std::filesystem::path& file, bool rag,
           const std::vector<vault_upgrade_revision>& revisions,
           const vault_upgrade_progress& progress) {
  database db (file);
  verify (db);
  if (!table_exists (db, "documents"))
    throw std::runtime_error ("Unknown index schema: " + file.string ());
  const std::string metadata= rag ? "meta" : "artifact_metadata";
  // Older artifact/RAG writers did not stamp a schema version. Recognize their
  // document revision columns before creating metadata, as the live readers do.
  for (const auto* column: {rag ? "rel_path" : "path", "size", "mtime_ns"})
    if (!column_exists (db, "documents", column))
      throw std::runtime_error ("Unknown index schema: " + file.string ());
  if (rag && (!column_exists (db, "documents", "content_hash") ||
              !column_exists (db, "documents", "status")))
    throw std::runtime_error ("Unknown RAG index schema: " + file.string ());
  if (table_exists (db, metadata)) {
    statement version (db, "SELECT value FROM " + metadata + " WHERE key='schema-version'");
    if (version.step () == SQLITE_ROW) {
      const auto v= version.column (0);
      if (v != "1" && v != "2") throw std::runtime_error ("Unsupported index version: " + v);
    }
  }
  db.exec ("BEGIN IMMEDIATE");
  db.exec ("CREATE TABLE IF NOT EXISTS " + metadata +
           "(key TEXT PRIMARY KEY,value TEXT NOT NULL)");
  if (rag) {
    if (!column_exists (db, "documents", "storage_hash"))
      db.exec ("ALTER TABLE documents ADD COLUMN storage_hash TEXT NOT NULL DEFAULT '';"
               "UPDATE documents SET storage_hash=content_hash,content_hash=''");
  }
  else {
    if (!column_exists (db, "documents", "semantic_hash"))
      db.exec ("ALTER TABLE documents ADD COLUMN semantic_hash TEXT NOT NULL DEFAULT ''");
    if (table_exists (db, "artifact_range_cache") &&
        !column_exists (db, "artifact_range_cache", "semantic_hash"))
      db.exec ("ALTER TABLE artifact_range_cache ADD COLUMN semantic_hash TEXT NOT NULL DEFAULT ''");
    migrate_names (db, progress);
  }
  db.exec ("INSERT INTO " + metadata + "(key,value) VALUES('schema-version','2') "
           "ON CONFLICT(key) DO UPDATE SET value='2'");
  for (const auto& r: revisions) {
    // Empty semantic revisions are safe to initialize only while the original
    // storage revision still matches. Stale records remain stale, never blessed.
    statement update (db, rag ?
      "UPDATE documents SET content_hash=?1 WHERE rel_path=?2 AND size=?3 AND mtime_ns=?4 "
      "AND storage_hash=?5 AND content_hash='' AND status='ok'" :
      "UPDATE documents SET semantic_hash=?1 WHERE path=?2 AND size=?3 AND mtime_ns=?4 AND semantic_hash=''");
    update.text (1, r.semantic_hash); update.text (2, r.path);
    update.number (3, r.size); update.number (4, r.mtime);
    if (rag) update.text (5, r.storage_hash);
    update.step ();
    if (!rag && table_exists (db, "artifact_range_cache")) {
      statement ranges (db, "UPDATE artifact_range_cache SET semantic_hash=?1 "
        "WHERE path=?2 AND size=?3 AND mtime_ns=?4 AND semantic_hash=''");
      ranges.text (1, r.semantic_hash); ranges.text (2, r.path);
      ranges.number (3, r.size); ranges.number (4, r.mtime); ranges.step ();
    }
  }
  db.exec ("COMMIT");
  verify (db);
  // Consolidate private WAL data before publishing the directory. No connection
  // to the original vault is opened, and no chunk/vector/model row is rewritten.
  db.exec ("PRAGMA wal_checkpoint(TRUNCATE)");
}
}

void prepare_vault_upgrade_indexes (const std::filesystem::path& root,
                                    const std::vector<vault_upgrade_revision>& revisions,
                                    const vault_upgrade_progress& progress) {
  AthenaVaultfileInfo info;
  std::string error;
  if (!athena_vaultfile_read (root, info, error)) throw std::runtime_error (error);
  filesystem::confined_root storage (root);
  for (const auto& item: {std::make_pair (info.artifacts_path, false),
                          std::make_pair (info.rag_index_path, true)}) {
    const std::filesystem::path relative (item.first);
    if (relative.empty () || relative.is_absolute ())
      throw std::runtime_error ("Index path must stay inside vault");
    for (const auto& part: relative) filesystem::confined_root::validate_component (part.string ());
    if (!std::filesystem::exists (root / relative)) continue;
    storage.open (relative); // Reject symlinked database paths before SQLite opens them.
    seed (root / relative, item.second, revisions, progress);
  }
  const std::filesystem::path bold (info.bold_text_path);
  if (bold.empty () || bold.is_absolute ())
    throw std::runtime_error ("Bold-text index path must stay inside vault");
  for (const auto& part: bold) filesystem::confined_root::validate_component (part.string ());
  if (std::filesystem::exists (root / bold)) {
    storage.open (bold);
    migrate_bold_trees (root / bold, progress);
  }
}
}

/******************************************************************************
* MODULE     : vault_xml_v3_indexes.cpp
* DESCRIPTION: Lossless typed-tree rewrite of staged Artifact databases
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/
#include "vault_xml_v3_indexes.hpp"
#include "athena_document_xml.hpp"
#include "document_file_codec.hpp"
#include "ATHENA/Data/artifact_title_filter.hpp"
#include "math_v2_to_v3.hpp"
#include "node_metadata.hpp"
#include "confined_filesystem.hpp"

#include <QByteArray>
#include <QUrl>
#include <QUrlQuery>
#include <sqlite3.h>
#include <initializer_list>
#include <stdexcept>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace athena::document {
namespace fs= std::filesystem;
namespace {

class connection {
  sqlite3* db_= nullptr;
public:
  explicit connection (const fs::path& path) {
    if (sqlite3_open_v2 (path.c_str (), &db_,
                         SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOFOLLOW,
                         nullptr) != SQLITE_OK) {
      const std::string message= db_ ? sqlite3_errmsg (db_) : "open failed";
      if (db_) sqlite3_close (db_);
      db_= nullptr;
      throw std::runtime_error ("Cannot open staged v3 index: " + message);
    }
    sqlite3_busy_timeout (db_, 10000);
  }
  ~connection () { if (db_) sqlite3_close (db_); }
  sqlite3* get () const { return db_; }
  void exec (const char* sql) {
    char* error= nullptr;
    if (sqlite3_exec (db_, sql, nullptr, nullptr, &error) != SQLITE_OK) {
      std::string problem= error ? error : sqlite3_errmsg (db_);
      sqlite3_free (error);
      throw std::runtime_error ("Staged v3 index SQL: " + problem);
    }
  }
};

class statement {
  sqlite3_stmt* stmt_= nullptr;
public:
  statement (connection& db, const std::string& sql) {
    if (sqlite3_prepare_v2 (db.get (), sql.c_str (), -1, &stmt_, nullptr) !=
        SQLITE_OK)
      throw std::runtime_error (sqlite3_errmsg (db.get ()));
  }
  ~statement () { if (stmt_) sqlite3_finalize (stmt_); }
  sqlite3_stmt* get () const { return stmt_; }
  int step () {
    const int status= sqlite3_step (stmt_);
    if (status != SQLITE_ROW && status != SQLITE_DONE)
      throw std::runtime_error (sqlite3_errmsg (sqlite3_db_handle (stmt_)));
    return status;
  }
  void reset () {
    if (sqlite3_reset (stmt_) != SQLITE_OK)
      throw std::runtime_error (sqlite3_errmsg (sqlite3_db_handle (stmt_)));
    sqlite3_clear_bindings (stmt_);
  }
  void value (int index, const std::string& text) {
    if (sqlite3_bind_text (stmt_, index, text.data (), int (text.size ()),
                           SQLITE_TRANSIENT) != SQLITE_OK)
      throw std::runtime_error ("Could not bind staged v3 tree value");
  }
  void row_id (int index, sqlite3_int64 value) {
    if (sqlite3_bind_int64 (stmt_, index, value) != SQLITE_OK)
      throw std::runtime_error ("Could not bind staged v3 row id");
  }
};

bool exists (connection& db, const std::string& table) {
  statement query (db,
    "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?1");
  query.value (1, table);
  return query.step () == SQLITE_ROW;
}

bool column (connection& db, const std::string& table, const std::string& field) {
  // Table names are constant strings supplied by the converter, not user SQL.
  statement query (db, "PRAGMA table_info(" + table + ")");
  while (query.step () == SQLITE_ROW) {
    const unsigned char* s= sqlite3_column_text (query.get (), 1);
    if (s && reinterpret_cast<const char*> (s) == field) return true;
  }
  return false;
}

void check (connection& db) {
  statement query (db, "PRAGMA quick_check");
  if (query.step () != SQLITE_ROW ||
      !sqlite3_column_text (query.get (), 0) ||
      std::string ((const char*) sqlite3_column_text (query.get (), 0)) != "ok")
    throw std::runtime_error ("Staged Artifact database integrity check failed");
}

std::string migrate_fragment (std::string source, bool opaque,
                              bool strict, v3_index_migration_stats& stats) {
  if (opaque) {
    static constexpr std::string_view prefix= "base64-v1:";
    if (source.rfind (prefix, 0) != 0)
      throw std::runtime_error ("Unexpected encoded index tree version");
    QByteArray text (source.data () + prefix.size (),
                     qsizetype (source.size () - prefix.size ()));
    const auto decoded= QByteArray::fromBase64Encoding (
      text, QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.toBase64 () != text)
      throw std::runtime_error ("Malformed Base64 in staged index tree");
    source.assign (decoded.decoded.constData (),
                   std::size_t (decoded.decoded.size ()));
  }
  const auto previous= read_xml_v2 (source, xml_kind::fragment);
  const auto migrated= migrate_math_v2_tree (previous, strict);
  std::string output= write_xml_v3 (migrated.document, xml_kind::fragment);
  const auto restored= read_xml_v3 (output, xml_kind::fragment);
  if (restored != migrated.document ||
      !athena::node::equal_metadata (restored, migrated.document))
    throw std::runtime_error ("Staged index tree v3 round trip differs");
  stats.warnings+= migrated.warnings.size ();
  ++stats.tree_fragments;
  if (opaque) {
    const auto b64= QByteArray (output.data (), qsizetype (output.size ())).toBase64 ();
    output= "base64-v3:" + std::string (b64.constData (),
                                        std::size_t (b64.size ()));
  }
  return output;
}

void replace_column (connection& db, const char* table, const char* column_name,
                     bool opaque, bool strict, v3_index_migration_stats& stats) {
  if (!exists (db, table) || !column (db, table, column_name)) return;
  // Only constant table/field names from call sites may be spliced here.
  const std::string field (column_name);
  const std::string name (table);
  std::vector<std::pair<sqlite3_int64,std::string>> updates;
  {
    statement query (db, "SELECT rowid," + field + " FROM " + name +
                         " WHERE " + field + "<>''");
    while (query.step () == SQLITE_ROW) {
      const auto id= sqlite3_column_int64 (query.get (), 0);
      const char* bytes= (const char*) sqlite3_column_text (query.get (), 1);
      const int length= sqlite3_column_bytes (query.get (), 1);
      if (!bytes || length < 0)
        throw std::runtime_error ("Invalid staged indexed source tree");
      updates.emplace_back (id, migrate_fragment (
        std::string (bytes, std::size_t (length)), opaque, strict, stats));
      if (updates.size () > 1000000)
        throw std::length_error ("Too many staged indexed tree fragments");
    }
  }
  statement update (db, "UPDATE " + name + " SET " + field +
                        "=?1 WHERE rowid=?2");
  for (const auto& [id, value]: updates) {
    update.reset ();
    update.value (1, value);
    update.row_id (2, id);
    if (update.step () != SQLITE_DONE || sqlite3_changes (db.get ()) != 1)
      throw std::runtime_error ("Staged indexed tree row vanished");
  }
}

void invalidate_computed_hashes (connection& db, const char* table,
                                 std::initializer_list<const char*> keys,
                                 v3_index_migration_stats& stats) {
  if (!exists (db, table)) return;
  for (const char* key: keys) {
    if (!column (db, table, key)) continue;
    db.exec ((std::string ("UPDATE ") + table + " SET " + key +
              "='' WHERE " + key + "<>''").c_str ());
    stats.invalidated_hashes+= std::size_t (sqlite3_changes (db.get ()));
  }
}

fs::path confined_database (const fs::path& root, const std::string& relative) {
  if (relative.empty ()) return {};
  const fs::path name (relative);
  if (name.is_absolute ())
    throw std::runtime_error ("Staged index database must be Vault-relative");
  for (const auto& component: name)
    if (component == "." || component == "..")
      throw std::runtime_error ("Staged index path escapes Vault root");
  const fs::path full= root / name;
  if (!fs::exists (full)) return {};
  if (!fs::is_regular_file (fs::symlink_status (full)))
    throw std::runtime_error ("Staged index database cannot be a symlink");
  // Descriptor-based resolution also rejects symlinks in parent components.
  athena::filesystem::confined_root confined (root);
  (void) confined.open (name);
  return full;
}

void update_tree_version (connection& db, const char* table) {
  if (!exists (db, table) || !column (db, table, "key") ||
      !column (db, table, "value")) return;
  statement update (db,
    std::string ("INSERT INTO ") + table +
    "(key,value) VALUES('tree-format','utf8-xml-v3') "
    "ON CONFLICT(key) DO UPDATE SET value=excluded.value");
  if (update.step () != SQLITE_DONE)
    throw std::runtime_error ("Could not mark staged tree format");
}

void migrate_database (const fs::path& file, const char* table,
                       const char* field, bool opaque, bool strict,
                       v3_index_migration_stats& stats) {
  if (file.empty ()) return;
  connection db (file);
  check (db);
  db.exec ("PRAGMA foreign_keys=ON");
  db.exec ("BEGIN IMMEDIATE");
  try {
    replace_column (db, table, field, opaque, strict, stats);
    if (std::string_view (table) == "artifact_names") {
      update_tree_version (db, "artifact_metadata");
      invalidate_computed_hashes (db, "documents",
        {"semantic_hash", "storage_hash", "content_hash", "locator_contract"}, stats);
      // load_range_checkpoint falls back to request_hash when input_hash is
      // empty. Clearing hashes alone leaves stale paragraph offsets reusable.
      if (exists (db, "artifact_range_cache")) {
        db.exec ("DELETE FROM artifact_range_cache");
        stats.invalidated_range_rows+= std::size_t (sqlite3_changes (db.get ()));
      }
      invalidate_computed_hashes (db, "artifacts", {"input_hash"}, stats);
    }
    if (opaque) update_tree_version (db, "bold_text_metadata");
    db.exec ("COMMIT");
  }
  catch (...) {
    try { db.exec ("ROLLBACK"); } catch (...) {}
    throw;
  }
  check (db);
  db.exec ("PRAGMA wal_checkpoint(TRUNCATE)");
}

void check_active_source_references (
    const fs::path& file,
    std::initializer_list<std::pair<const char*,const char*>> fields,
    const std::set<std::string>& removed) {
  if (file.empty () || removed.empty ()) return;
  const fs::path wal (file.string () + "-wal");
  if (fs::exists (wal) && fs::file_size (wal) != 0)
    throw std::runtime_error ("Index database must be checkpointed before "
                              "source-UUID audit: " + file.string ());
  QUrl uri= QUrl::fromLocalFile (QString::fromStdString (file.string ()));
  QUrlQuery options;
  options.addQueryItem ("immutable", "1");
  uri.setQuery (options);
  const QByteArray text= uri.toEncoded (QUrl::FullyEncoded);
  sqlite3* raw= nullptr;
  if (sqlite3_open_v2 (text.constData (), &raw,
                       SQLITE_OPEN_READONLY | SQLITE_OPEN_URI |
                       SQLITE_OPEN_NOFOLLOW, nullptr) != SQLITE_OK) {
    const std::string message= raw ? sqlite3_errmsg (raw) : "cannot open";
    if (raw) sqlite3_close (raw);
    throw std::runtime_error ("Could not audit immutable source references: " + message);
  }
  struct close_connection {
    sqlite3* db;
    ~close_connection () { sqlite3_close (db); }
  } guard {raw};
  for (const auto& [table, field]: fields) {
    // Both identifiers are fixed literals from the schema manifest below.
    sqlite3_stmt* declaration= nullptr;
    const std::string sql= std::string ("SELECT 1 FROM pragma_table_info('") +
      table + "') WHERE name=?1";
    if (sqlite3_prepare_v2 (raw, sql.c_str (), -1, &declaration, nullptr) != SQLITE_OK)
      throw std::runtime_error ("Cannot inspect active binding schema");
    sqlite3_bind_text (declaration, 1, field, -1, SQLITE_STATIC);
    const bool found= sqlite3_step (declaration) == SQLITE_ROW;
    sqlite3_finalize (declaration);
    if (!found) continue;
    sqlite3_stmt* cursor= nullptr;
    const std::string select= std::string ("SELECT ") + field + " FROM " +
      table + " WHERE " + field + " IS NOT NULL AND " + field + "<>''";
    if (sqlite3_prepare_v2 (raw, select.c_str (), -1, &cursor, nullptr) != SQLITE_OK)
      throw std::runtime_error ("Cannot inspect active binding values");
    int status;
    try {
      while ((status= sqlite3_step (cursor)) == SQLITE_ROW) {
        const unsigned char* value= sqlite3_column_text (cursor, 0);
        const int size= sqlite3_column_bytes (cursor, 0);
        if (!value || size < 36) continue;
        const std::string_view encoded ((const char*) value, std::size_t (size));
        for (std::size_t i=0; i+36 <= encoded.size (); ++i) {
          if (encoded[i+8] != '-' || encoded[i+13] != '-' ||
              encoded[i+18] != '-' || encoded[i+23] != '-') continue;
          const std::string id (encoded.substr (i, 36));
          if (!removed.count (id)) continue;
          throw std::runtime_error (
            "Active " + std::string (table) + "." + field +
            " still refers to removed source UUID " + id +
            " (migration must preserve or explicitly rebind its identity)");
        }
      }
      if (status != SQLITE_DONE)
        throw std::runtime_error ("Error reading active source identity references");
    }
    catch (...) {
      sqlite3_finalize (cursor);
      throw;
    }
    sqlite3_finalize (cursor);
  }
}

} // namespace

std::optional<v3_title_filter_migration> prepare_vault_title_filter_v3 (
    const fs::path& root, const AthenaVaultfileInfo& info, bool strict) {
  const fs::path relative= fs::path (info.artifact_title_filter_path).lexically_normal ();
  if (relative.empty () || relative.is_absolute () ||
      QString::fromStdString (relative.extension ().string ()).compare (
        ".lst", Qt::CaseInsensitive) != 0)
    throw std::runtime_error ("Invalid artifact title filter path for v3 migration");
  for (const auto& component: relative)
    filesystem::confined_root::validate_component (component.string ());
  if (!fs::exists (fs::symlink_status (root / relative))) return {};
  filesystem::confined_root storage (root);
  const auto bytes= storage.open (relative).read (codec_limits ().input_bytes);
  const auto original= athena_artifact_title_filter_parse (bytes);
  auto converted= athena_artifact_title_filter_from_entries (original.entries);
  v3_title_filter_migration result {relative, {}};
  for (const auto& encoded: original.structured_entries) {
    const auto decoded= decode_native_xml_bytes (encoded, xml_kind::fragment);
    tree content= decoded.document;
    if (decoded.version != xml_format_version::v3) {
      const auto migrated= migrate_math_v2_tree (content, strict);
      content= migrated.document;
      result.warnings+= migrated.warnings.size ();
    }
    athena_artifact_title_filter_add (converted, content, xml_format_version::v3);
    ++result.fragments;
  }
  result.bytes= athena_artifact_title_filter_serialize (converted);
  const auto restored= athena_artifact_title_filter_parse (result.bytes);
  if (athena_artifact_title_filter_fingerprint (restored) !=
      athena_artifact_title_filter_fingerprint (converted) ||
      restored.entries != converted.entries)
    throw std::runtime_error ("Artifact rejection list failed v3 round trip");
  return result;
}

std::vector<fs::path> vault_xml_v3_derived_cache_files (
    const fs::path& root, const AthenaVaultfileInfo& info) {
  // Owners: RAG/rag_storage.hpp, Data/reference_graph_cache.cpp and
  // Data/node_location_cache.cpp. Each recreates this derived storage when
  // absent. Old rag-backup-* directories are archival evidence, not targets.
  std::set<fs::path> candidates {
    ".athena/node-locations/data.mdb", ".athena/node-locations/lock.mdb"};
  for (const auto& database: {info.rag_index_path,
                             std::string (".athena/reference-graph.sqlite")}) {
    if (database.empty ()) continue;
    for (const char* suffix: {"", "-wal", "-shm"})
      candidates.emplace (database + suffix);
  }
  const std::set<fs::path> authoritative {
    "Vaultfile.json", "Vaultfile", ".athena/document-history.sqlite",
    ".athena/safe-rename.sqlite", ".athena/namespace-ontology.sqlite",
    info.artifacts_path, info.enunciations_path, info.bold_text_path,
    info.map_path, info.namespace_db_path, info.materials_db_path,
    info.preferences_path, info.websites_path, info.artifact_title_filter_path};
  std::vector<fs::path> result;
  filesystem::confined_root storage (root);
  for (const auto& path: candidates) {
    if (path.is_absolute () || path.empty ())
      throw std::runtime_error ("Derived cache path must stay inside its Vault");
    for (const auto& component: path)
      filesystem::confined_root::validate_component (component.string ());
    const auto first= *path.begin ();
    if (path.extension () == ".ath" || first == ".backup" || first == ".git" ||
        first == ".hg" || first == ".svn" ||
        path.generic_string ().rfind (".athena/maintenance/", 0) == 0 ||
        path.generic_string ().rfind (".athena/migrations/", 0) == 0 ||
        path.generic_string ().rfind (".athena/rag-backup-", 0) == 0)
      throw std::runtime_error ("Derived cache path overlaps source/archive: " +
                                path.generic_string ());
    for (const auto& source: authoritative) {
      if (source.empty ()) continue;
      for (const char* suffix: {"", "-wal", "-shm", "-journal"})
        if (path == fs::path (source.lexically_normal ().string () + suffix))
          throw std::runtime_error ("Derived cache path overlaps authoritative data: " +
                                    path.generic_string ());
    }
    fs::path prefix= root;
    for (const auto& component: path) {
      prefix/= component;
      if (fs::is_symlink (fs::symlink_status (prefix)))
        throw std::runtime_error ("Derived cache path contains a symlink: " +
                                  path.generic_string ());
    }
    const auto status= fs::symlink_status (root / path);
    if (!fs::exists (status)) continue;
    if (!fs::is_regular_file (status) || fs::hard_link_count (root / path) != 1)
      throw std::runtime_error ("Derived cache must be a regular non-hardlinked file: " +
                                path.generic_string ());
    if (storage.open (path).path () != storage.path () / path)
      throw std::runtime_error ("Derived cache path resolved to a different file");
    result.push_back (path);
  }
  return result;
}

v3_index_migration_stats migrate_staged_vault_indexes_v3 (
  const fs::path& root, const AthenaVaultfileInfo& info, bool strict) {
  v3_index_migration_stats stats;
  const auto caches= vault_xml_v3_derived_cache_files (root, info);
  migrate_database (confined_database (root, info.artifacts_path),
                    "artifact_names", "name_tree", false, strict, stats);
  migrate_database (confined_database (root, info.enunciations_path),
                    "entries", "keyword_tree", false, strict, stats);
  migrate_database (confined_database (root, info.bold_text_path),
                    "entries", "keyword_tree", true, strict, stats);
  filesystem::confined_root storage (root);
  if (const auto filter= prepare_vault_title_filter_v3 (root, info, strict)) {
    auto file= storage.open (filter->relative);
    const auto revision= file.stat ();
    const auto replacement= storage.replace (
      filter->relative, file, revision, filter->bytes);
    if (!replacement.directory_synced)
      throw std::runtime_error ("Artifact rejection list replacement was not durable");
    stats.tree_fragments+= filter->fragments;
    stats.warnings+= filter->warnings;
  }
  for (const auto& path: caches) {
    auto file= storage.open (path);
    if (file.path () != storage.path () / path)
      throw std::runtime_error ("Derived cache path changed before invalidation");
    if (!storage.remove (path, file, file.stat ()))
      throw std::runtime_error ("Derived cache invalidation was not durable: " +
                                path.generic_string ());
    stats.invalidated_cache_files.push_back (path);
  }
  return stats;
}

void audit_vault_uuid_references_v3 (
    const fs::path& root, const AthenaVaultfileInfo& info,
    const std::set<std::string>& removed) {
  check_active_source_references (
    confined_database (root, info.artifacts_path),
    {{"artifacts", "source_uuid"}, {"artifacts", "source_nodes"}},
    removed);
  check_active_source_references (
    confined_database (root, info.enunciations_path),
    {{"entries", "identity_focus"}, {"entries", "identity_host"},
     {"entries", "identity_before"}, {"entries", "identity_after"}},
    removed);
  check_active_source_references (
    confined_database (root, info.bold_text_path),
    {{"entries", "identity_focus"}, {"entries", "identity_host"},
     {"entries", "identity_before"}, {"entries", "identity_after"}},
    removed);
}

} // namespace athena::document

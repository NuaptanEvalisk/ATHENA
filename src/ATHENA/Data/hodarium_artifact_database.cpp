/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "hodarium_artifact_database.hpp"
#include "hodarium_artifact_database_internal.hpp"
#include "hodarium_local_notifications.hpp"
#include "Kernel/Types/node_metadata.hpp"

#include <QString>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <chrono>
#include <sstream>

namespace fs= std::filesystem;

namespace {
QString qstr (const std::string& s) {
  return QString::fromUtf8 (s.data (), (qsizetype) s.size ());
}

// Same filesystem-only normalization as vault maintenance, without linking its
// editor-facing translation unit.
fs::path
normalize_root (const fs::path& input) {
  std::error_code ec;
  fs::path absolute = fs::absolute (input, ec);
  if (ec) absolute = input;
  fs::path canonical = fs::weakly_canonical (absolute, ec);
  if (ec) return absolute.lexically_normal ();
  return canonical;
}

} // namespace

namespace athena::artifact_database {

bool exec_sql (sqlite3* db, const std::string& sql, std::string& error) {
  char* message= nullptr;
  int rc= sqlite3_exec (db, sql.c_str (), nullptr, nullptr, &message);
  if (rc == SQLITE_OK) return true;
  error= message ? message : sqlite3_errmsg (db);
  sqlite3_free (message);
  return false;
}

bool prepare (sqlite3* db, const char* sql, Statement& out,
              std::string& error) {
  if (sqlite3_prepare_v2 (db, sql, -1, &out.st, nullptr) == SQLITE_OK)
    return true;
  error= sqlite3_errmsg (db);
  return false;
}

bool bind_text (sqlite3_stmt* st, int index, const std::string& value) {
  return sqlite3_bind_text (st, index, value.data (), (int) value.size (),
                            SQLITE_TRANSIENT) == SQLITE_OK;
}

std::string column_text (sqlite3_stmt* st, int column) {
  const unsigned char* value= sqlite3_column_text (st, column);
  int n= sqlite3_column_bytes (st, column);
  return value ? std::string ((const char*) value, (size_t) n) : std::string ();
}

bool ensure_column (sqlite3* db, const char* table, const char* column,
                    const char* declaration, std::string& error) {
  std::string pragma= std::string ("PRAGMA table_info(") + table + ");";
  Statement columns;
  if (!prepare (db, pragma.c_str (), columns, error)) return false;
  int rc;
  while ((rc= sqlite3_step (columns.st)) == SQLITE_ROW)
    if (column_text (columns.st, 1) == column) return true;
  if (rc != SQLITE_DONE) {
    error= sqlite3_errmsg (db);
    return false;
  }
  std::string alter= std::string ("ALTER TABLE ") + table +
                     " ADD COLUMN " + column + " " + declaration + ";";
  return exec_sql (db, alter, error);
}

bool safe_relative_database (const std::string& value) {
  fs::path path (value);
  if (value.empty () || path.is_absolute ()) return false;
  for (const fs::path& part: path)
    if (part == "..") return false;
  return true;
}

std::string sql_quote (sqlite3* db, const fs::path& path) {
  char* value= sqlite3_mprintf ("%Q", path.string ().c_str ());
  std::string out= value ? value : "''";
  sqlite3_free (value);
  (void) db;
  return out;
}

bool open_databases (const fs::path& root, SqliteDb& holder,
                     AthenaVaultfileInfo& info, std::string& error,
                     bool read_only) {
  if (!athena_vaultfile_read (root, info, error)) return false;
  if (!safe_relative_database (info.artifacts_path) ||
      !safe_relative_database (info.enunciations_path) ||
      !safe_relative_database (info.bold_text_path)) {
    error= "Artifact database paths in Vaultfile.json must be relative paths";
    return false;
  }
  if (read_only) {
    std::error_code ec;
    bool exists= fs::exists (root / info.artifacts_path, ec);
    if (ec) { error= ec.message (); return false; }
    if (!exists) return true; // An unbuilt artifact index is empty, not created.
    if (sqlite3_open_v2 ((root / info.artifacts_path).string ().c_str (),
        &holder.db, SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, nullptr) != SQLITE_OK) {
      error= holder.db ? sqlite3_errmsg (holder.db) : "Could not read artifacts database";
      return false;
    }
    sqlite3_busy_timeout (holder.db, 5000);
    if (!exec_sql (holder.db, "PRAGMA query_only=ON;", error)) return false;
    for (const auto& attachment: {
        std::make_pair (info.enunciations_path, "enunciations"),
        std::make_pair (info.bold_text_path, "bold_text")}) {
      QUrl uri= QUrl::fromLocalFile (qstr (fs::absolute (root / attachment.first).string ()));
      QUrlQuery query; query.addQueryItem ("mode", "ro"); uri.setQuery (query);
      const auto encoded= uri.toEncoded (QUrl::FullyEncoded).toStdString ();
      Statement attach;
      const std::string sql= "ATTACH DATABASE ?1 AS " + std::string (attachment.second);
      if (!prepare (holder.db, sql.c_str (), attach, error)) return false;
      bind_text (attach.st, 1, encoded);
      if (sqlite3_step (attach.st) != SQLITE_DONE) {
        error= sqlite3_errmsg (holder.db);
        return false;
      }
    }
    return exec_sql (holder.db, "BEGIN;", error);
  }
  for (const std::string* relative:
       {&info.artifacts_path, &info.enunciations_path, &info.bold_text_path}) {
    fs::path parent= (root / *relative).parent_path ();
    std::error_code ec;
    fs::create_directories (parent, ec);
    if (ec) {
      error= "Could not create artifact database directory " +
             parent.string () + ": " + ec.message ();
      return false;
    }
  }
  if (sqlite3_open_v2 ((root / info.artifacts_path).string ().c_str (),
                       &holder.db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                       nullptr) != SQLITE_OK) {
    error= holder.db ? sqlite3_errmsg (holder.db) : "Could not open artifacts.db";
    return false;
  }
  sqlite3_busy_timeout (holder.db, 5000);
  std::string attach=
    "ATTACH DATABASE " + sql_quote (holder.db, root / info.enunciations_path) +
    " AS enunciations; ATTACH DATABASE " +
    sql_quote (holder.db, root / info.bold_text_path) + " AS bold_text;";
  if (!exec_sql (holder.db, attach, error)) return false;
  const char* schema=
    "PRAGMA foreign_keys=ON;"
    "CREATE TABLE IF NOT EXISTS artifact_metadata("
    " key TEXT PRIMARY KEY,value TEXT NOT NULL);"
    "CREATE TABLE IF NOT EXISTS bold_text.bold_text_metadata("
    " key TEXT PRIMARY KEY,value TEXT NOT NULL);"
    "CREATE TABLE IF NOT EXISTS documents("
    " path TEXT PRIMARY KEY,mtime_ns INTEGER NOT NULL,size INTEGER NOT NULL,"
    " locator_contract TEXT NOT NULL DEFAULT '',"
    " semantic_hash TEXT NOT NULL DEFAULT '',"
    " storage_hash TEXT NOT NULL DEFAULT '',"
    " content_hash TEXT NOT NULL DEFAULT '');"
    "CREATE TABLE IF NOT EXISTS enunciations.entries("
    " uuid TEXT PRIMARY KEY,path TEXT NOT NULL,anchor_stem TEXT NOT NULL,"
    " tag TEXT NOT NULL,display_text TEXT NOT NULL,document_order INTEGER NOT NULL,"
    " identity_focus TEXT NOT NULL DEFAULT '',"
    " identity_host TEXT NOT NULL DEFAULT '',"
    " identity_before TEXT NOT NULL DEFAULT '',"
    " identity_after TEXT NOT NULL DEFAULT '',"
    " UNIQUE(path,anchor_stem,document_order));"
    "CREATE INDEX IF NOT EXISTS enunciations.entries_path_idx ON entries(path);"
    "CREATE TABLE IF NOT EXISTS bold_text.entries("
    " uuid TEXT PRIMARY KEY,path TEXT NOT NULL,keyword_tree TEXT NOT NULL,"
    " keyword_display TEXT NOT NULL,occurrence INTEGER NOT NULL,"
    " paragraph_offsets TEXT NOT NULL,document_order INTEGER NOT NULL,"
    " identity_focus TEXT NOT NULL DEFAULT '',"
    " identity_host TEXT NOT NULL DEFAULT '',"
    " identity_before TEXT NOT NULL DEFAULT '',"
    " identity_after TEXT NOT NULL DEFAULT '',"
    " UNIQUE(path,keyword_tree,occurrence));"
    "CREATE INDEX IF NOT EXISTS bold_text.entries_path_idx ON entries(path);"
    "CREATE TABLE IF NOT EXISTS artifacts("
    " uuid TEXT PRIMARY KEY,type TEXT NOT NULL,origin TEXT NOT NULL,"
    " content_uuid TEXT NOT NULL,proof_uuid TEXT,path TEXT NOT NULL,"
    " source_uuid TEXT NOT NULL DEFAULT '',source_role TEXT NOT NULL DEFAULT '',"
    " input_hash TEXT NOT NULL DEFAULT '',"
    " anchor_stem TEXT NOT NULL,display_text TEXT NOT NULL,"
    " document_order INTEGER NOT NULL,identity_decision TEXT NOT NULL DEFAULT 'new',"
    " identity_evidence TEXT NOT NULL DEFAULT '',"
    " UNIQUE(origin,content_uuid));"
    "CREATE INDEX IF NOT EXISTS artifacts_path_idx ON artifacts(path);"
    "CREATE INDEX IF NOT EXISTS artifacts_search_idx ON artifacts(display_text);"
    "CREATE TABLE IF NOT EXISTS artifact_names("
    " artifact_uuid TEXT NOT NULL,name TEXT NOT NULL,ordinal INTEGER NOT NULL,"
    " name_tree TEXT NOT NULL,PRIMARY KEY(artifact_uuid,ordinal),"
    " FOREIGN KEY(artifact_uuid) REFERENCES artifacts(uuid) ON DELETE CASCADE);"
    "CREATE INDEX IF NOT EXISTS artifact_names_name_idx ON artifact_names(name);"
    "CREATE TABLE IF NOT EXISTS artifact_range_cache("
    " path TEXT NOT NULL,mtime_ns INTEGER NOT NULL,size INTEGER NOT NULL,"
    " semantic_hash TEXT NOT NULL DEFAULT '',"
    " input_hash TEXT NOT NULL DEFAULT '',"
    " request_hash TEXT NOT NULL,paragraph_offsets TEXT NOT NULL,"
    " updated_at INTEGER NOT NULL,"
    " PRIMARY KEY(path,mtime_ns,size,request_hash));"
    "CREATE INDEX IF NOT EXISTS artifact_range_cache_path_idx "
    "ON artifact_range_cache(path);"
    "CREATE TABLE IF NOT EXISTS artifact_identity_history("
    " sequence INTEGER PRIMARY KEY AUTOINCREMENT,path TEXT NOT NULL,"
    " origin TEXT NOT NULL,old_content_uuid TEXT,new_content_uuid TEXT,"
    " document_order INTEGER NOT NULL,decision TEXT NOT NULL,evidence TEXT NOT NULL,"
    " score INTEGER NOT NULL,old_margin INTEGER NOT NULL,new_margin INTEGER NOT NULL,"
    " global_delta INTEGER NOT NULL,created_at INTEGER NOT NULL);"
    "CREATE INDEX IF NOT EXISTS artifact_identity_history_path_idx "
    "ON artifact_identity_history(path,sequence);";
  if (!exec_sql (holder.db, schema, error)) return false;
  // Only empty indexes can be declared native without an offline conversion.
  // Schema v2 alone does not prove that preexisting tree payloads were migrated.
  if (!exec_sql (holder.db,
        "INSERT OR IGNORE INTO artifact_metadata(key,value) "
        "SELECT 'tree-format','utf8-xml-v1' WHERE NOT EXISTS(SELECT 1 FROM artifact_names);"
        "INSERT OR IGNORE INTO bold_text.bold_text_metadata(key,value) "
        "SELECT 'tree-format','utf8-xml-v1' WHERE NOT EXISTS(SELECT 1 FROM bold_text.entries);",
        error)) return false;
  if (!ensure_column (holder.db, "documents", "locator_contract",
                       "TEXT NOT NULL DEFAULT ''", error))
    return false;
  if (!ensure_column (holder.db, "documents", "semantic_hash",
                       "TEXT NOT NULL DEFAULT ''", error) ||
      !ensure_column (holder.db, "documents", "storage_hash",
                       "TEXT NOT NULL DEFAULT ''", error) ||
      !ensure_column (holder.db, "documents", "content_hash",
                       "TEXT NOT NULL DEFAULT ''", error) ||
      !ensure_column (holder.db, "artifact_range_cache", "semantic_hash",
                       "TEXT NOT NULL DEFAULT ''", error) ||
      !ensure_column (holder.db, "artifact_range_cache", "input_hash",
                       "TEXT NOT NULL DEFAULT ''", error) ||
      !ensure_column (holder.db, "artifacts", "source_uuid",
                       "TEXT NOT NULL DEFAULT ''", error) ||
      !ensure_column (holder.db, "artifacts", "source_role",
                       "TEXT NOT NULL DEFAULT ''", error) ||
      !ensure_column (holder.db, "artifacts", "input_hash",
                       "TEXT NOT NULL DEFAULT ''", error) ||
      !ensure_column (holder.db, "artifacts", "source_nodes",
                       "TEXT NOT NULL DEFAULT ''", error) ||
      !ensure_column (holder.db, "artifacts", "range_state",
                       "TEXT NOT NULL DEFAULT ''", error) ||
      !ensure_column (holder.db, "artifacts", "range_structure_hash",
                       "TEXT NOT NULL DEFAULT ''", error))
    return false;
  if (!exec_sql (holder.db,
        "CREATE UNIQUE INDEX IF NOT EXISTS artifacts_source_binding_idx "
        "ON artifacts(source_uuid,source_role) "
        "WHERE source_uuid<>'' AND source_role<>'';", error))
    return false;
  Statement version;
  if (!prepare (holder.db,
        "SELECT value FROM artifact_metadata WHERE key='schema-version';",
        version, error)) return false;
  int version_status= sqlite3_step (version.st);
  if (version_status == SQLITE_ROW) {
    std::string value= column_text (version.st, 0);
    if (value != "1" && value != "2" && value != "3" && value != "4" && value != "5") {
      error= "Unsupported artifact database schema version " + value;
      return false;
    }
  }
  else if (version_status != SQLITE_DONE) {
    error= sqlite3_errmsg (holder.db);
    return false;
  }
  if (!exec_sql (holder.db,
        "UPDATE artifact_range_cache SET input_hash=request_hash "
        "WHERE input_hash='';"
        "UPDATE artifacts SET range_state=CASE WHEN origin='bold-text' THEN 'resolved' "
        "ELSE 'not-applicable' END WHERE range_state='';"
        "INSERT INTO artifact_metadata(key,value) VALUES('schema-version','5') "
        "ON CONFLICT(key) DO UPDATE SET value='5';", error))
    return false;
  return true;
}


std::string offsets_text (const std::vector<int>& offsets) {
  std::ostringstream out;
  for (size_t i=0; i<offsets.size (); i++) {
    if (i) out << ',';
    out << offsets[i];
  }
  return out.str ();
}

std::vector<int> parse_offsets (const std::string& text) {
  std::vector<int> out;
  std::istringstream in (text);
  std::string part;
  while (std::getline (in, part, ',')) {
    try { out.push_back (std::stoi (part)); } catch (...) {}
  }
  return out;
}

} // namespace athena::artifact_database

using namespace athena::artifact_database;

bool
athena_artifacts_apply_path_rename (
  const fs::path& vault_root, const std::string& old_path,
  const std::string& new_path, bool is_directory, std::string& error) {
  if (old_path.empty () || new_path.empty () || old_path == new_path) {
    error= "Invalid artifact path rename";
    return false;
  }
  fs::path root= normalize_root (vault_root);
  AthenaVaultfileInfo configured;
  if (!athena_vaultfile_read (root, configured, error)) return false;
  if (!fs::exists (root / configured.artifacts_path)) return true;

  SqliteDb holder;
  AthenaVaultfileInfo info;
  if (!open_databases (root, holder, info, error)) return false;
  if (!exec_sql (holder.db, "BEGIN IMMEDIATE;", error)) return false;
  bool committed= false;
  auto rollback= [&] () {
    if (!committed) {
      std::string ignored;
      exec_sql (holder.db, "ROLLBACK;", ignored);
    }
  };

  auto is_affected= [&] (const std::string& path) {
    if (path == old_path) return true;
    return is_directory && path.size () > old_path.size () &&
           path.compare (0, old_path.size (), old_path) == 0 &&
           path[old_path.size ()] == '/';
  };
  auto renamed= [&] (const std::string& path) {
    return new_path + path.substr (old_path.size ());
  };
  auto rewrite_table= [&] (const char* table) {
    Statement query;
    std::string select= std::string ("SELECT DISTINCT path FROM ") + table +
                        " ORDER BY path;";
    if (!prepare (holder.db, select.c_str (), query, error)) return false;
    std::vector<std::pair<std::string,std::string>> changes;
    int status= SQLITE_ROW;
    while ((status= sqlite3_step (query.st)) == SQLITE_ROW) {
      std::string path= column_text (query.st, 0);
      if (is_affected (path)) changes.emplace_back (path, renamed (path));
    }
    if (status != SQLITE_DONE) {
      error= sqlite3_errmsg (holder.db);
      return false;
    }
    if (changes.empty ()) return true;

    Statement remove_destination;
    Statement update_source;
    std::string remove_sql= std::string ("DELETE FROM ") + table +
                            " WHERE path=?1;";
    std::string update_sql= std::string ("UPDATE ") + table +
                            " SET path=?1 WHERE path=?2;";
    if (!prepare (holder.db, remove_sql.c_str (), remove_destination, error) ||
        !prepare (holder.db, update_sql.c_str (), update_source, error))
      return false;
    // The filesystem destination did not exist when safe rename was planned.
    // Rows already carrying a destination path are therefore stale snapshots.
    for (const auto& change: changes) {
      sqlite3_reset (remove_destination.st);
      sqlite3_clear_bindings (remove_destination.st);
      bind_text (remove_destination.st, 1, change.second);
      if (sqlite3_step (remove_destination.st) != SQLITE_DONE) {
        error= sqlite3_errmsg (holder.db);
        return false;
      }
    }
    for (const auto& change: changes) {
      sqlite3_reset (update_source.st);
      sqlite3_clear_bindings (update_source.st);
      bind_text (update_source.st, 1, change.second);
      bind_text (update_source.st, 2, change.first);
      if (sqlite3_step (update_source.st) != SQLITE_DONE) {
        error= sqlite3_errmsg (holder.db);
        return false;
      }
    }
    return true;
  };

  for (const char* table:
       {"documents", "artifacts", "enunciations.entries",
        "bold_text.entries"})
    if (!rewrite_table (table)) {
      rollback ();
      return false;
    }
  if (!exec_sql (holder.db, "COMMIT;", error)) {
    rollback ();
    return false;
  }
  committed= true;
  athena::hodarium::notify_local_change (root, true);
  return true;
}

bool athena_artifacts_export_range_results (
  const fs::path& root, const std::string& after, unsigned limit,
  std::vector<HodariumArtifactRangeResult>& results, std::string& error) {
  results.clear (); error.clear ();
  if (limit == 0 || limit > 256) { error= "Invalid artifact result page size"; return false; }
  SqliteDb holder; AthenaVaultfileInfo info;
  if (!open_databases (root, holder, info, error, true)) return false;
  if (!holder.db) return true;
  Statement query;
  if (!prepare (holder.db,
    "SELECT a.uuid,a.source_uuid,a.source_role,a.input_hash,a.range_structure_hash,"
    "d.content_hash,b.paragraph_offsets FROM artifacts a "
    "JOIN documents d ON d.path=a.path JOIN bold_text.entries b ON b.uuid=a.content_uuid "
    "WHERE a.uuid>?1 AND a.origin='bold-text' AND a.range_state='resolved' "
    "AND a.source_uuid<>'' AND a.source_role<>'' AND a.input_hash<>'' "
    "AND a.range_structure_hash<>'' AND d.content_hash<>'' ORDER BY a.uuid LIMIT ?2",
    query, error)) return false;
  bind_text (query.st, 1, after); sqlite3_bind_int (query.st, 2, int (limit));
  int rc;
  while ((rc= sqlite3_step (query.st)) == SQLITE_ROW) {
    HodariumArtifactRangeResult result;
    result.artifact_uuid= column_text (query.st, 0);
    result.source_uuid= column_text (query.st, 1);
    result.source_role= column_text (query.st, 2);
    result.input_hash= column_text (query.st, 3);
    result.structure_hash= column_text (query.st, 4);
    result.content_hash= column_text (query.st, 5);
    result.offsets= parse_offsets (column_text (query.st, 6));
    results.push_back (std::move (result));
  }
  if (rc != SQLITE_DONE) { error= sqlite3_errmsg (holder.db); return false; }
  return true;
}

bool athena_artifacts_import_range_result (
  const fs::path& root, const HodariumArtifactRangeResult& result,
  bool& applicable, bool& changed, std::string& error,
  const std::function<bool()>& permitted) {
  applicable= false; changed= false; error.clear ();
  if (!athena::node::valid_id (result.artifact_uuid) ||
      !athena::node::valid_id (result.source_uuid) || result.source_role.empty () ||
      result.input_hash.empty () || result.structure_hash.empty () || result.content_hash.empty () ||
      result.offsets.empty () || result.offsets.size () > 11 ||
      result.offsets.front () < -5 || result.offsets.back () > 5 ||
      std::find (result.offsets.begin (), result.offsets.end (), 0) == result.offsets.end ()) {
    error= "Invalid reusable artifact range result"; return false;
  }
  for (size_t i=1; i<result.offsets.size (); ++i)
    if (result.offsets[i] != result.offsets[i-1] + 1) {
      error= "Noncontiguous reusable artifact range"; return false;
    }
  SqliteDb holder; AthenaVaultfileInfo info;
  if (!open_databases (root, holder, info, error)) return false;
  if (!exec_sql (holder.db, "BEGIN IMMEDIATE;", error)) return false;
  // The connection's destructor rolls back every early return. We deliberately
  // install only a checkpoint: the extractor rederives the model/input hash and
  // validates offsets before assigning a range to source nodes. No remote result
  // can overwrite an existing manual/resolved range or invent source identity.
  Statement source;
  if (!prepare (holder.db,
    "SELECT a.path,d.mtime_ns,d.size FROM artifacts a JOIN documents d ON d.path=a.path "
    "WHERE a.uuid=?1 AND a.source_uuid=?2 AND a.source_role=?3 "
    "AND a.origin='bold-text' AND a.range_structure_hash=?4 AND d.content_hash=?5",
    source, error)) return false;
  bind_text (source.st, 1, result.artifact_uuid); bind_text (source.st, 2, result.source_uuid);
  bind_text (source.st, 3, result.source_role); bind_text (source.st, 4, result.structure_hash);
  bind_text (source.st, 5, result.content_hash);
  int rc= sqlite3_step (source.st);
  if (rc == SQLITE_DONE) return true;
  if (rc != SQLITE_ROW) { error= sqlite3_errmsg (holder.db); return false; }
  const std::string relative= column_text (source.st, 0);
  const auto modified= sqlite3_column_int64 (source.st, 1);
  const auto size= sqlite3_column_int64 (source.st, 2);
  sqlite3_reset (source.st);
  Statement existing;
  if (!prepare (holder.db, "SELECT 1 FROM artifact_range_cache WHERE path=?1 "
    "AND COALESCE(NULLIF(input_hash,''),request_hash)=?2 LIMIT 1", existing, error)) return false;
  bind_text (existing.st, 1, relative); bind_text (existing.st, 2, result.input_hash);
  rc= sqlite3_step (existing.st);
  if (rc == SQLITE_ROW) { applicable= true; return true; }
  if (rc != SQLITE_DONE) { error= sqlite3_errmsg (holder.db); return false; }
  Statement insert;
  if (!prepare (holder.db, "INSERT INTO artifact_range_cache(path,mtime_ns,size,"
    "semantic_hash,input_hash,request_hash,paragraph_offsets,updated_at) "
    "VALUES(?1,?2,?3,?4,?5,?5,?6,?7)", insert, error)) return false;
  bind_text (insert.st, 1, relative); sqlite3_bind_int64 (insert.st, 2, modified);
  sqlite3_bind_int64 (insert.st, 3, size); bind_text (insert.st, 4, result.content_hash);
  bind_text (insert.st, 5, result.input_hash); bind_text (insert.st, 6, offsets_text (result.offsets));
  sqlite3_bind_int64 (insert.st, 7, std::chrono::duration_cast<std::chrono::seconds> (
    std::chrono::system_clock::now ().time_since_epoch ()).count ());
  if (sqlite3_step (insert.st) != SQLITE_DONE) { error= sqlite3_errmsg (holder.db); return false; }
  if (permitted && !permitted ()) return true;
  if (!exec_sql (holder.db, "COMMIT;", error)) return false;
  applicable= true; changed= true;
  return true;
}

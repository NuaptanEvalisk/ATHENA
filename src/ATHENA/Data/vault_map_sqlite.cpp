/******************************************************************************
* MODULE     : vault_map_sqlite.cpp
* DESCRIPTION: Non-temporal SQLite storage and TMDB migration for Vault maps
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "ATHENA/Data/vault_map_sqlite.hpp"

#include "ATHENA/Data/vaultfile_json.hpp"
#include "Database/database.hpp"
#include "tm_timer.hpp"
#include "url.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <climits>
#include <system_error>

namespace fs = std::filesystem;

namespace {

constexpr int vault_map_schema_version = 1;

std::string
sqlite_error (sqlite3* db, const std::string& operation) {
  return operation + ": " + (db == nullptr ? "SQLite is not open" :
                              sqlite3_errmsg (db));
}

bool
exec_sql (sqlite3* db, const char* sql, std::string& error) {
  char* message = nullptr;
  int status = sqlite3_exec (db, sql, nullptr, nullptr, &message);
  if (status == SQLITE_OK) return true;
  error = message == nullptr ? sqlite_error (db, "SQLite statement failed") :
                               std::string (message);
  sqlite3_free (message);
  return false;
}

bool
bind_text (sqlite3_stmt* statement, int index, const std::string& value,
           sqlite3* db, std::string& error) {
  if (sqlite3_bind_text (statement, index, value.data (), (int) value.size (),
                         SQLITE_TRANSIENT) == SQLITE_OK)
    return true;
  error = sqlite_error (db, "Could not bind Vault map value");
  return false;
}

std::string
column_text (sqlite3_stmt* statement, int column) {
  const unsigned char* value = sqlite3_column_text (statement, column);
  int bytes = sqlite3_column_bytes (statement, column);
  return value == nullptr ? std::string () :
                            std::string ((const char*) value, (size_t) bytes);
}

fs::path
numbered_backup_path (const fs::path& source) {
  fs::path base = source;
  base += ".old.bak";
  if (!fs::exists (base)) return base;
  for (int i=1; i<10000; ++i) {
    fs::path candidate = source;
    candidate += ".old.bak." + std::to_string (i);
    if (!fs::exists (candidate)) return candidate;
  }
  fs::path overflow = source;
  overflow += ".old.bak.overflow";
  return overflow;
}

bool
same_nodes (std::vector<AthenaVaultMapNode> a,
            std::vector<AthenaVaultMapNode> b) {
  auto less = [] (const AthenaVaultMapNode& x, const AthenaVaultMapNode& y) {
    return x.uuid < y.uuid;
  };
  std::sort (a.begin (), a.end (), less);
  std::sort (b.begin (), b.end (), less);
  if (a.size () != b.size ()) return false;
  for (size_t i=0; i<a.size (); ++i)
    if (a[i].uuid != b[i].uuid || a[i].path != b[i].path ||
        a[i].anchor_begin != b[i].anchor_begin ||
        a[i].anchor_end != b[i].anchor_end)
      return false;
  return true;
}

bool
read_tmdb_snapshot (const fs::path& source,
                    std::vector<AthenaVaultMapNode>& nodes,
                    std::string& error) {
  nodes.clear ();
  url db_url = url_system (string (source.string ().c_str ()));
  sync_databases ();
  db_time now = (db_time) (raw_time () / 1000);
  tree query_all (TUPLE);
  strings ids = query (db_url, query_all, now, INT_MAX, 0);
  for (int i=0; i<N(ids); ++i) {
    strings paths = get_field (db_url, ids[i], "v-path", now);
    if (N(paths) == 0) continue;
    strings begins = get_field (db_url, ids[i], "v-anchor-begin", now);
    strings ends = get_field (db_url, ids[i], "v-anchor-end", now);
    if (N(paths) != 1 || N(begins) > 1 || N(ends) > 1) {
      error = "Vault map migration found multiple active values for UUID " +
              std::string (as_charp (ids[i]), (size_t) N(ids[i]));
      return false;
    }
    AthenaVaultMapNode node;
    node.uuid = std::string (as_charp (ids[i]), (size_t) N(ids[i]));
    node.path = std::string (as_charp (paths[0]), (size_t) N(paths[0]));
    if (N(begins) == 1)
      node.anchor_begin = std::string (as_charp (begins[0]),
                                      (size_t) N(begins[0]));
    if (N(ends) == 1)
      node.anchor_end = std::string (as_charp (ends[0]),
                                    (size_t) N(ends[0]));
    nodes.push_back (std::move (node));
  }
  return true;
}

bool
validate_relative_map_path (const std::string& value, fs::path& relative,
                            std::string& error) {
  relative = fs::path (value).lexically_normal ();
  if (value.empty () || relative.is_absolute () || relative.empty () ||
      *relative.begin () == "..") {
    error = "Vault map path must be relative to the Vault root: " + value;
    return false;
  }
  return true;
}

} // namespace

struct AthenaVaultMapSqlite::Impl {
  sqlite3* db = nullptr;
};

AthenaVaultMapSqlite::AthenaVaultMapSqlite (): impl (new Impl) {}
AthenaVaultMapSqlite::~AthenaVaultMapSqlite () { close (); }

bool
AthenaVaultMapSqlite::open (const fs::path& path, bool create,
                            std::string& error) {
  close ();
  int flags = SQLITE_OPEN_READWRITE | (create ? SQLITE_OPEN_CREATE : 0);
  if (sqlite3_open_v2 (path.string ().c_str (), &impl->db, flags, nullptr) !=
      SQLITE_OK) {
    error = sqlite_error (impl->db, "Could not open Vault map " + path.string ());
    close ();
    return false;
  }
  sqlite3_busy_timeout (impl->db, 5000);
  if (!exec_sql (impl->db, "PRAGMA journal_mode=DELETE;", error) ||
      !exec_sql (impl->db, "PRAGMA foreign_keys=ON;", error)) {
    close ();
    return false;
  }
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2 (impl->db, "PRAGMA user_version;", -1, &statement,
                          nullptr) != SQLITE_OK ||
      sqlite3_step (statement) != SQLITE_ROW) {
    if (statement != nullptr) sqlite3_finalize (statement);
    error = sqlite_error (impl->db, "Could not read Vault map schema version");
    close ();
    return false;
  }
  int version = sqlite3_column_int (statement, 0);
  sqlite3_finalize (statement);
  if (create && version == 0) {
    statement = nullptr;
    const char* schema_query =
      "SELECT 1 FROM sqlite_master WHERE type='table' AND name='map_nodes';";
    if (sqlite3_prepare_v2 (impl->db, schema_query, -1, &statement, nullptr) !=
        SQLITE_OK) {
      error = sqlite_error (impl->db, "Could not inspect Vault map schema");
      close ();
      return false;
    }
    bool has_map_table = sqlite3_step (statement) == SQLITE_ROW;
    sqlite3_finalize (statement);
    if (has_map_table) {
      error = "Vault map has an unversioned map_nodes table: " + path.string ();
      close ();
      return false;
    }
    if (!exec_sql (impl->db,
        "CREATE TABLE IF NOT EXISTS map_nodes ("
        " sequence INTEGER PRIMARY KEY AUTOINCREMENT,"
        " uuid TEXT NOT NULL UNIQUE,"
        " path TEXT NOT NULL,"
        " anchor_begin TEXT NOT NULL DEFAULT '',"
        " anchor_end TEXT NOT NULL DEFAULT '');"
        "CREATE TABLE IF NOT EXISTS map_metadata ("
        " key TEXT PRIMARY KEY, value TEXT NOT NULL);"
        "INSERT INTO map_metadata(key,value) VALUES('format','athena-vault-map');"
        "CREATE INDEX IF NOT EXISTS map_nodes_location_idx "
        "ON map_nodes(path, anchor_begin, anchor_end);",
        error) ||
        !exec_sql (impl->db, "PRAGMA user_version=1;", error)) {
      close ();
      return false;
    }
    version = vault_map_schema_version;
  }
  if (version != vault_map_schema_version) {
    error = "Unsupported Vault map schema version " + std::to_string (version) +
            " in " + path.string ();
    close ();
    return false;
  }
  statement = nullptr;
  if (sqlite3_prepare_v2 (
        impl->db,
        "SELECT n.uuid,n.path,n.anchor_begin,n.anchor_end,m.value "
        "FROM map_nodes n, map_metadata m WHERE m.key='format' "
        "AND m.value='athena-vault-map' LIMIT 0;",
        -1, &statement, nullptr) != SQLITE_OK) {
    error = sqlite_error (impl->db, "Vault map schema is incomplete");
    close ();
    return false;
  }
  sqlite3_finalize (statement);
  if (!integrity_check (error)) {
    close ();
    return false;
  }
  return true;
}

void AthenaVaultMapSqlite::close () {
  if (impl->db != nullptr) sqlite3_close (impl->db);
  impl->db = nullptr;
}

bool AthenaVaultMapSqlite::valid () const { return impl->db != nullptr; }

bool
AthenaVaultMapSqlite::set_node (const AthenaVaultMapNode& node,
                                std::string& error) {
  const char* sql =
    "INSERT INTO map_nodes(uuid,path,anchor_begin,anchor_end) VALUES(?,?,?,?) "
    "ON CONFLICT(uuid) DO UPDATE SET path=excluded.path,"
    "anchor_begin=excluded.anchor_begin,anchor_end=excluded.anchor_end;";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2 (impl->db, sql, -1, &st, nullptr) != SQLITE_OK) {
    error = sqlite_error (impl->db, "Could not prepare Vault map upsert");
    return false;
  }
  bool ok = bind_text (st, 1, node.uuid, impl->db, error) &&
            bind_text (st, 2, node.path, impl->db, error) &&
            bind_text (st, 3, node.anchor_begin, impl->db, error) &&
            bind_text (st, 4, node.anchor_end, impl->db, error) &&
            sqlite3_step (st) == SQLITE_DONE;
  if (!ok && error.empty ()) error = sqlite_error (impl->db, "Vault map upsert failed");
  sqlite3_finalize (st);
  return ok;
}

bool
AthenaVaultMapSqlite::get_node (const std::string& uuid,
                                AthenaVaultMapNode& node, bool& found,
                                std::string& error) const {
  sqlite3_stmt* st = nullptr;
  const char* sql = "SELECT uuid,path,anchor_begin,anchor_end FROM map_nodes "
                    "WHERE uuid=?;";
  if (sqlite3_prepare_v2 (impl->db, sql, -1, &st, nullptr) != SQLITE_OK) {
    error = sqlite_error (impl->db, "Could not prepare Vault map lookup");
    return false;
  }
  if (!bind_text (st, 1, uuid, impl->db, error)) {
    sqlite3_finalize (st);
    return false;
  }
  int status = sqlite3_step (st);
  found = status == SQLITE_ROW;
  if (found) {
    node.uuid = column_text (st, 0);
    node.path = column_text (st, 1);
    node.anchor_begin = column_text (st, 2);
    node.anchor_end = column_text (st, 3);
  }
  bool ok = found || status == SQLITE_DONE;
  if (!ok) error = sqlite_error (impl->db, "Vault map lookup failed");
  sqlite3_finalize (st);
  return ok;
}

bool
AthenaVaultMapSqlite::remove_node (const std::string& uuid,
                                   std::string& error) {
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2 (impl->db, "DELETE FROM map_nodes WHERE uuid=?;", -1,
                          &st, nullptr) != SQLITE_OK) {
    error = sqlite_error (impl->db, "Could not prepare Vault map deletion");
    return false;
  }
  bool ok = bind_text (st, 1, uuid, impl->db, error) &&
            sqlite3_step (st) == SQLITE_DONE;
  if (!ok && error.empty ()) error = sqlite_error (impl->db, "Vault map deletion failed");
  sqlite3_finalize (st);
  return ok;
}

bool
AthenaVaultMapSqlite::has_node (const std::string& uuid, bool& found,
                                std::string& error) const {
  AthenaVaultMapNode ignored;
  return get_node (uuid, ignored, found, error);
}

bool
AthenaVaultMapSqlite::find_uuid (const std::string& path,
                                 const std::string& anchor_begin,
                                 const std::string& anchor_end,
                                 std::string& uuid,
                                 std::string& error) const {
  sqlite3_stmt* st = nullptr;
  const char* sql = "SELECT uuid FROM map_nodes WHERE path=? AND anchor_begin=? "
                    "AND anchor_end=? ORDER BY sequence LIMIT 1;";
  if (sqlite3_prepare_v2 (impl->db, sql, -1, &st, nullptr) != SQLITE_OK) {
    error = sqlite_error (impl->db, "Could not prepare Vault map reverse lookup");
    return false;
  }
  bool bound = bind_text (st, 1, path, impl->db, error) &&
               bind_text (st, 2, anchor_begin, impl->db, error) &&
               bind_text (st, 3, anchor_end, impl->db, error);
  if (!bound) {
    sqlite3_finalize (st);
    return false;
  }
  int status = sqlite3_step (st);
  uuid = status == SQLITE_ROW ? column_text (st, 0) : std::string ();
  bool ok = status == SQLITE_ROW || status == SQLITE_DONE;
  if (!ok) error = sqlite_error (impl->db, "Vault map reverse lookup failed");
  sqlite3_finalize (st);
  return ok;
}

bool
AthenaVaultMapSqlite::replace_all (
  const std::vector<AthenaVaultMapNode>& nodes, std::string& error) {
  if (!exec_sql (impl->db, "BEGIN IMMEDIATE;", error)) return false;
  if (!exec_sql (impl->db, "DELETE FROM map_nodes;", error)) {
    exec_sql (impl->db, "ROLLBACK;", error);
    return false;
  }
  for (const AthenaVaultMapNode& node: nodes)
    if (!set_node (node, error)) {
      std::string ignored;
      exec_sql (impl->db, "ROLLBACK;", ignored);
      return false;
    }
  if (!exec_sql (impl->db, "COMMIT;", error)) {
    std::string ignored;
    exec_sql (impl->db, "ROLLBACK;", ignored);
    return false;
  }
  return true;
}

bool
AthenaVaultMapSqlite::read_all (std::vector<AthenaVaultMapNode>& nodes,
                                std::string& error) const {
  nodes.clear ();
  sqlite3_stmt* st = nullptr;
  const char* sql = "SELECT uuid,path,anchor_begin,anchor_end FROM map_nodes "
                    "ORDER BY sequence;";
  if (sqlite3_prepare_v2 (impl->db, sql, -1, &st, nullptr) != SQLITE_OK) {
    error = sqlite_error (impl->db, "Could not enumerate Vault map");
    return false;
  }
  int status;
  while ((status = sqlite3_step (st)) == SQLITE_ROW) {
    nodes.push_back ({column_text (st, 0), column_text (st, 1),
                      column_text (st, 2), column_text (st, 3)});
  }
  bool ok = status == SQLITE_DONE;
  if (!ok) error = sqlite_error (impl->db, "Could not enumerate Vault map");
  sqlite3_finalize (st);
  return ok;
}

bool
AthenaVaultMapSqlite::rewrite_anchors (
  const std::string& path,
  const std::vector<std::pair<std::string, std::string>>& renames,
  size_t& changed, std::string& error) {
  changed = 0;
  if (renames.empty ()) return true;
  if (!exec_sql (impl->db, "BEGIN IMMEDIATE;", error)) return false;
  const char* fields[] = {"anchor_begin", "anchor_end"};
  for (const auto& rename: renames) {
    for (const char* field: fields) {
      std::string sql = "UPDATE map_nodes SET " + std::string (field) +
                        "=? WHERE path=? AND " + field + "=?;";
      sqlite3_stmt* st = nullptr;
      if (sqlite3_prepare_v2 (impl->db, sql.c_str (), -1, &st, nullptr) !=
          SQLITE_OK ||
          !bind_text (st, 1, rename.second, impl->db, error) ||
          !bind_text (st, 2, path, impl->db, error) ||
          !bind_text (st, 3, rename.first, impl->db, error) ||
          sqlite3_step (st) != SQLITE_DONE) {
        if (error.empty ()) error = sqlite_error (impl->db, "Anchor rewrite failed");
        if (st != nullptr) sqlite3_finalize (st);
        std::string ignored;
        exec_sql (impl->db, "ROLLBACK;", ignored);
        return false;
      }
      changed += (size_t) sqlite3_changes (impl->db);
      sqlite3_finalize (st);
    }
  }
  if (!exec_sql (impl->db, "COMMIT;", error)) {
    std::string ignored;
    exec_sql (impl->db, "ROLLBACK;", ignored);
    return false;
  }
  return true;
}

bool
AthenaVaultMapSqlite::integrity_check (std::string& error) const {
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2 (impl->db, "PRAGMA integrity_check;", -1, &st,
                          nullptr) != SQLITE_OK ||
      sqlite3_step (st) != SQLITE_ROW || column_text (st, 0) != "ok") {
    if (st != nullptr) sqlite3_finalize (st);
    error = sqlite_error (impl->db, "Vault map integrity check failed");
    return false;
  }
  sqlite3_finalize (st);
  return true;
}

bool
AthenaVaultMapSqlite::set_migration_source (const std::string& relative_path,
                                            std::string& error) {
  sqlite3_stmt* st = nullptr;
  const char* sql =
    "INSERT INTO map_metadata(key,value) VALUES('migration_source',?) "
    "ON CONFLICT(key) DO UPDATE SET value=excluded.value;";
  if (sqlite3_prepare_v2 (impl->db, sql, -1, &st, nullptr) != SQLITE_OK) {
    error = sqlite_error (impl->db, "Could not prepare migration metadata");
    return false;
  }
  bool ok = bind_text (st, 1, relative_path, impl->db, error) &&
            sqlite3_step (st) == SQLITE_DONE;
  if (!ok && error.empty ())
    error = sqlite_error (impl->db, "Could not store migration metadata");
  sqlite3_finalize (st);
  return ok;
}

bool
AthenaVaultMapSqlite::migration_source (std::string& relative_path,
                                        std::string& error) const {
  relative_path.clear ();
  sqlite3_stmt* st = nullptr;
  const char* sql =
    "SELECT value FROM map_metadata WHERE key='migration_source';";
  if (sqlite3_prepare_v2 (impl->db, sql, -1, &st, nullptr) != SQLITE_OK) {
    error = sqlite_error (impl->db, "Could not read migration metadata");
    return false;
  }
  int status = sqlite3_step (st);
  if (status == SQLITE_ROW) relative_path = column_text (st, 0);
  bool ok = status == SQLITE_ROW || status == SQLITE_DONE;
  if (!ok) error = sqlite_error (impl->db, "Could not read migration metadata");
  sqlite3_finalize (st);
  return ok;
}

bool
athena_vault_map_prepare (const fs::path& root,
                          const std::string& requested_relative_path,
                          std::string& resolved_relative_path,
                          std::string& error) {
  fs::path requested;
  if (!validate_relative_map_path (requested_relative_path, requested, error))
    return false;
  fs::path source = root / requested;
  if (requested.extension () == ".sqlite") {
    AthenaVaultMapSqlite map;
    if (!map.open (source, true, error)) return false;
    resolved_relative_path = requested.generic_string ();
    return true;
  }
  if (requested.extension () != ".tmdb") {
    error = "Unsupported Vault map format: " + requested_relative_path;
    return false;
  }
  fs::path target_relative = requested;
  target_relative.replace_extension (".sqlite");
  fs::path target = root / target_relative;
  fs::path temporary = target;
  temporary += ".migrate.tmp";
  if (!fs::exists (source)) {
    AthenaVaultMapSqlite recovered;
    std::string migration_source;
    if (!fs::exists (target) ||
        !recovered.open (target, false, error) ||
        !recovered.migration_source (migration_source, error) ||
        migration_source != requested.generic_string ()) {
      if (error.empty ())
        error = "Legacy Vault map does not exist: " + source.string ();
      return false;
    }
    AthenaVaultfileInfo recovered_info;
    if (!athena_vaultfile_read (root, recovered_info, error)) return false;
    recovered_info.map_path = target_relative.generic_string ();
    if (!athena_vaultfile_write (root, recovered_info, error)) return false;
    resolved_relative_path = target_relative.generic_string ();
    return true;
  }
  std::vector<AthenaVaultMapNode> legacy_nodes;
  if (!read_tmdb_snapshot (source, legacy_nodes, error)) return false;

  bool created_target = false;
  if (fs::exists (target)) {
    AthenaVaultMapSqlite existing;
    std::vector<AthenaVaultMapNode> existing_nodes;
    if (!existing.open (target, false, error) ||
        !existing.read_all (existing_nodes, error) ||
        !same_nodes (legacy_nodes, existing_nodes)) {
      if (error.empty ())
        error = "Existing SQLite Vault map does not match legacy map: " +
                target.string ();
      return false;
    }
  }
  else {
    std::error_code ec;
    fs::remove (temporary, ec);
    AthenaVaultMapSqlite migrated;
    if (!migrated.open (temporary, true, error) ||
        !migrated.replace_all (legacy_nodes, error) ||
        !migrated.set_migration_source (requested.generic_string (), error) ||
        !migrated.integrity_check (error)) {
      migrated.close ();
      fs::remove (temporary, ec);
      return false;
    }
    std::vector<AthenaVaultMapNode> migrated_nodes;
    if (!migrated.read_all (migrated_nodes, error) ||
        !same_nodes (legacy_nodes, migrated_nodes)) {
      migrated.close ();
      fs::remove (temporary, ec);
      if (error.empty ()) error = "Migrated Vault map verification failed";
      return false;
    }
    migrated.close ();
    fs::rename (temporary, target, ec);
    if (ec) {
      fs::remove (temporary);
      error = "Could not install migrated Vault map: " + ec.message ();
      return false;
    }
    created_target = true;
  }

  AthenaVaultfileInfo info;
  if (!athena_vaultfile_read (root, info, error)) {
    if (created_target) fs::remove (target);
    return false;
  }
  info.map_path = target_relative.generic_string ();
  fs::path backup = numbered_backup_path (source);
  std::error_code ec;
  fs::rename (source, backup, ec);
  if (ec) {
    if (created_target) fs::remove (target);
    error = "Could not archive legacy Vault map: " + ec.message ();
    return false;
  }
  if (!athena_vaultfile_write (root, info, error)) {
    std::error_code rollback_error;
    fs::rename (backup, source, rollback_error);
    if (created_target) fs::remove (target);
    if (rollback_error)
      error += "; additionally could not restore legacy map: " +
               rollback_error.message ();
    return false;
  }
  resolved_relative_path = target_relative.generic_string ();
  return true;
}

bool
athena_vault_map_rewrite_at_root (
  const fs::path& root, const std::string& relative_document_path,
  const std::vector<std::pair<std::string, std::string>>& renames,
  size_t& changed, std::string& error) {
  AthenaVaultfileInfo info;
  if (!athena_vaultfile_read (root, info, error)) return false;
  AthenaVaultMapSqlite map;
  if (!map.open (root / info.map_path, false, error)) return false;
  return map.rewrite_anchors (relative_document_path, renames, changed, error);
}

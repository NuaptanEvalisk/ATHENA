/******************************************************************************
* MODULE     : vault_map_sqlite.cpp
* DESCRIPTION: Non-temporal SQLite storage for Vault maps
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "ATHENA/Data/vault_map_sqlite.hpp"

#include "ATHENA/Data/vaultfile_json.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <system_error>

namespace fs = std::filesystem;

namespace {

constexpr int vault_map_schema_version = 2;

bool
path_is_renamed (const std::string& path, const std::string& old_path,
                 bool is_directory) {
  if (path == old_path) return true;
  return is_directory && path.size () > old_path.size () &&
         path.compare (0, old_path.size (), old_path) == 0 &&
         path[old_path.size ()] == '/';
}

std::string
renamed_path (const std::string& path, const std::string& old_path,
              const std::string& new_path) {
  return new_path + path.substr (old_path.size ());
}

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
        "CREATE TABLE IF NOT EXISTS rename_operations ("
        " operation_id TEXT PRIMARY KEY,"
        " old_path TEXT NOT NULL, new_path TEXT NOT NULL,"
        " is_directory INTEGER NOT NULL, phase TEXT NOT NULL);"
        "CREATE INDEX IF NOT EXISTS map_nodes_location_idx "
        "ON map_nodes(path, anchor_begin, anchor_end);",
        error) ||
        !exec_sql (impl->db, "PRAGMA user_version=2;", error)) {
      close ();
      return false;
    }
    version = vault_map_schema_version;
  }
  if (version == 1) {
    if (!exec_sql (impl->db,
          "BEGIN IMMEDIATE;"
          "CREATE TABLE IF NOT EXISTS rename_operations ("
          " operation_id TEXT PRIMARY KEY,"
          " old_path TEXT NOT NULL, new_path TEXT NOT NULL,"
          " is_directory INTEGER NOT NULL, phase TEXT NOT NULL);"
          "PRAGMA user_version=2;"
          "COMMIT;", error)) {
      close ();
      return false;
    }
    version = 2;
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
        "SELECT n.uuid,n.path,n.anchor_begin,n.anchor_end,m.value,o.phase "
        "FROM map_nodes n LEFT JOIN rename_operations o ON 0, map_metadata m "
        "WHERE m.key='format' AND m.value='athena-vault-map' LIMIT 0;",
        -1, &statement, nullptr) != SQLITE_OK) {
    error = sqlite_error (impl->db, "Vault map schema is incomplete");
    close ();
    return false;
  }
  sqlite3_finalize (statement);
  if (!quick_check (error)) {
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
AthenaVaultMapSqlite::quick_check (std::string& error) const {
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2 (impl->db, "PRAGMA quick_check;", -1, &st,
                          nullptr) != SQLITE_OK ||
      sqlite3_step (st) != SQLITE_ROW || column_text (st, 0) != "ok") {
    if (st != nullptr) sqlite3_finalize (st);
    error = sqlite_error (impl->db, "Vault map quick check failed");
    return false;
  }
  sqlite3_finalize (st);
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
AthenaVaultMapSqlite::count_path_rename (const std::string& old_path,
                                         bool is_directory, size_t& count,
                                         std::string& error) const {
  std::vector<AthenaVaultMapNode> nodes;
  if (!read_all (nodes, error)) return false;
  count = 0;
  for (const AthenaVaultMapNode& node: nodes)
    if (path_is_renamed (node.path, old_path, is_directory)) ++count;
  return true;
}

bool
AthenaVaultMapSqlite::prepare_path_rename (
  const AthenaVaultMapRenameOperation& operation, std::string& error) {
  sqlite3_stmt* st = nullptr;
  const char* sql =
    "INSERT INTO rename_operations(operation_id,old_path,new_path,"
    "is_directory,phase) VALUES(?,?,?,?,?);";
  if (sqlite3_prepare_v2 (impl->db, sql, -1, &st, nullptr) != SQLITE_OK) {
    error = sqlite_error (impl->db, "Could not prepare safe rename journal");
    return false;
  }
  bool ok = bind_text (st, 1, operation.operation_id, impl->db, error) &&
            bind_text (st, 2, operation.old_path, impl->db, error) &&
            bind_text (st, 3, operation.new_path, impl->db, error) &&
            sqlite3_bind_int (st, 4, operation.is_directory ? 1 : 0) ==
              SQLITE_OK &&
            bind_text (st, 5, operation.phase.empty () ? "prepared" :
                                                        operation.phase,
                       impl->db, error) &&
            sqlite3_step (st) == SQLITE_DONE;
  if (!ok && error.empty ())
    error = sqlite_error (impl->db, "Could not record safe rename journal");
  sqlite3_finalize (st);
  return ok;
}

bool
AthenaVaultMapSqlite::pending_path_renames (
  std::vector<AthenaVaultMapRenameOperation>& operations,
  std::string& error) const {
  operations.clear ();
  sqlite3_stmt* st = nullptr;
  const char* sql =
    "SELECT operation_id,old_path,new_path,is_directory,phase "
    "FROM rename_operations ORDER BY rowid;";
  if (sqlite3_prepare_v2 (impl->db, sql, -1, &st, nullptr) != SQLITE_OK) {
    error = sqlite_error (impl->db, "Could not read safe rename journal");
    return false;
  }
  int status;
  while ((status = sqlite3_step (st)) == SQLITE_ROW) {
    AthenaVaultMapRenameOperation operation;
    operation.operation_id = column_text (st, 0);
    operation.old_path = column_text (st, 1);
    operation.new_path = column_text (st, 2);
    operation.is_directory = sqlite3_column_int (st, 3) != 0;
    operation.phase = column_text (st, 4);
    operations.push_back (std::move (operation));
  }
  bool ok = status == SQLITE_DONE;
  if (!ok) error = sqlite_error (impl->db, "Could not read safe rename journal");
  sqlite3_finalize (st);
  return ok;
}

bool
AthenaVaultMapSqlite::apply_path_rename (const std::string& operation_id,
                                         size_t& changed,
                                         std::string& error) {
  changed = 0;
  std::vector<AthenaVaultMapRenameOperation> operations;
  if (!pending_path_renames (operations, error)) return false;
  auto hit = std::find_if (
    operations.begin (), operations.end (), [&] (const auto& operation) {
      return operation.operation_id == operation_id;
    });
  if (hit == operations.end ()) {
    error = "Unknown safe rename operation: " + operation_id;
    return false;
  }

  if (!exec_sql (impl->db, "BEGIN IMMEDIATE;", error)) return false;
  std::vector<AthenaVaultMapNode> nodes;
  if (!read_all (nodes, error)) {
    std::string ignored;
    exec_sql (impl->db, "ROLLBACK;", ignored);
    return false;
  }
  sqlite3_stmt* update = nullptr;
  const char* sql = "UPDATE map_nodes SET path=? WHERE uuid=?;";
  if (sqlite3_prepare_v2 (impl->db, sql, -1, &update, nullptr) != SQLITE_OK) {
    error = sqlite_error (impl->db, "Could not prepare Vault map path update");
    std::string ignored;
    exec_sql (impl->db, "ROLLBACK;", ignored);
    return false;
  }
  for (const AthenaVaultMapNode& node: nodes) {
    if (!path_is_renamed (node.path, hit->old_path, hit->is_directory))
      continue;
    std::string next = renamed_path (node.path, hit->old_path, hit->new_path);
    sqlite3_reset (update);
    sqlite3_clear_bindings (update);
    if (!bind_text (update, 1, next, impl->db, error) ||
        !bind_text (update, 2, node.uuid, impl->db, error) ||
        sqlite3_step (update) != SQLITE_DONE) {
      if (error.empty ())
        error = sqlite_error (impl->db, "Could not update Vault map path");
      sqlite3_finalize (update);
      std::string ignored;
      exec_sql (impl->db, "ROLLBACK;", ignored);
      return false;
    }
    ++changed;
  }
  sqlite3_finalize (update);
  sqlite3_stmt* phase = nullptr;
  if (sqlite3_prepare_v2 (
        impl->db,
        "UPDATE rename_operations SET phase='map_updated' "
        "WHERE operation_id=?;", -1, &phase, nullptr) != SQLITE_OK ||
      !bind_text (phase, 1, operation_id, impl->db, error) ||
      sqlite3_step (phase) != SQLITE_DONE) {
    if (error.empty ())
      error = sqlite_error (impl->db, "Could not update safe rename phase");
    if (phase != nullptr) sqlite3_finalize (phase);
    std::string ignored;
    exec_sql (impl->db, "ROLLBACK;", ignored);
    return false;
  }
  sqlite3_finalize (phase);
  if (!exec_sql (impl->db, "COMMIT;", error)) {
    std::string ignored;
    exec_sql (impl->db, "ROLLBACK;", ignored);
    return false;
  }
  return true;
}

bool
AthenaVaultMapSqlite::finish_path_rename (const std::string& operation_id,
                                          std::string& error) {
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2 (
        impl->db, "DELETE FROM rename_operations WHERE operation_id=?;", -1,
        &st, nullptr) != SQLITE_OK) {
    error = sqlite_error (impl->db, "Could not prepare safe rename completion");
    return false;
  }
  bool ok = bind_text (st, 1, operation_id, impl->db, error) &&
            sqlite3_step (st) == SQLITE_DONE;
  if (!ok && error.empty ())
    error = sqlite_error (impl->db, "Could not complete safe rename");
  sqlite3_finalize (st);
  return ok;
}

bool
athena_vault_map_prepare (const std::string& requested_relative_path,
                          std::string& resolved_relative_path,
                          std::string& error) {
  fs::path requested;
  if (!validate_relative_map_path (requested_relative_path, requested, error))
    return false;
  if (requested.extension () != ".sqlite") {
    error = "Unsupported Vault map format (expected .sqlite): " +
            requested_relative_path;
    return false;
  }
  resolved_relative_path = requested.generic_string ();
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

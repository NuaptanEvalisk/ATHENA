/******************************************************************************
* MODULE     : namespaces_schema.cpp
* DESCRIPTION: Shared namespace database opening and transactional migration
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "namespaces_schema.hpp"

#include <QUuid>
#include <sqlite3.h>
#include <memory>
#include <vector>

namespace {

using statement= std::unique_ptr<sqlite3_stmt, decltype (&sqlite3_finalize)>;

bool
exec (sqlite3* db, const char* sql, std::string& error) {
  if (sqlite3_exec (db, sql, nullptr, nullptr, nullptr) == SQLITE_OK)
    return true;
  error= sqlite3_errmsg (db);
  return false;
}

statement
prepare (sqlite3* db, const char* sql, std::string& error) {
  sqlite3_stmt* stmt= nullptr;
  if (sqlite3_prepare_v2 (db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    error= sqlite3_errmsg (db);
  return statement (stmt, sqlite3_finalize);
}

bool
schema_version (sqlite3* db, int& version, std::string& error) {
  version= 0;
  auto table= prepare (db,
    "SELECT 1 FROM sqlite_master WHERE type='table' AND name='meta';", error);
  if (!table) return false;
  int rc= sqlite3_step (table.get ());
  if (rc == SQLITE_DONE) return true;
  if (rc != SQLITE_ROW) { error= sqlite3_errmsg (db); return false; }
  auto value= prepare (db,
    "SELECT value FROM meta WHERE key='schema-version';", error);
  if (!value) return false;
  rc= sqlite3_step (value.get ());
  if (rc == SQLITE_DONE) return true;
  if (rc != SQLITE_ROW) { error= sqlite3_errmsg (db); return false; }
  const auto* text= sqlite3_column_text (value.get (), 0);
  std::string v= text ? reinterpret_cast<const char*> (text) : "";
  if (v == "1") version= 1;
  else if (v == "2") version= 2;
  else if (v == "3") version= 3;
  else {
    error= "Unsupported namespace schema version: " + v;
    return false;
  }
  return true;
}

bool
ensure_column (sqlite3* db, const char* column, const char* definition,
               std::string& error) {
  auto stmt= prepare (db, "PRAGMA table_info(namespaces);", error);
  if (!stmt) return false;
  int rc;
  while ((rc= sqlite3_step (stmt.get ())) == SQLITE_ROW) {
    const auto* name= sqlite3_column_text (stmt.get (), 1);
    if (name && std::string (reinterpret_cast<const char*> (name)) == column)
      return true;
  }
  if (rc != SQLITE_DONE) { error= sqlite3_errmsg (db); return false; }
  stmt.reset ();
  return exec (db, (std::string ("ALTER TABLE namespaces ADD COLUMN ") +
                    column + " " + definition + ";").c_str (), error);
}

bool
migrate (sqlite3* db, std::string& error) {
  if (!exec (db,
    "CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY,value TEXT NOT NULL);"
    "CREATE TABLE IF NOT EXISTS namespaces("
    "name TEXT PRIMARY KEY,"
    "kind TEXT NOT NULL CHECK(kind IN ('abstract','semi-concrete','concrete')),"
    "template TEXT NOT NULL DEFAULT '',"
    "sorter_path TEXT NOT NULL DEFAULT '',style_path TEXT NOT NULL DEFAULT '');"
    "CREATE TABLE IF NOT EXISTS namespace_parents("
    "child TEXT NOT NULL,parent TEXT NOT NULL,"
    "source TEXT NOT NULL CHECK(source IN ('declared','derived')),"
    "ord INTEGER NOT NULL DEFAULT 0,PRIMARY KEY(child,parent,source));"
    "CREATE INDEX IF NOT EXISTS namespace_parents_child_idx "
    "ON namespace_parents(child,source,ord);"
    "CREATE INDEX IF NOT EXISTS namespace_parents_parent_idx "
    "ON namespace_parents(parent);"
    "CREATE TABLE IF NOT EXISTS relation_decisions("
    "parent TEXT NOT NULL,child TEXT NOT NULL,"
    "decision TEXT NOT NULL CHECK(decision IN ('allow','deny')),"
    "source TEXT NOT NULL DEFAULT 'user',PRIMARY KEY(parent,child));", error) ||
      !ensure_column (db, "sorter_trivial", "INTEGER NOT NULL DEFAULT 0", error) ||
      !ensure_column (db, "initial_content_path", "TEXT NOT NULL DEFAULT ''", error) ||
      !ensure_column (db, "homepage_path", "TEXT NOT NULL DEFAULT ''", error) ||
      !ensure_column (db, "uuid", "TEXT NOT NULL DEFAULT ''", error))
    return false;

  // Add identities in place, preserving existing rows, indexes and relations.
  std::vector<std::string> names;
  auto rows= prepare (db, "SELECT name FROM namespaces WHERE uuid='';", error);
  if (!rows) return false;
  int rc;
  while ((rc= sqlite3_step (rows.get ())) == SQLITE_ROW) {
    const auto* name= sqlite3_column_text (rows.get (), 0);
    if (!name) { error= "Namespace has a null name"; return false; }
    names.emplace_back (reinterpret_cast<const char*> (name));
  }
  if (rc != SQLITE_DONE) { error= sqlite3_errmsg (db); return false; }
  rows.reset ();
  auto update= prepare (db, "UPDATE namespaces SET uuid=? WHERE name=?;", error);
  if (!update) return false;
  for (const auto& name: names) {
    std::string uuid= athena_namespace_new_uuid ();
    if (sqlite3_bind_text (update.get (), 1, uuid.c_str (), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text (update.get (), 2, name.c_str (), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_step (update.get ()) != SQLITE_DONE) {
      error= sqlite3_errmsg (db);
      return false;
    }
    sqlite3_reset (update.get ());
  }
  update.reset ();
  return exec (db,
    "CREATE UNIQUE INDEX namespaces_uuid_idx ON namespaces(uuid);"
    "CREATE TRIGGER namespaces_uuid_insert BEFORE INSERT ON namespaces "
    "WHEN NEW.uuid IS NULL OR NEW.uuid='' BEGIN "
    "SELECT RAISE(ABORT,'Namespace UUID must not be empty'); END;"
    "CREATE TRIGGER namespaces_uuid_update BEFORE UPDATE OF uuid ON namespaces "
    "WHEN NEW.uuid IS NOT OLD.uuid BEGIN "
    "SELECT RAISE(ABORT,'Namespace UUID is immutable'); END;"
    "INSERT INTO meta(key,value) VALUES('schema-version','2') "
    "ON CONFLICT(key) DO UPDATE SET value=excluded.value;", error);
}

bool
migrate_materials (sqlite3* db, std::string& error) {
  return exec (db,
    "CREATE TABLE namespace_materials("
    "namespace_uuid TEXT NOT NULL REFERENCES namespaces(uuid) ON DELETE CASCADE,"
    "material_uuid TEXT NOT NULL CHECK(material_uuid<>''),"
    "ord INTEGER NOT NULL CHECK(ord>=0),"
    "PRIMARY KEY(namespace_uuid,material_uuid),UNIQUE(namespace_uuid,ord));"
    "CREATE TRIGGER namespace_materials_concrete_insert BEFORE INSERT ON namespace_materials "
    "WHEN (SELECT kind FROM namespaces WHERE uuid=NEW.namespace_uuid)='abstract' "
    "BEGIN SELECT RAISE(ABORT,'Abstract namespaces cannot specify Materials'); END;"
    "CREATE TRIGGER namespace_materials_concrete_update BEFORE UPDATE ON namespace_materials "
    "WHEN (SELECT kind FROM namespaces WHERE uuid=NEW.namespace_uuid)='abstract' "
    "BEGIN SELECT RAISE(ABORT,'Abstract namespaces cannot specify Materials'); END;"
    "CREATE TRIGGER namespace_materials_abstract AFTER UPDATE OF kind ON namespaces "
    "WHEN NEW.kind='abstract' BEGIN DELETE FROM namespace_materials WHERE namespace_uuid=NEW.uuid; END;"
    "UPDATE meta SET value='3' WHERE key='schema-version';", error);
}

} // namespace

std::string
athena_namespace_new_uuid () {
  return QUuid::createUuid ().toString (QUuid::WithoutBraces).toStdString ();
}

bool
athena_namespace_schema_ensure (sqlite3* db, std::string& error) {
  error.clear ();
  int version;
  if (!schema_version (db, version, error)) return false;
  if (version == 3) return true;
  if (!exec (db, "BEGIN IMMEDIATE;", error)) return false;
  // Another opener may have migrated while this connection waited for the lock.
  bool ok= schema_version (db, version, error);
  if (ok && version < 2) ok= migrate (db, error);
  if (ok && version < 3) ok= migrate_materials (db, error);
  if (ok) ok= exec (db, "COMMIT;", error);
  if (!ok) sqlite3_exec (db, "ROLLBACK;", nullptr, nullptr, nullptr);
  return ok;
}

bool
athena_namespace_read_materials (sqlite3* db, const std::string& uuid,
                                 std::vector<std::string>& materials,
                                 std::string& error) {
  materials.clear ();
  auto query= prepare (db,
    "SELECT material_uuid FROM namespace_materials WHERE namespace_uuid=? ORDER BY ord;", error);
  if (!query) return false;
  if (sqlite3_bind_text (query.get (), 1, uuid.data (), int (uuid.size ()), SQLITE_TRANSIENT) != SQLITE_OK) {
    error= sqlite3_errmsg (db);
    return false;
  }
  int rc;
  while ((rc= sqlite3_step (query.get ())) == SQLITE_ROW) {
    const char* text= reinterpret_cast<const char*> (sqlite3_column_text (query.get (), 0));
    materials.emplace_back (text, sqlite3_column_bytes (query.get (), 0));
  }
  if (rc != SQLITE_DONE) { error= sqlite3_errmsg (db); return false; }
  return true;
}

bool
athena_namespace_database_open (const std::filesystem::path& path, bool create,
                                sqlite3*& db, std::string& error) {
  db= nullptr;
  error.clear ();
  if (path.empty ()) { error= "Namespace database path is empty"; return false; }
  if (create && path.has_parent_path ()) {
    std::error_code ec;
    std::filesystem::create_directories (path.parent_path (), ec);
    if (ec) { error= ec.message (); return false; }
  }
  int flags= SQLITE_OPEN_READWRITE | (create ? SQLITE_OPEN_CREATE : 0);
  int rc= sqlite3_open_v2 (path.c_str (), &db, flags, nullptr);
  if (rc == SQLITE_OK) {
    sqlite3_busy_timeout (db, 5000);
    if (exec (db, "PRAGMA foreign_keys=ON;", error) &&
        athena_namespace_schema_ensure (db, error)) return true;
  }
  else error= db ? sqlite3_errmsg (db) : sqlite3_errstr (rc);
  if (db) sqlite3_close (db);
  db= nullptr;
  error= "Cannot open namespace database " + path.string () + ": " + error;
  return false;
}

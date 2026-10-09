/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "hodarium_rename_records.hpp"
#include "hodarium_logical_database.hpp"
#include "vaultfile_json.hpp"
#include "confined_filesystem.hpp"
#include "namespace_ontology.hpp"
#include "vault.hpp"
#include <sqlite3.h>
#include <map>
#include <set>
#include <stdexcept>

namespace athena::hodarium {
namespace {
namespace fs= std::filesystem;
void require (bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error (message);
}
struct statement {
  sqlite3_stmt* p= nullptr;
  explicit statement (sqlite3* db, const char* sql) {
    if (sqlite3_prepare_v2 (db, sql, -1, &p, nullptr) != SQLITE_OK)
      throw std::runtime_error (sqlite3_errmsg (db));
  }
  ~statement () { sqlite3_finalize (p); }
  void text (int index, const std::string& value) {
    require (sqlite3_bind_text (p, index, value.data (), int (value.size ()), SQLITE_TRANSIENT) == SQLITE_OK,
             "Could not bind Hodarium rename record");
  }
  bool row () {
    const int rc= sqlite3_step (p);
    if (rc != SQLITE_ROW && rc != SQLITE_DONE)
      throw std::runtime_error (sqlite3_errmsg (sqlite3_db_handle (p)));
    return rc == SQLITE_ROW;
  }
  std::string text (int index) const {
    const auto* value= reinterpret_cast<const char*> (sqlite3_column_text (p, index));
    return value ? std::string (value, sqlite3_column_bytes (p, index)) : std::string ();
  }
};
struct store {
  sqlite3* db= nullptr;
  explicit store (const fs::path& root) {
    const auto directory= root / ".athena";
    const auto file= directory / "hodarium-rename-records.sqlite";
    require (!fs::is_symlink (fs::symlink_status (directory)) &&
             !fs::is_symlink (fs::symlink_status (file)),
             "Hodarium rename operation store cannot be a symbolic link");
    fs::create_directories (directory);
    try {
      if (sqlite3_open_v2 (file.string ().c_str (), &db,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOFOLLOW, nullptr) != SQLITE_OK)
        throw std::runtime_error (db ? sqlite3_errmsg (db) : "Cannot open Hodarium rename operation store");
      sqlite3_busy_timeout (db, 5000);
      exec ("PRAGMA synchronous=FULL; PRAGMA foreign_keys=ON;");
      { statement version (db, "PRAGMA user_version");
        require (version.row () && sqlite3_column_int (version.p, 0) <= 1,
                 "Unsupported Hodarium rename operation store version"); }
      exec ("BEGIN IMMEDIATE;"
        "CREATE TABLE IF NOT EXISTS operations("
        "operation TEXT PRIMARY KEY,object TEXT NOT NULL,old_path TEXT NOT NULL,"
        "new_path TEXT NOT NULL,namespace_path TEXT NOT NULL,present INTEGER NOT NULL,"
        "committed INTEGER NOT NULL DEFAULT 0);"
        "CREATE TABLE IF NOT EXISTS preimages("
        "operation TEXT NOT NULL REFERENCES operations(operation),"
        "key TEXT NOT NULL,expected TEXT NOT NULL,replacement TEXT NOT NULL,"
        "history INTEGER NOT NULL DEFAULT 0,PRIMARY KEY(operation,key));"
        "PRAGMA user_version=1; COMMIT;");
    }
    catch (...) { if (db) sqlite3_close (db); db= nullptr; throw; }
  }
  ~store () { if (db) sqlite3_close (db); }
  void exec (const char* sql) {
    if (sqlite3_exec (db, sql, nullptr, nullptr, nullptr) != SQLITE_OK)
      throw std::runtime_error (sqlite3_errmsg (db));
  }
};
struct transaction {
  store& db;
  bool committed= false;
  explicit transaction (store& value): db (value) { db.exec ("BEGIN IMMEDIATE"); }
  ~transaction () { if (!committed) sqlite3_exec (db.db, "ROLLBACK", nullptr, nullptr, nullptr); }
  void commit () { db.exec ("COMMIT"); committed= true; }
};
struct entry {
  logical_database_change change;
  std::int64_t history= 0;
};
struct plan {
  std::string namespace_path;
  bool present= false, committed= false;
  std::vector<entry> entries;
};

std::optional<std::string> replacement (const logical_database_object& object,
    const document_path_change& change) {
  if (object.format != "athena-namespace-v1") return {};
  auto next= rename_namespace_object_resources (object, change.old_path, change.new_path, false);
  return next.payload == object.payload ? std::nullopt : std::optional<std::string> (std::move (next.payload));
}
std::string namespace_path (const fs::path& root) {
  AthenaVaultfileInfo info;
  std::string error;
  require (athena_vaultfile_read (root, info, error), error);
  fs::path relative (info.namespace_db_path);
  require (!relative.empty () && !relative.is_absolute () &&
           relative == relative.lexically_normal (), "Invalid namespace database path");
  for (const auto& part: relative)
    require (part != ".." && part != ".", "Namespace database leaves the Vault");
  return relative.generic_string ();
}
bool database_present (const fs::path& root, const std::string& path) {
  filesystem::confined_root directory (root);
  try { (void) directory.open (path); return true; }
  catch (const std::system_error& error) {
    if (error.code () == std::errc::no_such_file_or_directory) return false;
    throw;
  }
}
void validate (const fs::path& root, const document_path_change& change) {
  require (change.root == root && !change.operation.empty () && change.operation.size () <= 128 &&
           change.operation.find ('\0') == std::string::npos && !change.object.empty () &&
           history::valid_relative_document_path (change.old_path) &&
           history::valid_relative_document_path (change.new_path) &&
           change.old_path != change.new_path, "Invalid Hodarium logical rename request");
}
std::optional<plan> load (store& db, const document_path_change& change) {
  statement op (db.db, "SELECT object,old_path,new_path,namespace_path,present,committed "
                      "FROM operations WHERE operation=?");
  op.text (1, change.operation);
  if (!op.row ()) return {};
  require (op.text (0) == change.object && op.text (1) == change.old_path &&
           op.text (2) == change.new_path, "Hodarium rename operation identity was reused");
  plan result;
  result.namespace_path= op.text (3);
  result.present= sqlite3_column_int (op.p, 4) != 0;
  result.committed= sqlite3_column_int (op.p, 5) != 0;
  statement rows (db.db, "SELECT key,expected,replacement,history FROM preimages "
                        "WHERE operation=? ORDER BY key");
  rows.text (1, change.operation);
  while (rows.row ()) result.entries.push_back (
    {{rows.text (0), rows.text (1), rows.text (2)}, sqlite3_column_int64 (rows.p, 3)});
  return result;
}
// A namespace commit is atomic but its local completion marker is another DB.
// Exact replacement values prove an interrupted successful CAS; mixed states
// or any other value are conflicts, never an invitation to overwrite edits.
bool check_current (const fs::path& root, const document_path_change& change,
                    const plan& prepared) {
  require (namespace_path (root) == prepared.namespace_path &&
           database_present (root, prepared.namespace_path) == prepared.present,
           "Hodarium rename namespace database configuration changed");
  if (!prepared.present) return false;
  auto current= export_namespace_objects (root / prepared.namespace_path, root);
  std::map<std::string, std::string> values;
  std::set<std::string> planned;
  for (const auto& item: prepared.entries) planned.insert (item.change.key);
  for (const auto& object: current) {
    require (planned.count (object.key) || !replacement (object, change),
             "Hodarium rename conflicts with a newly referenced namespace path");
    values.emplace (object.key, object.payload);
  }
  bool before= true, after= true;
  for (const auto& item: prepared.entries) {
    auto value= values.find (item.change.key);
    require (value != values.end (), "Hodarium rename conflicts with a removed namespace");
    before= before && value->second == *item.change.expected;
    after= after && value->second == *item.change.replacement;
  }
  require (before || after, "Hodarium rename conflicts with newer namespace edits");
  return after && !prepared.entries.empty ();
}
void prepare (const fs::path& root, const document_path_change& change) {
  validate (root, change);
  store db (root);
  {
    transaction tx (db);
    if (!load (db, change)) {
      const auto path= namespace_path (root);
      const bool present= database_present (root, path);
      statement insert (db.db, "INSERT INTO operations(operation,object,old_path,new_path,namespace_path,present) "
                              "VALUES(?,?,?,?,?,?)");
      insert.text (1, change.operation); insert.text (2, change.object);
      insert.text (3, change.old_path); insert.text (4, change.new_path); insert.text (5, path);
      sqlite3_bind_int (insert.p, 6, present ? 1 : 0); insert.row ();
      if (present) for (const auto& object: export_namespace_objects (root / path, root)) {
        auto next= replacement (object, change);
        if (!next) continue;
        statement row (db.db, "INSERT INTO preimages(operation,key,expected,replacement) VALUES(?,?,?,?)");
        row.text (1, change.operation); row.text (2, object.key);
        row.text (3, object.payload); row.text (4, *next); row.row ();
      }
    }
    tx.commit ();
  }
  transaction tx (db);
  auto prepared= *load (db, change);
  if (prepared.committed) { tx.commit (); return; }
  (void) check_current (root, change, prepared);
  history::document_history_store history;
  std::string error;
  require (history.open (root, error), error);
  for (const auto& item: prepared.entries) {
    auto id= item.history;
    if (id != 0) {
      std::string original;
      require (history.reconstruct (id, original, error), error);
      require (original == *item.change.expected, "Hodarium namespace recovery preimage mismatch");
    }
    else {
      require (history.protect_logical ("logical/" + item.change.key + ".json", *item.change.expected,
        change.operation + ":" + item.change.key, "athena-namespace-v1", item.change.key, id, error), error);
      statement update (db.db, "UPDATE preimages SET history=? WHERE operation=? AND key=?");
      sqlite3_bind_int64 (update.p, 1, id);
      update.text (2, change.operation); update.text (3, item.change.key); update.row ();
    }
    require (history.set_protected_metadata (id, "athena-namespace-v1", item.change.key, error), error);
  }
  tx.commit ();
}
void commit (const fs::path& root, const document_path_change& change) {
  validate (root, change);
  store db (root); transaction tx (db);
  auto prepared= load (db, change);
  require (bool (prepared), "Hodarium logical rename has no prepared preimages");
  if (!prepared->committed) {
    for (const auto& item: prepared->entries)
      require (item.history != 0, "Hodarium namespace rename is not protected by File History");
    const bool already_applied= check_current (root, change, *prepared);
    if (!already_applied && !prepared->entries.empty ()) {
      std::vector<logical_database_change> changes;
      for (const auto& item: prepared->entries) changes.push_back (item.change);
      const auto result= apply_namespace_objects (root / prepared->namespace_path, root, changes);
      require (result == logical_apply_result::applied || result == logical_apply_result::unchanged,
               "Hodarium rename conflicts with newer namespace edits or missing dependencies");
      require (check_current (root, change, *prepared),
               "Hodarium namespace rename changed again before completion");
    }
    statement done (db.db, "UPDATE operations SET committed=1 WHERE operation=?");
    done.text (1, change.operation); done.row ();
  }
  tx.commit ();
  const auto context= vault_capture_context ();
  if (context && context->root == root) athena_namespace_ontology_invalidate (true);
}
}
document_rename_records make_document_rename_records (const fs::path& path) {
  const auto root= filesystem::confined_root (path).path ();
  return {
    [root] (const document_path_change& change) { prepare (root, change); },
    [root] (const document_path_change& change) { commit (root, change); }
  };
}
}

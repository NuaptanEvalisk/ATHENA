/******************************************************************************
* MODULE     : namespaces_db.cpp
* DESCRIPTION: SQLite persistence for ATHENA vault namespaces
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
*******************************************************************************/

#include "namespaces_private.hpp"

#include "namespace_ontology.hpp"
#include "vault.hpp"

#include <sqlite3.h>

#include <QApplication>
#include <QMessageBox>

#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace athena_namespaces {

static url
ns_db () {
  return vault_get_namespace_db ();
}

static std::string
ns_db_path () {
  return tm_to_std (concretize (ns_db ()));
}

static bool
ns_db_exists () {
  if (!vault_active ()) return false;
  std::string path= ns_db_path ();
  return !path.empty () && std::filesystem::exists (path);
}

static void
set_sql_error (sqlite3* db, string context, string& error) {
  error= context * ": " * std_to_tm (sqlite3_errmsg (db));
}

static bool
exec_sql (sqlite3* db, const char* sql, string& error) {
  char* msg= nullptr;
  int status= sqlite3_exec (db, sql, nullptr, nullptr, &msg);
  if (status == SQLITE_OK) return true;
  error= std_to_tm (msg == nullptr ? sqlite3_errmsg (db) : msg);
  sqlite3_free (msg);
  return false;
}

static bool
prepare_sql (sqlite3* db, const char* sql, sqlite3_stmt** st,
             string& error) {
  int status= sqlite3_prepare_v2 (db, sql, -1, st, nullptr);
  if (status == SQLITE_OK) return true;
  set_sql_error (db, "SQLite prepare failed", error);
  return false;
}

static bool
bind_tm_string (sqlite3_stmt* st, int index, string value, string& error) {
  int status= sqlite3_bind_text (st, index, as_charp (value), -1,
                                 SQLITE_TRANSIENT);
  if (status == SQLITE_OK) return true;
  sqlite3* db= sqlite3_db_handle (st);
  set_sql_error (db, "SQLite bind failed", error);
  return false;
}

static string
column_tm_string (sqlite3_stmt* st, int col) {
  const unsigned char* text= sqlite3_column_text (st, col);
  return text == nullptr ? "" : std_to_tm ((const char*) text);
}

static bool
exec_prepared (sqlite3* db, const char* sql, const std::vector<string>& args,
               string& error) {
  sqlite3_stmt* st= nullptr;
  if (!prepare_sql (db, sql, &st, error)) return false;
  for (size_t i=0; i<args.size (); i++) {
    if (!bind_tm_string (st, (int) i + 1, args[i], error)) {
      sqlite3_finalize (st);
      return false;
    }
  }
  int status= sqlite3_step (st);
  if (status != SQLITE_DONE) {
    set_sql_error (db, "SQLite statement failed", error);
    sqlite3_finalize (st);
    return false;
  }
  sqlite3_finalize (st);
  return true;
}

class ns_sqlite_connection {
public:
  sqlite3* db= nullptr;

  ~ns_sqlite_connection () {
    if (db != nullptr) sqlite3_close (db);
  }

  bool open (bool create, string& error) {
    if (!vault_active ()) {
      error= "No active vault.";
      return false;
    }

    std::string path= ns_db_path ();
    if (path.empty ()) {
      error= "Namespace database path is empty.";
      return false;
    }
    if (!create && !std::filesystem::exists (path)) return false;

    if (create) {
      std::filesystem::path fp (path);
      if (fp.has_parent_path ()) {
        std::error_code ec;
        std::filesystem::create_directories (fp.parent_path (), ec);
        if (ec) {
          error= "Cannot create namespace database directory: " *
                 std_to_tm (ec.message ());
          return false;
        }
      }
    }

    int flags= SQLITE_OPEN_READWRITE;
    if (create) flags |= SQLITE_OPEN_CREATE;
    int status= sqlite3_open_v2 (path.c_str (), &db, flags, nullptr);
    if (status != SQLITE_OK) {
      error= "Cannot open namespace database: " *
             std_to_tm (db == nullptr ? path : sqlite3_errmsg (db));
      if (db != nullptr) {
        sqlite3_close (db);
        db= nullptr;
      }
      return false;
    }
    sqlite3_busy_timeout (db, 5000);
    if (!exec_sql (db, "PRAGMA foreign_keys=ON;", error)) return false;
    if (!ensure_schema (error)) return false;
    return true;
  }

private:
  bool ensure_schema (string& error) {
    static const char* schema=
      "CREATE TABLE IF NOT EXISTS meta ("
      "  key TEXT PRIMARY KEY,"
      "  value TEXT NOT NULL"
      ");"
      "CREATE TABLE IF NOT EXISTS namespaces ("
      "  name TEXT PRIMARY KEY,"
      "  kind TEXT NOT NULL CHECK(kind IN"
      "    ('abstract','semi-concrete','concrete')),"
      "  template TEXT NOT NULL DEFAULT '',"
      "  sorter_trivial INTEGER NOT NULL DEFAULT 0,"
      "  sorter_path TEXT NOT NULL DEFAULT '',"
      "  style_path TEXT NOT NULL DEFAULT '',"
      "  initial_content_path TEXT NOT NULL DEFAULT '',"
      "  homepage_path TEXT NOT NULL DEFAULT ''"
      ");"
      "CREATE TABLE IF NOT EXISTS namespace_parents ("
      "  child TEXT NOT NULL,"
      "  parent TEXT NOT NULL,"
      "  source TEXT NOT NULL CHECK(source IN ('declared','derived')),"
      "  ord INTEGER NOT NULL DEFAULT 0,"
      "  PRIMARY KEY(child, parent, source)"
      ");"
      "CREATE INDEX IF NOT EXISTS namespace_parents_child_idx "
      "  ON namespace_parents(child, source, ord);"
      "CREATE INDEX IF NOT EXISTS namespace_parents_parent_idx "
      "  ON namespace_parents(parent);"
      "CREATE TABLE IF NOT EXISTS relation_decisions ("
      "  parent TEXT NOT NULL,"
      "  child TEXT NOT NULL,"
      "  decision TEXT NOT NULL CHECK(decision IN ('allow','deny')),"
      "  source TEXT NOT NULL DEFAULT 'user',"
      "  PRIMARY KEY(parent, child)"
      ");"
      "INSERT INTO meta(key, value) VALUES('schema-version', '1') "
      "  ON CONFLICT(key) DO NOTHING;";
    if (!exec_sql (db, schema, error)) return false;
    return ensure_column ("namespaces", "sorter_trivial",
                          "INTEGER NOT NULL DEFAULT 0", error) &&
           ensure_column ("namespaces", "initial_content_path",
                          "TEXT NOT NULL DEFAULT ''", error) &&
           ensure_column ("namespaces", "homepage_path",
                          "TEXT NOT NULL DEFAULT ''", error);
  }

  bool ensure_column (const char* table, const char* column,
                      const char* definition, string& error) {
    std::string pragma= std::string ("PRAGMA table_info(") + table + ");";
    sqlite3_stmt* st= nullptr;
    if (!prepare_sql (db, pragma.c_str (), &st, error)) return false;
    bool found= false;
    while (true) {
      int status= sqlite3_step (st);
      if (status == SQLITE_ROW) {
        const unsigned char* name= sqlite3_column_text (st, 1);
        if (name != nullptr && std::strcmp ((const char*) name, column) == 0)
          found= true;
      }
      else if (status == SQLITE_DONE) break;
      else {
        set_sql_error (db, "SQLite schema query failed", error);
        sqlite3_finalize (st);
        return false;
      }
    }
    sqlite3_finalize (st);
    if (found) return true;
    std::string sql= std::string ("ALTER TABLE ") + table +
                     " ADD COLUMN " + column + " " + definition + ";";
    return exec_sql (db, sql.c_str (), error);
  }
};

static bool
query_parent_list (sqlite3* db, string child, string source,
                   std::vector<string>& out, string& error) {
  sqlite3_stmt* st= nullptr;
  if (!prepare_sql (db,
        "SELECT parent FROM namespace_parents "
        "WHERE child=? AND source=? ORDER BY ord, parent;",
        &st, error)) return false;
  if (!bind_tm_string (st, 1, child, error) ||
      !bind_tm_string (st, 2, source, error)) {
    sqlite3_finalize (st);
    return false;
  }
  while (true) {
    int status= sqlite3_step (st);
    if (status == SQLITE_ROW) out.push_back (column_tm_string (st, 0));
    else if (status == SQLITE_DONE) break;
    else {
      set_sql_error (db, "SQLite parent query failed", error);
      sqlite3_finalize (st);
      return false;
    }
  }
  sqlite3_finalize (st);
  return true;
}

static bool
get_namespace_from_db (sqlite3* db, string name,
                       athena_namespace_definition& out, string& error) {
  sqlite3_stmt* st= nullptr;
  if (!prepare_sql (db,
        "SELECT name, kind, template, sorter_trivial, sorter_path, style_path, "
        "initial_content_path, homepage_path "
        "FROM namespaces WHERE name=?;",
        &st, error)) return false;
  if (!bind_tm_string (st, 1, name, error)) {
    sqlite3_finalize (st);
    return false;
  }
  int status= sqlite3_step (st);
  if (status == SQLITE_DONE) {
    sqlite3_finalize (st);
    return false;
  }
  if (status != SQLITE_ROW) {
    set_sql_error (db, "SQLite namespace query failed", error);
    sqlite3_finalize (st);
    return false;
  }
  out.name= column_tm_string (st, 0);
  out.kind= canonical_kind (column_tm_string (st, 1));
  out.templ= column_tm_string (st, 2);
  out.sorter_trivial= sqlite3_column_int (st, 3) != 0;
  out.sorter_path= column_tm_string (st, 4);
  out.style_path= column_tm_string (st, 5);
  out.initial_content_path= column_tm_string (st, 6);
  out.homepage_path= column_tm_string (st, 7);
  sqlite3_finalize (st);
  out.parents.clear ();
  out.derived_parents.clear ();
  if (!query_parent_list (db, out.name, "declared", out.parents, error))
    return false;
  if (!query_parent_list (db, out.name, "derived", out.derived_parents, error))
    return false;
  return true;
}

static bool
upsert_relation_decision (sqlite3* db, string parent, string child,
                          string decision, string source, string& error) {
  return exec_prepared (
    db,
    "INSERT INTO relation_decisions(parent, child, decision, source) "
    "VALUES(?, ?, ?, ?) "
    "ON CONFLICT(parent, child) DO UPDATE SET "
    "  decision=excluded.decision, source=excluded.source;",
    { parent, child, decision, source == "" ? "user" : source },
    error);
}

static bool
namespace_has_template (const athena_namespace_definition& ns) {
  return ns.kind != "abstract" && ns.templ != "";
}

static bool
namespace_row_list (sqlite3* db, std::vector<athena_namespace_definition>& out,
                    string& error) {
  sqlite3_stmt* st= nullptr;
  if (!prepare_sql (db,
        "SELECT name, kind, template, sorter_trivial, sorter_path, style_path, "
        "initial_content_path, homepage_path "
        "FROM namespaces ORDER BY name;",
        &st, error)) return false;

  while (true) {
    int status= sqlite3_step (st);
    if (status == SQLITE_DONE) break;
    if (status != SQLITE_ROW) {
      set_sql_error (db, "SQLite namespace list query failed", error);
      sqlite3_finalize (st);
      return false;
    }
    athena_namespace_definition ns;
    ns.name= column_tm_string (st, 0);
    ns.kind= canonical_kind (column_tm_string (st, 1));
    ns.templ= column_tm_string (st, 2);
    ns.sorter_trivial= sqlite3_column_int (st, 3) != 0;
    ns.sorter_path= column_tm_string (st, 4);
    ns.style_path= column_tm_string (st, 5);
    ns.initial_content_path= column_tm_string (st, 6);
    ns.homepage_path= column_tm_string (st, 7);
    ns.parents.clear ();
    ns.derived_parents.clear ();
    out.push_back (ns);
  }
  sqlite3_finalize (st);
  return true;
}

static bool
recompute_derived_parents (sqlite3* db, string& error) {
  std::vector<athena_namespace_definition> namespaces;
  if (!namespace_row_list (db, namespaces, error)) return false;

  if (!exec_sql (db, "DELETE FROM namespace_parents WHERE source='derived';",
                 error))
    return false;
  if (!exec_sql (db, "DELETE FROM relation_decisions WHERE source='derived';",
                 error))
    return false;

  for (const athena_namespace_definition& child: namespaces) {
    if (!namespace_has_template (child)) continue;
    int ord= 0;
    for (const athena_namespace_definition& parent: namespaces) {
      if (child.name == parent.name || !namespace_has_template (parent))
        continue;
      bool derives= false;
      if (!template_derives_from (child.templ, parent.templ, derives, error))
        return false;
      if (!derives) continue;
      if (!exec_prepared (db,
            "INSERT OR REPLACE INTO namespace_parents"
            "(child, parent, source, ord) VALUES(?, ?, 'derived', ?);",
            { child.name, parent.name, std_to_tm (std::to_string (ord++)) },
            error))
        return false;
      if (!upsert_relation_decision (db, parent.name, child.name, "allow",
                                     "derived", error))
        return false;
    }
  }
  return true;
}

static void
fingerprint_bytes (uint64_t& value, const void* data, size_t length) {
  const unsigned char* bytes= static_cast<const unsigned char*> (data);
  for (size_t i=0; i<length; ++i) {
    value ^= (uint64_t) bytes[i];
    value *= UINT64_C (1099511628211);
  }
  value ^= UINT64_C (255);
  value *= UINT64_C (1099511628211);
}

static bool
fingerprint_query (sqlite3* db, const char* sql, uint64_t& value,
                   string& error) {
  sqlite3_stmt* st= nullptr;
  if (!prepare_sql (db, sql, &st, error)) return false;
  while (true) {
    int status= sqlite3_step (st);
    if (status == SQLITE_DONE) break;
    if (status != SQLITE_ROW) {
      set_sql_error (db, "SQLite fingerprint query failed", error);
      sqlite3_finalize (st);
      return false;
    }
    int columns= sqlite3_column_count (st);
    for (int column=0; column<columns; ++column) {
      const void* text= sqlite3_column_text (st, column);
      int length= sqlite3_column_bytes (st, column);
      if (text != nullptr && length > 0)
        fingerprint_bytes (value, text, (size_t) length);
      else
        fingerprint_bytes (value, "", 0);
    }
  }
  sqlite3_finalize (st);
  return true;
}

static bool
derived_source_fingerprint (sqlite3* db, std::string& out, string& error) {
  uint64_t value= UINT64_C (1469598103934665603);
  if (!fingerprint_query (
        db,
        "SELECT name, kind, template FROM namespaces ORDER BY name;",
        value, error))
    return false;
  std::ostringstream stream;
  stream << std::hex << std::setw (16) << std::setfill ('0') << value;
  out= stream.str ();
  return true;
}

static bool
meta_value (sqlite3* db, const char* key, std::string& out, string& error) {
  sqlite3_stmt* st= nullptr;
  if (!prepare_sql (db, "SELECT value FROM meta WHERE key=?;", &st, error))
    return false;
  if (sqlite3_bind_text (st, 1, key, -1, SQLITE_STATIC) != SQLITE_OK) {
    set_sql_error (db, "SQLite meta bind failed", error);
    sqlite3_finalize (st);
    return false;
  }
  int status= sqlite3_step (st);
  if (status == SQLITE_ROW) {
    const unsigned char* value= sqlite3_column_text (st, 0);
    out= value == nullptr ? "" : (const char*) value;
  }
  else if (status != SQLITE_DONE) {
    set_sql_error (db, "SQLite meta query failed", error);
    sqlite3_finalize (st);
    return false;
  }
  sqlite3_finalize (st);
  return true;
}

static bool
set_meta_value (sqlite3* db, const char* key, const std::string& value,
                string& error) {
  return exec_prepared (
    db,
    "INSERT INTO meta(key, value) VALUES(?, ?) "
    "ON CONFLICT(key) DO UPDATE SET value=excluded.value;",
    { std_to_tm (key), std_to_tm (value) }, error);
}

bool
refresh_derived_parents_if_needed (bool force, bool& changed,
                                   string& error) {
  changed= false;
  if (!vault_active ()) {
    error= "No active vault.";
    return false;
  }
  if (!ns_db_exists ()) return true;

  ns_sqlite_connection cx;
  if (!cx.open (true, error)) return false;
  if (!exec_sql (cx.db, "BEGIN IMMEDIATE;", error)) return false;

  std::string fingerprint;
  std::string previous;
  bool ok= derived_source_fingerprint (cx.db, fingerprint, error) &&
           meta_value (cx.db, "derived-source-fingerprint", previous, error);
  if (ok && (force || fingerprint != previous)) {
    ok= recompute_derived_parents (cx.db, error) &&
        set_meta_value (cx.db, "derived-source-fingerprint", fingerprint,
                        error);
    changed= ok;
  }

  if (ok) {
    if (!exec_sql (cx.db, "COMMIT;", error)) {
      string ignored;
      exec_sql (cx.db, "ROLLBACK;", ignored);
      ok= false;
    }
  }
  else {
    string ignored;
    exec_sql (cx.db, "ROLLBACK;", ignored);
  }
  return ok;
}

bool
load_namespace_snapshot_from_db (
  std::vector<athena_namespace_definition>& namespaces,
  std::vector<athena_namespace_relation>& relations, string& error) {
  namespaces.clear ();
  relations.clear ();
  if (!ns_db_exists ()) return true;

  ns_sqlite_connection cx;
  if (!cx.open (false, error)) return false;
  if (!namespace_row_list (cx.db, namespaces, error)) return false;

  std::unordered_map<std::string,size_t> indices;
  for (size_t i=0; i<namespaces.size (); ++i)
    indices[tm_to_std (namespaces[i].name)]= i;

  sqlite3_stmt* st= nullptr;
  if (!prepare_sql (
        cx.db,
        "SELECT child, parent, source FROM namespace_parents "
        "ORDER BY child, source, ord, parent;",
        &st, error))
    return false;
  while (true) {
    int status= sqlite3_step (st);
    if (status == SQLITE_DONE) break;
    if (status != SQLITE_ROW) {
      set_sql_error (cx.db, "SQLite parent snapshot failed", error);
      sqlite3_finalize (st);
      return false;
    }
    string child= column_tm_string (st, 0);
    auto found= indices.find (tm_to_std (child));
    if (found == indices.end ()) continue;
    string parent= column_tm_string (st, 1);
    string source= column_tm_string (st, 2);
    if (source == "declared") namespaces[found->second].parents.push_back (parent);
    else if (source == "derived")
      namespaces[found->second].derived_parents.push_back (parent);
  }
  sqlite3_finalize (st);

  if (!prepare_sql (
        cx.db,
        "SELECT parent, child, decision, source FROM relation_decisions "
        "ORDER BY parent, child;",
        &st, error))
    return false;
  while (true) {
    int status= sqlite3_step (st);
    if (status == SQLITE_DONE) break;
    if (status != SQLITE_ROW) {
      set_sql_error (cx.db, "SQLite relation snapshot failed", error);
      sqlite3_finalize (st);
      return false;
    }
    athena_namespace_relation relation;
    relation.parent= column_tm_string (st, 0);
    relation.child= column_tm_string (st, 1);
    relation.decision= column_tm_string (st, 2);
    relation.source= column_tm_string (st, 3);
    if (relation.parent != "" && relation.child != "")
      relations.push_back (relation);
  }
  sqlite3_finalize (st);
  return true;
}



} // namespace athena_namespaces

using namespace athena_namespaces;

bool
athena_namespace_refresh_derived (string& error) {
  bool changed= false;
  bool ok= refresh_derived_parents_if_needed (true, changed, error);
  if (ok) athena_namespace_ontology_invalidate (false);
  return ok;
}

namespace_records<athena_namespace_definition>
athena_namespaces_list () {
  namespace_records<athena_namespace_definition> cached;
  if (athena_namespace_ontology_namespaces (cached)) return cached;
  std::vector<athena_namespace_definition> out;
  std::vector<athena_namespace_relation> ignored;
  string error;
  load_namespace_snapshot_from_db (out, ignored, error);
  return namespace_records<athena_namespace_definition> (std::move (out));
}

bool
athena_namespace_get (
  string name, std::shared_ptr<const athena_namespace_definition>& out) {
  if (athena_namespace_ontology_namespace (name, out)) return true;
  if (!ns_db_exists () || name == "") return false;
  string error;
  ns_sqlite_connection cx;
  if (!cx.open (false, error)) return false;
  athena_namespace_definition value;
  if (!get_namespace_from_db (cx.db, name, value, error)) return false;
  out= std::make_shared<const athena_namespace_definition> (std::move (value));
  return true;
}

bool
athena_namespace_save (const athena_namespace_definition& ns, string& error) {
  if (!vault_active ()) {
    error= "No active vault.";
    return false;
  }
  if (ns.name == "") {
    error= "Namespace name cannot be empty.";
    return false;
  }
  if (tm_to_std (ns.name).find ('!') != std::string::npos) {
    error= "Namespace name cannot contain '!'.";
    return false;
  }
  string kind= canonical_kind (ns.kind);
  if ((kind == "semi-concrete" || kind == "concrete") && ns.templ == "") {
    error= "Semi-concrete and concrete namespaces need a filename template.";
    return false;
  }
  std::vector<template_token> toks;
  if (ns.templ != "" && !parse_template (ns.templ, toks, error))
    return false;

  ns_sqlite_connection cx;
  if (!cx.open (true, error)) return false;
  if (!exec_sql (cx.db, "BEGIN IMMEDIATE;", error)) return false;

  bool ok=
    exec_prepared (
      cx.db,
      "INSERT INTO namespaces"
      "(name, kind, template, sorter_trivial, sorter_path, style_path, "
      "initial_content_path, homepage_path) "
      "VALUES(?, ?, ?, ?, ?, ?, ?, ?) "
      "ON CONFLICT(name) DO UPDATE SET "
      "  kind=excluded.kind,"
      "  template=excluded.template,"
      "  sorter_trivial=excluded.sorter_trivial,"
      "  sorter_path=excluded.sorter_path,"
      "  style_path=excluded.style_path,"
      "  initial_content_path=excluded.initial_content_path,"
      "  homepage_path=excluded.homepage_path;",
      { ns.name, kind, ns.templ, ns.sorter_trivial ? "1" : "0",
        ns.sorter_path, ns.style_path, ns.initial_content_path,
        ns.homepage_path },
      error) &&
    exec_prepared (cx.db,
      "DELETE FROM namespace_parents WHERE child=? AND source='declared';",
      { ns.name }, error);

  for (int i=0; ok && i<(int) ns.parents.size (); i++)
    ok= exec_prepared (cx.db,
      "INSERT OR REPLACE INTO namespace_parents"
      "(child, parent, source, ord) VALUES(?, ?, 'declared', ?);",
      { ns.name, ns.parents[i], std_to_tm (std::to_string (i)) }, error);

  if (ok) {
    if (!exec_sql (cx.db, "COMMIT;", error)) {
      string ignored;
      exec_sql (cx.db, "ROLLBACK;", ignored);
      ok= false;
    }
  }
  else {
    string ignored;
    exec_sql (cx.db, "ROLLBACK;", ignored);
  }
  if (ok) athena_namespace_ontology_invalidate (false);
  return ok;
}

bool
athena_namespace_remove (string name, string& error) {
  if (!vault_active ()) {
    error= "No active vault.";
    return false;
  }
  if (!ns_db_exists ()) return true;

  ns_sqlite_connection cx;
  if (!cx.open (false, error)) return false;
  if (!exec_sql (cx.db, "BEGIN IMMEDIATE;", error)) return false;
  bool ok=
    exec_prepared (cx.db, "DELETE FROM namespaces WHERE name=?;",
                   { name }, error) &&
    exec_prepared (cx.db,
      "DELETE FROM namespace_parents WHERE child=? OR parent=?;",
      { name, name }, error) &&
    exec_prepared (cx.db,
      "DELETE FROM relation_decisions WHERE child=? OR parent=?;",
      { name, name }, error);

  if (ok) {
    if (!exec_sql (cx.db, "COMMIT;", error)) {
      string ignored;
      exec_sql (cx.db, "ROLLBACK;", ignored);
      ok= false;
    }
  }
  else {
    string ignored;
    exec_sql (cx.db, "ROLLBACK;", ignored);
  }
  if (ok) athena_namespace_ontology_invalidate (false);
  return ok;
}

namespace_records<athena_namespace_relation>
athena_namespace_relations_list () {
  namespace_records<athena_namespace_relation> cached;
  if (athena_namespace_ontology_relations (cached)) return cached;
  std::vector<athena_namespace_relation> out;
  if (!ns_db_exists ()) return {};

  string error;
  ns_sqlite_connection cx;
  if (!cx.open (false, error)) return {};

  sqlite3_stmt* st= nullptr;
  if (!prepare_sql (cx.db,
        "SELECT parent, child, decision, source "
        "FROM relation_decisions ORDER BY parent, child;",
        &st, error)) return {};
  while (true) {
    int status= sqlite3_step (st);
    if (status == SQLITE_DONE) break;
    if (status != SQLITE_ROW) {
      sqlite3_finalize (st);
      return namespace_records<athena_namespace_relation> (std::move (out));
    }
    athena_namespace_relation r;
    r.parent= column_tm_string (st, 0);
    r.child= column_tm_string (st, 1);
    r.decision= column_tm_string (st, 2);
    r.source= column_tm_string (st, 3);
    if (r.parent != "" && r.child != "") out.push_back (r);
  }
  sqlite3_finalize (st);
  return namespace_records<athena_namespace_relation> (std::move (out));
}

bool
athena_namespace_relation_set (string parent, string child, string decision,
                               string source, string& error) {
  if (!vault_active ()) {
    error= "No active vault.";
    return false;
  }
  if (parent == "" || child == "") {
    error= "Relation parent and child cannot be empty.";
    return false;
  }
  if (decision != "allow" && decision != "deny") {
    error= "Relation decision must be allow or deny.";
    return false;
  }

  ns_sqlite_connection cx;
  if (!cx.open (true, error)) return false;
  bool ok= upsert_relation_decision (cx.db, parent, child, decision, source,
                                     error);
  if (ok) athena_namespace_ontology_invalidate (false);
  return ok;
}

bool
athena_namespace_relation_remove (string parent, string child, string& error) {
  if (!vault_active ()) {
    error= "No active vault.";
    return false;
  }
  if (!ns_db_exists ()) return true;

  ns_sqlite_connection cx;
  if (!cx.open (false, error)) return false;
  bool ok= exec_prepared (cx.db,
    "DELETE FROM relation_decisions WHERE parent=? AND child=?;",
    { parent, child }, error);
  if (ok) athena_namespace_ontology_invalidate (false);
  return ok;
}

bool
athena_namespace_validate_relation (string parent, string child, bool ask_user,
                                    string& error) {
  if (parent == child) return true;
  if (!vault_active ()) {
    error= "No active vault.";
    return false;
  }

  std::shared_ptr<const athena_namespace_definition> child_ns;
  if (athena_namespace_get (child, child_ns)) {
    if (has_string (child_ns->parents, parent) ||
        has_string (child_ns->derived_parents, parent)) {
      string ignored;
      athena_namespace_relation_set (parent, child, "allow", "derived",
                                     ignored);
      return true;
    }
  }

  if (ns_db_exists ()) {
    ns_sqlite_connection cx;
    string err;
    if (cx.open (false, err)) {
      sqlite3_stmt* st= nullptr;
      if (prepare_sql (cx.db,
            "SELECT decision FROM relation_decisions "
            "WHERE parent=? AND child=?;",
            &st, err)) {
        if (bind_tm_string (st, 1, parent, err) &&
            bind_tm_string (st, 2, child, err)) {
          int status= sqlite3_step (st);
          if (status == SQLITE_ROW) {
            string decision= column_tm_string (st, 0);
            sqlite3_finalize (st);
            if (decision == "allow") return true;
            if (decision == "deny") {
              error= "Namespace relation denied by cached decision.";
              return false;
            }
          }
        }
        sqlite3_finalize (st);
      }
    }
  }
  if (!ask_user) {
    error= "Namespace relation needs user confirmation.";
    return false;
  }

  QMessageBox::StandardButton r= QMessageBox::question (
    QApplication::activeWindow (), "Namespace Relation",
    QString ("Treat namespace \"%1\" as a subspace of \"%2\"?")
      .arg (QString::fromUtf8 (as_charp (child)))
      .arg (QString::fromUtf8 (as_charp (parent))),
    QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
  string ignored;
  if (r == QMessageBox::Yes) {
    athena_namespace_relation_set (parent, child, "allow", "user", ignored);
    return true;
  }
  athena_namespace_relation_set (parent, child, "deny", "user", ignored);
  error= "Namespace relation denied.";
  return false;
}

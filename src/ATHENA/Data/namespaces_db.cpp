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
#include "namespaces_schema.hpp"
#include "vault.hpp"

#include <sqlite3.h>

#include <QApplication>
#include <QMessageBox>

#include <iomanip>
#include <sstream>
#include <set>
#include <unordered_map>

namespace athena_namespaces {

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
  string value= text == nullptr ? "" : std_to_tm ((const char*) text);
  value.ensure_transferable ();
  return value;
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
  const vault_context_handle context;

  explicit ns_sqlite_connection (
    vault_context_handle captured= vault_capture_context ()):
    context (std::move (captured)) {}

  ~ns_sqlite_connection () {
    if (db != nullptr) sqlite3_close (db);
  }

  bool open (bool create, string& error) {
    error= "";
    if (!vault_context_is_current (context)) {
      error= context ? "Vault context is stale." : "No active vault.";
      return false;
    }
    std::string native_error;
    if (!athena_namespace_database_open (
          context->namespace_db, create, db, native_error)) {
      error= std_to_tm (native_error);
      return false;
    }
    // Opening may wait for a database lock. Validate again after acquiring the
    // connection; admitted work now pins that database, not just its pathname.
    if (!vault_context_is_current (context)) {
      error= "Vault context is stale.";
      sqlite3_close (db);
      db= nullptr;
      return false;
    }
    return true;
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
get_namespace_from_db (sqlite3* db, string key,
                       athena_namespace_definition& out, string& error,
                       bool by_uuid= false) {
  sqlite3_stmt* st= nullptr;
  if (!prepare_sql (db,
        by_uuid ?
        "SELECT name, kind, template, sorter_trivial, sorter_path, style_path, "
        "initial_content_path, homepage_path, uuid FROM namespaces WHERE uuid=?;" :
        "SELECT name, kind, template, sorter_trivial, sorter_path, style_path, "
        "initial_content_path, homepage_path, uuid FROM namespaces WHERE name=?;",
        &st, error)) return false;
  if (!bind_tm_string (st, 1, key, error)) {
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
  out.uuid= column_tm_string (st, 8);
  sqlite3_finalize (st);
  std::string materials_error;
  if (!athena_namespace_read_materials (db, tm_to_std (out.uuid), out.materials, materials_error)) {
    error= std_to_tm (materials_error);
    return false;
  }
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
        "initial_content_path, homepage_path, uuid "
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
    ns.uuid= column_tm_string (st, 8);
    std::string materials_error;
    if (!athena_namespace_read_materials (db, tm_to_std (ns.uuid), ns.materials, materials_error)) {
      error= std_to_tm (materials_error);
      sqlite3_finalize (st);
      return false;
    }
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
  ns_sqlite_connection cx;
  if (!cx.open (false, error)) return false;
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

static bool
read_namespace_snapshot (ns_sqlite_connection& cx,
  std::vector<athena_namespace_definition>& namespaces,
  std::vector<athena_namespace_relation>& relations, string& error) {
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



static namespace_query_status
open_query (ns_sqlite_connection& cx, string& error) {
  error= "";
  if (!vault_context_is_current (cx.context)) {
    error= "Vault context is stale or closed.";
    return namespace_query_status::stale;
  }
  if (!cx.open (false, error))
    return vault_context_is_current (cx.context) ?
      namespace_query_status::error : namespace_query_status::stale;
  return namespace_query_status::ok;
}

static namespace_query_status
load_namespace_snapshot (const vault_context_handle& context,
                         std::vector<athena_namespace_definition>& namespaces,
                         std::vector<athena_namespace_relation>& relations,
                         string& error) {
  namespaces.clear ();
  relations.clear ();
  ns_sqlite_connection cx (context);
  auto status= open_query (cx, error);
  if (status != namespace_query_status::ok) return status;
  std::vector<athena_namespace_definition> definitions;
  std::vector<athena_namespace_relation> decisions;
  if (!exec_sql (cx.db, "BEGIN;", error) ||
      !read_namespace_snapshot (cx, definitions, decisions, error) ||
      !exec_sql (cx.db, "COMMIT;", error))
    return namespace_query_status::error;
  namespaces= std::move (definitions);
  relations= std::move (decisions);
  return namespace_query_status::ok;
}

bool
load_namespace_snapshot_from_db (
  std::vector<athena_namespace_definition>& namespaces,
  std::vector<athena_namespace_relation>& relations, string& error) {
  return load_namespace_snapshot (vault_capture_context (), namespaces,
                                   relations, error) == namespace_query_status::ok;
}

static namespace_query_status
query_namespace (const vault_context_handle& context, string key, bool by_uuid,
                 std::shared_ptr<const athena_namespace_definition>& out,
                 string& error) {
  out.reset ();
  ns_sqlite_connection cx (context);
  auto status= open_query (cx, error);
  if (status != namespace_query_status::ok) return status;
  if (!exec_sql (cx.db, "BEGIN;", error)) return namespace_query_status::error;
  athena_namespace_definition value;
  bool found= get_namespace_from_db (cx.db, key, value, error, by_uuid);
  if (error != "" || !exec_sql (cx.db, "COMMIT;", error))
    return namespace_query_status::error;
  if (!found) return namespace_query_status::not_found;
  out= std::make_shared<const athena_namespace_definition> (std::move (value));
  return namespace_query_status::ok;
}

} // namespace athena_namespaces

using namespace athena_namespaces;

namespace_query_status
athena_namespace_get (const vault_context_handle& context, string name,
                      std::shared_ptr<const athena_namespace_definition>& out,
                      string& error) {
  return query_namespace (context, name, false, out, error);
}

namespace_query_status
athena_namespace_get_by_uuid (const vault_context_handle& context, string uuid,
                              std::shared_ptr<const athena_namespace_definition>& out,
                              string& error) {
  return query_namespace (context, uuid, true, out, error);
}

namespace_query_status
athena_namespaces_list (const vault_context_handle& context,
                        namespace_records<athena_namespace_definition>& out,
                        string& error) {
  out= {};
  std::vector<athena_namespace_definition> definitions;
  std::vector<athena_namespace_relation> relations;
  auto status= load_namespace_snapshot (context, definitions, relations, error);
  if (status == namespace_query_status::ok)
    out= namespace_records<athena_namespace_definition> (std::move (definitions));
  return status;
}

namespace_query_status
athena_namespace_relations_list (const vault_context_handle& context,
                                 namespace_records<athena_namespace_relation>& out,
                                 string& error) {
  out= {};
  std::vector<athena_namespace_definition> definitions;
  std::vector<athena_namespace_relation> relations;
  auto status= load_namespace_snapshot (context, definitions, relations, error);
  if (status == namespace_query_status::ok)
    out= namespace_records<athena_namespace_relation> (std::move (relations));
  return status;
}

bool
athena_namespace_refresh_derived (string& error) {
  bool changed= false;
  bool ok= refresh_derived_parents_if_needed (true, changed, error);
  if (ok) athena_namespace_ontology_invalidate (false);
  return ok;
}

namespace_records<athena_namespace_definition>
athena_namespaces_list () {
  auto context= vault_capture_context ();
  if (!context) return {};
  namespace_records<athena_namespace_definition> cached;
  if (athena_namespace_ontology_namespaces (cached) &&
      vault_context_is_current (context)) return cached;
  string error;
  athena_namespaces_list (context, cached, error);
  if (error != "") std_warning << "Cannot list namespaces: " << error << LF;
  return cached;
}

bool
athena_namespace_get (
  string name, std::shared_ptr<const athena_namespace_definition>& out) {
  out.reset ();
  auto context= vault_capture_context ();
  if (!context) return false;
  if (athena_namespace_ontology_namespace (name, out) &&
      vault_context_is_current (context)) return true;
  string error;
  auto status= athena_namespace_get (context, name, out, error);
  if (error != "") std_warning << "Cannot read namespace: " << error << LF;
  return status == namespace_query_status::ok;
}

bool
athena_namespace_save (const athena_namespace_definition& ns, string& error) {
  return athena_namespace_save (vault_capture_context (), ns, error);
}

static bool
save_namespace (const vault_context_handle& context,
                const athena_namespace_definition& ns, string& error,
                bool create_only) {
  error= "";
  if (ns.name == "") {
    error= "Namespace name cannot be empty.";
    return false;
  }
  if (tm_to_std (ns.name).find ('!') != std::string::npos) {
    error= "Namespace name cannot contain '!'.";
    return false;
  }
  string kind= canonical_kind (ns.kind);
  if (kind == "abstract" && !ns.materials.empty ()) {
    error= "Abstract namespaces cannot specify Materials.";
    return false;
  }
  std::set<std::string> material_ids;
  for (const auto& material: ns.materials)
    if (material.empty () || material.find ('\0') != std::string::npos ||
        !material_ids.insert (material).second) {
      error= "Namespace Materials must contain unique nonempty UUIDs.";
      return false;
    }
  if ((kind == "semi-concrete" || kind == "concrete") && ns.templ == "") {
    error= "Semi-concrete and concrete namespaces need a filename template.";
    return false;
  }
  std::vector<template_token> toks;
  if (ns.templ != "" && !parse_template (ns.templ, toks, error))
    return false;

  ns_sqlite_connection cx (context);
  if (!cx.open (false, error)) return false;
  if (!exec_sql (cx.db, "BEGIN IMMEDIATE;", error)) return false;

  athena_namespace_definition previous;
  bool found= get_namespace_from_db (cx.db, ns.uuid == "" ? ns.name : ns.uuid,
                                     previous, error, ns.uuid != "");
  bool ok= error == "";
  if (ok && create_only && found) {
    error= "Namespace already exists.";
    return false;
  }
  if (ok && ns.uuid != "" && !found) {
    error= "Namespace no longer exists (stale UUID).";
    ok= false;
  }
  string uuid= found ? previous.uuid : std_to_tm (athena_namespace_new_uuid ());
  if (ok && found && previous.name != ns.name) {
    // Rename the identity and every graph reference in the same transaction.
    ok= exec_prepared (cx.db, "UPDATE namespaces SET name=? WHERE uuid=?;",
                       {ns.name, uuid}, error) &&
        exec_prepared (cx.db, "UPDATE namespace_parents SET child=? WHERE child=?;",
                       {ns.name, previous.name}, error) &&
        exec_prepared (cx.db, "UPDATE namespace_parents SET parent=? WHERE parent=?;",
                       {ns.name, previous.name}, error) &&
        exec_prepared (cx.db, "UPDATE relation_decisions SET child=? WHERE child=?;",
                       {ns.name, previous.name}, error) &&
        exec_prepared (cx.db, "UPDATE relation_decisions SET parent=? WHERE parent=?;",
                       {ns.name, previous.name}, error);
  }
  ok= ok &&
    exec_prepared (
      cx.db,
      "INSERT INTO namespaces"
      "(name, kind, template, sorter_trivial, sorter_path, style_path, "
      "initial_content_path, homepage_path, uuid) "
      "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?) "
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
        ns.homepage_path, uuid },
      error) &&
    exec_prepared (cx.db,
      "DELETE FROM namespace_parents WHERE child=? AND source='declared';",
      { ns.name }, error);

  for (int i=0; ok && i<(int) ns.parents.size (); i++)
    ok= exec_prepared (cx.db,
      "INSERT OR REPLACE INTO namespace_parents"
      "(child, parent, source, ord) VALUES(?, ?, 'declared', ?);",
      { ns.name, found && ns.parents[i] == previous.name ? ns.name : ns.parents[i],
        std_to_tm (std::to_string (i)) }, error);

  ok= ok && exec_prepared (cx.db,
    "DELETE FROM namespace_materials WHERE namespace_uuid=?;", {uuid}, error);
  for (size_t i=0; ok && i<ns.materials.size (); ++i)
    ok= exec_prepared (cx.db,
      "INSERT INTO namespace_materials(namespace_uuid,material_uuid,ord) VALUES(?,?,?);",
      {uuid, std_to_tm (ns.materials[i]), std_to_tm (std::to_string (i))}, error);

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
  if (ok && vault_context_is_current (context))
    athena_namespace_ontology_invalidate (false);
  return ok;
}

bool
athena_namespace_save (const vault_context_handle& context,
                       const athena_namespace_definition& ns, string& error) {
  return save_namespace (context, ns, error, false);
}

bool
athena_namespace_create (const vault_context_handle& context,
                         const athena_namespace_definition& ns, string& error) {
  if (ns.uuid != "") {
    error= "New namespaces must not supply a UUID.";
    return false;
  }
  return save_namespace (context, ns, error, true);
}

static namespace_query_status
remove_namespace (const vault_context_handle& context, string key, bool by_uuid,
                  string& error) {
  ns_sqlite_connection cx (context);
  auto status= open_query (cx, error);
  if (status != namespace_query_status::ok) return status;
  if (!exec_sql (cx.db, "BEGIN IMMEDIATE;", error))
    return namespace_query_status::error;
  athena_namespace_definition value;
  if (!get_namespace_from_db (cx.db, key, value, error, by_uuid))
    return error == "" ? namespace_query_status::not_found :
                         namespace_query_status::error;
  string name= value.name;
  bool ok=
    exec_prepared (cx.db, "DELETE FROM namespaces WHERE uuid=?;",
                   { value.uuid }, error) &&
    exec_prepared (cx.db,
      "DELETE FROM namespace_parents WHERE child=? OR parent=?;",
      { name, name }, error) &&
    exec_prepared (cx.db,
      "DELETE FROM relation_decisions WHERE child=? OR parent=?;",
      { name, name }, error) &&
    exec_sql (cx.db, "COMMIT;", error);
  if (ok && vault_context_is_current (context))
    athena_namespace_ontology_invalidate (false);
  return ok ? namespace_query_status::ok : namespace_query_status::error;
}

bool
athena_namespace_remove (string name, string& error) {
  auto status= remove_namespace (vault_capture_context (), name, false, error);
  return status == namespace_query_status::ok ||
         status == namespace_query_status::not_found;
}

namespace_query_status
athena_namespace_remove_by_uuid (const vault_context_handle& context,
                                 string uuid, string& error) {
  return remove_namespace (context, uuid, true, error);
}

namespace_records<athena_namespace_relation>
athena_namespace_relations_list () {
  auto context= vault_capture_context ();
  if (!context) return {};
  namespace_records<athena_namespace_relation> cached;
  if (athena_namespace_ontology_relations (cached) &&
      vault_context_is_current (context)) return cached;
  string error;
  athena_namespace_relations_list (context, cached, error);
  if (error != "") std_warning << "Cannot list namespace relations: " << error << LF;
  return cached;
}

bool
athena_namespace_relation_set (string parent, string child, string decision,
                               string source, string& error) {
  ns_sqlite_connection cx;
  if (parent == "" || child == "") {
    error= "Relation parent and child cannot be empty.";
    return false;
  }
  if (decision != "allow" && decision != "deny") {
    error= "Relation decision must be allow or deny.";
    return false;
  }

  if (!cx.open (false, error)) return false;
  bool ok= upsert_relation_decision (cx.db, parent, child, decision, source,
                                     error);
  if (ok && vault_context_is_current (cx.context))
    athena_namespace_ontology_invalidate (false);
  return ok;
}

namespace_query_status
athena_namespace_rename_by_uuid (const vault_context_handle& context,
                                 string uuid, string name, string& error) {
  if (name == "" || tm_to_std (name).find ('!') != std::string::npos) {
    error= "Invalid namespace name.";
    return namespace_query_status::error;
  }
  ns_sqlite_connection cx (context);
  auto status= open_query (cx, error);
  if (status != namespace_query_status::ok) return status;
  if (!exec_sql (cx.db, "BEGIN IMMEDIATE;", error)) return namespace_query_status::error;
  athena_namespace_definition ns;
  if (!get_namespace_from_db (cx.db, uuid, ns, error, true))
    return error == "" ? namespace_query_status::not_found : namespace_query_status::error;
  const bool ok= exec_prepared (cx.db, "UPDATE namespaces SET name=? WHERE uuid=?;", {name, uuid}, error) &&
    exec_prepared (cx.db, "UPDATE namespace_parents SET child=? WHERE child=?;", {name, ns.name}, error) &&
    exec_prepared (cx.db, "UPDATE namespace_parents SET parent=? WHERE parent=?;", {name, ns.name}, error) &&
    exec_prepared (cx.db, "UPDATE relation_decisions SET child=? WHERE child=?;", {name, ns.name}, error) &&
    exec_prepared (cx.db, "UPDATE relation_decisions SET parent=? WHERE parent=?;", {name, ns.name}, error) &&
    exec_sql (cx.db, "COMMIT;", error);
  if (ok && vault_context_is_current (context)) athena_namespace_ontology_invalidate (false);
  return ok ? namespace_query_status::ok : namespace_query_status::error;
}

namespace_query_status
athena_namespace_relation_write_by_uuid (const vault_context_handle& context,
  string parent_uuid, string child_uuid, string decision, string source,
  bool remove, string& error) {
  if (!remove && decision != "allow" && decision != "deny") {
    error= "Relation decision must be allow or deny.";
    return namespace_query_status::error;
  }
  ns_sqlite_connection cx (context);
  auto status= open_query (cx, error);
  if (status != namespace_query_status::ok) return status;
  if (!exec_sql (cx.db, "BEGIN IMMEDIATE;", error)) return namespace_query_status::error;
  athena_namespace_definition parent, child;
  if (!get_namespace_from_db (cx.db, parent_uuid, parent, error, true) ||
      !get_namespace_from_db (cx.db, child_uuid, child, error, true))
    return error == "" ? namespace_query_status::not_found : namespace_query_status::error;
  bool ok= remove ? exec_prepared (cx.db,
    "DELETE FROM relation_decisions WHERE parent=? AND child=?;", {parent.name, child.name}, error) :
    upsert_relation_decision (cx.db, parent.name, child.name, decision, source, error);
  ok= ok && exec_sql (cx.db, "COMMIT;", error);
  if (ok && vault_context_is_current (context)) athena_namespace_ontology_invalidate (false);
  return ok ? namespace_query_status::ok : namespace_query_status::error;
}

bool
athena_namespace_relation_remove (string parent, string child, string& error) {
  ns_sqlite_connection cx;
  if (!cx.open (false, error)) return false;
  bool ok= exec_prepared (cx.db,
    "DELETE FROM relation_decisions WHERE parent=? AND child=?;",
    { parent, child }, error);
  if (ok && vault_context_is_current (cx.context))
    athena_namespace_ontology_invalidate (false);
  return ok;
}

bool
athena_namespace_validate_relation (string parent, string child, bool ask_user,
                                    string& error) {
  error= "";
  if (parent == child) return true;
  ns_sqlite_connection cx;
  if (!cx.open (false, error)) return false;

  athena_namespace_definition child_ns;
  if (get_namespace_from_db (cx.db, child, child_ns, error)) {
    if (has_string (child_ns.parents, parent) ||
        has_string (child_ns.derived_parents, parent)) {
      bool ok= upsert_relation_decision (cx.db, parent, child, "allow", "derived", error);
      if (ok && vault_context_is_current (cx.context))
        athena_namespace_ontology_invalidate (false);
      return ok;
    }
  }
  if (error != "") return false;

  sqlite3_stmt* st= nullptr;
  if (!prepare_sql (cx.db,
        "SELECT decision FROM relation_decisions WHERE parent=? AND child=?;",
        &st, error)) return false;
  if (!bind_tm_string (st, 1, parent, error) ||
      !bind_tm_string (st, 2, child, error)) {
    sqlite3_finalize (st);
    return false;
  }
  int status= sqlite3_step (st);
  string decision;
  if (status == SQLITE_ROW) decision= column_tm_string (st, 0);
  else if (status != SQLITE_DONE)
    set_sql_error (cx.db, "SQLite relation query failed", error);
  sqlite3_finalize (st);
  if (error != "") return false;
  if (decision == "allow") return true;
  if (decision == "deny") {
    error= "Namespace relation denied by cached decision.";
    return false;
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
  if (!vault_context_is_current (cx.context)) {
    error= "Vault context is stale.";
    return false;
  }
  if (!upsert_relation_decision (cx.db, parent, child,
                                 r == QMessageBox::Yes ? "allow" : "deny",
                                 "user", error))
    return false;
  if (vault_context_is_current (cx.context))
    athena_namespace_ontology_invalidate (false);
  if (r == QMessageBox::Yes) return true;
  error= "Namespace relation denied.";
  return false;
}

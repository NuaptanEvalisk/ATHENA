/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "hodarium_logical_database.hpp"
#include "namespaces_schema.hpp"
#include "namespaces_private.hpp"
#include "hodarium_artifact_database.hpp"
#include "artifact_title_filter.hpp"
#include "hodarium_materials_database.hpp"
#include "Data/Convert/Xml/document_file_codec.hpp"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonObject>
#include <QUuid>
#include <QtEndian>
#include <sqlite3.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <tuple>

namespace athena::hodarium {
namespace {
constexpr size_t payload_limit= 8 * 1024 * 1024;
constexpr size_t graph_limit= 100000;
constexpr const char* namespace_format= "athena-namespace-v1";
constexpr const char* relation_format= "athena-namespace-relation-v1";
constexpr const char* embedding_format= "athena-rag-vector-v1";
using objects= std::map<std::string,logical_database_object>;

void require (bool value, const char* message) {
  if (!value) throw std::runtime_error (message);
}
QString qs (const std::string& value) {
  auto result= QString::fromUtf8 (value.data (), qsizetype (value.size ()));
  require (result.toUtf8 ().toStdString () == value &&
           value.find ('\0') == std::string::npos, "Invalid logical UTF-8 text");
  return result;
}
std::string text (const QJsonValue& value) {
  require (value.isString (), "Expected logical text field");
  auto result= value.toString ().toUtf8 ().toStdString ();
  require (result.find ('\0') == std::string::npos, "Embedded NUL in logical text");
  return result;
}
std::string encode (const QJsonArray& value) {
  auto result= QJsonDocument (value).toJson (QJsonDocument::Compact).toStdString ();
  require (result.size () <= payload_limit, "Logical object exceeds payload budget");
  return result;
}
QJsonArray decode (const std::string& value, int count) {
  require (value.size () <= payload_limit, "Logical object exceeds payload budget");
  QJsonParseError error;
  auto doc= QJsonDocument::fromJson (QByteArray::fromStdString (value), &error);
  require (error.error == QJsonParseError::NoError && doc.isArray () &&
           doc.array ().size () == count, "Invalid logical object envelope");
  auto result= doc.array ();
  require (encode (result) == value, "Noncanonical logical object envelope");
  require (result[0].isDouble () && result[0].toDouble () == 1,
           "Unsupported logical object version");
  return result;
}
bool uuid (const std::string& value) {
  const auto parsed= QUuid (QString::fromStdString (value));
  return !parsed.isNull () &&
    parsed.toString (QUuid::WithoutBraces).toStdString () == value;
}
void identifier (const std::string& value) {
  require (uuid (value), "Invalid logical UUID");
}

struct database {
  sqlite3* db= nullptr;
  database (const std::filesystem::path& path, bool write, bool namespaces= false) {
    if (namespaces) {
      std::string error;
      if (!athena_namespace_database_open (path, false, db, error))
        throw std::runtime_error (error);
    }
    else {
      int rc= sqlite3_open_v2 (path.c_str (), &db,
        write ? SQLITE_OPEN_READWRITE : SQLITE_OPEN_READONLY, nullptr);
      if (rc != SQLITE_OK) {
        std::string error= db ? sqlite3_errmsg (db) : sqlite3_errstr (rc);
        if (db) sqlite3_close (db);
        db= nullptr;
        throw std::runtime_error (error);
      }
      sqlite3_busy_timeout (db, 5000);
    }
  }
  ~database () { if (db) sqlite3_close (db); }
  void exec (const char* sql) {
    if (sqlite3_exec (db, sql, nullptr, nullptr, nullptr) != SQLITE_OK)
      throw std::runtime_error (sqlite3_errmsg (db));
  }
};
struct transaction {
  database& db;
  bool done= false;
  transaction (database& owner, bool write): db (owner) {
    db.exec (write ? "BEGIN IMMEDIATE" : "BEGIN");
  }
  ~transaction () { if (!done) sqlite3_exec (db.db, "ROLLBACK", nullptr, nullptr, nullptr); }
  void commit () { db.exec ("COMMIT"); done= true; }
};
struct statement {
  sqlite3_stmt* st= nullptr;
  explicit statement (database& db, const char* sql) {
    if (sqlite3_prepare_v2 (db.db, sql, -1, &st, nullptr) != SQLITE_OK)
      throw std::runtime_error (sqlite3_errmsg (db.db));
  }
  ~statement () { sqlite3_finalize (st); }
  void bind (int i, const std::string& value) {
    if (sqlite3_bind_text (st, i, value.data (), int (value.size ()), SQLITE_TRANSIENT) != SQLITE_OK)
      throw std::runtime_error (sqlite3_errmsg (sqlite3_db_handle (st)));
  }
  bool row () {
    int rc= sqlite3_step (st);
    if (rc != SQLITE_ROW && rc != SQLITE_DONE)
      throw std::runtime_error (sqlite3_errmsg (sqlite3_db_handle (st)));
    return rc == SQLITE_ROW;
  }
  std::string str (int i) const {
    auto value= reinterpret_cast<const char*> (sqlite3_column_text (st, i));
    return value ? std::string (value, sqlite3_column_bytes (st, i)) : std::string ();
  }
};
void execute (database& db, const char* sql,
              const std::vector<std::string>& bindings= {}) {
  statement st (db, sql);
  for (size_t i=0; i<bindings.size (); ++i) st.bind (int (i+1), bindings[i]);
  require (!st.row (), "Unexpected result from logical mutation");
}

std::string portable_path (const std::filesystem::path& root, std::string value,
                           bool exporting) {
  if (value.empty ()) return {};
  qs (value);
  std::filesystem::path path (value);
  if (path.is_absolute () && exporting) path= path.lexically_relative (root);
  require (!path.empty () && !path.is_absolute (), "Logical resource is outside the Vault");
  for (const auto& part: path)
    require (part != ".." && part != ".", "Nonportable logical resource path");
  require (value.find ('\\') == std::string::npos && value.find (':') == std::string::npos,
           "Logical resource path must be Vault-relative");
  return path.generic_string ();
}

struct definition {
  std::string id, name, kind, templ, sorter, style, initial, homepage;
  bool trivial= false;
  std::vector<std::string> parents, materials;
};
QJsonArray array (const std::vector<std::string>& values) {
  QJsonArray result;
  for (const auto& value: values) result.append (qs (value));
  return result;
}
std::vector<std::string> identifiers (const QJsonValue& value) {
  require (value.isArray (), "Expected logical reference list");
  std::vector<std::string> result;
  std::set<std::string> seen;
  for (const auto& item: value.toArray ()) {
    auto id= text (item); identifier (id);
    require (seen.insert (id).second, "Duplicate logical reference");
    result.push_back (std::move (id));
  }
  return result;
}
logical_database_object serialize (const definition& ns) {
  return {"namespace/" + ns.id, namespace_format,
    encode (QJsonArray {1, qs (ns.id), qs (ns.name), qs (ns.kind), qs (ns.templ),
      ns.trivial, qs (ns.sorter), qs (ns.style), qs (ns.initial), qs (ns.homepage),
      array (ns.parents), array (ns.materials)})};
}
definition parse_definition (const std::string& key, const std::string& payload) {
  auto value= decode (payload, 12);
  definition result;
  result.id= text (value[1]); identifier (result.id);
  require (key == "namespace/" + result.id, "Namespace identity mismatch");
  result.name= text (value[2]); result.kind= text (value[3]);
  result.templ= text (value[4]);
  if (!result.templ.empty ()) {
    std::vector<athena_namespaces::template_token> tokens;
    std::string error;
    if (!athena_namespaces::parse_template_std (result.templ, tokens, error))
      throw std::runtime_error (error);
  }
  require (!result.name.empty () && result.name.find ('!') == std::string::npos,
           "Invalid namespace name");
  require (result.kind == "abstract" || result.kind == "semi-concrete" ||
           result.kind == "concrete", "Invalid namespace kind");
  require (result.kind == "abstract" || !result.templ.empty (),
           "Concrete namespace requires a template");
  require (value[5].isBool (), "Invalid namespace sorter mode");
  result.trivial= value[5].toBool ();
  result.sorter= portable_path ({}, text (value[6]), false);
  result.style= portable_path ({}, text (value[7]), false);
  result.initial= portable_path ({}, text (value[8]), false);
  result.homepage= portable_path ({}, text (value[9]), false);
  result.parents= identifiers (value[10]); result.materials= identifiers (value[11]);
  require (result.kind != "abstract" || result.materials.empty (),
           "Abstract namespace cannot contain Materials");
  return result;
}
struct relation { std::string parent, child, decision, source; };
logical_database_object serialize (const relation& relation) {
  return {"namespace-relation/" + relation.parent + "/" + relation.child,
    relation_format, encode (QJsonArray {1, qs (relation.parent), qs (relation.child),
      qs (relation.decision), qs (relation.source)})};
}
relation parse_relation (const std::string& key, const std::string& payload) {
  auto value= decode (payload, 5);
  relation result {text (value[1]), text (value[2]), text (value[3]), text (value[4])};
  identifier (result.parent); identifier (result.child);
  require (key == "namespace-relation/" + result.parent + "/" + result.child,
           "Namespace relation identity mismatch");
  require (result.decision == "allow" || result.decision == "deny",
           "Invalid namespace relation decision");
  require (!result.source.empty () && result.source != "derived",
           "Derived namespace relations are local cache data");
  return result;
}

objects namespace_snapshot (database& db, const std::filesystem::path& root) {
  std::map<std::string,definition> namespaces;
  std::map<std::string,std::string> names;
  statement defs (db, "SELECT uuid,name,kind,template,sorter_trivial,sorter_path,"
    "style_path,initial_content_path,homepage_path FROM namespaces ORDER BY uuid");
  while (defs.row ()) {
    require (namespaces.size () < graph_limit, "Namespace snapshot exceeds object budget");
    definition value;
    value.id= defs.str (0); identifier (value.id);
    value.name= defs.str (1); value.kind= defs.str (2); value.templ= defs.str (3);
    value.trivial= sqlite3_column_int (defs.st, 4) != 0;
    value.sorter= portable_path (root, defs.str (5), true);
    value.style= portable_path (root, defs.str (6), true);
    value.initial= portable_path (root, defs.str (7), true);
    value.homepage= portable_path (root, defs.str (8), true);
    names.emplace (value.name, value.id); namespaces.emplace (value.id, std::move (value));
  }
  auto resolve= [&] (const std::string& name)->std::string {
    auto it= names.find (name);
    if (it == names.end ()) throw std::runtime_error (
      "Namespace references a missing named object: " + name);
    return it->second;
  };
  statement parents (db, "SELECT child,parent FROM namespace_parents "
    "WHERE source='declared' ORDER BY child,ord,parent");
  size_t count= namespaces.size ();
  while (parents.row ()) {
    require (++count <= graph_limit, "Namespace snapshot exceeds relation budget");
    namespaces.at (resolve (parents.str (0))).parents.push_back (resolve (parents.str (1)));
  }
  statement materials (db, "SELECT namespace_uuid,material_uuid FROM namespace_materials "
    "ORDER BY namespace_uuid,ord");
  while (materials.row ()) {
    require (++count <= graph_limit, "Namespace snapshot exceeds relation budget");
    auto id= materials.str (1); identifier (id);
    namespaces.at (materials.str (0)).materials.push_back (std::move (id));
  }
  objects result;
  for (const auto& [id, value]: namespaces) {
    auto object= serialize (value); result.emplace (object.key, std::move (object));
  }
  statement relations (db, "SELECT parent,child,decision,source FROM relation_decisions "
    "WHERE source<>'derived' ORDER BY parent,child");
  while (relations.row ()) {
    require (++count <= graph_limit, "Namespace snapshot exceeds relation budget");
    auto object= serialize (relation {resolve (relations.str (0)), resolve (relations.str (1)),
      relations.str (2), relations.str (3)});
    result.emplace (object.key, std::move (object));
  }
  return result;
}

std::string embedding_key (const std::string& space, const std::string& input) {
  return "rag-vector/" + QByteArray::fromStdString (space).toHex ().toStdString () + "/" + input;
}
void rag_schema (database& db) {
  statement version (db, "SELECT value FROM meta WHERE key='schema-version'");
  require (version.row () && version.str (0) == "3", "Unsupported RAG schema");
}
QByteArray vector_wire (const void* data, int bytes, int dimension, bool to_wire) {
  require (dimension > 0 && dimension <= 65536 && bytes == dimension * 4 && data,
           "Invalid RAG vector dimensions");
  QByteArray result (bytes, Qt::Uninitialized);
  const auto* input= static_cast<const unsigned char*> (data);
  for (int i=0; i<dimension; ++i) {
    quint32 word;
    if (to_wire) std::memcpy (&word, input + i*4, 4);
    else word= qFromLittleEndian<quint32> (input + i*4);
    float value; std::memcpy (&value, &word, 4);
    require (std::isfinite (value), "Nonfinite RAG embedding component");
    if (to_wire) qToLittleEndian (word, result.data () + i*4);
    else std::memcpy (result.data () + i*4, &word, 4);
  }
  return result;
}
} // namespace

std::vector<logical_database_object> export_namespace_objects (
  const std::filesystem::path& path, const std::filesystem::path& root) {
  database db (path, false, true); transaction tx (db, false);
  auto snapshot= namespace_snapshot (db, root);
  std::vector<logical_database_object> result;
  for (auto& [key, value]: snapshot) result.push_back (std::move (value));
  tx.commit (); return result;
}

logical_apply_result apply_namespace_objects (
  const std::filesystem::path& path, const std::filesystem::path& root,
  const std::vector<logical_database_change>& changes, const std::function<bool()>& permitted) {
  if (changes.empty ()) return logical_apply_result::unchanged;
  require (changes.size () <= graph_limit, "Logical batch exceeds object budget");
  database db (path, true, true); transaction tx (db, true);
  auto old= namespace_snapshot (db, root), next= old;
  std::set<std::string> seen;
  bool changed= false;
  for (const auto& change: changes) {
    require (seen.insert (change.key).second, "Duplicate logical object mutation");
    auto found= old.find (change.key);
    std::optional<std::string> present;
    if (found != old.end ()) present= found->second.payload;
    if (present != change.expected) return logical_apply_result::stale;
    if (present == change.replacement) continue;
    changed= true;
    if (!change.replacement) { next.erase (change.key); continue; }
    logical_database_object object;
    if (change.key.rfind ("namespace/", 0) == 0)
      object= serialize (parse_definition (change.key, *change.replacement));
    else if (change.key.rfind ("namespace-relation/", 0) == 0)
      object= serialize (parse_relation (change.key, *change.replacement));
    else throw std::runtime_error ("Unknown logical namespace object");
    next[change.key]= std::move (object);
  }
  if (!changed) return logical_apply_result::unchanged;
  std::map<std::string,definition> definitions;
  std::set<std::string> names;
  std::vector<relation> relations;
  for (const auto& [key, value]: next) {
    if (value.format == namespace_format) {
      auto ns= parse_definition (key, value.payload);
      require (names.insert (ns.name).second, "Conflicting namespace names");
      definitions.emplace (ns.id, std::move (ns));
    }
    else relations.push_back (parse_relation (key, value.payload));
  }
  for (const auto& [id, ns]: definitions)
    for (const auto& parent: ns.parents)
      if (!definitions.count (parent)) return logical_apply_result::dependency_missing;
  for (const auto& relation: relations)
    if (!definitions.count (relation.parent) || !definitions.count (relation.child))
      return logical_apply_result::dependency_missing;

  // Name-based legacy storage is rewritten under one lock from UUID references.
  // Temporary names allow a valid batch that exchanges two display names.
  for (const auto& change: changes) {
    if (change.key.rfind ("namespace/", 0) != 0) continue;
    auto it= old.find (change.key);
    if (it == old.end ()) continue;
    auto previous= parse_definition (it->first, it->second.payload);
    if (!change.replacement) {
      execute (db, "DELETE FROM namespaces WHERE uuid=?", {previous.id});
      continue;
    }
    const auto& ns= definitions.at (previous.id);
    if (previous.name != ns.name) execute (db,
      "UPDATE namespaces SET name=? WHERE uuid=?",
      {"!hodarium-" + QUuid::createUuid ().toString (QUuid::WithoutBraces).toStdString (), ns.id});
  }
  for (const auto& change: changes) {
    if (change.key.rfind ("namespace/", 0) != 0 || !change.replacement) continue;
    auto ns= parse_definition (change.key, *change.replacement);
    execute (db, "INSERT INTO namespaces(uuid,name,kind,template,sorter_trivial,sorter_path,"
      "style_path,initial_content_path,homepage_path) VALUES(?,?,?,?,?,?,?,?,?) "
      "ON CONFLICT(uuid) DO UPDATE SET name=excluded.name,kind=excluded.kind,"
      "template=excluded.template,sorter_trivial=excluded.sorter_trivial,"
      "sorter_path=excluded.sorter_path,style_path=excluded.style_path,"
      "initial_content_path=excluded.initial_content_path,homepage_path=excluded.homepage_path",
      {ns.id, ns.name, ns.kind, ns.templ, ns.trivial ? "1" : "0", ns.sorter,
       ns.style, ns.initial, ns.homepage});
    execute (db, "DELETE FROM namespace_materials WHERE namespace_uuid=?", {ns.id});
    for (size_t i=0; i<ns.materials.size (); ++i)
      execute (db, "INSERT INTO namespace_materials(namespace_uuid,material_uuid,ord) VALUES(?,?,?)",
        {ns.id, ns.materials[i], std::to_string (i)});
  }
  // Derived parents are deliberately invalidated rather than replicated. This
  // also removes stale name references after a rename/delete batch.
  db.exec ("DELETE FROM namespace_parents; DELETE FROM relation_decisions;"
    "DELETE FROM meta WHERE key='derived-source-fingerprint';");
  for (const auto& [id, ns]: definitions)
    for (size_t i=0; i<ns.parents.size (); ++i)
      execute (db, "INSERT INTO namespace_parents(child,parent,source,ord) VALUES(?,?,'declared',?)",
        {ns.name, definitions.at (ns.parents[i]).name, std::to_string (i)});
  for (const auto& relation: relations)
    execute (db, "INSERT INTO relation_decisions(parent,child,decision,source) VALUES(?,?,?,?)",
      {definitions.at (relation.parent).name, definitions.at (relation.child).name,
       relation.decision, relation.source});
  if (permitted && !permitted ()) return logical_apply_result::stale;
  tx.commit (); return logical_apply_result::applied;
}

std::vector<logical_database_object> export_embedding_objects (
  const std::filesystem::path& path, const std::string& after, unsigned limit) {
  require (limit > 0 && limit <= 256, "Invalid logical export page size");
  database db (path, false); transaction tx (db, false); rag_schema (db);
  std::string after_space, after_input;
  if (!after.empty ()) {
    require (after.rfind ("rag-vector/", 0) == 0, "Invalid vector cursor");
    auto slash= after.find ('/', 11);
    require (slash != std::string::npos, "Invalid vector cursor");
    auto hex= QByteArray::fromStdString (after.substr (11, slash-11));
    auto decoded= QByteArray::fromHex (hex);
    require (decoded.toHex () == hex, "Invalid vector cursor encoding");
    after_space= decoded.toStdString (); after_input= after.substr (slash+1);
  }
  statement st (db, "SELECT e.space_id,e.input_hash,e.embedding,e.embedding_dim,s.contract "
    "FROM embeddings e JOIN embedding_spaces s ON s.space_id=e.space_id "
    "WHERE (e.space_id,e.input_hash)>(?,?) AND e.embedding_dim=s.dimension "
    "AND EXISTS(SELECT 1 FROM chunks c WHERE c.embedding_space=e.space_id "
    "AND c.embedding_input_hash=e.input_hash) ORDER BY e.space_id,e.input_hash LIMIT ?");
  st.bind (1, after_space); st.bind (2, after_input);
  require (sqlite3_bind_int (st.st, 3, int (limit)) == SQLITE_OK, "Could not bind vector page size");
  std::vector<logical_database_object> result;
  while (st.row ()) {
    auto space= st.str (0), input= st.str (1), key= embedding_key (space, input);
    const int dim= sqlite3_column_int (st.st, 3);
    auto vector= vector_wire (sqlite3_column_blob (st.st, 2),
      sqlite3_column_bytes (st.st, 2), dim, true);
    auto payload= encode (QJsonArray {1, qs (space), qs (input), dim,
      qs (st.str (4)), QString::fromLatin1 (vector.toBase64 ())});
    result.push_back ({key, embedding_format, std::move (payload)});
  }
  tx.commit (); return result;
}

void initialize_embedding_changes (const std::filesystem::path& path) {
  database db (path,true); transaction tx (db,true); rag_schema (db);
  statement exists (db,"SELECT 1 FROM sqlite_master WHERE type='table' AND name='hodarium_embedding_changes'");
  const bool seeded= exists.row ();
  sqlite3_reset (exists.st);
  db.exec ("CREATE TABLE IF NOT EXISTS hodarium_embedding_changes("
    "sequence INTEGER PRIMARY KEY AUTOINCREMENT,space_id TEXT NOT NULL,input_hash TEXT NOT NULL,"
    "UNIQUE(space_id,input_hash));"
    "CREATE TABLE IF NOT EXISTS hodarium_embedding_contracts("
    "space_id TEXT PRIMARY KEY,dimension INTEGER NOT NULL,contract TEXT NOT NULL);"
    "CREATE TRIGGER IF NOT EXISTS hodarium_embedding_insert AFTER INSERT ON embeddings BEGIN "
    "INSERT OR REPLACE INTO hodarium_embedding_changes(space_id,input_hash) VALUES(NEW.space_id,NEW.input_hash); END;"
    "CREATE TRIGGER IF NOT EXISTS hodarium_embedding_update AFTER UPDATE ON embeddings BEGIN "
    "DELETE FROM hodarium_embedding_changes WHERE space_id=OLD.space_id AND input_hash=OLD.input_hash;"
    "INSERT OR REPLACE INTO hodarium_embedding_changes(space_id,input_hash) VALUES(NEW.space_id,NEW.input_hash); END;"
    "CREATE TRIGGER IF NOT EXISTS hodarium_embedding_delete AFTER DELETE ON embeddings BEGIN "
    "DELETE FROM hodarium_embedding_changes WHERE space_id=OLD.space_id AND input_hash=OLD.input_hash; END;"
    "DROP TRIGGER IF EXISTS hodarium_embedding_space_insert;"
    "DROP TRIGGER IF EXISTS hodarium_embedding_space_update;"
    "CREATE TRIGGER hodarium_embedding_space_insert AFTER INSERT ON embedding_spaces "
    "WHEN NOT EXISTS(SELECT 1 FROM hodarium_embedding_contracts WHERE space_id=NEW.space_id "
    "AND dimension=NEW.dimension AND contract=NEW.contract) BEGIN "
    "INSERT OR REPLACE INTO hodarium_embedding_changes(space_id,input_hash) "
    "SELECT space_id,input_hash FROM embeddings WHERE space_id=NEW.space_id;"
    "INSERT OR REPLACE INTO hodarium_embedding_contracts VALUES(NEW.space_id,NEW.dimension,NEW.contract); END;"
    "CREATE TRIGGER hodarium_embedding_space_update AFTER UPDATE OF dimension,contract ON embedding_spaces "
    "WHEN NOT EXISTS(SELECT 1 FROM hodarium_embedding_contracts WHERE space_id=NEW.space_id "
    "AND dimension=NEW.dimension AND contract=NEW.contract) BEGIN "
    "INSERT OR REPLACE INTO hodarium_embedding_changes(space_id,input_hash) "
    "SELECT space_id,input_hash FROM embeddings WHERE space_id=NEW.space_id;"
    "INSERT OR REPLACE INTO hodarium_embedding_contracts VALUES(NEW.space_id,NEW.dimension,NEW.contract); END;");
  db.exec ("INSERT OR IGNORE INTO hodarium_embedding_contracts SELECT space_id,dimension,contract FROM embedding_spaces");
  if (!seeded) db.exec ("INSERT INTO hodarium_embedding_changes(space_id,input_hash) "
    "SELECT space_id,input_hash FROM embeddings ORDER BY space_id,input_hash");
  tx.commit ();
}

embedding_change_page export_embedding_changes (
  const std::filesystem::path& path, std::int64_t after, unsigned limit) {
  require (after>=0 && limit>0 && limit<=256,"Invalid embedding change cursor");
  database db (path,false); transaction tx (db,false); rag_schema (db);
  statement changes (db,"SELECT sequence,space_id,input_hash FROM hodarium_embedding_changes "
    "WHERE sequence>? ORDER BY sequence LIMIT ?");
  sqlite3_bind_int64 (changes.st,1,after); sqlite3_bind_int (changes.st,2,int(limit));
  embedding_change_page result; result.through= after;
  unsigned count= 0;
  while (changes.row ()) {
    ++count; result.through= sqlite3_column_int64 (changes.st,0);
    const auto space= changes.str (1),input= changes.str (2);
    statement row (db,"SELECT e.embedding,e.embedding_dim,s.contract FROM embeddings e "
      "JOIN embedding_spaces s ON s.space_id=e.space_id "
      "WHERE e.space_id=? AND e.input_hash=? AND e.embedding_dim=s.dimension");
    row.bind (1,space); row.bind (2,input);
    if (!row.row ()) continue;
    const int dimension= sqlite3_column_int (row.st,1);
    auto vector= vector_wire (sqlite3_column_blob(row.st,0),sqlite3_column_bytes(row.st,0),dimension,true);
    result.objects.push_back ({embedding_key(space,input),embedding_format,encode(QJsonArray {
      1,qs(space),qs(input),dimension,qs(row.str(2)),QString::fromLatin1(vector.toBase64())})});
  }
  result.more= count==limit; tx.commit (); return result;
}

logical_database_object rename_namespace_object_resources (
  const logical_database_object& object, const std::string& old_path,
  const std::string& new_path, bool directory) {
  if (object.format != namespace_format) return object;
  auto previous= portable_path ({}, old_path, false);
  auto next= portable_path ({}, new_path, false);
  require (!previous.empty () && !next.empty (), "Empty namespace resource rename");
  auto ns= parse_definition (object.key, object.payload);
  for (auto* value: {&ns.sorter, &ns.style, &ns.initial, &ns.homepage}) {
    if (*value == previous) *value= next;
    else if (directory && value->rfind (previous + "/", 0) == 0)
      *value= next + value->substr (previous.size ());
  }
  return serialize (ns);
}

logical_apply_result apply_embedding_object (
  const std::filesystem::path& path, const logical_database_object& object,
  const std::function<bool()>& permitted) {
  require (object.format == embedding_format, "Unsupported logical vector format");
  auto value= decode (object.payload, 6);
  auto space= text (value[1]), input= text (value[2]), contract= text (value[4]);
  require (!space.empty () && !input.empty () && object.key == embedding_key (space, input),
           "Logical vector identity mismatch");
  require (value[3].isDouble () && value[3].toDouble () == value[3].toInt (),
           "Invalid logical vector dimension");
  int dimension= value[3].toInt ();
  auto encoded= QByteArray::fromStdString (text (value[5]));
  auto raw= QByteArray::fromBase64 (encoded, QByteArray::AbortOnBase64DecodingErrors);
  require (raw.toBase64 () == encoded, "Invalid logical vector encoding");
  auto vector= vector_wire (raw.constData (), int (raw.size ()), dimension, false);
  database db (path, true); transaction tx (db, true); rag_schema (db);
  statement definition (db, "SELECT dimension,contract FROM embedding_spaces WHERE space_id=?");
  definition.bind (1, space);
  if (!definition.row () || sqlite3_column_int (definition.st, 0) != dimension ||
      definition.str (1) != contract) return logical_apply_result::dependency_missing;
  statement used (db, "SELECT 1 FROM chunks WHERE (embedding_space=? OR embedding_space='') "
    "AND embedding_input_hash=? LIMIT 1");
  used.bind (1, space); used.bind (2, input);
  if (!used.row ()) return logical_apply_result::dependency_missing;
  statement insert (db, "INSERT INTO embeddings(space_id,input_hash,embedding,embedding_dim) "
    "VALUES(?,?,?,?) ON CONFLICT(space_id,input_hash) DO NOTHING");
  insert.bind (1, space); insert.bind (2, input);
  require (sqlite3_bind_blob (insert.st, 3, vector.constData (), int (vector.size ()), SQLITE_TRANSIENT) == SQLITE_OK &&
           sqlite3_bind_int (insert.st, 4, dimension) == SQLITE_OK, "Could not bind logical vector");
  require (!insert.row (), "Unexpected logical vector result");
  bool changed= sqlite3_changes (db.db) != 0;
  if (permitted && !permitted ()) return logical_apply_result::stale;
  tx.commit ();
  return changed ? logical_apply_result::applied : logical_apply_result::unchanged;
}

std::vector<logical_database_object> export_artifact_objects (
  const std::filesystem::path& root, const std::string& after, unsigned limit) {
  std::string cursor;
  if (!after.empty ()) {
    require (after.rfind ("artifact-range/", 0) == 0, "Invalid artifact cursor");
    cursor= after.substr (15, 36); identifier (cursor);
  }
  std::vector<HodariumArtifactRangeResult> records;
  std::string error;
  if (!athena_artifacts_export_range_results (root, cursor, limit, records, error))
    throw std::runtime_error (error);
  std::vector<logical_database_object> result;
  for (const auto& record: records) result.push_back (encode_artifact_range_result(record));
  return result;
}

logical_database_object encode_artifact_range_result (const HodariumArtifactRangeResult& record) {
    QJsonArray offsets;
    for (int offset: record.offsets) offsets.append (offset);
    return {"artifact-range/" + record.artifact_uuid + "/" + record.input_hash,
      "athena-artifact-range-v1", encode (QJsonArray {1, qs (record.artifact_uuid),
        qs (record.source_uuid), qs (record.source_role), qs (record.input_hash),
        qs (record.structure_hash), qs (record.content_hash), offsets})};
}

logical_apply_result apply_artifact_object (
  const std::filesystem::path& root, const logical_database_object& object,
  const std::function<bool()>& permitted) {
  require (object.format == "athena-artifact-range-v1", "Unsupported artifact result format");
  auto value= decode (object.payload, 8);
  HodariumArtifactRangeResult result;
  result.artifact_uuid= text (value[1]); result.source_uuid= text (value[2]);
  result.source_role= text (value[3]); result.input_hash= text (value[4]);
  result.structure_hash= text (value[5]); result.content_hash= text (value[6]);
  require (object.key == "artifact-range/" + result.artifact_uuid + "/" + result.input_hash,
           "Artifact result identity mismatch");
  require (value[7].isArray () && value[7].toArray ().size () <= 11,
           "Invalid artifact result range");
  for (const auto& offset: value[7].toArray ()) {
    require (offset.isDouble () && offset.toDouble () == offset.toInt (),
             "Invalid artifact result offset");
    result.offsets.push_back (offset.toInt ());
  }
  bool applicable= false, changed= false;
  std::string error;
  if (!athena_artifacts_import_range_result (root, result, applicable, changed, error, permitted))
    throw std::runtime_error (error);
  return !applicable ? logical_apply_result::dependency_missing :
    changed ? logical_apply_result::applied : logical_apply_result::unchanged;
}

namespace {
std::string filter_payload (const AthenaArtifactTitleFilter& filter) {
  auto names= filter.entries, structured= filter.structured_entries;
  std::sort (names.begin (), names.end ());
  std::sort (structured.begin (), structured.end ());
  return encode (QJsonArray {1, array (names), array (structured)});
}
AthenaArtifactTitleFilter parse_filter (const std::string& payload) {
  auto value= decode (payload, 3);
  require (value[1].isArray () && value[2].isArray (), "Invalid artifact rejection lists");
  std::vector<std::string> names;
  for (const auto& item: value[1].toArray ()) names.push_back (text (item));
  auto result= athena_artifact_title_filter_from_entries (names);
  for (const auto& item: value[2].toArray ())
    athena_artifact_title_filter_add (result, athena::document::read_xml (
      text (item), athena::document::xml_kind::fragment));
  require (filter_payload (result) == payload, "Noncanonical artifact rejection list");
  return result;
}
}

logical_database_object export_artifact_rejections (const std::filesystem::path& root) {
  AthenaArtifactTitleFilter filter;
  std::string error;
  if (!athena_artifact_title_filter_read (root, filter, error, false))
    throw std::runtime_error (error);
  return {"artifact-rejections", "athena-artifact-rejections-v1", filter_payload (filter)};
}

logical_apply_result apply_artifact_rejections (
  const std::filesystem::path& root, const logical_database_change& change,
  const std::function<bool()>& permitted) {
  require (change.key == "artifact-rejections" && change.expected.has_value () &&
           change.replacement.has_value (), "Artifact rejections require explicit lists");
  auto expected= parse_filter (*change.expected);
  auto replacement= parse_filter (*change.replacement);
  if (*change.expected == *change.replacement) return logical_apply_result::unchanged;
  bool matched= false;
  std::string error;
  if (!athena_artifact_title_filter_replace_if_current (
        root, expected, replacement, matched, error, permitted)) throw std::runtime_error (error);
  return matched ? logical_apply_result::applied : logical_apply_result::stale;
}

bool material_logical_format (const std::string& format) {
  return format == "athena-material-v1" || format == "athena-material-attachment-v1" ||
    format == "athena-material-relation-v1" || format == "athena-material-alias-v1";
}

bool local_material_provenance (const MaterialProvenance& value) {
  const auto& reference= value.source_reference;
  return std::filesystem::path (reference).is_absolute () ||
    reference.rfind ("file:", 0) == 0 || reference.rfind ("~/", 0) == 0 ||
    (reference.size () > 2 && reference[1] == ':' &&
      (reference[2] == '/' || reference[2] == '\\'));
}

logical_database_object encode_material_record (const MaterialRecord& record) {
  QJsonParseError error;
  auto extra= QJsonDocument::fromJson (QByteArray::fromStdString (record.extra_json), &error);
  require (error.error == QJsonParseError::NoError && extra.isObject (), "Invalid Material extra metadata");
  auto portable_extra= extra.object ();
  if (portable_extra.value ("zotero").isObject ()) {
    auto zotero= portable_extra.value ("zotero").toObject ();
    zotero.remove ("localServerId");
    if (zotero.isEmpty ()) portable_extra.remove ("zotero");
    else portable_extra.insert ("zotero",zotero);
  }
  auto fields= record.fields;
  std::sort (fields.begin (), fields.end (), [] (const auto& a, const auto& b) {
    return std::tie (a.name,a.ordinal) < std::tie (b.name,b.ordinal);
  });
  auto creators= record.creators;
  std::sort (creators.begin (), creators.end (), [] (const auto& a, const auto& b) {
    return std::tie (a.ordinal,a.role) < std::tie (b.ordinal,b.role);
  });
  auto identifiers= record.identifiers;
  std::sort (identifiers.begin (), identifiers.end (), [] (const auto& a, const auto& b) {
    return std::tie (a.scheme,a.value) < std::tie (b.scheme,b.value);
  });
  auto tags= record.tags; std::sort (tags.begin (), tags.end ());
  auto provenance= record.provenance;
  std::sort (provenance.begin (), provenance.end (), [] (const auto& a, const auto& b) {
    return std::tie (a.field_name,a.source_kind,a.source_reference,a.observed_value,a.confidence) <
      std::tie (b.field_name,b.source_kind,b.source_reference,b.observed_value,b.confidence);
  });
  QJsonArray f,c,i,t,p;
  for (const auto& field: fields)
    f.append (QJsonArray {qs (field.name),qs (field.value),qs (field.language),field.ordinal});
  for (const auto& creator: creators)
    c.append (QJsonArray {qs (creator.role),qs (creator.given),qs (creator.family),
      qs (creator.literal),qs (creator.suffix),creator.ordinal});
  for (const auto& identifier: identifiers)
    i.append (QJsonArray {qs (identifier.scheme),qs (identifier.value)});
  for (const auto& tag: tags) t.append (qs (tag));
  for (const auto& provenance: provenance) if (!local_material_provenance (provenance)) {
    require (std::isfinite (provenance.confidence) && provenance.confidence >= 0 &&
      provenance.confidence <= 1, "Invalid Material provenance confidence");
    p.append (QJsonArray {qs (provenance.field_name),qs (provenance.source_kind),
      qs (provenance.source_reference),qs (provenance.observed_value),provenance.confidence});
  }
  return {"material/" + record.uuid, "athena-material-v1", encode (QJsonArray {
    1,qs (record.uuid),qs (record.item_type),qs (record.review_state),portable_extra,f,c,i,t,p})};
}

logical_database_object encode_material_attachment (const MaterialAttachment& attachment) {
  return {"material-attachment/" + attachment.uuid, "athena-material-attachment-v1",
    encode (QJsonArray {1,qs (attachment.uuid),qs (attachment.material_uuid),qs (attachment.role),
      qs (portable_path ({}, attachment.stored_path, false)),qs (attachment.original_name),
      qs (attachment.canonical_name),qs (attachment.mime_type),qs (attachment.sha256),
      QString::number (qlonglong (attachment.byte_size)),attachment.primary})};
}

logical_database_object encode_material_relation (const MaterialRelation& relation) {
  auto name_hash= QCryptographicHash::hash (QByteArray::fromStdString (relation.relation),
    QCryptographicHash::Sha256).toHex ().toStdString ();
  return {"material-relation/" + relation.subject_uuid + "/" + name_hash + "/" + relation.object_uuid,
    "athena-material-relation-v1", encode (QJsonArray {1,qs (relation.subject_uuid),
      qs (relation.relation),qs (relation.object_uuid)})};
}

logical_database_object encode_material_alias (const std::string& alias, const std::string& canonical) {
  return {"material-alias/" + alias, "athena-material-alias-v1",
    encode (QJsonArray {1,qs (alias),qs (canonical)})};
}

material_object decode_material_object (const logical_database_object& object) {
  material_object out;
  logical_database_object canonical;
  const auto array_field= [] (const QJsonValue& value, int count) {
    require (value.isArray () && value.toArray ().size () == count, "Invalid Material field arity");
    return value.toArray ();
  };
  const auto ordinal= [] (const QJsonValue& value) {
    require (value.isDouble () && value.toDouble () == value.toInt () && value.toInt () >= 0,
      "Invalid Material ordinal");
    return value.toInt ();
  };
  if (object.format == "athena-material-v1") {
    out.kind= material_object_kind::record;
    auto value= decode (object.payload, 10);
    auto& record= out.record;
    record.uuid= text (value[1]); identifier (record.uuid);
    record.item_type= text (value[2]); record.review_state= text (value[3]);
    require (!record.item_type.empty () && (record.review_state == "ready" ||
      record.review_state == "needs_review" || record.review_state == "unrecognized" ||
      record.review_state == "error"), "Invalid Material type or review state");
    require (value[4].isObject (), "Material extra metadata must be an object");
    record.extra_json= QJsonDocument (value[4].toObject ()).toJson (QJsonDocument::Compact).toStdString ();
    for (int n=5; n<10; ++n) require (value[n].isArray (), "Invalid Material list");
    std::set<std::pair<std::string,int>> field_keys,creator_keys;
    std::set<std::pair<std::string,std::string>> identifier_keys;
    std::set<std::string> tag_keys;
    for (const auto& item: value[5].toArray ()) {
      auto row= array_field (item, 4);
      MaterialField field {text (row[0]),text (row[1]),text (row[2]),ordinal (row[3])};
      require (!field.name.empty () && field_keys.emplace (field.name,field.ordinal).second,
        "Duplicate or empty Material field");
      record.fields.push_back (std::move (field));
    }
    for (const auto& item: value[6].toArray ()) {
      auto row= array_field (item, 6);
      MaterialCreator creator {text (row[0]),text (row[1]),text (row[2]),text (row[3]),
        text (row[4]),ordinal (row[5])};
      require (!creator.role.empty () && creator_keys.emplace (creator.role,creator.ordinal).second,
        "Duplicate or empty Material creator role");
      record.creators.push_back (std::move (creator));
    }
    for (const auto& item: value[7].toArray ()) {
      auto row= array_field (item, 2);
      MaterialIdentifier id {text (row[0]),text (row[1]),{}};
      require (!id.scheme.empty () && qs (id.scheme).trimmed ().toLower ().toStdString () == id.scheme &&
        qs (id.value).trimmed ().toStdString () == id.value, "Noncanonical Material identifier");
      id.normalized_value= MaterialsStore::normalize_identifier (id.scheme,id.value);
      require (!id.normalized_value.empty () && identifier_keys.emplace (id.scheme,id.normalized_value).second,
        "Duplicate Material identifier");
      record.identifiers.push_back (std::move (id));
    }
    for (const auto& item: value[8].toArray ()) {
      auto tag= text (item);
      require (!tag.empty () && qs (tag).trimmed ().toStdString () == tag && tag_keys.insert (tag).second,
        "Duplicate or noncanonical Material tag");
      record.tags.push_back (std::move (tag));
    }
    for (const auto& item: value[9].toArray ()) {
      auto row= array_field (item, 5);
      require (row[4].isDouble () && std::isfinite (row[4].toDouble ()) &&
        row[4].toDouble () >= 0 && row[4].toDouble () <= 1, "Invalid Material confidence");
      MaterialProvenance provenance {text (row[0]),text (row[1]),text (row[2]),text (row[3]),row[4].toDouble ()};
      require (!local_material_provenance (provenance), "Machine-local provenance is not synchronizable");
      record.provenance.push_back (std::move (provenance));
    }
    canonical= encode_material_record (record);
  }
  else if (object.format == "athena-material-attachment-v1") {
    out.kind= material_object_kind::attachment;
    auto value= decode (object.payload, 11);
    auto& a= out.attachment;
    a.uuid= text (value[1]); a.material_uuid= text (value[2]); identifier (a.uuid); identifier (a.material_uuid);
    a.role= text (value[3]); a.stored_path= portable_path ({},text (value[4]),false);
    a.original_name= text (value[5]); a.canonical_name= text (value[6]);
    a.mime_type= text (value[7]); a.sha256= text (value[8]);
    require (!a.role.empty () && !a.stored_path.empty () && a.sha256.size () == 64 &&
      std::all_of (a.sha256.begin (),a.sha256.end (), [] (char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }), "Invalid Material attachment");
    for (const auto* name: {&a.original_name,&a.canonical_name})
      require (!name->empty () && std::filesystem::path (*name).filename ().string () == *name &&
        name->find ('\\') == std::string::npos, "Material attachment name is not a basename");
    bool ok= false;
    const auto size= text (value[9]); a.byte_size= QString::fromStdString (size).toLongLong (&ok);
    require (ok && a.byte_size >= 0 && std::to_string (a.byte_size) == size && value[10].isBool (),
      "Invalid Material attachment size or primary marker");
    a.primary= value[10].toBool (); canonical= encode_material_attachment (a);
  }
  else if (object.format == "athena-material-relation-v1") {
    out.kind= material_object_kind::relation;
    auto value= decode (object.payload, 4);
    out.relation= {text (value[1]),text (value[2]),text (value[3])};
    identifier (out.relation.subject_uuid); identifier (out.relation.object_uuid);
    require (!out.relation.relation.empty () && out.relation.subject_uuid != out.relation.object_uuid,
      "Invalid Material relation");
    canonical= encode_material_relation (out.relation);
  }
  else if (object.format == "athena-material-alias-v1") {
    out.kind= material_object_kind::alias;
    auto value= decode (object.payload, 3);
    out.alias= text (value[1]); out.canonical= text (value[2]);
    identifier (out.alias); identifier (out.canonical);
    require (out.alias != out.canonical, "Self-referential Material alias");
    canonical= encode_material_alias (out.alias,out.canonical);
  }
  else throw std::invalid_argument ("Unsupported Material logical format");
  require (canonical.key == object.key && canonical.payload == object.payload,
    "Noncanonical Material logical object or identity mismatch");
  return out;
}

std::vector<logical_database_object> export_material_objects (const std::filesystem::path& root) {
  AthenaVaultfileInfo info; std::string error;
  if (!athena_vaultfile_read (root,info,error)) throw std::runtime_error (error);
  auto store= MaterialsStore::open_reader (root,info,error);
  if (!store) throw std::runtime_error (error);
  return store->logical_objects ();
}

logical_apply_result apply_material_object (const std::filesystem::path& root,
  const logical_database_change& change, const std::function<bool()>& permitted) {
  AthenaVaultfileInfo info; std::string error;
  if (!athena_vaultfile_read (root,info,error)) throw std::runtime_error (error);
  MaterialsStore store;
  if (!store.open (root,info,error)) throw std::runtime_error (error);
  return store.apply_logical_change (change,permitted);
}

void validate_logical_object (const logical_database_object& object) {
  if (material_logical_format (object.format)) {
    (void) decode_material_object (object); return;
  }
  if (object.format == namespace_format) {
    (void) parse_definition (object.key, object.payload); return;
  }
  if (object.format == relation_format) {
    (void) parse_relation (object.key, object.payload); return;
  }
  if (object.format == "athena-artifact-rejections-v1") {
    require (object.key == "artifact-rejections", "Invalid rejection object identity");
    (void) parse_filter (object.payload); return;
  }
  if (object.format == embedding_format) {
    auto value= decode (object.payload, 6);
    auto space= text (value[1]), input= text (value[2]);
    require (!space.empty () && !input.empty () && object.key == embedding_key (space, input),
             "Logical vector identity mismatch");
    (void) text (value[4]);
    require (value[3].isDouble () && value[3].toDouble () == value[3].toInt (),
             "Invalid logical vector dimension");
    auto encoded= QByteArray::fromStdString (text (value[5]));
    auto raw= QByteArray::fromBase64 (encoded, QByteArray::AbortOnBase64DecodingErrors);
    require (raw.toBase64 () == encoded, "Invalid logical vector encoding");
    (void) vector_wire (raw.constData (), int (raw.size ()), value[3].toInt (), false);
    return;
  }
  if (object.format == "athena-artifact-range-v1") {
    auto value= decode (object.payload, 8);
    auto artifact= text (value[1]), source= text (value[2]), role= text (value[3]);
    auto input= text (value[4]), structure= text (value[5]), content= text (value[6]);
    identifier (artifact); identifier (source);
    require (!role.empty () && !input.empty () && !structure.empty () && !content.empty () &&
             object.key == "artifact-range/" + artifact + "/" + input,
             "Artifact result identity mismatch");
    require (value[7].isArray (), "Invalid artifact offsets");
    auto offsets= value[7].toArray ();
    require (!offsets.empty () && offsets.size () <= 11, "Invalid artifact offset count");
    bool contains_origin= false;
    int previous= -6;
    for (int i=0; i<offsets.size (); ++i) {
      auto offset= offsets[i];
      require (offset.isDouble () && offset.toDouble () == offset.toInt () &&
               offset.toInt () >= -5 && offset.toInt () <= 5 &&
               (i == 0 || offset.toInt () == previous + 1), "Invalid artifact range offset");
      previous= offset.toInt (); contains_origin |= previous == 0;
    }
    require (contains_origin, "Artifact range omits its defining object");
    return;
  }
  throw std::invalid_argument ("Unsupported logical database format");
}
} // namespace athena::hodarium

/******************************************************************************
* MODULE     : artifacts.cpp
* DESCRIPTION: Semantic mathematical artifact index for ATHENA vaults
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include "ATHENA/Data/artifacts.hpp"
#include "ATHENA/Data/enunciation_model.hpp"

#include "ATHENA/Data/artifact_identity.hpp"
#include "ATHENA/Data/artifact_radioactive_links.hpp"
#include "ATHENA/Data/artifact_range_llm.hpp"
#include "ATHENA/Data/artifact_title_filter.hpp"
#include "ATHENA/Data/document_node_copy.hpp"
#include "ATHENA/Data/document_node_model.hpp"
#include "ATHENA/Data/new_buffer.hpp"
#include "ATHENA/Data/vault.hpp"
#include "ATHENA/Data/vault_maintenance_internal.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"
#include "ATHENA/buffer_actor.hpp"
#include "ATHENA/buffer_name_catalog.hpp"
#include "Data/Convert/Xml/document_file_codec.hpp"
#include "Data/Convert/Xml/document_upgrade_file.hpp"
#include "convert.hpp"
#include "drd_std.hpp"
#include "file.hpp"
#include "scheme.hpp"
#include "System/Boot/boot.hpp"
#include "tm_ostream.hpp"

#include <sqlite3.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <thread>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs= std::filesystem;

namespace {

constexpr size_t range_checkpoint_batch_size= 128;
constexpr const char* source_locator_contract= "source-uuid-role-list-v2";

bool freeze_paragraph_sources (const tree&, AthenaArtifactRecord&, std::string&);

void artifact_log (const AthenaArtifactsProgress& progress,
                   const std::string& message) {
  if (progress) return;
  athena_spdlog_debug ("artifacts: " + message);
}

bool report_progress (const AthenaArtifactsProgress& progress,
                      AthenaArtifactsBuildPhase phase, size_t current,
                      size_t total, const std::string& path= {},
                      const std::string& detail= {}, size_t queued= 0,
                      size_t running= 0) {
  if (!progress) return true;
  return progress ({phase, current, total, path, detail, queued, running});
}

struct SqliteDb {
  sqlite3* db= nullptr;
  ~SqliteDb () { if (db) sqlite3_close (db); }
};

struct Statement {
  sqlite3_stmt* st= nullptr;
  ~Statement () { if (st) sqlite3_finalize (st); }
};

std::string to_std (string s) {
  return std::string (as_charp (s), (size_t) N(s));
}

string to_tm (const std::string& s) { return string (s.data (), (int) s.size ()); }

void clear_all_metadata (tree& value) {
  if (athena::node::get (value)) athena::node::clear (value);
  if (!is_compound (value)) return;
  for (int i=0; i<N(value); ++i) clear_all_metadata (value[i]);
}

tree artifact_plain_tree (const tree& value) {
  tree result= copy (value);
  clear_all_metadata (result);
  return result;
}

std::string fragment_bytes (const tree& value) {
  return athena::document::write_xml (
    artifact_plain_tree (value), athena::document::xml_kind::fragment);
}

tree fragment_tree (const std::string& bytes) {
  return athena::document::read_xml (bytes, athena::document::xml_kind::fragment);
}

QString qstr (const std::string& s) {
  return QString::fromUtf8 (s.data (), (qsizetype) s.size ());
}

std::string encode_opaque (const std::string& value) {
  QByteArray encoded= QByteArray (value.data (), (qsizetype) value.size ())
                        .toBase64 (QByteArray::Base64Encoding);
  return "base64-v1:" +
         std::string (encoded.constData (), (size_t) encoded.size ());
}

std::string decode_opaque (const std::string& value) {
  constexpr const char* prefix= "base64-v1:";
  if (value.rfind (prefix, 0) != 0) return value;
  QByteArray encoded (value.data () + std::char_traits<char>::length (prefix),
                      (qsizetype) (value.size () -
                                   std::char_traits<char>::length (prefix)));
  QByteArray decoded= QByteArray::fromBase64 (
    encoded, QByteArray::AbortOnBase64DecodingErrors);
  return std::string (decoded.constData (), (size_t) decoded.size ());
}

std::string tag_name (const tree& t) {
  return is_compound (t) ? to_std (as_string (L(t))) : std::string ();
}

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
                     bool read_only= false) {
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

std::string collapse_spaces (const std::string& value) {
  std::string out;
  bool pending= false;
  for (unsigned char c: value) {
    if (std::isspace (c)) { pending= !out.empty (); continue; }
    if (pending) out.push_back (' ');
    pending= false;
    out.push_back ((char) c);
  }
  return out;
}

bool has_name_bearing_text (const std::string& value) {
  const QString text= qstr (value);
  for (QChar character: text)
    if (character.isLetter ()) return true;
  return false;
}

bool path_in_configured_subtree (const fs::path& root, const fs::path& path,
                                 const std::string& configured) {
  fs::path subtree= fs::path (configured).lexically_normal ();
  if (subtree.empty () || subtree.is_absolute ()) return false;
  for (const fs::path& part: subtree)
    if (part == "." || part == "..") return false;
  fs::path relative= path.lexically_relative (root);
  auto expected= subtree.begin ();
  auto actual= relative.begin ();
  for (; expected != subtree.end (); ++expected, ++actual)
    if (actual == relative.end () || *actual != *expected) return false;
  return true;
}

bool formatting_wrapper (const std::string& tag) {
  return tag == "with" || tag == "style-with";
}

bool bold_wrapper (const tree& t) {
  std::string tag= tag_name (t);
  if (tag == "strong") return N(t) >= 1;
  if (!formatting_wrapper (tag) || N(t) < 3) return false;
  for (int i=0; i+1<N(t)-1; i += 2) {
    std::string key= is_atomic (t[i]) ? to_std (t[i]->label) : "";
    std::string value= is_atomic (t[i+1]) ? to_std (t[i+1]->label) : "";
    if ((key == "font-series" || key == "fontseries") &&
        (value == "bold" || value == "bold-series")) return true;
  }
  return false;
}

tree visible_body (const tree& t) {
  std::string tag= tag_name (t);
  if (tag == "strong" && N(t) >= 1) return t[0];
  if (formatting_wrapper (tag) && N(t) >= 1) return t[N(t)-1];
  return t;
}

bool nonsemantic_resource_tag (const std::string& tag) {
  return tag == "image" || tag == "include" || tag == "bibliography";
}

std::string plain_text (const tree& t, bool first_line= false) {
  if (is_atomic (t)) return to_std (t->label);
  std::string tag= tag_name (t);
  if (tag == "label" || nonsemantic_resource_tag (tag)) return "";
  if (formatting_wrapper (tag) && N(t) >= 1)
    return plain_text (t[N(t)-1], first_line);
  std::string out;
  int count= first_line && is_func (t, DOCUMENT) ? std::min (1, N(t)) : N(t);
  for (int i=0; i<count; i++) {
    if (first_line && (is_func (t[i], NEXT_LINE, 0) || is_func (t[i], NEW_LINE, 0)))
      break;
    std::string part= plain_text (t[i], first_line);
    if (part.empty ()) continue;
    if (!out.empty ()) out += " ";
    out += part;
  }
  return collapse_spaces (out);
}

bool contains_tag (const tree& t, const std::string& wanted) {
  if (!is_compound (t)) return false;
  if (tag_name (t) == wanted) return true;
  for (int i=0; i<N(t); i++)
    if (contains_tag (t[i], wanted)) return true;
  return false;
}

bool leading_bold_scope (const tree& t, path base, path& scope,
                         std::string* text= nullptr) {
  if (!is_compound (t)) return false;
  if (bold_wrapper (t)) {
    std::string value= plain_text (visible_body (t));
    if (value.empty ()) return false;
    scope= base;
    if (text != nullptr) *text= value;
    return true;
  }
  if (formatting_wrapper (tag_name (t)) && N(t) >= 1)
    return leading_bold_scope (
      t[N(t)-1], base * (N(t)-1), scope, text);
  for (int i=0; i<N(t); i++) {
    if (plain_text (t[i]).empty ()) continue;
    return leading_bold_scope (t[i], base * i, scope, text);
  }
  return false;
}

bool leading_bold_text (const tree& t, std::string& text) {
  path scope;
  return leading_bold_scope (t, path (), scope, &text);
}

void append_title_parts (const tree& t, tree& parts) {
  if (is_func (t, DOCUMENT)) {
    if (N(t) > 0) append_title_parts (t[0], parts);
  }
  else if (is_func (t, CONCAT)) {
    for (int i=0; i<N(t); ++i) {
      if (is_func (t[i], NEXT_LINE, 0) || is_func (t[i], NEW_LINE, 0)) break;
      append_title_parts (t[i], parts);
    }
  }
  else parts << t;
}

std::string definition_name_text (const tree& name) {
  if (!is_func (name, CONCAT)) return plain_text (name);
  std::string text;
  for (int i=0; i<N(name); ++i) text += definition_name_text (name[i]);
  return text;
}

void append_definition_names (const tree& title, AthenaArtifactRecord& record) {
  tree parts (CONCAT);
  append_title_parts (title, parts);
  if (N(parts) > 0 && is_atomic (parts[0]) && is_atomic (parts[N(parts)-1])) {
    QString first= qstr (to_std (parts[0]->label)).trimmed ();
    QString last= qstr (to_std (parts[N(parts)-1]->label)).trimmed ();
    if ((first.startsWith ('(') && last.endsWith (')')) ||
        (first.startsWith (QChar (0xff08)) && last.endsWith (QChar (0xff09)))) {
      auto native= [] (const QString& s) {
        QByteArray bytes= s.toUtf8 ();
        return tree (string (bytes.constData (), bytes.size ()));
      };
      if (N(parts) == 1) parts[0]= native (first.mid (1, first.size ()-2));
      else {
        parts[0]= native (first.mid (1));
        parts[N(parts)-1]= native (last.left (last.size ()-1));
      }
    }
  }
  // Delimit only text; commas inside a mathematical subtree are not aliases.
  std::vector<tree> aliases (1, tree (CONCAT));
  for (int i=0; i<N(parts); ++i) {
    if (!is_atomic (parts[i])) { aliases.back () << parts[i]; continue; }
    QString text= qstr (to_std (parts[i]->label));
    const QStringList pieces= text.split (
      QRegularExpression (QStringLiteral ("[,\\x{ff0c}]")));
    for (qsizetype j=0; j<pieces.size (); ++j) {
      if (j != 0) aliases.emplace_back (CONCAT);
      QByteArray bytes= pieces[j].toUtf8 ();
      aliases.back () << tree (string (bytes.constData (), bytes.size ()));
    }
  }
  for (tree alias: aliases) {
    if (N(alias) == 0) continue;
    if (is_atomic (alias[0])) alias[0]= trim_spaces (alias[0]->label);
    if (is_atomic (alias[N(alias)-1]))
      alias[N(alias)-1]= trim_spaces (alias[N(alias)-1]->label);
    alias= simplify_concat (alias);
    std::string display= collapse_spaces (definition_name_text (alias));
    std::string serialized= fragment_bytes (alias);
    if (display.empty () || std::find (record.semantic_name_trees.begin (),
        record.semantic_name_trees.end (), serialized) != record.semantic_name_trees.end ())
      continue;
    record.semantic_names.push_back (display);
    record.semantic_name_trees.push_back (serialized);
  }
}

void definition_names_in_first_line (
  const tree& t, AthenaArtifactRecord& record) {
  if (!is_compound (t)) return;
  if (bold_wrapper (t)) {
    append_definition_names (visible_body (t), record);
    return;
  }
  if (formatting_wrapper (tag_name (t)) && N(t) >= 1) {
    definition_names_in_first_line (t[N(t)-1], record);
    return;
  }
  if (is_func (t, DOCUMENT)) {
    if (N(t) > 0) definition_names_in_first_line (t[0], record);
    return;
  }
  if (is_func (t, CONCAT))
    for (int i=0; i<N(t); ++i) {
      if (is_func (t[i], NEXT_LINE, 0) || is_func (t[i], NEW_LINE, 0)) break;
      definition_names_in_first_line (t[i], record);
    }
}

std::vector<std::string> semantic_names_for (
  const std::string& origin, const std::string& type,
  const std::string& display_text, const std::string& explicit_title= {}) {
  if (type == "completion") return {};
  QString display= qstr (display_text).simplified ();
  if (display.isEmpty ()) return {};
  if (origin == "bold-text") return {display.toStdString ()};
  if (origin != "enunciation") return {};

  QString title_source= qstr (explicit_title).simplified ();
  if (title_source.isEmpty ()) return {display.toStdString ()};
  QChar opening= title_source.front ();
  QChar closing;
  if (opening == QChar ('(')) closing= QChar (')');
  else if (opening == QChar (0xff08)) closing= QChar (0xff09);
  else return {display.toStdString ()};

  qsizetype close= title_source.indexOf (closing, 1);
  if (close <= 1) return {display.toStdString ()};
  if (close + 1 < title_source.size () &&
      !title_source[close + 1].isSpace ())
    return {display.toStdString ()};
  QString title= title_source.mid (1, close - 1).trimmed ();
  if (title.isEmpty ()) return {display.toStdString ()};

  std::vector<std::string> names= {title.toStdString ()};
  qsizetype comma= title.indexOf (QRegularExpression (QStringLiteral ("[,，]")));
  if (comma > 0) {
    QString leading= title.left (comma).trimmed ();
    if (!leading.isEmpty () && leading != title)
      names.push_back (leading.toStdString ());
  }
  return names;
}

std::string enunciation_type (const std::string& original,
                              std::string& base_tag) {
  const auto& registry= athena::enunciation::standard_registry ();
  const auto* alias= registry.legacy (original);
  if (alias && !alias->artifact_base.empty ()) {
    base_tag= alias->artifact_base;
    const auto* kind= registry.kind (alias->kind);
    return kind ? kind->category : std::string ();
  }
  // Canonical v2 rows store the kind name in the legacy-compatible tag column.
  const auto* kind= registry.kind (original);
  if (!kind) return "";
  base_tag= kind->kind;
  return kind->category;
}

std::string enunciation_type (const tree& source, std::string& base_tag) {
  const auto& registry= athena::enunciation::standard_registry ();
  if (athena::enunciation::is_canonical (source)) {
    const auto* definition= registry.definition (source);
    if (!definition) return "";
    base_tag= definition->kind;
    return definition->category;
  }
  return enunciation_type (tag_name (source), base_tag);
}

std::vector<int> source_path_vector (path value) {
  std::vector<int> result;
  for (; !is_nil (value); value= value->next) result.push_back (value->item);
  return result;
}

void find_shared_source_paths (const tree& source, const tree& target,
                               path where, path& found, unsigned& count) {
  if (inside (source) == inside (target)) {
    if (++count == 1) found= where;
    return;
  }
  if (!is_compound (source)) return;
  for (int i=0; i<N(source); ++i)
    find_shared_source_paths (source[i], target, where * i, found, count);
}

bool ignorable (const tree& t) {
  return is_atomic (t) && collapse_spaces (to_std (t->label)).empty ();
}

std::string label_text (const tree& t) {
  return tag_name (t) == "label" && N(t) >= 1 ? plain_text (t[0]) : "";
}

std::string anchor_stem (std::string value) {
  value= collapse_spaces (value);
  while (!value.empty () &&
         (value.back () == '{' || value.back () == '}' ||
          std::isspace ((unsigned char) value.back ()))) value.pop_back ();
  while (!value.empty () &&
         (value.front () == '{' || value.front () == '}' ||
          std::isspace ((unsigned char) value.front ()))) value.erase (value.begin ());
  return value;
}

tree document_body (const tree& document) {
  if (!is_compound (document)) return document;
  for (int i=0; i<N(document); i++)
    if (tag_name (document[i]) == "body" && N(document[i]) >= 1)
      return document[i][0];
  return document;
}

bool standalone_attachment (const tree& t) {
  static const std::set<std::string> tags= {
    "equation", "equation*", "eqnarray", "eqnarray*", "align", "align*",
    "table", "tabular", "tabular*", "big-figure", "commutative-diagram"
  };
  return tags.count (tag_name (t)) != 0;
}

tree artifact_semantic_tree (const tree& t) {
  if (is_atomic (t)) return t;
  std::string tag= tag_name (t);
  if (nonsemantic_resource_tag (tag)) return tree ("");
  tree out (L(t), N(t));
  for (int i=0; i<N(t); i++) out[i]= artifact_semantic_tree (t[i]);
  return out;
}

std::string latex_for_tree (const tree& t) {
  tree semantic= artifact_semantic_tree (t);
  // Standalone artifact readers and unit tests do not boot Guile.  Their range
  // selectors only need a stable textual representation; the full ATHENA
  // process continues to use the normal LaTeX converter below.
  if (headless_mode)
    return to_std (tree_to_texmacs (semantic));
  try {
    return to_std (as_string (call ("convert", semantic, "texmacs-tree",
                                    "latex-snippet")));
  }
  catch (...) {
    return to_std (tree_to_texmacs (semantic));
  }
}

struct Paragraph {
  tree value;
  path parent;
  int segment= 0;
  int first_child= -1;
  int last_child= -1;
  std::string fingerprint;
};

void find_bold (const tree& t, std::vector<tree>& found) {
  if (!is_compound (t)) return;
  std::string base;
  if (!enunciation_type (t, base).empty ()) return;
  if (bold_wrapper (t)) {
    found.push_back (t);
    return;
  }
  for (int i=0; i<N(t); i++) find_bold (t[i], found);
}

struct ExtractedDocument {
  std::vector<AthenaArtifactRecord> records;
};

std::string serialized_tree (const tree& value) {
  return to_std (tree_to_texmacs (artifact_plain_tree (value)));
}

std::string identity_fingerprint (const std::string& serialized) {
  QByteArray bytes (serialized.data (), (qsizetype) serialized.size ());
  QByteArray digest= QCryptographicHash::hash (
    bytes, QCryptographicHash::Sha256).toHex ();
  return "sha256:" + std::string (digest.constData (),
                                   (size_t) digest.size ());
}

std::string identity_fingerprint (const tree& value) {
  return identity_fingerprint (serialized_tree (value));
}

void strip_artifact_bindings (tree& value) {
  if (auto* metadata= athena::node::edit (value)) {
    metadata->properties.erase (
      athena::document_node::artifact_bindings_property);
    if (metadata->empty ()) athena::node::clear (value);
  }
  if (!is_compound (value)) return;
  for (int i=0; i<N(value); ++i) strip_artifact_bindings (value[i]);
}

// Artifact extraction content is independent of source UUID allocation and of
// producer-maintained role bindings. Other typed properties remain because
// canonical enunciation kind/name/etc. can change artifact semantics.
std::string artifact_content_fingerprint (const tree& document) {
  tree projected= athena::node::content_projection (document);
  strip_artifact_bindings (projected);
  return identity_fingerprint (athena::document::write_xml_v2 (
    projected, athena::document::xml_kind::document));
}

std::string artifact_binding (const tree& source, const std::string& role) {
  const auto* metadata= athena::node::get (source);
  if (!metadata) return {};
  auto found= metadata->properties.find (
    athena::document_node::artifact_bindings_property);
  if (found == metadata->properties.end ()) return {};
  const auto* bindings=
    std::get_if<athena::node::property::dictionary> (&found->second.data);
  if (!bindings) return {};
  auto bound= bindings->find (role);
  if (bound == bindings->end ()) return {};
  const auto* id= std::get_if<std::string> (&bound->second.data);
  return id && athena::node::valid_id (*id) ? *id : std::string ();
}

path native_path (const std::vector<int>& source) {
  path result;
  for (int index: source) result= result * index;
  return result;
}

std::string identity_neighbor (const tree& parent, int start, int step) {
  for (int i=start; i>=0 && i<N(parent); i += step) {
    if (ignorable (parent[i]) || tag_name (parent[i]) == "label") continue;
    return identity_fingerprint (parent[i]);
  }
  return "";
}

void scan_enunciations (const tree& parent, const std::string& rel,
                        std::vector<AthenaArtifactRecord>& out, int& order,
                        path where= path (),
                        std::vector<path>* source_paths= nullptr) {
  if (!is_compound (parent)) return;
  for (int i=0; i<N(parent); i++) {
    const tree& child= parent[i];
    std::string base;
    std::string type= enunciation_type (child, base);
    if (!type.empty ()) {
      std::string display= plain_text (child);
      // Image-only enunciations have no textual semantic identity that can be
      // named, searched, or matched reliably. Leave them out until image
      // understanding becomes part of artifactization.
      if (display.empty () && contains_tag (child, "image")) continue;
      std::string anchor;
      for (int j=i-1; j>=0; j--) {
        if (ignorable (parent[j])) continue;
        anchor= anchor_stem (label_text (parent[j]));
        break;
      }
      if (anchor.empty ()) {
        for (int j=i+1; j<N(parent); j++) {
          if (ignorable (parent[j])) continue;
          anchor= anchor_stem (label_text (parent[j]));
          break;
        }
      }
      AthenaArtifactRecord record;
      record.type= type;
      record.origin= "enunciation";
      record.source_role= "enunciation";
      record.source_path= source_path_vector (where * i);
      record.source_uuid= athena::node::id (child);
      record.relative_path= rel;
      record.anchor_stem= anchor;
      record.display_text= display;
      std::string explicit_title;
      (void) leading_bold_text (child, explicit_title);
      if (type == "definition" && N(child) > 0)
        definition_names_in_first_line (child[N(child)-1], record);
      else
        record.semantic_names= semantic_names_for (
          record.origin, record.type, record.display_text, explicit_title);
      record.keyword_tree= base;
      record.identity_focus= identity_fingerprint (child);
      record.identity_before= identity_neighbor (parent, i - 1, -1);
      record.identity_after= identity_neighbor (parent, i + 1, 1);
      record.document_order= order++;
      if (type == "provable") {
        for (int j=i+1; j<N(parent); j++) {
          if (ignorable (parent[j]) || tag_name (parent[j]) == "label")
            continue;
          std::string next_base;
          if (enunciation_type (parent[j], next_base) ==
              "completion")
            record.proof_uuid= "@order:" + std::to_string (order);
          break;
        }
      }
      out.push_back (record);
      if (source_paths != nullptr) source_paths->push_back (where * i);
      continue;
    }
    scan_enunciations (child, rel, out, order, where * i, source_paths);
  }
}

bool contains_document (const tree& value) {
  if (!is_compound (value)) return false;
  if (is_document (value)) return true;
  for (int i=0; i<N(value); i++)
    if (contains_document (value[i])) return true;
  return false;
}

void collect_paragraphs_in (const tree& value, path where,
                            std::vector<Paragraph>& paragraphs,
                            int& next_segment) {
  if (!is_compound (value)) return;
  if (!is_document (value)) {
    for (int i=0; i<N(value); i++)
      if (contains_document (value[i]))
        collect_paragraphs_in (
          value[i], where * i, paragraphs, next_segment);
    return;
  }

  int segment= next_segment++;
  for (int i=0; i<N(value); i++) {
    const tree& child= value[i];
    std::string base;
    if (!enunciation_type (child, base).empty ()) {
      segment= next_segment++;
      continue;
    }
    if (tag_name (child) == "label" || ignorable (child)) continue;
    if (standalone_attachment (child) && !paragraphs.empty () &&
        paragraphs.back ().segment == segment &&
        paragraphs.back ().parent == where) {
      tree joined (CONCAT);
      joined << paragraphs.back ().value << child;
      paragraphs.back ().value= joined;
      paragraphs.back ().last_child= i;
      continue;
    }
    if (contains_document (child)) {
      collect_paragraphs_in (
        child, where * i, paragraphs, next_segment);
      segment= next_segment++;
      continue;
    }
    paragraphs.push_back ({child, where, segment, i, i, ""});
  }
}

void collect_paragraphs (const tree& body, std::vector<Paragraph>& paragraphs) {
  int next_segment= 0;
  collect_paragraphs_in (body, path (), paragraphs, next_segment);
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

bool valid_definition_offsets (const AthenaArtifactRangeRequest& request,
                               const std::vector<int>& offsets) {
  if (offsets.empty ()) return false;
  if (std::find (offsets.begin (), offsets.end (), 0) == offsets.end () ||
      !std::is_sorted (offsets.begin (), offsets.end ()) ||
      std::adjacent_find (offsets.begin (), offsets.end ()) != offsets.end ())
    return false;
  std::set<int> allowed;
  for (const auto& paragraph: request.paragraphs)
    allowed.insert (paragraph.first);
  for (size_t i=0; i<offsets.size (); i++)
    if (!allowed.count (offsets[i]) ||
        (i > 0 && offsets[i] != offsets[i - 1] + 1))
      return false;
  return true;
}

std::string range_request_hash (const AthenaArtifactRangeRequest& request,
                                const std::string& cache_contract) {
  std::ostringstream canonical;
  canonical << cache_contract.size () << ':' << cache_contract << '\n'
            << request.keyword_latex.size () << ':' << request.keyword_latex
            << '\n';
  for (const auto& paragraph: request.paragraphs)
    canonical << paragraph.first << ':' << paragraph.second.size () << ':'
              << paragraph.second << '\n';
  return identity_fingerprint (canonical.str ());
}

bool load_range_checkpoint (sqlite3* db, const std::string& path,
                             const std::string& input_hash,
                             const AthenaArtifactRangeRequest& request,
                             std::vector<int>& offsets, bool& found,
                             std::string& error) {
  Statement statement;
  if (!prepare (
        db,
        "SELECT paragraph_offsets FROM artifact_range_cache "
        "WHERE path=?1 AND COALESCE(NULLIF(input_hash,''),request_hash)=?2 "
        "ORDER BY updated_at DESC LIMIT 1;",
        statement, error))
    return false;
  bind_text (statement.st, 1, path);
  bind_text (statement.st, 2, input_hash);
  int status= sqlite3_step (statement.st);
  if (status == SQLITE_DONE) { found= false; return true; }
  if (status != SQLITE_ROW) { error= sqlite3_errmsg (db); return false; }
  offsets= parse_offsets (column_text (statement.st, 0));
  found= valid_definition_offsets (request, offsets);
  return true;
}

struct RangeCheckpoint {
  std::string path;
  long long modified= 0;
  long long size= 0;
  // Stored in the legacy semantic_hash column; this is now the artifact
  // extraction content revision (source IDs/bindings excluded).
  std::string content_hash;
  std::string input_hash;
  std::vector<int> offsets;
};

bool store_range_checkpoints (sqlite3* db,
                              const std::vector<RangeCheckpoint>& checkpoints,
                              std::string& error) {
  if (checkpoints.empty ()) return true;
  if (!exec_sql (db, "BEGIN IMMEDIATE;", error)) return false;
  bool committed= false;
  auto rollback= [&] () {
    if (!committed) {
      std::string ignored;
      exec_sql (db, "ROLLBACK;", ignored);
    }
  };
  Statement insert;
  if (!prepare (
        db,
        "INSERT INTO artifact_range_cache(path,mtime_ns,size,semantic_hash,input_hash,request_hash,"
        "paragraph_offsets,updated_at) VALUES(?1,?2,?3,?4,?5,?5,?6,?7) "
        "ON CONFLICT(path,mtime_ns,size,request_hash) DO UPDATE SET "
        "semantic_hash=excluded.semantic_hash,"
        "input_hash=excluded.input_hash,"
        "paragraph_offsets=excluded.paragraph_offsets,"
        "updated_at=excluded.updated_at;",
        insert, error)) {
    rollback ();
    return false;
  }
  long long updated_at= (long long) std::chrono::duration_cast<
    std::chrono::seconds> (
      std::chrono::system_clock::now ().time_since_epoch ()).count ();
  for (const RangeCheckpoint& checkpoint: checkpoints) {
    sqlite3_reset (insert.st);
    sqlite3_clear_bindings (insert.st);
    bind_text (insert.st, 1, checkpoint.path);
    sqlite3_bind_int64 (insert.st, 2, checkpoint.modified);
    sqlite3_bind_int64 (insert.st, 3, checkpoint.size);
    bind_text (insert.st, 4, checkpoint.content_hash);
    bind_text (insert.st, 5, checkpoint.input_hash);
    bind_text (insert.st, 6, offsets_text (checkpoint.offsets));
    sqlite3_bind_int64 (insert.st, 7, updated_at);
    if (sqlite3_step (insert.st) != SQLITE_DONE) {
      error= sqlite3_errmsg (db);
      rollback ();
      return false;
    }
  }
  if (!exec_sql (db, "COMMIT;", error)) { rollback (); return false; }
  committed= true;
  return true;
}

bool extract (const tree& document, const std::string& rel,
              const AthenaArtifactTitleFilter& title_filter,
              ExtractedDocument& extracted, std::string& error,
              bool structural_only= false) {
  tree body= document_body (document);
  if (!is_compound (body)) {
    error= "Document has no structural body";
    return false;
  }
  int order= 0;
  scan_enunciations (body, rel, extracted.records, order);

  std::vector<Paragraph> paragraphs;
  collect_paragraphs (body, paragraphs);
  for (Paragraph& paragraph: paragraphs)
    paragraph.fingerprint= identity_fingerprint (paragraph.value);
  std::unordered_map<std::string,int> occurrences;
  for (size_t paragraph_index=0; paragraph_index<paragraphs.size ();
       paragraph_index++) {
    std::vector<tree> bolds;
    find_bold (paragraphs[paragraph_index].value, bolds);
    for (const tree& keyword: bolds) {
      std::string display= plain_text (visible_body (keyword));
      if (collapse_spaces (display).empty () ||
          !has_name_bearing_text (display) ||
          athena_artifact_title_filter_contains (
            title_filter, visible_body (keyword))) continue;
      std::string serialized= fragment_bytes (keyword);
      int occurrence= ++occurrences[serialized];
      std::vector<std::pair<int,std::string>> candidates;
      for (int offset=-5; !structural_only && offset<=5; offset++) {
        long index= (long) paragraph_index + offset;
        if (index < 0 || index >= (long) paragraphs.size ()) continue;
        if (paragraphs[(size_t) index].segment !=
            paragraphs[paragraph_index].segment) continue;
        candidates.push_back ({offset,
          fragment_bytes (paragraphs[(size_t) index].value)});
      }
      AthenaArtifactRecord record;
      record.type= "definition";
      record.origin= "bold-text";
      record.range_state= AthenaArtifactRangeState::pending;
      record.source_role= "bold-text-definition";
      record.relative_path= rel;
      record.display_text= display;
      record.semantic_names= semantic_names_for (
        record.origin, record.type, record.display_text);
      record.keyword_tree= serialized;
      record.semantic_name_trees= {fragment_bytes (visible_body (keyword))};
      record.keyword_occurrence= occurrence;
      record.definition_candidates= candidates;
      if (!structural_only) {
        QJsonArray input;
        input.append (qstr (serialized));
        for (const auto& candidate: candidates)
          input.append (QJsonArray {candidate.first, qstr (candidate.second)});
        record.range_structure_fingerprint= identity_fingerprint (
          QJsonDocument (input).toJson (QJsonDocument::Compact).toStdString ());
      }
      record.identity_focus= identity_fingerprint (keyword);
      record.identity_host= paragraphs[paragraph_index].fingerprint;
      if (paragraph_index > 0 &&
          paragraphs[paragraph_index - 1].segment ==
            paragraphs[paragraph_index].segment)
        record.identity_before= paragraphs[paragraph_index - 1].fingerprint;
      if (paragraph_index + 1 < paragraphs.size () &&
          paragraphs[paragraph_index + 1].segment ==
            paragraphs[paragraph_index].segment)
        record.identity_after= paragraphs[paragraph_index + 1].fingerprint;
      record.document_order= order++;
      path keyword_path;
      unsigned keyword_occurrences= 0;
      find_shared_source_paths (
        body, keyword, path (), keyword_path, keyword_occurrences);
      if (keyword_occurrences != 1) {
        error= keyword_occurrences == 0 ?
          "Could not retain bold artifact source path" :
          "Bold artifact source occurs more than once in the source tree";
        return false;
      }
      record.source_path= source_path_vector (keyword_path);
      record.source_uuid= athena::node::id (keyword);
      extracted.records.push_back (record);
    }
  }
  return true;
}

QJsonObject record_json (const AthenaArtifactRecord& record) {
  QJsonObject object;
  object["range_state"]= record.range_state == AthenaArtifactRangeState::pending ? "pending" :
    record.range_state == AthenaArtifactRangeState::resolved ? "resolved" :
    record.range_state == AthenaArtifactRangeState::failed ? "failed" : "not-applicable";
  object["range_structure_hash"]= qstr (record.range_structure_fingerprint);
  object["type"]= qstr (record.type);
  object["origin"]= qstr (record.origin);
  object["proof"]= qstr (record.proof_uuid);
  object["path"]= qstr (record.relative_path);
  object["source_uuid"]= qstr (record.source_uuid);
  object["source_role"]= qstr (record.source_role);
  QJsonArray source_path;
  for (int index: record.source_path) source_path.append (index);
  object["source_path"]= source_path;
  object["anchor"]= qstr (record.anchor_stem);
  object["display"]= qstr (record.display_text);
  QJsonArray semantic_names;
  for (const std::string& name: record.semantic_names)
    semantic_names.append (qstr (name));
  object["semantic_names"]= semantic_names;
  QJsonArray name_trees;
  for (const std::string& name: record.semantic_name_trees)
    name_trees.append (qstr (encode_opaque (name)));
  object["semantic_name_trees"]= name_trees;
  object["keyword"]= qstr (encode_opaque (record.keyword_tree));
  object["occurrence"]= record.keyword_occurrence;
  object["order"]= record.document_order;
  object["identity_focus"]= qstr (record.identity_focus);
  object["identity_host"]= qstr (record.identity_host);
  object["identity_before"]= qstr (record.identity_before);
  object["identity_after"]= qstr (record.identity_after);
  object["input_fingerprint"]= qstr (record.input_fingerprint);
  object["keyword_latex"]= qstr (record.keyword_latex);
  QJsonArray candidates;
  for (const auto& candidate: record.definition_candidates) {
    QJsonObject item;
    item["offset"]= candidate.first;
    item["source"]= qstr (encode_opaque (candidate.second));
    candidates.append (item);
  }
  object["candidates"]= candidates;
  return object;
}

AthenaArtifactRecord record_from_json (const QJsonObject& object) {
  AthenaArtifactRecord record;
  auto s= [&] (const char* key) {
    QByteArray value= object.value (key).toString ().toUtf8 ();
    return std::string (value.constData (), (size_t) value.size ());
  };
  record.type= s ("type");
  record.origin= s ("origin");
  if (record.origin == "bold-text") record.range_state= AthenaArtifactRangeState::pending;
  if (s ("range_state") == "resolved") record.range_state= AthenaArtifactRangeState::resolved;
  if (s ("range_state") == "failed") record.range_state= AthenaArtifactRangeState::failed;
  record.range_structure_fingerprint= s ("range_structure_hash");
  record.proof_uuid= s ("proof");
  record.relative_path= s ("path");
  record.source_uuid= s ("source_uuid");
  record.source_role= s ("source_role");
  for (const QJsonValue& index: object.value ("source_path").toArray ())
    record.source_path.push_back (index.toInt ());
  record.anchor_stem= s ("anchor");
  record.display_text= s ("display");
  for (const QJsonValue& value: object.value ("semantic_names").toArray ()) {
    QByteArray name= value.toString ().toUtf8 ();
    record.semantic_names.emplace_back (name.constData (), (size_t) name.size ());
  }
  record.keyword_tree= decode_opaque (s ("keyword"));
  for (const QJsonValue& value: object.value ("semantic_name_trees").toArray ())
    record.semantic_name_trees.push_back (decode_opaque (value.toString ().toStdString ()));
  record.keyword_occurrence= object.value ("occurrence").toInt ();
  record.document_order= object.value ("order").toInt ();
  record.identity_focus= s ("identity_focus");
  record.identity_host= s ("identity_host");
  record.identity_before= s ("identity_before");
  record.identity_after= s ("identity_after");
  record.input_fingerprint= s ("input_fingerprint");
  record.keyword_latex= s ("keyword_latex");
  for (const QJsonValue& value: object.value ("candidates").toArray ()) {
    QJsonObject item= value.toObject ();
    QByteArray source= item.value ("source").toString ().toUtf8 ();
    record.definition_candidates.push_back (
      {item.value ("offset").toInt (),
       decode_opaque (
         std::string (source.constData (), (size_t) source.size ())) });
  }
  return record;
}

struct DocumentWork {
  fs::path path;
  std::string rel;
  long long modified= 0;
  long long size= 0;
  std::string storage_hash;
  std::string content_hash;
  std::string semantic_hash;
  athena::document::document_source_format source_format=
    athena::document::document_source_format::xml_v1;
};

bool read_document (const fs::path& path, tree& document, std::string& error,
                    athena::document::document_source_format* format= nullptr,
                    std::string* storage_hash= nullptr);

bool extract_serial (const std::vector<DocumentWork>& work,
                     const AthenaArtifactTitleFilter& title_filter,
                     std::map<std::string,ExtractedDocument>& extracted,
                     const AthenaArtifactsProgress& progress,
                     std::string& error) {
  for (size_t i=0; i<work.size (); i++) {
    if (!report_progress (progress, AthenaArtifactsBuildPhase::Extracting,
                          i, work.size (), work[i].rel)) {
      error= "Artifact build cancelled";
      return false;
    }
    tree document;
    if (!read_document (work[i].path, document, error)) return false;
    if (!extract (document, work[i].rel, title_filter,
                  extracted[work[i].rel], error))
      return false;
    artifact_log (progress, "extracted " + work[i].rel + ": " +
                  std::to_string (extracted[work[i].rel].records.size ()) +
                  " artifact candidate(s)");
  }
  return true;
}

bool select_definition_ranges (
  sqlite3* db, const std::vector<DocumentWork>& documents,
  std::map<std::string,ExtractedDocument>& extracted,
  const AthenaArtifactsProgress& progress, std::string& error,
  const AthenaArtifactRangeSelector& selector= {}, bool allow_inference= true) {
  struct ReleaseRangeModel {
    bool enabled;
    ~ReleaseRangeModel () { if (enabled) athena_artifact_range_model_release (); }
  } release_range_model {allow_inference};
  struct RangeWork {
    AthenaArtifactRecord* record;
    std::string path;
    long long modified;
    long long size;
    std::string content_hash;
    AthenaArtifactRangeRequest request;
    std::string input_hash;
  };
  std::unordered_map<std::string,const DocumentWork*> metadata;
  for (const DocumentWork& document: documents)
    metadata[document.rel]= &document;
  std::string model_path= athena_artifact_range_model_path ();
  std::string cache_contract=
    athena_artifact_definition_range_cache_contract (model_path);
  std::vector<RangeWork> work;
  for (auto& document: extracted) {
    auto document_metadata= metadata.find (document.first);
    if (document_metadata == metadata.end ()) {
      error= "Artifact range selection has no source metadata for " +
             document.first;
      return false;
    }
    for (AthenaArtifactRecord& record: document.second.records) {
      if (record.origin != "bold-text") continue;
      AthenaArtifactRangeRequest request;
      request.keyword_latex= latex_for_tree (
        fragment_tree (record.keyword_tree));
      record.keyword_latex= request.keyword_latex;
      request.paragraphs.reserve (record.definition_candidates.size ());
      for (const auto& candidate: record.definition_candidates)
        request.paragraphs.push_back (
          {candidate.first, latex_for_tree (
                              fragment_tree (candidate.second))});
      const DocumentWork& source= *document_metadata->second;
      work.push_back ({&record, document.first, source.modified, source.size,
                        source.content_hash, std::move (request), {}});
      work.back ().input_hash= range_request_hash (
        work.back ().request, cache_contract);
      record.input_fingerprint= work.back ().input_hash;
    }
  }
  size_t range_total= work.size ();
  artifact_log (progress, "definition-range phase: " + std::to_string (range_total) +
                " bold-text artifact(s) require semantic range selection");
  if (range_total == 0) {
    if (!report_progress (
          progress, AthenaArtifactsBuildPhase::SelectingDefinitionRanges,
          1, 1)) {
      error= "Artifact build cancelled";
      return false;
    }
    return true;
  }

  std::vector<std::vector<int>> selected (range_total);
  std::vector<bool> resolved (range_total, false);
  std::vector<size_t> missing;
  size_t cached= 0;
  for (size_t index=0; index<work.size (); index++) {
    if (!report_progress (
          progress, AthenaArtifactsBuildPhase::SelectingDefinitionRanges,
          cached, range_total, work[index].path,
          work[index].record->display_text)) {
      error= "Artifact build cancelled";
      return false;
    }
    bool found= false;
    if (db && !load_range_checkpoint (
                db, work[index].path, work[index].input_hash,
                work[index].request, selected[index],
                found, error))
      return false;
    if (found) { cached++; resolved[index]= true; }
    else missing.push_back (index);
    artifact_log (progress, "queued definition range " +
                  std::to_string (index + 1) + "/" +
                  std::to_string (range_total) + " in " + work[index].path +
                  ": \"" + work[index].record->display_text + "\" (" +
                  std::to_string (work[index].request.paragraphs.size ()) +
                  " candidate paragraph(s)" +
                  (found ? ", checkpoint hit)" : ")"));
  }
  artifact_log (progress, "definition-range incremental plan: " +
                std::to_string (cached) + " checkpoint hit(s), " +
                std::to_string (missing.size ()) + " request(s) to evaluate");

  if (!allow_inference || (!selector && !athena_artifact_range_model_available (model_path))) {
    missing.clear ();
  }

  auto started= std::chrono::steady_clock::now ();
  for (size_t base=0; base<missing.size (); base += range_checkpoint_batch_size) {
    size_t count= std::min (range_checkpoint_batch_size,
                           missing.size () - base);
    std::vector<AthenaArtifactRangeRequest> requests;
    requests.reserve (count);
    for (size_t i=0; i<count; i++)
      requests.push_back (work[missing[base + i]].request);
    std::vector<std::vector<int>> chunk_selected;
    auto update= [&] (size_t current, size_t, size_t queued, size_t running) {
      size_t local= std::min (current, count);
      size_t detail_index= missing[base + std::min (local, count - 1)];
      return report_progress (
        progress, AthenaArtifactsBuildPhase::SelectingDefinitionRanges,
        cached + base + local, range_total, work[detail_index].path,
        work[detail_index].record->display_text, queued, running);
    };
    if (selector) {
      if (!selector (requests, chunk_selected, update, error)) return false;
    }
    else {
      std::atomic<bool> cancelled (false);
      std::atomic<size_t> completed (0);
      std::future<std::vector<std::vector<int>>> inference= std::async (
        std::launch::async,
        [requests, model_path, &cancelled, &completed] () {
          return athena_artifact_select_definition_ranges (
            requests, model_path, &cancelled, &completed);
        });
      while (inference.wait_for (std::chrono::milliseconds (40)) !=
             std::future_status::ready)
        if (!update (std::min (completed.load (), count), count, 0, 0))
          cancelled.store (true);
      try { chunk_selected= inference.get (); }
      catch (const std::exception& exception) {
        error= std::string ("Artifact range inference failed: ") +
               exception.what ();
        return false;
      }
      if (cancelled.load ()) {
        error= "Artifact build cancelled";
        return false;
      }
    }
    if (chunk_selected.size () != count) {
      error= "Artifact range inference returned an incomplete result";
      return false;
    }
    std::vector<RangeCheckpoint> checkpoints;
    checkpoints.reserve (count);
    for (size_t i=0; i<count; i++) {
      size_t index= missing[base + i];
      if (!valid_definition_offsets (work[index].request, chunk_selected[i])) {
        error= "Artifact range inference returned invalid offsets";
        return false;
      }
      selected[index]= std::move (chunk_selected[i]);
      resolved[index]= true;
      checkpoints.push_back ({work[index].path, work[index].modified,
                               work[index].size, work[index].content_hash,
                               work[index].input_hash,
                               selected[index]});
    }
    if (db && !store_range_checkpoints (db, checkpoints, error)) return false;
    artifact_log (progress, "definition-range checkpoint committed: completed=" +
                  std::to_string (cached + base + count) + "/" +
                  std::to_string (range_total));
  }
  if (selected.size () != work.size ()) {
    error= "Artifact range inference returned an incomplete result";
    return false;
  }
  for (size_t index=0; index<work.size (); index++) {
    AthenaArtifactRecord& record= *work[index].record;
    record.paragraph_offsets= std::move (selected[index]);
    record.range_state= resolved[index] ? AthenaArtifactRangeState::resolved :
                                        AthenaArtifactRangeState::pending;
    std::ostringstream offsets;
    for (size_t i=0; i<record.paragraph_offsets.size (); i++) {
      if (i) offsets << ',';
      offsets << record.paragraph_offsets[i];
    }
    artifact_log (progress, std::string (resolved[index] ? "definition range selected for \"" :
                                               "definition range pending for \"") + record.display_text +
                  "\" in " + work[index].path + ": [" + offsets.str () +
                  "]");
  }
  auto elapsed= std::chrono::duration_cast<std::chrono::milliseconds> (
    std::chrono::steady_clock::now () - started).count ();
  artifact_log (progress, "definition-range phase complete: " +
                std::to_string (range_total) + " request(s) in " +
                std::to_string (elapsed) + " ms using batch size " +
                std::to_string (athena_artifact_range_batch_size ()));
  if (!report_progress (
        progress, AthenaArtifactsBuildPhase::SelectingDefinitionRanges,
        range_total, range_total, work.back ().path,
        work.back ().record->display_text)) {
    error= "Artifact build cancelled";
    return false;
  }
  return true;
}

#if defined(__unix__) || defined(__APPLE__)
fs::path current_executable_path () {
#if defined(__linux__)
  std::vector<char> buffer (4096);
  ssize_t size= readlink ("/proc/self/exe", buffer.data (), buffer.size () - 1);
  if (size > 0) {
    buffer[(size_t) size]= '\0';
    return fs::path (buffer.data ());
  }
#endif
  return {};
}

pid_t start_extract_worker (const fs::path& executable,
                            const fs::path& manifest,
                            const fs::path& output) {
  std::string exe= executable.string ();
  std::string input= manifest.string ();
  std::string result= output.string ();
  pid_t pid= fork ();
  if (pid != 0) return pid;
  execl (exe.c_str (), exe.c_str (), "-H", "--skip-fonts-cache",
         "--artifact-extract-worker", input.c_str (), result.c_str (),
         (char*) nullptr);
  _exit (127);
}

bool extract_parallel (sqlite3* db, const std::vector<DocumentWork>& work,
                       const AthenaArtifactTitleFilter& title_filter,
                       std::map<std::string,ExtractedDocument>& extracted,
                       const AthenaArtifactsProgress& progress,
                       const AthenaArtifactRangeSelector& selector,
                       std::string& error) {
  if (work.size () < 2) {
    if (!extract_serial (work, title_filter, extracted, progress, error))
      return false;
    return select_definition_ranges (
      db, work, extracted, progress, error, selector);
  }
  const char* configured= std::getenv ("ATHENA_ARTIFACT_WORKER_EXECUTABLE");
  fs::path executable= configured && *configured ? fs::path (configured)
                                                  : current_executable_path ();
  if (executable.empty () || !fs::exists (executable)) {
    if (!extract_serial (work, title_filter, extracted, progress, error))
      return false;
    return select_definition_ranges (
      db, work, extracted, progress, error, selector);
  }
  unsigned hardware= std::max (1u, std::thread::hardware_concurrency ());
  int jobs= (int) std::min<size_t> (work.size (), hardware);
  fs::path temp= fs::temp_directory_path () /
    ("athena-artifact-workers-" + generate_uuid_v4 ());
  std::error_code ec;
  fs::create_directories (temp, ec);
  if (ec) { error= "Could not create artifact worker directory"; return false; }
  std::vector<pid_t> pids;
  for (int worker=0; worker<jobs; worker++) {
    QJsonArray documents;
    for (size_t index=(size_t) worker; index<work.size (); index += jobs) {
      QJsonObject item;
      item["file"]= qstr (work[index].path.string ());
      item["path"]= qstr (work[index].rel);
      documents.append (item);
    }
    fs::path manifest= temp / (std::to_string (worker) + ".manifest.json");
    fs::path output= temp / (std::to_string (worker) + ".result.json");
    QJsonArray filtered_names;
    for (const std::string& entry: title_filter.entries)
      filtered_names.append (qstr (entry));
    QJsonObject manifest_root;
    manifest_root["title_filter"]= filtered_names;
    QJsonArray structured_names;
    for (const auto& entry: title_filter.structured_entries)
      structured_names.append (qstr (entry));
    manifest_root["structured_title_filter"]= structured_names;
    manifest_root["documents"]= documents;
    QByteArray bytes= QJsonDocument (manifest_root)
                        .toJson (QJsonDocument::Compact);
    std::ofstream stream (manifest, std::ios::binary | std::ios::trunc);
    stream.write (bytes.constData (), bytes.size ());
    stream.close ();
    if (!stream.good ()) {
      error= "Could not write artifact worker manifest";
      break;
    }
    pid_t pid= start_extract_worker (executable, manifest, output);
    if (pid < 0) { error= "Could not start artifact reader process"; break; }
    pids.push_back (pid);
  }
  bool ok= error.empty ();
  size_t completed= 0;
  for (size_t worker=0; worker<pids.size (); worker++) {
    int status= 0;
    while (waitpid (pids[worker], &status, 0) < 0 && errno == EINTR) {}
    if (!WIFEXITED (status) || WEXITSTATUS (status) != 0) ok= false;
    std::ifstream input (temp / (std::to_string (worker) + ".result.json"),
                         std::ios::binary);
    std::string bytes ((std::istreambuf_iterator<char> (input)), {});
    QJsonDocument json= QJsonDocument::fromJson (
      QByteArray (bytes.data (), (qsizetype) bytes.size ()));
    QJsonObject root= json.object ();
    std::string child_error= root.value ("error").toString ().toStdString ();
    if (!child_error.empty ()) { error= child_error; ok= false; }
    for (const QJsonValue& value: root.value ("documents").toArray ()) {
      QJsonObject item= value.toObject ();
      std::string rel= item.value ("path").toString ().toStdString ();
      ExtractedDocument result;
      for (const QJsonValue& record: item.value ("records").toArray ())
        result.records.push_back (record_from_json (record.toObject ()));
      size_t record_count= result.records.size ();
      extracted[rel]= std::move (result);
      completed++;
      artifact_log (progress, "extracted " + rel + ": " +
                    std::to_string (record_count) +
                    " artifact candidate(s)");
      if (!report_progress (progress, AthenaArtifactsBuildPhase::Extracting,
                            completed, work.size (), rel)) {
        error= "Artifact build cancelled"; ok= false;
      }
    }
  }
  fs::remove_all (temp, ec);
  if (!ok && error.empty ()) error= "An artifact reader process failed";
  if (!ok) return false;
  // The parent owns one model instance and performs all semantic range choices.
  return select_definition_ranges (
    db, work, extracted, progress, error, selector);
}
#endif

bool extract_documents (sqlite3* db, const std::vector<DocumentWork>& work,
                        const AthenaArtifactTitleFilter& title_filter,
                        std::map<std::string,ExtractedDocument>& extracted,
                        const AthenaArtifactsProgress& progress,
                        const AthenaArtifactRangeSelector& selector,
                        std::string& error) {
#if defined(__unix__) || defined(__APPLE__)
  return extract_parallel (db, work, title_filter, extracted, progress,
                           selector, error);
#else
  if (!extract_serial (work, title_filter, extracted, progress, error))
    return false;
  return select_definition_ranges (
    db, work, extracted, progress, error, selector);
#endif
}

long long mtime_ns (const fs::path& path) {
  return (long long) fs::last_write_time (path).time_since_epoch ().count ();
}

std::string relative_key (const fs::path& root, const fs::path& path) {
  std::error_code ec;
  fs::path rel= fs::relative (path, root, ec);
  return ec ? path.filename ().generic_string () : rel.generic_string ();
}

bool read_document (const fs::path& path, tree& document, std::string& error,
                    athena::document::document_source_format* format,
                    std::string* storage_hash) {
  std::string bytes;
  if (!read_file_bytes (path, bytes)) {
    error= "Could not read " + path.string ();
    return false;
  }
  try {
    auto decoded= athena::document::decode_document_bytes (bytes, path);
    if (format) *format= decoded.format;
    if (storage_hash)
      *storage_hash= athena::document::storage_bytes_fingerprint (bytes);
    document= std::move (decoded.document);
  }
  catch (const std::exception& exception) {
    error= "Could not parse " + path.string () + ": " + exception.what ();
    return false;
  }
  catch (...) { error= "Could not parse " + path.string (); return false; }
  if (is_func (document, _ERROR)) {
    error= "Malformed ATHENA document: " + path.string ();
    return false;
  }
  return true;
}

std::map<std::string,std::string> existing_ids (
  sqlite3* db, const char* sql, const std::string& path, std::string& error) {
  std::map<std::string,std::string> out;
  Statement st;
  if (!prepare (db, sql, st, error)) return out;
  bind_text (st.st, 1, path);
  int rc;
  while ((rc= sqlite3_step (st.st)) == SQLITE_ROW)
    out[column_text (st.st, 0)]= column_text (st.st, 1);
  if (rc != SQLITE_DONE) error= sqlite3_errmsg (db);
  return out;
}

bool load_identity_observations (
  sqlite3* db, const std::string& path,
  std::vector<AthenaArtifactIdentityObservation>& observations,
  std::string& error) {
  Statement enunciations;
  if (!prepare (
        db,
        "SELECT uuid,tag,anchor_stem,display_text,document_order,"
        "identity_focus,identity_host,identity_before,identity_after "
        "FROM enunciations.entries WHERE path=?1 ORDER BY document_order;",
        enunciations, error))
    return false;
  bind_text (enunciations.st, 1, path);
  int rc;
  while ((rc= sqlite3_step (enunciations.st)) == SQLITE_ROW) {
    AthenaArtifactIdentityObservation observation;
    observation.uuid= column_text (enunciations.st, 0);
    std::string base;
    observation.type= enunciation_type (column_text (enunciations.st, 1), base);
    observation.origin= "enunciation";
    observation.anchor= column_text (enunciations.st, 2);
    observation.display= column_text (enunciations.st, 3);
    observation.document_order= sqlite3_column_int (enunciations.st, 4);
    observation.focus= column_text (enunciations.st, 5);
    observation.host= column_text (enunciations.st, 6);
    observation.before= column_text (enunciations.st, 7);
    observation.after= column_text (enunciations.st, 8);
    observations.push_back (std::move (observation));
  }
  if (rc != SQLITE_DONE) {
    error= sqlite3_errmsg (db);
    return false;
  }

  Statement bold;
  if (!prepare (
        db,
        "SELECT uuid,keyword_tree,keyword_display,document_order,"
        "identity_focus,identity_host,identity_before,identity_after "
        "FROM bold_text.entries WHERE path=?1 ORDER BY document_order;",
        bold, error))
    return false;
  bind_text (bold.st, 1, path);
  while ((rc= sqlite3_step (bold.st)) == SQLITE_ROW) {
    AthenaArtifactIdentityObservation observation;
    observation.uuid= column_text (bold.st, 0);
    observation.origin= "bold-text";
    observation.type= "definition";
    std::string keyword= decode_opaque (column_text (bold.st, 1));
    observation.display= column_text (bold.st, 2);
    observation.document_order= sqlite3_column_int (bold.st, 3);
    observation.focus= column_text (bold.st, 4);
    if (observation.focus.empty ())
      observation.focus= identity_fingerprint (fragment_tree (keyword));
    observation.host= column_text (bold.st, 5);
    observation.before= column_text (bold.st, 6);
    observation.after= column_text (bold.st, 7);
    observations.push_back (std::move (observation));
  }
  if (rc != SQLITE_DONE) {
    error= sqlite3_errmsg (db);
    return false;
  }
  return true;
}

AthenaArtifactIdentityObservation identity_observation (
  const AthenaArtifactRecord& record) {
  AthenaArtifactIdentityObservation observation;
  observation.origin= record.origin;
  observation.type= record.type;
  observation.anchor= record.anchor_stem;
  observation.focus= record.identity_focus;
  observation.host= record.identity_host;
  observation.before= record.identity_before;
  observation.after= record.identity_after;
  observation.display= record.display_text;
  observation.document_order= record.document_order;
  return observation;
}

struct ArtifactBindingAssignment {
  size_t record_index= 0;
  athena::document_node::source_path where;
  std::string role;
  std::string artifact_uuid;
  std::string source_uuid;
};

tree& source_at (tree& root, const athena::document_node::source_path& where) {
  tree* current= &root;
  for (int index: where) {
    if (!is_compound (*current) || index < 0 || index >= N(*current))
      throw std::invalid_argument ("Artifact source path no longer exists");
    current= &(*current)[index];
  }
  return *current;
}

std::string binding_diagnostic (
    const athena::document_node::prepared_property_edit& prepared) {
  if (prepared.diagnostics.empty ()) return "Artifact binding failed";
  const auto& problem= prepared.diagnostics.front ();
  return problem.property.empty () ? problem.detail :
    problem.property + ": " + problem.detail;
}

bool apply_binding_assignments (
    tree& document, std::vector<ArtifactBindingAssignment>& assignments,
    std::string& error) {
  try {
    tree body= document_body (document);
    for (ArtifactBindingAssignment& assignment: assignments) {
      auto prepared= athena::document_node::prepare_artifact_binding (
        body, assignment.where, assignment.role, assignment.artifact_uuid,
        assignment.source_uuid);
      if (!prepared.ok ()) {
        error= binding_diagnostic (prepared);
        return false;
      }
      if (prepared.change) ::apply (body, *prepared.change);
      assignment.source_uuid= prepared.id;
    }
    return true;
  }
  catch (const std::exception& failure) {
    error= failure.what ();
    return false;
  }
}

bool apply_bindings_to_open_buffer (
    const fs::path& path, std::vector<ArtifactBindingAssignment>& assignments,
    std::string& error) {
  const string system_name= as_string (url_system (to_tm (path.string ())));
  const auto endpoint= published_buffer_source (to_std (system_name));
  if (endpoint.first == ATHENA_NO_ACTOR) return false;
  const url name= url_system (to_tm (path.string ()));
  if (buffer_modified (name)) {
    error= "Save the open document before rebuilding artifacts that need source bindings: " +
           path.string ();
    return true;
  }

  struct Response { bool ok= false; std::string error; };
  auto response= std::make_shared<Response> ();
  auto continuation= actor_continuation_registry::instance ().store (
    [response, assignments_ptr= &assignments, view= endpoint.second] {
      try {
        auto* owner= current_scheme_execution_context ()->actor;
        if (!owner) throw std::runtime_error ("Artifact binding has no BufferActor owner");
        tree& source= owner->current_source (view);
        tree live_body= document_body (source);

        // Preflight the complete batch on an owner-local copy, using the exact
        // UUIDs chosen from the saved source/legacy association.
        tree staged= copy (live_body);
        for (const ArtifactBindingAssignment& assignment: *assignments_ptr) {
          auto prepared= athena::document_node::prepare_artifact_binding (
            staged, assignment.where, assignment.role, assignment.artifact_uuid,
            assignment.source_uuid);
          if (!prepared.ok ())
            throw std::invalid_argument (binding_diagnostic (prepared));
          if (prepared.change) ::apply (staged, *prepared.change);
        }
        for (const ArtifactBindingAssignment& assignment: *assignments_ptr) {
          auto prepared= athena::document_node::prepare_artifact_binding (
            live_body, assignment.where, assignment.role,
            assignment.artifact_uuid, assignment.source_uuid);
          if (!prepared.ok ())
            throw std::invalid_argument (binding_diagnostic (prepared));
          if (prepared.change) ::apply (live_body, *prepared.change);
        }
        owner->commit_current_source (view);
        response->ok= true;
      }
      catch (const std::exception& failure) { response->error= failure.what (); }
      catch (const string& failure) { response->error= to_std (failure); }
      catch (...) { response->error= "Artifact source binding failed"; }
    });
  if (!buffer_actor::invoke_on (
        endpoint.first, actor_command_kind::run_native_continuation,
        endpoint.second, ATHENA_NO_BLOB, ATHENA_NO_BLOB, nullptr,
        SCHEME_CAPABILITY_BUFFER, continuation)) {
    actor_continuation_registry::instance ().discard (continuation);
    error= "Open document stopped accepting artifact binding work";
    return true;
  }
  if (!response->ok) {
    error= response->error.empty () ? "Artifact source binding failed" :
                                      response->error;
    return true;
  }
  if (buffer_save (name)) {
    error= "Could not persist artifact source bindings in " + path.string ();
    return true;
  }
  return true;
}

bool choose_and_persist_native_bindings (
    sqlite3* db, const fs::path& vault_root, DocumentWork& work,
    ExtractedDocument& extracted, std::string& error,
    const AthenaArtifactsBuildOptions& options) {
  if (work.source_format != athena::document::document_source_format::xml_v2)
    return true;

  tree document;
  athena::document::document_source_format format;
  std::string current_storage;
  if (!read_document (
        work.path, document, error, &format, &current_storage)) return false;
  if (!options.saved_sha256.empty () && current_storage != options.saved_sha256) {
    error= "Deferred: saved artifact revision was superseded";
    return false;
  }
  if (format != athena::document::document_source_format::xml_v2) {
    error= "Artifact source changed persistence format during build: " + work.rel;
    return false;
  }
  if (current_storage != work.storage_hash) {
    error= "Deferred: artifact source changed during build: " + work.rel;
    return false;
  }
  if (artifact_content_fingerprint (document) != work.content_hash) {
    error= "Artifact source content changed during build: " + work.rel;
    return false;
  }
  if (!options.saved_sha256.empty ()) {
    tree body= document_body (document);
    for (auto& record: extracted.records) {
      tree& source= source_at (body, record.source_path);
      record.uuid= artifact_binding (source, record.source_role);
      record.source_uuid= record.content_uuid= athena::node::id (source);
      if (record.uuid.empty () || record.source_uuid.empty ()) {
        error= "Deferred: saved source has no artifact binding"; return false;
      }
      record.identity_decision= "source-binding";
      if (record.origin == "bold-text" && record.range_state == AthenaArtifactRangeState::resolved &&
          !freeze_paragraph_sources (document, record, error)) return false;
    }
    return true;
  }

  std::vector<AthenaArtifactIdentityObservation> old_observations;
  if (!load_identity_observations (db, work.rel, old_observations, error))
    return false;
  std::vector<AthenaArtifactIdentityObservation> observations;
  observations.reserve (extracted.records.size ());
  for (const auto& record: extracted.records)
    observations.push_back (identity_observation (record));
  const auto identity=
    athena_artifact_associate_identities (old_observations, observations);
  auto old_artifact_ids= existing_ids (
    db, "SELECT origin||char(31)||content_uuid,uuid FROM artifacts WHERE path=?1;",
    work.rel, error);
  if (!error.empty ()) return false;

  tree body= document_body (document);
  std::vector<ArtifactBindingAssignment> assignments;
  assignments.reserve (extracted.records.size ());
  for (size_t i=0; i<extracted.records.size (); ++i) {
    auto& record= extracted.records[i];
    if (record.source_role.empty () || record.source_path.empty ()) {
      error= "XML v2 artifact has no source role/path: " + work.rel;
      return false;
    }
    tree& source= source_at (body, record.source_path);
    std::string source_uuid= athena::node::id (source);
    std::string artifact_uuid= artifact_binding (source, record.source_role);
    const auto& decision= identity.decisions[i];
    if (artifact_uuid.empty () &&
        decision.kind == AthenaArtifactIdentityDecisionKind::Matched &&
        decision.old_index >= 0) {
      const auto& old= old_observations[(size_t) decision.old_index];
      auto found= old_artifact_ids.find (
        old.origin + char (31) + old.uuid);
      if (found != old_artifact_ids.end ()) artifact_uuid= found->second;
    }
    if (artifact_uuid.empty ()) artifact_uuid= generate_uuid_v4 ();
    assignments.push_back (
      {i, record.source_path, record.source_role, artifact_uuid, source_uuid});
    record.uuid= artifact_uuid;
    record.identity_decision= !artifact_binding (source, record.source_role).empty () ?
      "source-binding" :
      (decision.kind == AthenaArtifactIdentityDecisionKind::Matched ?
       "adopted-source-binding" : "new-source-binding");
    record.identity_evidence= decision.evidence;
  }

  // First assign any missing source UUIDs on the detached snapshot. The same
  // exact IDs are replayed if a live unmodified buffer owns the document.
  tree staged= copy (document);
  if (!apply_binding_assignments (staged, assignments, error)) return false;
  if (artifact_content_fingerprint (staged) != work.content_hash) {
    error= "Artifact identity binding changed extraction content for " + work.rel;
    return false;
  }
  for (const auto& assignment: assignments) {
    auto& record= extracted.records[assignment.record_index];
    record.source_uuid= assignment.source_uuid;
    record.content_uuid= assignment.source_uuid;
  }

  const string system_name= as_string (url_system (to_tm (work.path.string ())));
  const bool open=
    published_buffer_source (to_std (system_name)).first != ATHENA_NO_ACTOR;
  if (open) {
    if (options.closed_sources_only) {
      error= "Deferred: artifact source is open"; return false;
    }
    if (!apply_bindings_to_open_buffer (work.path, assignments, error)) {
      error= "Open artifact source disappeared during binding: " + work.rel;
      return false;
    }
    if (!error.empty ()) return false;
  }
  else {
    try {
      if (options.closed_sources_only) {
        athena::filesystem::confined_root root (vault_root);
        auto source= root.open (work.rel);
        const auto revision= source.stat ();
        if (athena::document::storage_bytes_fingerprint (
              source.read (athena::document::codec_limits ().input_bytes)) != work.storage_hash)
          throw std::runtime_error ("Deferred: artifact source changed");
        if (staged != document) {
          const auto bytes= athena::document::write_xml_v2 (staged);
          if (athena::document::read_xml_v2 (bytes) != staged)
            throw std::runtime_error ("Artifact binding XML round-trip failed");
          std::unique_lock<std::recursive_mutex> gate (
            document_publication_mutex (), std::defer_lock);
          auto saved= root.replace (work.rel, source, revision, bytes, [&] {
            if (!gate.try_lock () || published_buffer_source (to_std (system_name)).first != ATHENA_NO_ACTOR)
              throw std::runtime_error ("Deferred: artifact source is opening");
          });
          if (!saved.directory_synced) throw std::runtime_error ("Artifact binding directory sync failed");
          work.storage_hash= athena::document::storage_bytes_fingerprint (bytes);
        }
      }
      else {
      auto storage= athena::document::document_file::capture (
        work.path, vault_root);
      if (storage.version () != athena::document::xml_storage_version::v2 ||
          storage.source_sha256 () != work.storage_hash) {
        error= "Artifact source changed before binding publish: " + work.rel;
        return false;
      }
      auto saved= storage.save (staged);
      work.storage_hash= saved.xml_sha256;
      }
    }
    catch (const std::exception& failure) {
      error= "Could not persist artifact source bindings for " + work.rel +
             ": " + failure.what ();
      return false;
    }
  }

  tree persisted;
  std::string persisted_hash;
  if (!read_document (work.path, persisted, error, nullptr, &persisted_hash))
    return false;
  if (artifact_content_fingerprint (persisted) != work.content_hash) {
    error= "Persisted artifact bindings changed extraction content for " + work.rel;
    return false;
  }
  for (auto& record: extracted.records)
    if (record.origin == "bold-text" && record.range_state == AthenaArtifactRangeState::resolved &&
        !freeze_paragraph_sources (persisted, record, error)) return false;
  try {
    work.semantic_hash=
      athena::document::semantic_document_fingerprint (persisted);
  }
  catch (const std::exception& failure) {
    error= "Could not fingerprint persisted artifact source " + work.rel +
           ": " + failure.what ();
    return false;
  }
  work.storage_hash= persisted_hash;
  work.modified= mtime_ns (work.path);
  work.size= (long long) fs::file_size (work.path);
  return true;
}

bool replace_document (sqlite3* db, const std::string& rel,
                         ExtractedDocument& extracted, long long modified,
                         long long size, const std::string& storage_hash,
                         const std::string& content_hash,
                         const std::string& semantic_hash,
                         const std::string& extraction_contract,
                         std::string& error) {
  std::vector<AthenaArtifactIdentityObservation> old_observations;
  if (!load_identity_observations (db, rel, old_observations, error))
    return false;
  std::vector<AthenaArtifactIdentityObservation> new_observations;
  new_observations.reserve (extracted.records.size ());
  for (const AthenaArtifactRecord& record: extracted.records)
    new_observations.push_back (identity_observation (record));
  AthenaArtifactIdentityResult identity=
    athena_artifact_associate_identities (old_observations, new_observations);

  auto artifact_ids= existing_ids (
    db, "SELECT origin||char(31)||content_uuid,uuid FROM artifacts WHERE path=?1;",
    rel, error);
  if (!error.empty ()) return false;

  for (size_t i=0; i<extracted.records.size (); i++) {
    AthenaArtifactRecord& record= extracted.records[i];
    const AthenaArtifactIdentityDecision& decision= identity.decisions[i];
    if (record.source_uuid.empty ()) {
      if (decision.kind == AthenaArtifactIdentityDecisionKind::Matched &&
          decision.old_index >= 0)
        record.content_uuid=
          old_observations[(size_t) decision.old_index].uuid;
      else
        record.content_uuid= generate_uuid_v4 ();
      record.identity_decision=
        athena_artifact_identity_decision_name (decision.kind);
      record.identity_evidence= decision.evidence;
    }
  }

  Statement del_artifacts, del_enunciations, del_bold;
  if (!prepare (db, "DELETE FROM artifacts WHERE path=?1;", del_artifacts, error) ||
      !prepare (db, "DELETE FROM enunciations.entries WHERE path=?1;",
                del_enunciations, error) ||
      !prepare (db, "DELETE FROM bold_text.entries WHERE path=?1;", del_bold,
                error)) return false;
  for (Statement* st: {&del_artifacts, &del_enunciations, &del_bold}) {
    bind_text (st->st, 1, rel);
    if (sqlite3_step (st->st) != SQLITE_DONE) {
      error= sqlite3_errmsg (db); return false;
    }
  }

  Statement insert_enun, insert_bold, insert_artifact, insert_name;
  if (!prepare (db,
      "INSERT INTO enunciations.entries(uuid,path,anchor_stem,tag,display_text,"
      "document_order,identity_focus,identity_host,identity_before,identity_after) "
      "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10);", insert_enun, error) ||
      !prepare (db,
      "INSERT INTO bold_text.entries(uuid,path,keyword_tree,keyword_display,"
      "occurrence,paragraph_offsets,document_order,identity_focus,identity_host,"
      "identity_before,identity_after) "
      "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11);", insert_bold, error) ||
      !prepare (db,
      "INSERT INTO artifacts(uuid,type,origin,content_uuid,proof_uuid,path,"
      "source_uuid,source_role,input_hash,anchor_stem,display_text,document_order,"
      "identity_decision,identity_evidence,source_nodes,range_state,range_structure_hash) "
      "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16,?17);",
      insert_artifact, error))
    return false;
  if (!prepare (db,
                "INSERT INTO artifact_names(artifact_uuid,name,ordinal,name_tree) "
                "VALUES(?1,?2,?3,?4);", insert_name, error))
    return false;

  std::map<int,std::string> enunciation_order_ids;
  for (AthenaArtifactRecord& record: extracted.records) {
    if (record.origin == "enunciation") {
      sqlite3_reset (insert_enun.st); sqlite3_clear_bindings (insert_enun.st);
      bind_text (insert_enun.st, 1, record.content_uuid);
      bind_text (insert_enun.st, 2, rel);
      bind_text (insert_enun.st, 3, record.anchor_stem);
      bind_text (insert_enun.st, 4, record.keyword_tree);
      bind_text (insert_enun.st, 5, record.display_text);
      sqlite3_bind_int (insert_enun.st, 6, record.document_order);
      bind_text (insert_enun.st, 7, record.identity_focus);
      bind_text (insert_enun.st, 8, record.identity_host);
      bind_text (insert_enun.st, 9, record.identity_before);
      bind_text (insert_enun.st, 10, record.identity_after);
      if (sqlite3_step (insert_enun.st) != SQLITE_DONE) {
        error= sqlite3_errmsg (db); return false;
      }
      enunciation_order_ids[record.document_order]= record.content_uuid;
    }
    else {
      sqlite3_reset (insert_bold.st); sqlite3_clear_bindings (insert_bold.st);
      bind_text (insert_bold.st, 1, record.content_uuid);
      bind_text (insert_bold.st, 2, rel);
      bind_text (insert_bold.st, 3, encode_opaque (record.keyword_tree));
      bind_text (insert_bold.st, 4, record.display_text);
      sqlite3_bind_int (insert_bold.st, 5, record.keyword_occurrence);
      bind_text (insert_bold.st, 6, offsets_text (record.paragraph_offsets));
      sqlite3_bind_int (insert_bold.st, 7, record.document_order);
      bind_text (insert_bold.st, 8, record.identity_focus);
      bind_text (insert_bold.st, 9, record.identity_host);
      bind_text (insert_bold.st, 10, record.identity_before);
      bind_text (insert_bold.st, 11, record.identity_after);
      if (sqlite3_step (insert_bold.st) != SQLITE_DONE) {
        error= sqlite3_errmsg (db); return false;
      }
    }
  }

  for (AthenaArtifactRecord& record: extracted.records) {
    if (record.proof_uuid.rfind ("@order:", 0) == 0) {
      int order= std::stoi (record.proof_uuid.substr (7));
      record.proof_uuid= enunciation_order_ids[order];
    }
    if (record.uuid.empty ()) {
      std::string artifact_key= record.origin + char (31) + record.content_uuid;
      record.uuid= artifact_ids.count (artifact_key) ? artifact_ids[artifact_key]
                                                     : generate_uuid_v4 ();
    }
    sqlite3_reset (insert_artifact.st);
    sqlite3_clear_bindings (insert_artifact.st);
    bind_text (insert_artifact.st, 1, record.uuid);
    bind_text (insert_artifact.st, 2, record.type);
    bind_text (insert_artifact.st, 3, record.origin);
    bind_text (insert_artifact.st, 4, record.content_uuid);
    if (record.proof_uuid.empty ()) sqlite3_bind_null (insert_artifact.st, 5);
    else bind_text (insert_artifact.st, 5, record.proof_uuid);
    bind_text (insert_artifact.st, 6, rel);
    bind_text (insert_artifact.st, 7, record.source_uuid);
    bind_text (insert_artifact.st, 8, record.source_role);
    bind_text (insert_artifact.st, 9, record.input_fingerprint);
    bind_text (insert_artifact.st, 10, record.anchor_stem);
    bind_text (insert_artifact.st, 11, record.display_text);
    sqlite3_bind_int (insert_artifact.st, 12, record.document_order);
    bind_text (insert_artifact.st, 13, record.identity_decision);
    bind_text (insert_artifact.st, 14, record.identity_evidence);
    QJsonArray source_nodes;
    for (const auto& id: record.source_nodes) source_nodes.append (qstr (id));
    bind_text (insert_artifact.st, 15,
      QJsonDocument (source_nodes).toJson (QJsonDocument::Compact).toStdString ());
    bind_text (insert_artifact.st, 16,
      record.range_state == AthenaArtifactRangeState::pending ? "pending" :
      record.range_state == AthenaArtifactRangeState::resolved ? "resolved" :
      record.range_state == AthenaArtifactRangeState::failed ? "failed" : "not-applicable");
    bind_text (insert_artifact.st, 17, record.range_structure_fingerprint);
    if (sqlite3_step (insert_artifact.st) != SQLITE_DONE) {
      error= sqlite3_errmsg (db); return false;
    }
    for (size_t i=0; i<record.semantic_names.size (); i++) {
      sqlite3_reset (insert_name.st);
      sqlite3_clear_bindings (insert_name.st);
      bind_text (insert_name.st, 1, record.uuid);
      bind_text (insert_name.st, 2, record.semantic_names[i]);
      sqlite3_bind_int (insert_name.st, 3, (int) i);
      bind_text (insert_name.st, 4, i < record.semantic_name_trees.size ()
        ? record.semantic_name_trees[i] : std::string ());
      if (sqlite3_step (insert_name.st) != SQLITE_DONE) {
        error= sqlite3_errmsg (db); return false;
      }
    }
  }

  Statement history;
  if (!prepare (
        db,
        "INSERT INTO artifact_identity_history("
        "path,origin,old_content_uuid,new_content_uuid,document_order,decision,"
        "evidence,score,old_margin,new_margin,global_delta,created_at) "
        "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12);",
        history, error))
    return false;
  long long created_at= (long long) std::chrono::duration_cast<std::chrono::seconds> (
    std::chrono::system_clock::now ().time_since_epoch ()).count ();
  for (size_t i=0; i<extracted.records.size (); i++) {
    const AthenaArtifactRecord& record= extracted.records[i];
    const AthenaArtifactIdentityDecision& decision= identity.decisions[i];
    sqlite3_reset (history.st);
    sqlite3_clear_bindings (history.st);
    bind_text (history.st, 1, rel);
    bind_text (history.st, 2, record.origin);
    if (decision.old_index >= 0)
      bind_text (history.st, 3,
                 old_observations[(size_t) decision.old_index].uuid);
    else
      sqlite3_bind_null (history.st, 3);
    bind_text (history.st, 4, record.content_uuid);
    sqlite3_bind_int (history.st, 5, record.document_order);
    bind_text (history.st, 6, record.identity_decision);
    bind_text (history.st, 7, record.identity_evidence);
    sqlite3_bind_int64 (history.st, 8, decision.score);
    sqlite3_bind_int64 (history.st, 9, decision.old_margin);
    sqlite3_bind_int64 (history.st, 10, decision.new_margin);
    sqlite3_bind_int64 (history.st, 11, decision.global_delta);
    sqlite3_bind_int64 (history.st, 12, created_at);
    if (sqlite3_step (history.st) != SQLITE_DONE) {
      error= sqlite3_errmsg (db);
      return false;
    }
  }
  for (int old_index: identity.deleted_old_indices) {
    const AthenaArtifactIdentityObservation& old_value=
      old_observations[(size_t) old_index];
    sqlite3_reset (history.st);
    sqlite3_clear_bindings (history.st);
    bind_text (history.st, 1, rel);
    bind_text (history.st, 2, old_value.origin);
    bind_text (history.st, 3, old_value.uuid);
    sqlite3_bind_null (history.st, 4);
    sqlite3_bind_int (history.st, 5, old_value.document_order);
    bind_text (history.st, 6, "deleted");
    bind_text (history.st, 7, "no-accepted-successor");
    for (int column= 8; column<=11; column++) sqlite3_bind_int (history.st, column, 0);
    sqlite3_bind_int64 (history.st, 12, created_at);
    if (sqlite3_step (history.st) != SQLITE_DONE) {
      error= sqlite3_errmsg (db);
      return false;
    }
  }

  Statement doc;
  if (!prepare (db,
      "INSERT INTO documents(path,mtime_ns,size,locator_contract,semantic_hash,"
      "storage_hash,content_hash) VALUES(?1,?2,?3,?4,?5,?6,?7) "
      "ON CONFLICT(path) DO UPDATE SET mtime_ns=excluded.mtime_ns,"
      "size=excluded.size,locator_contract=excluded.locator_contract,"
      "semantic_hash=excluded.semantic_hash,storage_hash=excluded.storage_hash,"
      "content_hash=excluded.content_hash;",
      doc, error)) return false;
  bind_text (doc.st, 1, rel);
  sqlite3_bind_int64 (doc.st, 2, modified);
  sqlite3_bind_int64 (doc.st, 3, size);
  bind_text (doc.st, 4, extraction_contract);
  bind_text (doc.st, 5, semantic_hash);
  bind_text (doc.st, 6, storage_hash);
  bind_text (doc.st, 7, content_hash);
  if (sqlite3_step (doc.st) != SQLITE_DONE) {
    error= sqlite3_errmsg (db); return false;
  }
  // Definition-range checkpoints are keyed by the exact model-input hash.
  // A document content revision can change while an individual model request
  // remains byte-for-byte identical, so unrelated edits do not evict it.
  return true;
}

bool delete_document (sqlite3* db, const std::string& rel,
                      std::string& error) {
  std::vector<AthenaArtifactIdentityObservation> observations;
  if (!load_identity_observations (db, rel, observations, error)) return false;
  Statement history;
  if (!prepare (
        db,
        "INSERT INTO artifact_identity_history("
        "path,origin,old_content_uuid,new_content_uuid,document_order,decision,"
        "evidence,score,old_margin,new_margin,global_delta,created_at) "
        "VALUES(?1,?2,?3,NULL,?4,'deleted','source-document-removed',0,0,0,0,?5);",
        history, error))
    return false;
  long long created_at= (long long) std::chrono::duration_cast<
    std::chrono::seconds> (
      std::chrono::system_clock::now ().time_since_epoch ()).count ();
  for (const AthenaArtifactIdentityObservation& observation: observations) {
    sqlite3_reset (history.st);
    sqlite3_clear_bindings (history.st);
    bind_text (history.st, 1, rel);
    bind_text (history.st, 2, observation.origin);
    bind_text (history.st, 3, observation.uuid);
    sqlite3_bind_int (history.st, 4, observation.document_order);
    sqlite3_bind_int64 (history.st, 5, created_at);
    if (sqlite3_step (history.st) != SQLITE_DONE) {
      error= sqlite3_errmsg (db);
      return false;
    }
  }
  for (const char* sql: {
      "DELETE FROM artifacts WHERE path=?1;",
      "DELETE FROM enunciations.entries WHERE path=?1;",
      "DELETE FROM bold_text.entries WHERE path=?1;",
      "DELETE FROM artifact_range_cache WHERE path=?1;",
      "DELETE FROM documents WHERE path=?1;"}) {
    Statement st;
    if (!prepare (db, sql, st, error)) return false;
    bind_text (st.st, 1, rel);
    if (sqlite3_step (st.st) != SQLITE_DONE) {
      error= sqlite3_errmsg (db); return false;
    }
  }
  return true;
}

struct ArtifactDocumentRevision {
  bool found= false;
  long long modified= 0;
  long long size= 0;
  std::string extraction_contract;
  std::string semantic_hash;
  std::string storage_hash;
  std::string content_hash;
};

bool load_document_revision (sqlite3* db, const std::string& rel,
                             ArtifactDocumentRevision& revision,
                             std::string& error) {
  Statement st;
  if (!prepare (
        db,
        "SELECT mtime_ns,size,locator_contract,semantic_hash,storage_hash,content_hash "
        "FROM documents WHERE path=?1;",
        st, error)) return false;
  bind_text (st.st, 1, rel);
  int rc= sqlite3_step (st.st);
  if (rc == SQLITE_DONE) { revision= {}; return true; }
  if (rc != SQLITE_ROW) { error= sqlite3_errmsg (db); return false; }
  revision.found= true;
  revision.modified= sqlite3_column_int64 (st.st, 0);
  revision.size= sqlite3_column_int64 (st.st, 1);
  revision.extraction_contract= column_text (st.st, 2);
  revision.semantic_hash= column_text (st.st, 3);
  revision.storage_hash= column_text (st.st, 4);
  revision.content_hash= column_text (st.st, 5);
  return true;
}

bool update_document_revision (sqlite3* db, const std::string& rel,
                               long long modified, long long size,
                               const std::string& storage_hash,
                               const std::string& content_hash,
                               const std::string& semantic_hash,
                               const std::string& extraction_contract,
                               std::string& error) {
  Statement st;
  if (!prepare (
        db,
        "UPDATE documents SET mtime_ns=?2,size=?3,storage_hash=?4,"
        "content_hash=?5,semantic_hash=?6,locator_contract=?7 WHERE path=?1;",
        st, error)) return false;
  bind_text (st.st, 1, rel);
  sqlite3_bind_int64 (st.st, 2, modified);
  sqlite3_bind_int64 (st.st, 3, size);
  bind_text (st.st, 4, storage_hash);
  bind_text (st.st, 5, content_hash);
  bind_text (st.st, 6, semantic_hash);
  bind_text (st.st, 7, extraction_contract);
  if (sqlite3_step (st.st) != SQLITE_DONE) {
    error= sqlite3_errmsg (db);
    return false;
  }
  return true;
}

bool backfill_range_cache_semantic (sqlite3* db, const std::string& rel,
                                    long long modified, long long size,
                                    const std::string& semantic_hash,
                                    std::string& error) {
  Statement st;
  if (!prepare (
        db,
        "UPDATE artifact_range_cache SET semantic_hash=?4 WHERE path=?1 AND "
        "mtime_ns=?2 AND size=?3 AND semantic_hash='';",
        st, error)) return false;
  bind_text (st.st, 1, rel);
  sqlite3_bind_int64 (st.st, 2, modified);
  sqlite3_bind_int64 (st.st, 3, size);
  bind_text (st.st, 4, semantic_hash);
  if (sqlite3_step (st.st) != SQLITE_DONE) {
    error= sqlite3_errmsg (db);
    return false;
  }
  return true;
}

} // namespace

bool
athena_artifacts_run_extract_worker (const fs::path& manifest,
                                     const fs::path& output,
                                     std::string& error) {
  // This CLI entrypoint precedes init_athena; XML labels must already map to
  // native DOCUMENT/CONCAT/etc. before decoding its first source tree.
  init_std_drd ();
  std::ifstream input (manifest, std::ios::binary);
  std::string bytes ((std::istreambuf_iterator<char> (input)), {});
  QJsonDocument request= QJsonDocument::fromJson (
    QByteArray (bytes.data (), (qsizetype) bytes.size ()));
  if (!input.good () && !input.eof ()) error= "Could not read worker manifest";
  else if (!request.isObject ()) error= "Invalid artifact worker manifest";

  AthenaArtifactTitleFilter title_filter;
  QJsonArray requested_documents;
  bool structural_only= false;
  if (error.empty ()) {
    QJsonObject root= request.object ();
    structural_only= root.value ("structural_only").toBool (false);
    std::vector<std::string> entries;
    for (const QJsonValue& value: root.value ("title_filter").toArray ())
      entries.push_back (value.toString ().toStdString ());
    title_filter= athena_artifact_title_filter_from_entries (entries);
    for (const auto& value: root.value ("structured_title_filter").toArray ())
      athena_artifact_title_filter_add (title_filter,
        fragment_tree (value.toString ().toStdString ()));
    requested_documents= root.value ("documents").toArray ();
  }

  QJsonArray documents;
  if (error.empty ()) {
    for (const QJsonValue& value: requested_documents) {
      QJsonObject source= value.toObject ();
      fs::path file (source.value ("file").toString ().toStdString ());
      std::string rel= source.value ("path").toString ().toStdString ();
      tree document;
      ExtractedDocument extracted;
      if (!read_document (file, document, error) ||
          !extract (document, rel, title_filter, extracted, error, structural_only)) break;
      QJsonArray records;
      for (const AthenaArtifactRecord& record: extracted.records)
        records.append (record_json (record));
      QJsonObject item;
      item["path"]= qstr (rel);
      item["records"]= records;
      documents.append (item);
    }
  }

  QJsonObject result;
  result["error"]= qstr (error);
  result["documents"]= documents;
  QByteArray encoded= QJsonDocument (result).toJson (QJsonDocument::Compact);
  std::ofstream stream (output, std::ios::binary | std::ios::trunc);
  stream.write (encoded.constData (), encoded.size ());
  stream.close ();
  if (!stream.good () && error.empty ()) error= "Could not write worker output";
  return error.empty ();
}

bool
athena_artifacts_extract_structure (
  const tree& document, const std::string& relative_path,
  std::vector<AthenaArtifactRecord>& records, std::string& error) {
  ExtractedDocument extracted;
  if (!extract (document, relative_path, athena_artifact_title_filter_defaults (),
                extracted, error, true)) return false;
  records= std::move (extracted.records);
  return true;
}

bool
athena_artifacts_bind_structure (
  tree& document, std::vector<AthenaArtifactRecord>& records, std::string& error) {
  tree body= document_body (document);
  std::vector<ArtifactBindingAssignment> assignments;
  for (size_t i=0; i<records.size (); ++i) {
    auto& record= records[i];
    tree& source= source_at (body, record.source_path);
    const auto bound= artifact_binding (source, record.source_role);
    if (!bound.empty ()) record.uuid= bound;
    if (record.uuid.empty ()) record.uuid= generate_uuid_v4 ();
    assignments.push_back ({i, record.source_path, record.source_role,
                           record.uuid, athena::node::id (source)});
  }
  if (!apply_binding_assignments (document, assignments, error)) return false;
  std::map<int,std::string> enunciations;
  for (const auto& assignment: assignments) {
    auto& record= records[assignment.record_index];
    record.source_uuid= record.content_uuid= assignment.source_uuid;
    record.identity_decision= "source-binding";
    if (record.origin == "enunciation") enunciations[record.document_order]= record.source_uuid;
  }
  for (auto& record: records)
    if (record.proof_uuid.rfind ("@order:", 0) == 0)
      record.proof_uuid= enunciations[std::stoi (record.proof_uuid.substr (7))];
  return true;
}

bool
athena_artifacts_extract_document (
  const tree& document, const std::string& relative_path,
  std::vector<AthenaArtifactRecord>& records, std::string& error) {
  std::map<std::string,ExtractedDocument> extracted;
  std::vector<DocumentWork> source= {
    {fs::path (), relative_path, 0, 0, ""}
  };
  AthenaArtifactTitleFilter title_filter=
    athena_artifact_title_filter_defaults ();
  if (!extract (document, relative_path, title_filter,
                extracted[relative_path], error) ||
      !select_definition_ranges (nullptr, source, extracted, {}, error))
    return false;
  records= std::move (extracted[relative_path].records);
  return true;
}

bool
athena_artifact_locate_paragraph (
  const tree& document, const AthenaArtifactRecord& record,
  AthenaArtifactParagraphLocation& location, std::string& error) {
  location= AthenaArtifactParagraphLocation ();
  if (record.origin != "bold-text") {
    error= "Artifact is not a paragraph artifact";
    return false;
  }
  tree body= document_body (document);
  if (!is_compound (body)) {
    error= "Document has no structural body";
    return false;
  }

  if (!record.source_nodes.empty ()) {
    std::map<std::string, std::vector<path>> locations;
    std::function<void (const tree&, path)> visit= [&] (const tree& value, path where) {
      const auto id= athena::node::id (value);
      if (!id.empty ()) locations[id].push_back (where);
      if (is_compound (value))
        for (int i=0; i<N(value); ++i) visit (value[i], where * i);
    };
    visit (body, path ());
    std::set<std::string> seen;
    for (const auto& id: record.source_nodes) {
      if (!athena::node::valid_id (id)) {
        error= "Invalid Artifact source UUID"; return false;
      }
      if (!seen.insert (id).second) continue;
      const auto& hits= locations[id];
      if (hits.size () != 1) {
        error= hits.empty () ? "Artifact source object is missing: " + id :
                               "Artifact source UUID is duplicated: " + id;
        return false;
      }
      for (const auto& prior: location.nodes)
        if (prior <= hits[0] || hits[0] <= prior) {
          error= "Artifact source list contains overlapping objects";
          return false;
        }
      location.nodes.push_back (hits[0]);
    }
    return true;
  }

  std::vector<Paragraph> paragraphs;
  collect_paragraphs (body, paragraphs);
  std::unordered_map<std::string,int> occurrences;
  long focus= -1;
  int identity_matches= 0;
  for (size_t i=0; i<paragraphs.size (); i++) {
    std::vector<tree> bolds;
    find_bold (paragraphs[i].value, bolds);
    for (const tree& keyword: bolds) {
      if (!record.source_uuid.empty ()) {
        if (athena::node::id (keyword) == record.source_uuid) {
          focus= (long) i;
          ++identity_matches;
        }
        continue;
      }
      std::string display= plain_text (visible_body (keyword));
      if (collapse_spaces (display).empty ()) continue;
      std::string serialized= fragment_bytes (keyword);
      int occurrence= ++occurrences[serialized];
      if (serialized == record.keyword_tree &&
          occurrence == record.keyword_occurrence) {
        focus= (long) i;
        break;
      }
    }
    if (focus >= 0 && record.source_uuid.empty ()) break;
  }
  if (!record.source_uuid.empty () && identity_matches != 1) {
    error= identity_matches == 0 ? "Artifact source UUID is no longer present" :
                                  "Artifact source UUID is duplicated";
    return false;
  }
  if (focus < 0) {
    error= "Artifact source no longer matches the artifact database";
    return false;
  }
  if (record.paragraph_offsets.empty () ||
      std::find (record.paragraph_offsets.begin (),
                 record.paragraph_offsets.end (), 0) ==
        record.paragraph_offsets.end ()) {
    error= "Artifact paragraph range is invalid";
    return false;
  }

  long first= focus;
  long last= focus;
  const bool native_range= !record.source_uuid.empty ();
  if (native_range && record.definition_candidates.empty () &&
      (record.source_content_fingerprint.empty () ||
       artifact_content_fingerprint (document) !=
         record.source_content_fingerprint)) {
    error= "Artifact paragraph selection is stale; rebuild artifacts";
    return false;
  }
  for (int offset: record.paragraph_offsets) {
    long index= focus + offset;
    if (index < 0 || index >= (long) paragraphs.size () ||
        paragraphs[(size_t) index].segment !=
          paragraphs[(size_t) focus].segment) {
      error= "Artifact paragraph range is stale";
      return false;
    }
    if (native_range && !record.definition_candidates.empty ()) {
      const auto candidate= std::find_if (
        record.definition_candidates.begin (), record.definition_candidates.end (),
        [offset] (const auto& entry) { return entry.first == offset; });
      if (candidate == record.definition_candidates.end () ||
          candidate->second != fragment_bytes (paragraphs[(size_t) index].value)) {
        error= "Artifact paragraph selection is stale; rebuild artifacts";
        return false;
      }
    }
    first= std::min (first, index);
    last= std::max (last, index);
  }
  for (long index= first; index<=last; index++)
    if (std::find (record.paragraph_offsets.begin (),
                   record.paragraph_offsets.end (),
                   (int) (index - focus)) ==
        record.paragraph_offsets.end ()) {
      error= "Artifact paragraph range is not continuous";
      return false;
    }

  location.focus_child= paragraphs[(size_t) focus].first_child;
  location.first_child= paragraphs[(size_t) first].first_child;
  location.last_child= paragraphs[(size_t) last].last_child;
  location.parent= paragraphs[(size_t) focus].parent;
  return true;
}

bool athena_artifact_freeze_source_nodes (const tree& document, AthenaArtifactRecord& record,
                               std::string& error) {
  if (!record.source_nodes.empty ()) return true;
  AthenaArtifactParagraphLocation location;
  if (!athena_artifact_locate_paragraph (document, record, location, error)) return false;
  tree body= document_body (document);
  tree parent= is_nil (location.parent) ? body : subtree (body, location.parent);
  std::vector<std::string> ids;
  for (int i=location.first_child; i<=location.last_child; ++i) {
    const auto& child= parent[i];
    if (is_func (child, LABEL) || ignorable (child)) continue;
    const auto id= athena::node::id (child);
    if (!athena::node::valid_id (id)) {
      error= "Artifact source paragraph has no persistent UUID";
      return false;
    }
    if (std::find (ids.begin (), ids.end (), id) == ids.end ()) ids.push_back (id);
  }
  if (ids.empty ()) { error= "Artifact source list is empty"; return false; }
  record.source_nodes= std::move (ids);
  return true;
}

namespace {

bool freeze_paragraph_sources (const tree& document, AthenaArtifactRecord& record,
                               std::string& error) {
  return athena_artifact_freeze_source_nodes (document, record, error);
}

bool enunciation_matches_record (
  tree body, path scope, const tree& enunciation,
  const AthenaArtifactRecord& record) {
  std::string base;
  if (enunciation_type (enunciation, base) != record.type)
    return false;
  path parent_path= path_up (scope);
  if (!has_subtree (body, parent_path)) return false;
  tree parent= subtree (body, parent_path);
  if (!is_compound (parent)) return false;
  int index= last_item (scope);
  std::string anchor;
  for (int i=index-1; i>=0; i--) {
    if (ignorable (parent[i])) continue;
    anchor= anchor_stem (label_text (parent[i]));
    break;
  }
  if (anchor.empty ()) {
    for (int i=index+1; i<N(parent); i++) {
      if (ignorable (parent[i])) continue;
      anchor= anchor_stem (label_text (parent[i]));
      break;
    }
  }
  return (!record.anchor_stem.empty () &&
          anchor == record.anchor_stem) ||
         (!record.identity_focus.empty () &&
          identity_fingerprint (enunciation) == record.identity_focus);
}

bool find_enunciation_by_order (
  const tree& parent, int target, int& order, path where,
  path& source_path) {
  if (!is_compound (parent)) return false;
  for (int i=0; i<N(parent); i++) {
    const tree& child= parent[i];
    std::string base;
    if (!enunciation_type (child, base).empty ()) {
      if (plain_text (child).empty () && contains_tag (child, "image"))
        continue;
      if (order++ == target) {
        source_path= where * i;
        return true;
      }
      continue;
    }
    if (find_enunciation_by_order (
          child, target, order, where * i, source_path)) return true;
  }
  return false;
}

void find_source_uuid_paths (const tree& source, const std::string& id,
                             path where, std::vector<path>& found) {
  if (athena::node::id (source) == id) found.push_back (where);
  if (!is_compound (source)) return;
  for (int i=0; i<N(source); ++i)
    find_source_uuid_paths (source[i], id, where * i, found);
}

} // namespace

bool
athena_artifact_locate_source (
  const tree& document, const AthenaArtifactRecord& record,
  path& source_path, std::string& error) {
  source_path= path ();
  tree body= document_body (document);
  if (!record.source_uuid.empty ()) {
    std::vector<path> matches;
    find_source_uuid_paths (
      body, record.source_uuid, path (), matches);
    if (matches.size () == 1) {
      source_path= matches.front ();
      return true;
    }
    error= matches.empty () ?
      "Artifact source UUID is no longer present in the document" :
      "Artifact source UUID is duplicated in the document";
    return false;
  }
  if (record.origin == "bold-text") {
    AthenaArtifactParagraphLocation location;
    if (!athena_artifact_locate_paragraph (
          document, record, location, error)) return false;
    // Legacy bold-text rows identify a paragraph range plus the exact serialized
    // keyword occurrence.  The authoritative node-model binding belongs on the
    // concrete bold wrapper itself (the same source_path produced by native v2
    // extraction), not on the containing paragraph child.  Returning only the
    // paragraph path collapses multiple bold artifacts in one paragraph onto the
    // same role and creates false binding conflicts during migration.
    std::vector<Paragraph> paragraphs;
    collect_paragraphs (body, paragraphs);
    std::unordered_map<std::string,int> occurrences;
    tree matched;
    bool matched_found= false;
    for (const auto& paragraph: paragraphs) {
      std::vector<tree> bolds;
      find_bold (paragraph.value, bolds);
      for (const tree& keyword: bolds) {
        std::string display= plain_text (visible_body (keyword));
        if (collapse_spaces (display).empty ()) continue;
        std::string serialized= fragment_bytes (keyword);
        int occurrence= ++occurrences[serialized];
        if (serialized == record.keyword_tree &&
            occurrence == record.keyword_occurrence) {
          matched= keyword;
          matched_found= true;
          break;
        }
      }
      if (matched_found) break;
    }
    if (!matched_found) {
      error= "Artifact bold source no longer matches the artifact database";
      return false;
    }
    path keyword_path;
    unsigned matches= 0;
    find_shared_source_paths (
      body, matched, path (), keyword_path, matches);
    if (matches != 1) {
      error= matches == 0 ?
        "Artifact bold source path is no longer present" :
        "Artifact bold source occurs more than once in the source tree";
      return false;
    }
    source_path= keyword_path;
    return true;
  }
  if (record.origin != "enunciation") {
    error= "Artifact has no supported source locator";
    return false;
  }

  int hinted_order= 0;
  path hinted_path;
  if (find_enunciation_by_order (
        body, record.document_order, hinted_order, path (), hinted_path) &&
      has_subtree (body, hinted_path)) {
    tree hinted= subtree (body, hinted_path);
    if (enunciation_matches_record (body, hinted_path, hinted, record)) {
      source_path= hinted_path;
      return true;
    }
  }

  // The document changed around this artifact. Fall back to stable identity
  // association so navigation remains correct when source paths have shifted.
  std::vector<AthenaArtifactRecord> current;
  std::vector<path> source_paths;
  int order= 0;
  scan_enunciations (
    body, record.relative_path, current, order, path (), &source_paths);
  std::vector<AthenaArtifactIdentityObservation> candidates;
  candidates.reserve (current.size ());
  for (const AthenaArtifactRecord& candidate: current)
    candidates.push_back (identity_observation (candidate));
  AthenaArtifactIdentityResult associated=
    athena_artifact_associate_identities (
      {identity_observation (record)}, candidates);
  int matched= -1;
  for (size_t i=0; i<associated.decisions.size (); i++)
    if (associated.decisions[i].kind ==
          AthenaArtifactIdentityDecisionKind::Matched &&
        associated.decisions[i].old_index == 0) {
      if (matched >= 0) {
        error= "Artifact source identity is ambiguous";
        return false;
      }
      matched= (int) i;
    }
  if (matched < 0) {
    error= "Artifact source no longer matches the artifact database";
    return false;
  }
  source_path= source_paths[(size_t) matched];
  return true;
}

bool
athena_artifact_is_defining_occurrence (
  const tree& document, path source_path,
  const AthenaArtifactRecord& record) {
  tree body= document_body (document);
  for (path current= source_path; !is_nil (current);
       current= path_up (current)) {
    if (!has_subtree (body, current)) continue;
    tree value= subtree (body, current);
    if (!record.source_uuid.empty () &&
        athena::node::id (value) == record.source_uuid) return true;
    if (record.origin == "bold-text" && bold_wrapper (value) &&
        fragment_bytes (value) == record.keyword_tree) return true;
    if (record.origin == "enunciation" &&
        enunciation_matches_record (body, current, value, record)) return true;
  }
  return false;
}

bool
athena_artifact_is_inside_definition (
  const tree& document, path source_path) {
  tree body= document_body (document);
  if (!has_subtree (body, source_path)) return false;
  for (path current= source_path; !is_nil (current);
       current= path_up (current)) {
    if (is_compound (subtree (body, current), "definition", 1)) return true;
  }
  return false;
}

bool
athena_artifacts_build (
  const fs::path& vault_root,
  const std::vector<fs::path>& requested_documents, bool full_vault,
  const AthenaArtifactsProgress& progress, AthenaArtifactsBuildResult& result,
  std::string& error, const AthenaArtifactsBuildOptions& options) {
  result= AthenaArtifactsBuildResult ();
  fs::path root= normalize_root (vault_root);
  artifact_log (progress, "build started: root=" + root.string () +
                (full_vault ? ", scope=entire vault" :
                              ", scope=requested document(s)"));
  if (!report_progress (progress, AthenaArtifactsBuildPhase::Preparing, 0, 0,
                        root.string ())) {
    error= "Artifact build cancelled";
    return false;
  }
  SqliteDb holder;
  AthenaVaultfileInfo info;
  if (!open_databases (root, holder, info, error)) return false;
  AthenaArtifactTitleFilter title_filter;
  if (!athena_artifact_title_filter_read (root, title_filter, error))
    return false;
  const std::string complete_contract= std::string (source_locator_contract) +
    ":title-filter=" +
    athena_artifact_title_filter_fingerprint (title_filter);
  const std::string extraction_contract= complete_contract +
    (options.structural_only ? ":structural" : "");
  artifact_log (progress, "databases: artifacts=" + (root / info.artifacts_path).string () +
                ", enunciations=" +
                (root / info.enunciations_path).string () +
                ", bold-text=" + (root / info.bold_text_path).string ());

  std::vector<fs::path> documents= full_vault ? scan_ath_documents (root)
                                               : requested_documents;
  if (full_vault && !info.maintenance_summary_path.empty ())
    documents.erase (
      std::remove_if (
        documents.begin (), documents.end (), [&] (const fs::path& path) {
          return path_in_configured_subtree (
            root, path, info.maintenance_summary_path);
        }),
      documents.end ());
  std::sort (documents.begin (), documents.end ());
  result.documents_seen= documents.size ();
  artifact_log (progress, "discovered " + std::to_string (documents.size ()) +
                " .ath document(s)");
  std::vector<std::string> deleted;
  if (full_vault) {
    std::set<std::string> live;
    for (const fs::path& path: documents) live.insert (relative_key (root, path));
    Statement st;
    if (!prepare (holder.db, "SELECT path FROM documents;", st, error))
      return false;
    while (sqlite3_step (st.st) == SQLITE_ROW) {
      std::string rel= column_text (st.st, 0);
      if (!live.count (rel)) deleted.push_back (rel);
    }
  }

  std::vector<DocumentWork> work;
  for (const fs::path& path: documents) {
    if (!fs::exists (path)) continue;
    std::string rel= relative_key (root, path);
    long long modified= mtime_ns (path);
    long long size= (long long) fs::file_size (path);
    ArtifactDocumentRevision cached;
    if (!load_document_revision (holder.db, rel, cached, error)) return false;
    const bool contract_same= cached.found &&
      (cached.extraction_contract == extraction_contract ||
       (options.structural_only && cached.extraction_contract == complete_contract));

    tree document;
    athena::document::document_source_format source_format;
    std::string storage_hash;
    if (!read_document (
          path, document, error, &source_format, &storage_hash)) return false;
    if (!options.saved_sha256.empty () && storage_hash != options.saved_sha256) {
      error= "Deferred: saved artifact revision was superseded"; return false;
    }
    const bool storage_same= cached.found &&
      (!cached.storage_hash.empty () ? cached.storage_hash == storage_hash :
       (cached.modified == modified && cached.size == size));
    std::string content_hash;
    std::string semantic_hash;
    try {
      content_hash= artifact_content_fingerprint (document);
      semantic_hash= athena::document::semantic_document_fingerprint (document);
    }
    catch (const std::exception& e) {
      error= "Could not fingerprint " + rel + ": " + e.what ();
      return false;
    }
    const bool identity_same= cached.found && !cached.semantic_hash.empty () &&
                              cached.semantic_hash == semantic_hash;
    const bool content_same= cached.found &&
      (!cached.content_hash.empty () ?
       (cached.content_hash == content_hash && identity_same) : identity_same);
    Statement incomplete;
    if (!prepare (holder.db, "SELECT 1 FROM artifacts WHERE path=?1 AND "
                  "range_state IN ('pending','failed') LIMIT 1;", incomplete, error)) return false;
    bind_text (incomplete.st, 1, rel);
    int incomplete_status= sqlite3_step (incomplete.st);
    if (incomplete_status != SQLITE_ROW && incomplete_status != SQLITE_DONE) {
      error= sqlite3_errmsg (holder.db); return false;
    }
    const bool needs_ranges= incomplete_status == SQLITE_ROW && !options.structural_only;
    if ((full_vault || options.structural_only) && !needs_ranges && contract_same && cached.found &&
        (storage_same || content_same)) {
      if (cached.semantic_hash.empty () && storage_same &&
          !backfill_range_cache_semantic (
            holder.db, rel, modified, size, semantic_hash, error))
        return false;
      if (!update_document_revision (
            holder.db, rel, modified, size, storage_hash, content_hash,
            semantic_hash,
            cached.extraction_contract, error))
        return false;
      artifact_log (progress, storage_same ?
        "storage revision already matches artifact content: " + rel :
        "storage rewrite preserved artifact content revision: " + rel);
      continue;
    }
    work.push_back ({path, rel, modified, size, storage_hash, content_hash,
                     semantic_hash, source_format});
  }
  artifact_log (progress, "incremental plan: rebuild " + std::to_string (work.size ()) +
                " document(s), purge " + std::to_string (deleted.size ()) +
                " deleted document(s)");

  std::map<std::string,ExtractedDocument> extracted;
  if (options.structural_only) {
    if (!extract_serial (work, title_filter, extracted, progress, error)) return false;
    // Never call the Scheme LaTeX converter from a structural worker. Reuse a
    // selected range only when its exact native candidate input still matches.
    Statement previous;
    if (!prepare (holder.db,
          "SELECT b.paragraph_offsets,a.input_hash FROM artifacts a JOIN bold_text.entries b "
          "ON b.uuid=a.content_uuid WHERE a.path=?1 AND a.source_uuid=?2 AND "
          "a.source_role=?3 AND a.range_state='resolved' AND a.range_structure_hash=?4;",
          previous, error)) return false;
    for (auto& item: extracted) for (auto& record: item.second.records) {
      if (record.origin != "bold-text" || record.range_structure_fingerprint.empty ()) continue;
      sqlite3_reset (previous.st);
      bind_text (previous.st, 1, item.first);
      bind_text (previous.st, 2, record.source_uuid);
      bind_text (previous.st, 3, record.source_role);
      bind_text (previous.st, 4, record.range_structure_fingerprint);
      const int rc= sqlite3_step (previous.st);
      if (rc == SQLITE_ROW) {
        record.paragraph_offsets= parse_offsets (column_text (previous.st, 0));
        record.input_fingerprint= column_text (previous.st, 1);
        record.range_state= AthenaArtifactRangeState::resolved;
      }
      else if (rc != SQLITE_DONE) { error= sqlite3_errmsg (holder.db); return false; }
    }
    sqlite3_reset (previous.st);
  }
  else if (!extract_documents (
        holder.db, work, title_filter, extracted, progress,
        options.range_selector, error))
    return false;

  // XML v2 source metadata is the artifact identity authority. Persist missing
  // source IDs/bindings before replacing the disposable database cache. A DB
  // failure can therefore be recovered by rebuilding from the source, whereas
  // committing DB-only identity first would lose the authoritative binding.
  for (DocumentWork& item: work) {
    auto found= extracted.find (item.rel);
    if (found == extracted.end ()) {
      error= "Artifact reader returned no result for " + item.rel;
      return false;
    }
    if (!choose_and_persist_native_bindings (
          holder.db, root, item, found->second, error, options))
      return false;
  }

  if (!exec_sql (holder.db, "BEGIN IMMEDIATE;", error)) return false;
  bool committed= false;
  auto rollback= [&] () {
    if (!committed) {
      std::string ignored;
      exec_sql (holder.db, "ROLLBACK;", ignored);
    }
  };
  size_t write_total= deleted.size () + work.size ();
  size_t written= 0;
  if (!report_progress (progress,
                        AthenaArtifactsBuildPhase::WritingDatabase,
                        written, write_total)) {
    error= "Artifact build cancelled"; rollback (); return false;
  }
  for (const std::string& rel: deleted) {
    if (!delete_document (holder.db, rel, error)) { rollback (); return false; }
    result.documents_deleted++;
    written++;
    artifact_log (progress, "purged deleted document from artifact databases: " + rel);
    if (!report_progress (progress,
                          AthenaArtifactsBuildPhase::WritingDatabase,
                          written, write_total, rel)) {
      error= "Artifact build cancelled"; rollback (); return false;
    }
  }
  for (const DocumentWork& item: work) {
    auto found= extracted.find (item.rel);
    if (found == extracted.end ()) {
      error= "Artifact reader returned no result for " + item.rel;
      rollback ();
      return false;
    }
    if (!replace_document (holder.db, item.rel, found->second, item.modified,
                             item.size, item.storage_hash, item.content_hash,
                             item.semantic_hash, extraction_contract,
                             error)) {
      rollback ();
      return false;
    }
    result.documents_changed++;
    for (const AthenaArtifactRecord& record: found->second.records) {
      if (record.origin == "enunciation") result.enunciations++;
      else result.bold_texts++;
      result.artifacts++;
    }
    written++;
    artifact_log (progress, "wrote " + item.rel + ": " +
                  std::to_string (found->second.records.size ()) +
                  " artifact(s)");
    if (!report_progress (progress,
                          AthenaArtifactsBuildPhase::WritingDatabase,
                          written, write_total, item.rel)) {
      error= "Artifact build cancelled"; rollback (); return false;
    }
  }
  for (const auto& item: work) {
    tree current; std::string hash;
    if (!read_document (item.path, current, error, nullptr, &hash) || hash != item.storage_hash) {
      if (error.empty ()) error= "Deferred: artifact source changed before database commit";
      rollback (); return false;
    }
  }
  if (!exec_sql (holder.db, "COMMIT;", error)) { rollback (); return false; }
  committed= true;
  if (options.structural_only) {
    for (const auto& item: work) {
      auto& records= extracted.at (item.rel).records;
      for (auto& record: records) record.source_content_fingerprint= item.content_hash;
      athena_artifact_radioactive_saved_document (root, item.rel, records);
    }
  }
  else if (!work.empty () || !deleted.empty ()) athena_artifact_radioactive_invalidate ();
  report_progress (progress, AthenaArtifactsBuildPhase::Complete, 1, 1);
  artifact_log (progress, "build complete: " + std::to_string (result.artifacts) +
                " artifact(s), " + std::to_string (result.enunciations) +
                " enunciation(s), " + std::to_string (result.bold_texts) +
                " bold-text definition(s), " +
                std::to_string (result.documents_changed) +
                " rebuilt document(s), " +
                std::to_string (result.documents_deleted) +
                " purged document(s)");
  return true;
}

bool
athena_artifacts_build_active_vault (
  bool current_document_only, const AthenaArtifactsProgress& progress,
  AthenaArtifactsBuildResult& result, std::string& error,
  const AthenaArtifactsBuildOptions& options) {
  if (!vault_active ()) { error= "No active vault"; return false; }
  fs::path root (to_std (concretize (vault_get_root ())));
  std::vector<fs::path> documents;
  if (current_document_only) {
    fs::path current (to_std (concretize (get_current_buffer_safe ())));
    std::error_code ec;
    fs::path rel= fs::relative (current, root, ec);
    if (ec || rel.empty () || rel.string ().rfind ("..", 0) == 0 ||
        current.extension () != ".ath") {
      error= "The current buffer is not an .ath document in the active vault";
      return false;
    }
    documents.push_back (current);
  }
  return athena_artifacts_build (root, documents, !current_document_only,
                                 progress, result, error, options);
}

bool
athena_artifacts_mark_document_stale (
  const fs::path& vault_root, const std::string& relative_path,
  std::string& error) {
  SqliteDb holder;
  AthenaVaultfileInfo info;
  if (!open_databases (normalize_root (vault_root), holder, info, error))
    return false;
  Statement stale;
  if (!prepare (
        holder.db,
        "UPDATE documents SET locator_contract='' WHERE path=?1;",
        stale, error)) return false;
  bind_text (stale.st, 1, relative_path);
  if (sqlite3_step (stale.st) != SQLITE_DONE) {
    error= sqlite3_errmsg (holder.db);
    return false;
  }
  return true;
}

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
  athena_artifact_radioactive_invalidate ();
  return true;
}

namespace {

bool table_has_column (sqlite3* db, const char* table, const char* column,
                       bool& found, std::string& error) {
  found= false;
  const std::string sql= std::string ("PRAGMA table_info(") + table + ");";
  Statement statement;
  if (!prepare (db, sql.c_str (), statement, error)) return false;
  int status;
  while ((status= sqlite3_step (statement.st)) == SQLITE_ROW)
    if (column_text (statement.st, 1) == column) found= true;
  if (status != SQLITE_DONE) {
    error= sqlite3_errmsg (db);
    return false;
  }
  return true;
}

std::string artifact_select_columns (sqlite3* db, std::string& error) {
  bool source_uuid= false, source_role= false, input_hash= false, range_state= false,
       content_hash= false, source_nodes= false, structure_hash= false;
  if (!table_has_column (db, "artifacts", "source_uuid", source_uuid, error) ||
      !table_has_column (db, "artifacts", "source_role", source_role, error) ||
      !table_has_column (db, "artifacts", "input_hash", input_hash, error) ||
      !table_has_column (db, "documents", "content_hash", content_hash, error) ||
      !table_has_column (db, "artifacts", "source_nodes", source_nodes, error) ||
      !table_has_column (db, "artifacts", "range_state", range_state, error) ||
      !table_has_column (db, "artifacts", "range_structure_hash", structure_hash, error))
    return {};
  return std::string (
    "SELECT a.uuid,a.type,a.origin,a.content_uuid,COALESCE(a.proof_uuid,''),"
    "a.path,") +
    (source_uuid ? "a.source_uuid," : "'',") +
    (source_role ? "a.source_role," : "'',") +
    (input_hash ? "a.input_hash," : "'',") +
    "a.anchor_stem,a.display_text,a.document_order,"
    "COALESCE(b.keyword_tree,''),COALESCE(b.occurrence,0),"
    "COALESCE(b.paragraph_offsets,''),"
    "CASE WHEN a.origin='bold-text' THEN COALESCE(b.identity_focus,'') "
    "ELSE COALESCE(e.identity_focus,'') END,"
    "CASE WHEN a.origin='bold-text' THEN COALESCE(b.identity_host,'') "
    "ELSE COALESCE(e.identity_host,'') END,"
    "CASE WHEN a.origin='bold-text' THEN COALESCE(b.identity_before,'') "
    "ELSE COALESCE(e.identity_before,'') END,"
    "CASE WHEN a.origin='bold-text' THEN COALESCE(b.identity_after,'') "
    "ELSE COALESCE(e.identity_after,'') END,"
    "a.identity_decision,a.identity_evidence," +
    (content_hash ?
      "COALESCE((SELECT d.content_hash FROM documents d WHERE d.path=a.path),'')" :
      "''") + "," + (source_nodes ? "a.source_nodes" : "''") + "," +
    (range_state ? "a.range_state" : "CASE WHEN a.origin='bold-text' THEN 'resolved' ELSE 'not-applicable' END") +
    std::string (",") + (structure_hash ? "a.range_structure_hash" : "''") +
    " FROM artifacts a "
    "LEFT JOIN bold_text.entries b ON a.origin='bold-text' AND "
    "b.uuid=a.content_uuid LEFT JOIN enunciations.entries e ON "
    "a.origin='enunciation' AND e.uuid=a.content_uuid ";
}

bool artifact_record_from_statement (sqlite3_stmt* statement,
                                     AthenaArtifactRecord& record, std::string& error) {
  record= AthenaArtifactRecord ();
  record.uuid= column_text (statement, 0);
  record.type= column_text (statement, 1);
  record.origin= column_text (statement, 2);
  record.content_uuid= column_text (statement, 3);
  record.proof_uuid= column_text (statement, 4);
  record.relative_path= column_text (statement, 5);
  record.source_uuid= column_text (statement, 6);
  record.source_role= column_text (statement, 7);
  record.input_fingerprint= column_text (statement, 8);
  record.anchor_stem= column_text (statement, 9);
  record.display_text= column_text (statement, 10);
  record.document_order= sqlite3_column_int (statement, 11);
  record.keyword_tree= decode_opaque (column_text (statement, 12));
  record.keyword_occurrence= sqlite3_column_int (statement, 13);
  record.paragraph_offsets= parse_offsets (column_text (statement, 14));
  record.identity_focus= column_text (statement, 15);
  record.identity_host= column_text (statement, 16);
  record.identity_before= column_text (statement, 17);
  record.identity_after= column_text (statement, 18);
  record.identity_decision= column_text (statement, 19);
  record.identity_evidence= column_text (statement, 20);
  record.source_content_fingerprint= column_text (statement, 21);
  record.range_structure_fingerprint= column_text (statement, 24);
  const auto range= column_text (statement, 23);
  if (range == "pending") record.range_state= AthenaArtifactRangeState::pending;
  else if (range == "resolved") record.range_state= AthenaArtifactRangeState::resolved;
  else if (range == "failed") record.range_state= AthenaArtifactRangeState::failed;
  const auto stored_nodes= column_text (statement, 22);
  if (!stored_nodes.empty ()) {
    QJsonParseError parse_error;
    auto nodes= QJsonDocument::fromJson (QByteArray::fromStdString (stored_nodes), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !nodes.isArray ()) {
      error= "Invalid Artifact source UUID list";
      return false;
    }
    for (const auto& id: nodes.array ()) {
      if (!id.isString () || !athena::node::valid_id (id.toString ().toStdString ())) {
        error= "Invalid Artifact source UUID";
        return false;
      }
      record.source_nodes.push_back (id.toString ().toStdString ());
    }
  }
  return true;
}

bool upgrade_source_lists (const fs::path& root, sqlite3* db,
                           std::vector<AthenaArtifactRecord>& records,
                           bool read_only, std::string& error) {
  // Old offsets are converted only against their exact saved content revision.
  // No model invocation and no write to the document is permitted here.
  std::error_code root_error;
  const auto canonical_root= fs::weakly_canonical (root, root_error);
  if (root_error) { error= root_error.message (); return false; }
  std::map<std::string, tree> documents;
  for (auto& record: records) {
    if (record.origin != "bold-text" || record.range_state != AthenaArtifactRangeState::resolved || record.source_uuid.empty () ||
        !record.source_nodes.empty () || record.source_content_fingerprint.empty ()) continue;
    std::error_code ec;
    const auto file= fs::weakly_canonical (root / record.relative_path, ec);
    const auto relative= file.lexically_relative (canonical_root);
    if (ec || relative.empty () || relative.is_absolute () || *relative.begin () == "..") continue;
    std::string diagnostic;
    auto found= documents.find (file.string ());
    if (found == documents.end ()) {
      tree document;
      if (!read_document (file, document, diagnostic)) continue;
      found= documents.emplace (file.string (), document).first;
    }
    if (!freeze_paragraph_sources (found->second, record, diagnostic)) continue;
    if (read_only) continue;
    Statement update;
    if (!prepare (db, "UPDATE artifacts SET source_nodes=?1 WHERE uuid=?2 "
                       "AND (source_nodes='' OR source_nodes='[]');", update, error)) return false;
    QJsonArray ids;
    for (const auto& id: record.source_nodes) ids.append (qstr (id));
    bind_text (update.st, 1, QJsonDocument (ids).toJson (QJsonDocument::Compact).toStdString ());
    bind_text (update.st, 2, record.uuid);
    if (sqlite3_step (update.st) != SQLITE_DONE) { error= sqlite3_errmsg (db); return false; }
  }
  return true;
}

bool load_semantic_names (sqlite3* db,
                          std::vector<AthenaArtifactRecord>& records,
                          std::string& error) {
  if (records.empty ()) return true;
  std::unordered_map<std::string,size_t> by_uuid;
  by_uuid.reserve (records.size ());
  for (size_t i=0; i<records.size (); i++) by_uuid[records[i].uuid]= i;

  Statement names;
  const char* sql= records.size () == 1
    ? "SELECT artifact_uuid,name,name_tree FROM artifact_names WHERE artifact_uuid=?1 "
      "ORDER BY ordinal;"
    : "SELECT artifact_uuid,name,name_tree FROM artifact_names "
      "ORDER BY artifact_uuid,ordinal;";
  if (!prepare (db, sql, names, error))
    return false;
  if (records.size () == 1) bind_text (names.st, 1, records.front ().uuid);
  int rc;
  while ((rc= sqlite3_step (names.st)) == SQLITE_ROW) {
    auto found= by_uuid.find (column_text (names.st, 0));
    if (found != by_uuid.end ()) {
      records[found->second].semantic_names.push_back (column_text (names.st, 1));
      records[found->second].semantic_name_trees.push_back (column_text (names.st, 2));
    }
  }
  if (rc != SQLITE_DONE) {
    error= sqlite3_errmsg (db);
    return false;
  }
  return true;
}

} // namespace

bool
athena_artifacts_query (const fs::path& vault_root,
                        std::vector<AthenaArtifactRecord>& records,
                        std::string& error, bool read_only, bool include_live) {
  records.clear ();
  SqliteDb holder;
  AthenaVaultfileInfo info;
  if (!open_databases (vault_root, holder, info, error, read_only)) return false;
  if (!holder.db) {
    if (include_live) athena_artifact_radioactive_merge (vault_root, records);
    return true;
  }
  Statement st;
  std::string sql= artifact_select_columns (holder.db, error);
  if (!error.empty ()) return false;
  sql += "ORDER BY a.path,a.document_order;";
  if (!prepare (holder.db, sql.c_str (), st, error))
    return false;
  int rc;
  while ((rc= sqlite3_step (st.st)) == SQLITE_ROW) {
    AthenaArtifactRecord record;
    if (!artifact_record_from_statement (st.st, record, error)) return false;
    records.push_back (std::move (record));
  }
  if (rc != SQLITE_DONE) {
    error= sqlite3_errmsg (holder.db);
    return false;
  }
  if (!load_semantic_names (holder.db, records, error) ||
      !upgrade_source_lists (vault_root, holder.db, records, read_only, error)) return false;
  if (include_live) athena_artifact_radioactive_merge (vault_root, records);
  return true;
}

bool athena_artifacts_prune_missing (const fs::path& root, std::string& error) {
  SqliteDb holder;
  AthenaVaultfileInfo info;
  if (!open_databases (root, holder, info, error)) return false;
  Statement rows;
  if (!prepare (holder.db, "SELECT path FROM documents;", rows, error)) return false;
  std::vector<std::string> missing;
  int rc;
  while ((rc= sqlite3_step (rows.st)) == SQLITE_ROW) {
    const auto relative= column_text (rows.st, 0);
    if (!safe_relative_database (relative)) { error= "Invalid artifact source path"; return false; }
    const auto file= root / relative;
    std::error_code ec;
    const bool exists= fs::exists (file, ec);
    if (!ec && !exists && published_buffer_source (file.string ()).first == ATHENA_NO_ACTOR)
      missing.push_back (relative);
  }
  if (rc != SQLITE_DONE) { error= sqlite3_errmsg (holder.db); return false; }
  sqlite3_reset (rows.st);
  if (missing.empty ()) return true;
  if (!exec_sql (holder.db, "BEGIN IMMEDIATE;", error)) return false;
  for (const auto& relative: missing)
    if (!delete_document (holder.db, relative, error)) {
      std::string ignored; exec_sql (holder.db, "ROLLBACK;", ignored); return false;
    }
  if (!exec_sql (holder.db, "COMMIT;", error)) return false;
  for (const auto& relative: missing) athena_artifact_radioactive_saved_document (root, relative, {});
  return true;
}

bool
athena_artifact_query_uuid (const fs::path& vault_root,
                            const std::string& uuid,
                            AthenaArtifactRecord& record, bool& found,
                            std::string& error, bool read_only) {
  found= false;
  std::vector<AthenaArtifactRecord> live;
  athena_artifact_radioactive_merge (vault_root, live);
  for (const auto& item: live) if (item.uuid == uuid) {
    record= item; found= true; return true;
  }
  SqliteDb holder;
  AthenaVaultfileInfo info;
  if (!open_databases (vault_root, holder, info, error, read_only)) return false;
  if (!holder.db) return true;
  Statement st;
  std::string sql= artifact_select_columns (holder.db, error);
  if (!error.empty ()) return false;
  sql += "WHERE a.uuid=?1;";
  if (!prepare (holder.db, sql.c_str (), st, error))
    return false;
  bind_text (st.st, 1, uuid);
  int rc= sqlite3_step (st.st);
  if (rc == SQLITE_DONE) return true;
  if (rc != SQLITE_ROW) {
    error= sqlite3_errmsg (holder.db);
    return false;
  }
  if (!artifact_record_from_statement (st.st, record, error)) return false;
  sqlite3_reset (st.st);
  std::vector<AthenaArtifactRecord> records= {record};
  if (!load_semantic_names (holder.db, records, error) ||
      !upgrade_source_lists (vault_root, holder.db, records, read_only, error)) return false;
  record= std::move (records.front ());
  // A live document replaces its entire saved artifact set, including removals.
  records= {record};
  athena_artifact_radioactive_merge (vault_root, records);
  if (std::none_of (records.begin (), records.end (), [&] (const auto& item) {
        return item.uuid == uuid;
      })) return true;
  found= true;
  return true;
}

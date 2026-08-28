/******************************************************************************
* MODULE     : artifacts.cpp
* DESCRIPTION: Semantic mathematical artifact index for ATHENA vaults
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include "ATHENA/Data/artifacts.hpp"

#include "ATHENA/Data/artifact_identity.hpp"
#include "ATHENA/Data/artifact_radioactive_links.hpp"
#include "ATHENA/Data/artifact_range_llm.hpp"
#include "ATHENA/Data/new_buffer.hpp"
#include "ATHENA/Data/vault.hpp"
#include "ATHENA/Data/vault_maintenance_internal.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"
#include "convert.hpp"
#include "file.hpp"
#include "scheme.hpp"
#include "System/Boot/boot.hpp"

#include <sqlite3.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QRegularExpression>

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
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>
#include <thread>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs= std::filesystem;

namespace {

constexpr size_t range_checkpoint_batch_size= 128;

void artifact_log (const std::string& message) {
  std::cout << "[artifacts] " << message << std::endl;
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

std::string cork_bytes_to_utf8 (const std::string& value) {
  return to_std (cork_to_utf8 (to_tm (value)));
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

std::string column_text (sqlite3_stmt* st, int column);

bool table_has_column (sqlite3* db, const std::string& schema,
                       const std::string& table, const std::string& column,
                       bool& found, std::string& error) {
  Statement statement;
  std::string sql= "PRAGMA " + schema + ".table_info(" + table + ");";
  if (!prepare (db, sql.c_str (), statement, error)) return false;
  found= false;
  while (sqlite3_step (statement.st) == SQLITE_ROW)
    if (column_text (statement.st, 1) == column) {
      found= true;
      break;
    }
  return true;
}

bool ensure_column (sqlite3* db, const std::string& schema,
                    const std::string& table, const std::string& column,
                    const std::string& declaration, std::string& error) {
  bool found= false;
  if (!table_has_column (db, schema, table, column, found, error)) return false;
  if (found) return true;
  return exec_sql (db, "ALTER TABLE " + schema + "." + table +
                         " ADD COLUMN " + column + " " + declaration + ";",
                   error);
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
                     AthenaVaultfileInfo& info, std::string& error) {
  if (!athena_vaultfile_read (root, info, error)) return false;
  if (!safe_relative_database (info.artifacts_path) ||
      !safe_relative_database (info.enunciations_path) ||
      !safe_relative_database (info.bold_text_path)) {
    error= "Artifact database paths in Vaultfile.json must be relative paths";
    return false;
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
    "CREATE TABLE IF NOT EXISTS documents("
    " path TEXT PRIMARY KEY,mtime_ns INTEGER NOT NULL,size INTEGER NOT NULL);"
    "CREATE TABLE IF NOT EXISTS artifact_meta("
    " key TEXT PRIMARY KEY,value TEXT NOT NULL);"
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
    " anchor_stem TEXT NOT NULL,display_text TEXT NOT NULL,"
    " document_order INTEGER NOT NULL,identity_decision TEXT NOT NULL DEFAULT 'new',"
    " identity_evidence TEXT NOT NULL DEFAULT '',"
    " UNIQUE(origin,content_uuid));"
    "CREATE INDEX IF NOT EXISTS artifacts_path_idx ON artifacts(path);"
    "CREATE INDEX IF NOT EXISTS artifacts_search_idx ON artifacts(display_text);"
    "CREATE TABLE IF NOT EXISTS artifact_names("
    " artifact_uuid TEXT NOT NULL,name TEXT NOT NULL,ordinal INTEGER NOT NULL,"
    " PRIMARY KEY(artifact_uuid,ordinal),UNIQUE(artifact_uuid,name),"
    " FOREIGN KEY(artifact_uuid) REFERENCES artifacts(uuid) ON DELETE CASCADE);"
    "CREATE INDEX IF NOT EXISTS artifact_names_name_idx ON artifact_names(name);"
    "CREATE TABLE IF NOT EXISTS artifact_range_cache("
    " path TEXT NOT NULL,mtime_ns INTEGER NOT NULL,size INTEGER NOT NULL,"
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
  for (const std::string& attached: {"enunciations", "bold_text"})
    for (const std::string& column:
         {"identity_focus", "identity_host", "identity_before",
          "identity_after"})
      if (!ensure_column (holder.db, attached, "entries", column,
                          "TEXT NOT NULL DEFAULT ''", error))
        return false;
  if (!ensure_column (holder.db, "main", "artifacts", "identity_decision",
                      "TEXT NOT NULL DEFAULT 'new'", error) ||
      !ensure_column (holder.db, "main", "artifacts", "identity_evidence",
                      "TEXT NOT NULL DEFAULT ''", error))
    return false;
  Statement encoding;
  if (!prepare (holder.db,
                "SELECT value FROM artifact_meta WHERE key='text_encoding';",
                encoding, error))
    return false;
  int rc= sqlite3_step (encoding.st);
  if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
    error= sqlite3_errmsg (holder.db);
    return false;
  }
  if (rc != SQLITE_ROW ||
      column_text (encoding.st, 0) != "utf8-opaque-v1") {
    // Earlier builders treated TeXmacs' Cork bytes as UTF-8 at the worker
    // boundary.  Emptying this incremental stamp table makes every source get
    // revisited while the existing rows remain available for UUID matching.
    if (!exec_sql (holder.db, "BEGIN IMMEDIATE;DELETE FROM documents;"
                              "INSERT OR REPLACE INTO artifact_meta(key,value)"
                              " VALUES('text_encoding','utf8-opaque-v1');COMMIT;",
                   error)) {
      std::string ignored;
      exec_sql (holder.db, "ROLLBACK;", ignored);
      return false;
    }
  }
  Statement record_format;
  if (!prepare (holder.db,
                "SELECT value FROM artifact_meta WHERE key='record_format';",
                record_format, error))
    return false;
  rc= sqlite3_step (record_format.st);
  if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
    error= sqlite3_errmsg (holder.db);
    return false;
  }
  if (rc != SQLITE_ROW || column_text (record_format.st, 0) != "v2") {
    // v2 retains complete enunciation text instead of a truncated display
    // value. Keep old artifact rows available while their source stamps are
    // invalidated so identity association can preserve UUIDs.
    if (!exec_sql (holder.db, "BEGIN IMMEDIATE;DELETE FROM documents;"
                              "INSERT OR REPLACE INTO artifact_meta(key,value)"
                              " VALUES('record_format','v2');COMMIT;",
                   error)) {
      std::string ignored;
      exec_sql (holder.db, "ROLLBACK;", ignored);
      return false;
    }
  }
  Statement semantic_names;
  if (!prepare (holder.db,
                "SELECT value FROM artifact_meta WHERE key='semantic_names';",
                semantic_names, error))
    return false;
  rc= sqlite3_step (semantic_names.st);
  if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
    error= sqlite3_errmsg (holder.db);
    return false;
  }
  if (rc != SQLITE_ROW || column_text (semantic_names.st, 0) != "content-v1") {
    // Navigation labels and semantic names are different representations.
    // Revisit every source once so names are extracted from content while old
    // rows remain available for stable UUID association.
    if (!exec_sql (holder.db, "BEGIN IMMEDIATE;DELETE FROM documents;"
                              "INSERT OR REPLACE INTO artifact_meta(key,value)"
                              " VALUES('semantic_names','content-v1');COMMIT;",
                   error)) {
      std::string ignored;
      exec_sql (holder.db, "ROLLBACK;", ignored);
      return false;
    }
  }
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

std::string plain_text (const tree& t) {
  if (is_atomic (t)) return to_std (t->label);
  std::string tag= tag_name (t);
  if (tag == "label" || tag == "image" || tag == "include" ||
      tag == "bibliography") return "";
  if (formatting_wrapper (tag) && N(t) >= 1) return plain_text (t[N(t)-1]);
  std::string out;
  for (int i=0; i<N(t); i++) {
    std::string part= plain_text (t[i]);
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

bool leading_bold_text (const tree& t, std::string& text) {
  if (!is_compound (t)) return false;
  if (bold_wrapper (t)) {
    text= plain_text (visible_body (t));
    return !text.empty ();
  }
  if (formatting_wrapper (tag_name (t)) && N(t) >= 1)
    return leading_bold_text (t[N(t)-1], text);
  for (int i=0; i<N(t); i++) {
    if (plain_text (t[i]).empty ()) continue;
    return leading_bold_text (t[i], text);
  }
  return false;
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
  static const std::map<std::string,std::pair<std::string,std::string>> tags= {
    {"definition", {"definition", "definition"}},
    {"axiom", {"axiom", "axiom"}},
    {"theorem", {"provable", "theorem"}},
    {"lemma", {"provable", "lemma"}},
    {"corollary", {"provable", "corollary"}},
    {"proposition", {"provable", "proposition"}},
    {"conjecture", {"provable", "conjecture"}},
    {"question", {"provable", "question"}},
    {"example", {"provable", "example"}},
    {"proof", {"completion", "proof"}},
    {"proof-alternative", {"completion", "proof-alternative"}},
    {"alternative-proof", {"completion", "proof-alternative"}},
    {"proof-standard", {"completion", "proof-standard"}},
    {"standard-proof", {"completion", "proof-standard"}},
    {"solution", {"completion", "solution"}},
    {"solution*", {"completion", "solution*"}},
    {"render-proof", {"completion", "proof"}},
    {"render-proof-alternative", {"completion", "proof-alternative"}},
    {"render-proof-standard", {"completion", "proof-standard"}},
    {"render-solution", {"completion", "solution"}},
    {"render-theorem", {"provable", "theorem"}}
  };
  auto found= tags.find (original);
  if (found == tags.end ()) return "";
  base_tag= found->second.second;
  return found->second.first;
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

std::string latex_for_tree (const tree& t) {
  // Standalone artifact readers and unit tests do not boot Guile.  Their range
  // selectors only need a stable textual representation; the full ATHENA
  // process continues to use the normal LaTeX converter below.
  if (headless_mode)
    return cork_bytes_to_utf8 (to_std (tree_to_texmacs (t)));
  try {
    return cork_bytes_to_utf8 (
      to_std (as_string (call ("convert", t, "texmacs-tree",
                               "latex-snippet"))));
  }
  catch (...) {
    return cork_bytes_to_utf8 (to_std (tree_to_texmacs (t)));
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
  if (!enunciation_type (tag_name (t), base).empty ()) return;
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
  return to_std (tree_to_texmacs (value));
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

std::string identity_neighbor (const tree& parent, int start, int step) {
  for (int i=start; i>=0 && i<N(parent); i += step) {
    if (ignorable (parent[i]) || tag_name (parent[i]) == "label") continue;
    return identity_fingerprint (parent[i]);
  }
  return "";
}

void scan_enunciations (const tree& parent, const std::string& rel,
                        std::vector<AthenaArtifactRecord>& out, int& order) {
  if (!is_compound (parent)) return;
  for (int i=0; i<N(parent); i++) {
    const tree& child= parent[i];
    std::string base;
    std::string type= enunciation_type (tag_name (child), base);
    if (!type.empty ()) {
      std::string display= cork_bytes_to_utf8 (plain_text (child));
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
      record.relative_path= rel;
      record.anchor_stem= cork_bytes_to_utf8 (anchor);
      record.display_text= display;
      std::string explicit_title;
      if (leading_bold_text (child, explicit_title))
        explicit_title= cork_bytes_to_utf8 (explicit_title);
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
          if (enunciation_type (tag_name (parent[j]), next_base) ==
              "completion")
            record.proof_uuid= "@order:" + std::to_string (order);
          break;
        }
      }
      out.push_back (record);
      continue;
    }
    scan_enunciations (child, rel, out, order);
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
    if (!enunciation_type (tag_name (child), base).empty ()) {
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
  if (offsets.empty ()) return true;
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

std::string range_request_hash (const AthenaArtifactRangeRequest& request) {
  std::ostringstream canonical;
  canonical << "athena-artifact-range-v7\n"
            << request.keyword_latex.size () << ':' << request.keyword_latex
            << '\n';
  for (const auto& paragraph: request.paragraphs)
    canonical << paragraph.first << ':' << paragraph.second.size () << ':'
              << paragraph.second << '\n';
  return identity_fingerprint (canonical.str ());
}

bool load_range_checkpoint (sqlite3* db, const std::string& path,
                            long long modified, long long size,
                            const std::string& request_hash,
                            const AthenaArtifactRangeRequest& request,
                            std::vector<int>& offsets, bool& found,
                            std::string& error) {
  Statement statement;
  if (!prepare (
        db,
        "SELECT paragraph_offsets FROM artifact_range_cache "
        "WHERE path=?1 AND mtime_ns=?2 AND size=?3 AND request_hash=?4;",
        statement, error))
    return false;
  bind_text (statement.st, 1, path);
  sqlite3_bind_int64 (statement.st, 2, modified);
  sqlite3_bind_int64 (statement.st, 3, size);
  bind_text (statement.st, 4, request_hash);
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
  std::string request_hash;
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
        "INSERT INTO artifact_range_cache(path,mtime_ns,size,request_hash,"
        "paragraph_offsets,updated_at) VALUES(?1,?2,?3,?4,?5,?6) "
        "ON CONFLICT(path,mtime_ns,size,request_hash) DO UPDATE SET "
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
    bind_text (insert.st, 4, checkpoint.request_hash);
    bind_text (insert.st, 5, offsets_text (checkpoint.offsets));
    sqlite3_bind_int64 (insert.st, 6, updated_at);
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
              ExtractedDocument& extracted, std::string& error) {
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
      if (collapse_spaces (display).empty ()) continue;
      std::string serialized= to_std (tree_to_texmacs (keyword));
      int occurrence= ++occurrences[serialized];
      std::vector<std::pair<int,std::string>> candidates;
      for (int offset=-5; offset<=5; offset++) {
        long index= (long) paragraph_index + offset;
        if (index < 0 || index >= (long) paragraphs.size ()) continue;
        if (paragraphs[(size_t) index].segment !=
            paragraphs[paragraph_index].segment) continue;
        candidates.push_back ({offset,
          to_std (tree_to_texmacs (paragraphs[(size_t) index].value))});
      }
      AthenaArtifactRecord record;
      record.type= "definition";
      record.origin= "bold-text";
      record.relative_path= rel;
      record.display_text= cork_bytes_to_utf8 (display);
      record.semantic_names= semantic_names_for (
        record.origin, record.type, record.display_text);
      record.keyword_tree= serialized;
      record.keyword_occurrence= occurrence;
      record.definition_candidates= candidates;
      record.paragraph_offsets= {0};
      record.identity_focus= identity_fingerprint (serialized);
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
      extracted.records.push_back (record);
    }
  }
  return true;
}

QJsonObject record_json (const AthenaArtifactRecord& record) {
  QJsonObject object;
  object["type"]= qstr (record.type);
  object["origin"]= qstr (record.origin);
  object["proof"]= qstr (record.proof_uuid);
  object["path"]= qstr (record.relative_path);
  object["anchor"]= qstr (record.anchor_stem);
  object["display"]= qstr (record.display_text);
  QJsonArray semantic_names;
  for (const std::string& name: record.semantic_names)
    semantic_names.append (qstr (name));
  object["semantic_names"]= semantic_names;
  object["keyword"]= qstr (encode_opaque (record.keyword_tree));
  object["occurrence"]= record.keyword_occurrence;
  object["order"]= record.document_order;
  object["identity_focus"]= qstr (record.identity_focus);
  object["identity_host"]= qstr (record.identity_host);
  object["identity_before"]= qstr (record.identity_before);
  object["identity_after"]= qstr (record.identity_after);
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
  record.proof_uuid= s ("proof");
  record.relative_path= s ("path");
  record.anchor_stem= s ("anchor");
  record.display_text= s ("display");
  for (const QJsonValue& value: object.value ("semantic_names").toArray ()) {
    QByteArray name= value.toString ().toUtf8 ();
    record.semantic_names.emplace_back (name.constData (), (size_t) name.size ());
  }
  record.keyword_tree= decode_opaque (s ("keyword"));
  record.keyword_occurrence= object.value ("occurrence").toInt ();
  record.document_order= object.value ("order").toInt ();
  record.identity_focus= s ("identity_focus");
  record.identity_host= s ("identity_host");
  record.identity_before= s ("identity_before");
  record.identity_after= s ("identity_after");
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
};

bool read_document (const fs::path& path, tree& document, std::string& error);

bool extract_serial (const std::vector<DocumentWork>& work,
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
    if (!extract (document, work[i].rel, extracted[work[i].rel], error))
      return false;
    artifact_log ("extracted " + work[i].rel + ": " +
                  std::to_string (extracted[work[i].rel].records.size ()) +
                  " artifact candidate(s)");
  }
  return true;
}

bool select_definition_ranges (
  sqlite3* db, const std::vector<DocumentWork>& documents,
  std::map<std::string,ExtractedDocument>& extracted,
  const AthenaArtifactsProgress& progress, std::string& error,
  const AthenaArtifactRangeSelector& selector= {}) {
  struct ReleaseRangeModel {
    ~ReleaseRangeModel () { athena_artifact_range_model_release (); }
  } release_range_model;
  struct RangeWork {
    AthenaArtifactRecord* record;
    std::string path;
    long long modified;
    long long size;
    AthenaArtifactRangeRequest request;
    std::string request_hash;
  };
  std::unordered_map<std::string,const DocumentWork*> metadata;
  for (const DocumentWork& document: documents)
    metadata[document.rel]= &document;
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
        texmacs_to_tree (to_tm (record.keyword_tree)));
      record.keyword_latex= request.keyword_latex;
      request.paragraphs.reserve (record.definition_candidates.size ());
      for (const auto& candidate: record.definition_candidates)
        request.paragraphs.push_back (
          {candidate.first, latex_for_tree (
                              texmacs_to_tree (to_tm (candidate.second)))});
      const DocumentWork& source= *document_metadata->second;
      work.push_back ({&record, document.first, source.modified, source.size,
                       std::move (request), {}});
      work.back ().request_hash= range_request_hash (work.back ().request);
    }
  }
  size_t range_total= work.size ();
  artifact_log ("definition-range phase: " + std::to_string (range_total) +
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
                db, work[index].path, work[index].modified, work[index].size,
                work[index].request_hash, work[index].request, selected[index],
                found, error))
      return false;
    if (found) cached++;
    else missing.push_back (index);
    artifact_log ("queued definition range " +
                  std::to_string (index + 1) + "/" +
                  std::to_string (range_total) + " in " + work[index].path +
                  ": \"" + work[index].record->display_text + "\" (" +
                  std::to_string (work[index].request.paragraphs.size ()) +
                  " candidate paragraph(s)" +
                  (found ? ", checkpoint hit)" : ")"));
  }
  artifact_log ("definition-range incremental plan: " +
                std::to_string (cached) + " checkpoint hit(s), " +
                std::to_string (missing.size ()) + " request(s) to evaluate");

  std::string model_path= athena_artifact_range_model_path ();
  if (!selector && !athena_artifact_range_model_available (model_path)) {
    for (size_t index: missing) selected[index]= {0};
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
      checkpoints.push_back ({work[index].path, work[index].modified,
                              work[index].size, work[index].request_hash,
                              selected[index]});
    }
    if (db && !store_range_checkpoints (db, checkpoints, error)) return false;
    artifact_log ("definition-range checkpoint committed: completed=" +
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
    std::ostringstream offsets;
    for (size_t i=0; i<record.paragraph_offsets.size (); i++) {
      if (i) offsets << ',';
      offsets << record.paragraph_offsets[i];
    }
    artifact_log ("definition range selected for \"" + record.display_text +
                  "\" in " + work[index].path + ": [" + offsets.str () +
                  "]");
  }
  auto elapsed= std::chrono::duration_cast<std::chrono::milliseconds> (
    std::chrono::steady_clock::now () - started).count ();
  artifact_log ("definition-range phase complete: " +
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
  for (auto& document: extracted) {
    auto& records= document.second.records;
    records.erase (
      std::remove_if (records.begin (), records.end (), [] (const auto& record) {
        return record.origin == "bold-text" && record.paragraph_offsets.empty ();
      }), records.end ());
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
                       std::map<std::string,ExtractedDocument>& extracted,
                       const AthenaArtifactsProgress& progress,
                       const AthenaArtifactRangeSelector& selector,
                       std::string& error) {
  if (work.size () < 2) {
    if (!extract_serial (work, extracted, progress, error)) return false;
    return select_definition_ranges (
      db, work, extracted, progress, error, selector);
  }
  const char* configured= std::getenv ("ATHENA_ARTIFACT_WORKER_EXECUTABLE");
  fs::path executable= configured && *configured ? fs::path (configured)
                                                  : current_executable_path ();
  if (executable.empty () || !fs::exists (executable)) {
    if (!extract_serial (work, extracted, progress, error)) return false;
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
    QByteArray bytes= QJsonDocument (documents).toJson (QJsonDocument::Compact);
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
      artifact_log ("extracted " + rel + ": " +
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
                        std::map<std::string,ExtractedDocument>& extracted,
                        const AthenaArtifactsProgress& progress,
                        const AthenaArtifactRangeSelector& selector,
                        std::string& error) {
#if defined(__unix__) || defined(__APPLE__)
  return extract_parallel (db, work, extracted, progress, selector, error);
#else
  if (!extract_serial (work, extracted, progress, error)) return false;
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

bool read_document (const fs::path& path, tree& document, std::string& error) {
  std::string bytes;
  if (!read_file_bytes (path, bytes)) {
    error= "Could not read " + path.string ();
    return false;
  }
  try { document= texmacs_document_to_tree (to_tm (bytes)); }
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
      observation.focus= identity_fingerprint (keyword);
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

bool replace_document (sqlite3* db, const std::string& rel,
                       ExtractedDocument& extracted, long long modified,
                       long long size, std::string& error) {
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
      "anchor_stem,display_text,document_order,identity_decision,identity_evidence) "
      "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11);", insert_artifact, error))
    return false;
  if (!prepare (db,
                "INSERT INTO artifact_names(artifact_uuid,name,ordinal) "
                "VALUES(?1,?2,?3);", insert_name, error))
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
    std::string artifact_key= record.origin + char (31) + record.content_uuid;
    record.uuid= artifact_ids.count (artifact_key) ? artifact_ids[artifact_key]
                                                   : generate_uuid_v4 ();
    sqlite3_reset (insert_artifact.st);
    sqlite3_clear_bindings (insert_artifact.st);
    bind_text (insert_artifact.st, 1, record.uuid);
    bind_text (insert_artifact.st, 2, record.type);
    bind_text (insert_artifact.st, 3, record.origin);
    bind_text (insert_artifact.st, 4, record.content_uuid);
    if (record.proof_uuid.empty ()) sqlite3_bind_null (insert_artifact.st, 5);
    else bind_text (insert_artifact.st, 5, record.proof_uuid);
    bind_text (insert_artifact.st, 6, rel);
    bind_text (insert_artifact.st, 7, record.anchor_stem);
    bind_text (insert_artifact.st, 8, record.display_text);
    sqlite3_bind_int (insert_artifact.st, 9, record.document_order);
    bind_text (insert_artifact.st, 10, record.identity_decision);
    bind_text (insert_artifact.st, 11, record.identity_evidence);
    if (sqlite3_step (insert_artifact.st) != SQLITE_DONE) {
      error= sqlite3_errmsg (db); return false;
    }
    for (size_t i=0; i<record.semantic_names.size (); i++) {
      sqlite3_reset (insert_name.st);
      sqlite3_clear_bindings (insert_name.st);
      bind_text (insert_name.st, 1, record.uuid);
      bind_text (insert_name.st, 2, record.semantic_names[i]);
      sqlite3_bind_int (insert_name.st, 3, (int) i);
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
      "INSERT INTO documents(path,mtime_ns,size) VALUES(?1,?2,?3) "
      "ON CONFLICT(path) DO UPDATE SET mtime_ns=excluded.mtime_ns,"
      "size=excluded.size;", doc, error)) return false;
  bind_text (doc.st, 1, rel);
  sqlite3_bind_int64 (doc.st, 2, modified);
  sqlite3_bind_int64 (doc.st, 3, size);
  if (sqlite3_step (doc.st) != SQLITE_DONE) {
    error= sqlite3_errmsg (db); return false;
  }
  Statement prune_cache;
  if (!prepare (
        db,
        "DELETE FROM artifact_range_cache WHERE path=?1 AND "
        "(mtime_ns<>?2 OR size<>?3);",
        prune_cache, error))
    return false;
  bind_text (prune_cache.st, 1, rel);
  sqlite3_bind_int64 (prune_cache.st, 2, modified);
  sqlite3_bind_int64 (prune_cache.st, 3, size);
  if (sqlite3_step (prune_cache.st) != SQLITE_DONE) {
    error= sqlite3_errmsg (db);
    return false;
  }
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

bool is_current_fingerprint (sqlite3* db, const std::string& rel,
                             long long modified, long long size,
                             std::string& error) {
  Statement st;
  if (!prepare (db, "SELECT mtime_ns,size FROM documents WHERE path=?1;",
                st, error)) return false;
  bind_text (st.st, 1, rel);
  int rc= sqlite3_step (st.st);
  return rc == SQLITE_ROW && sqlite3_column_int64 (st.st, 0) == modified &&
         sqlite3_column_int64 (st.st, 1) == size;
}

} // namespace

bool
athena_artifacts_run_extract_worker (const fs::path& manifest,
                                     const fs::path& output,
                                     std::string& error) {
  std::ifstream input (manifest, std::ios::binary);
  std::string bytes ((std::istreambuf_iterator<char> (input)), {});
  QJsonDocument request= QJsonDocument::fromJson (
    QByteArray (bytes.data (), (qsizetype) bytes.size ()));
  if (!input.good () && !input.eof ()) error= "Could not read worker manifest";
  else if (!request.isArray ()) error= "Invalid artifact worker manifest";

  QJsonArray documents;
  if (error.empty ()) {
    for (const QJsonValue& value: request.array ()) {
      QJsonObject source= value.toObject ();
      fs::path file (source.value ("file").toString ().toStdString ());
      std::string rel= source.value ("path").toString ().toStdString ();
      tree document;
      ExtractedDocument extracted;
      if (!read_document (file, document, error) ||
          !extract (document, rel, extracted, error)) break;
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
athena_artifacts_extract_document (
  const tree& document, const std::string& relative_path,
  std::vector<AthenaArtifactRecord>& records, std::string& error) {
  std::map<std::string,ExtractedDocument> extracted;
  std::vector<DocumentWork> source= {
    {fs::path (), relative_path, 0, 0}
  };
  if (!extract (document, relative_path, extracted[relative_path], error) ||
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

  std::vector<Paragraph> paragraphs;
  collect_paragraphs (body, paragraphs);
  std::unordered_map<std::string,int> occurrences;
  long focus= -1;
  for (size_t i=0; i<paragraphs.size (); i++) {
    std::vector<tree> bolds;
    find_bold (paragraphs[i].value, bolds);
    for (const tree& keyword: bolds) {
      std::string display= plain_text (visible_body (keyword));
      if (collapse_spaces (display).empty ()) continue;
      std::string serialized= to_std (tree_to_texmacs (keyword));
      int occurrence= ++occurrences[serialized];
      if (serialized == record.keyword_tree &&
          occurrence == record.keyword_occurrence) {
        focus= (long) i;
        break;
      }
    }
    if (focus >= 0) break;
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
  for (int offset: record.paragraph_offsets) {
    long index= focus + offset;
    if (index < 0 || index >= (long) paragraphs.size () ||
        paragraphs[(size_t) index].segment !=
          paragraphs[(size_t) focus].segment) {
      error= "Artifact paragraph range is stale";
      return false;
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

bool
athena_artifacts_build (
  const fs::path& vault_root,
  const std::vector<fs::path>& requested_documents, bool full_vault,
  const AthenaArtifactsProgress& progress, AthenaArtifactsBuildResult& result,
  std::string& error, const AthenaArtifactsBuildOptions& options) {
  result= AthenaArtifactsBuildResult ();
  fs::path root= normalize_root (vault_root);
  artifact_log ("build started: root=" + root.string () +
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
  artifact_log ("databases: artifacts=" + (root / info.artifacts_path).string () +
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
  artifact_log ("discovered " + std::to_string (documents.size ()) +
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
    if (full_vault && is_current_fingerprint (holder.db, rel, modified, size,
                                              error)) continue;
    if (!error.empty ()) return false;
    work.push_back ({path, rel, modified, size});
  }
  artifact_log ("incremental plan: rebuild " + std::to_string (work.size ()) +
                " document(s), purge " + std::to_string (deleted.size ()) +
                " deleted document(s)");

  std::map<std::string,ExtractedDocument> extracted;
  if (!extract_documents (
        holder.db, work, extracted, progress, options.range_selector, error))
    return false;

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
    artifact_log ("purged deleted document from artifact databases: " + rel);
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
                           item.size, error)) {
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
    artifact_log ("wrote " + item.rel + ": " +
                  std::to_string (found->second.records.size ()) +
                  " artifact(s)");
    if (!report_progress (progress,
                          AthenaArtifactsBuildPhase::WritingDatabase,
                          written, write_total, item.rel)) {
      error= "Artifact build cancelled"; rollback (); return false;
    }
  }
  if (!exec_sql (holder.db, "COMMIT;", error)) { rollback (); return false; }
  committed= true;
  if (!work.empty () || !deleted.empty ())
    athena_artifact_radioactive_invalidate ();
  report_progress (progress, AthenaArtifactsBuildPhase::Complete, 1, 1);
  artifact_log ("build complete: " + std::to_string (result.artifacts) +
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

const char* artifact_select_columns () {
  return
    "SELECT a.uuid,a.type,a.origin,a.content_uuid,COALESCE(a.proof_uuid,''),"
    "a.path,a.anchor_stem,a.display_text,a.document_order,"
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
    "a.identity_decision,a.identity_evidence FROM artifacts a "
    "LEFT JOIN bold_text.entries b ON a.origin='bold-text' AND "
    "b.uuid=a.content_uuid LEFT JOIN enunciations.entries e ON "
    "a.origin='enunciation' AND e.uuid=a.content_uuid ";
}

AthenaArtifactRecord artifact_record_from_statement (sqlite3_stmt* statement) {
  AthenaArtifactRecord record;
  record.uuid= column_text (statement, 0);
  record.type= column_text (statement, 1);
  record.origin= column_text (statement, 2);
  record.content_uuid= column_text (statement, 3);
  record.proof_uuid= column_text (statement, 4);
  record.relative_path= column_text (statement, 5);
  record.anchor_stem= column_text (statement, 6);
  record.display_text= column_text (statement, 7);
  record.document_order= sqlite3_column_int (statement, 8);
  record.keyword_tree= decode_opaque (column_text (statement, 9));
  record.keyword_occurrence= sqlite3_column_int (statement, 10);
  record.paragraph_offsets= parse_offsets (column_text (statement, 11));
  record.identity_focus= column_text (statement, 12);
  record.identity_host= column_text (statement, 13);
  record.identity_before= column_text (statement, 14);
  record.identity_after= column_text (statement, 15);
  record.identity_decision= column_text (statement, 16);
  record.identity_evidence= column_text (statement, 17);
  return record;
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
    ? "SELECT artifact_uuid,name FROM artifact_names WHERE artifact_uuid=?1 "
      "ORDER BY ordinal;"
    : "SELECT artifact_uuid,name FROM artifact_names "
      "ORDER BY artifact_uuid,ordinal;";
  if (!prepare (db, sql, names, error))
    return false;
  if (records.size () == 1) bind_text (names.st, 1, records.front ().uuid);
  int rc;
  while ((rc= sqlite3_step (names.st)) == SQLITE_ROW) {
    auto found= by_uuid.find (column_text (names.st, 0));
    if (found != by_uuid.end ())
      records[found->second].semantic_names.push_back (column_text (names.st, 1));
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
                        std::string& error) {
  records.clear ();
  SqliteDb holder;
  AthenaVaultfileInfo info;
  if (!open_databases (vault_root, holder, info, error)) return false;
  Statement st;
  std::string sql= std::string (artifact_select_columns ()) +
                   "ORDER BY a.path,a.document_order;";
  if (!prepare (holder.db, sql.c_str (), st, error))
    return false;
  int rc;
  while ((rc= sqlite3_step (st.st)) == SQLITE_ROW)
    records.push_back (artifact_record_from_statement (st.st));
  if (rc != SQLITE_DONE) {
    error= sqlite3_errmsg (holder.db);
    return false;
  }
  return load_semantic_names (holder.db, records, error);
}

bool
athena_artifact_query_uuid (const fs::path& vault_root,
                            const std::string& uuid,
                            AthenaArtifactRecord& record, bool& found,
                            std::string& error) {
  found= false;
  SqliteDb holder;
  AthenaVaultfileInfo info;
  if (!open_databases (vault_root, holder, info, error)) return false;
  Statement st;
  std::string sql= std::string (artifact_select_columns ()) +
                   "WHERE a.uuid=?1;";
  if (!prepare (holder.db, sql.c_str (), st, error))
    return false;
  bind_text (st.st, 1, uuid);
  int rc= sqlite3_step (st.st);
  if (rc == SQLITE_DONE) return true;
  if (rc != SQLITE_ROW) {
    error= sqlite3_errmsg (holder.db);
    return false;
  }
  record= artifact_record_from_statement (st.st);
  std::vector<AthenaArtifactRecord> records= {record};
  if (!load_semantic_names (holder.db, records, error)) return false;
  record= std::move (records.front ());
  found= true;
  return true;
}

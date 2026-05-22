/******************************************************************************
* MODULE     : namespaces.cpp
* DESCRIPTION: ATHENA vault namespace registry
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "namespaces.hpp"

#include "analyze.hpp"
#include "convert.hpp"
#include "file.hpp"
#include "sys_utils.hpp"
#include "tm_configure.hpp"
#include "tm_timer.hpp"
#include "vault.hpp"

#include <libtcc.h>
#include <sqlite3.h>

#include <QApplication>
#include <QMessageBox>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

using std::size_t;

namespace {

static std::string
tm_to_std (string s) {
  return std::string (as_charp (s));
}

static string
std_to_tm (const std::string& s) {
  return string (s.c_str ());
}

static std::string
trim_std (const std::string& s) {
  size_t a= 0, b= s.size ();
  while (a < b && std::isspace ((unsigned char) s[a])) a++;
  while (b > a && std::isspace ((unsigned char) s[b - 1])) b--;
  return s.substr (a, b - a);
}

static strings
split_list (string s) {
  strings out;
  std::string x= tm_to_std (s);
  size_t start= 0;
  while (start <= x.size ()) {
    size_t p= x.find (',', start);
    std::string item= trim_std (
      x.substr (start, p == std::string::npos ? std::string::npos : p - start));
    if (!item.empty ()) out << std_to_tm (item);
    if (p == std::string::npos) break;
    start= p + 1;
  }
  return out;
}

static string
join_list (const strings& s) {
  string out= "";
  for (int i=0; i<N(s); i++) {
    if (i != 0) out << ", ";
    out << s[i];
  }
  return out;
}

static bool
has_string (const strings& xs, string x) {
  for (int i=0; i<N(xs); i++)
    if (xs[i] == x) return true;
  return false;
}

static string canonical_kind (string kind);

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
      "  style_path TEXT NOT NULL DEFAULT ''"
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
                          "INTEGER NOT NULL DEFAULT 0", error);
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
query_parent_list (sqlite3* db, string child, string source, strings& out,
                   string& error) {
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
    if (status == SQLITE_ROW) out << column_tm_string (st, 0);
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
        "SELECT name, kind, template, sorter_trivial, sorter_path, style_path "
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
  sqlite3_finalize (st);
  out.parents= strings ();
  out.derived_parents= strings ();
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

static string
canonical_kind (string kind) {
  if (kind == "abstract" || kind == "semi-concrete" || kind == "concrete")
    return kind;
  return "concrete";
}

static std::string
percent_decode (const std::string& s) {
  std::string out;
  for (size_t i=0; i<s.size (); i++) {
    if (s[i] == '%' && i + 2 < s.size ()) {
      int hi= std::isxdigit ((unsigned char) s[i + 1]) ?
              std::toupper ((unsigned char) s[i + 1]) : -1;
      int lo= std::isxdigit ((unsigned char) s[i + 2]) ?
              std::toupper ((unsigned char) s[i + 2]) : -1;
      if (hi >= 0 && lo >= 0) {
        hi= hi <= '9' ? hi - '0' : hi - 'A' + 10;
        lo= lo <= '9' ? lo - '0' : lo - 'A' + 10;
        out.push_back ((char) ((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    out.push_back (s[i]);
  }
  return out;
}

static std::vector<string>
split_tmfs_path (string path) {
  std::vector<string> out;
  std::string s= tm_to_std (path);
  size_t start= 0;
  while (start <= s.size ()) {
    size_t p= s.find ('/', start);
    std::string item= percent_decode (
      s.substr (start, p == std::string::npos ? std::string::npos : p - start));
    if (!item.empty ()) out.push_back (std_to_tm (item));
    if (p == std::string::npos) break;
    start= p + 1;
  }
  return out;
}

enum ns_field_type {
  ns_string_field,
  ns_word_field,
  ns_char_field,
  ns_int_field,
  ns_pos_int_field,
  ns_roman_field
};

static const char*
field_type_name (ns_field_type t) {
  switch (t) {
  case ns_string_field: return "string";
  case ns_word_field: return "word";
  case ns_char_field: return "char";
  case ns_int_field: return "int";
  case ns_pos_int_field: return "positive-int";
  case ns_roman_field: return "roman";
  }
  return "string";
}

struct template_token {
  bool          field;
  std::string   literal;
  ns_field_type type;
};

static bool
parse_template (string templ, std::vector<template_token>& out,
                string& error) {
  std::string t= tm_to_std (templ);
  std::string lit;
  for (size_t i=0; i<t.size (); i++) {
    if (t[i] != '%') {
      lit.push_back (t[i]);
      continue;
    }
    if (i + 1 >= t.size ()) {
      error= "Template ends with a bare %.";
      return false;
    }
    if (t[i + 1] == '%') {
      lit.push_back ('%');
      i++;
      continue;
    }
    if (!lit.empty ()) {
      template_token tok;
      tok.field= false;
      tok.literal= lit;
      tok.type= ns_string_field;
      out.push_back (tok);
      lit.clear ();
    }
    template_token tok;
    tok.field= true;
    tok.literal= "";
    switch (t[i + 1]) {
    case 's': tok.type= ns_string_field; break;
    case 'w': tok.type= ns_word_field; break;
    case 'c': tok.type= ns_char_field; break;
    case 'd': tok.type= ns_int_field; break;
    case 'N': tok.type= ns_pos_int_field; break;
    case 'R': tok.type= ns_roman_field; break;
    default:
      error= "Unknown namespace template placeholder %" *
             std_to_tm (std::string (1, t[i + 1])) * ".";
      return false;
    }
    out.push_back (tok);
    i++;
  }
  if (!lit.empty ()) {
    template_token tok;
    tok.field= false;
    tok.literal= lit;
    tok.type= ns_string_field;
    out.push_back (tok);
  }
  return true;
}

static size_t
utf8_char_len (const std::string& s, size_t pos) {
  if (pos >= s.size ()) return 0;
  unsigned char c= (unsigned char) s[pos];
  if ((c & 0x80) == 0) return 1;
  if ((c & 0xe0) == 0xc0 && pos + 1 < s.size ()) return 2;
  if ((c & 0xf0) == 0xe0 && pos + 2 < s.size ()) return 3;
  if ((c & 0xf8) == 0xf0 && pos + 3 < s.size ()) return 4;
  return 1;
}

static bool
is_space_byte (char c) {
  return std::isspace ((unsigned char) c) != 0;
}

static int
roman_value (char c) {
  switch (std::toupper ((unsigned char) c)) {
  case 'I': return 1;
  case 'V': return 5;
  case 'X': return 10;
  case 'L': return 50;
  case 'C': return 100;
  case 'D': return 500;
  case 'M': return 1000;
  }
  return 0;
}

static int
parse_roman_value (const std::string& s) {
  if (s.empty ()) return 0;
  int total= 0;
  int prev= 0;
  for (int i=(int) s.size () - 1; i>=0; i--) {
    int v= roman_value (s[(size_t) i]);
    if (v == 0) return 0;
    if (v < prev) total -= v;
    else {
      total += v;
      prev= v;
    }
  }
  return total;
}

static bool
match_field_lengths (ns_field_type type, const std::string& stem, size_t pos,
                     std::vector<size_t>& lens) {
  if (pos >= stem.size ()) return false;

  if (type == ns_char_field) {
    size_t len= utf8_char_len (stem, pos);
    if (len == 0) return false;
    lens.push_back (len);
    return true;
  }

  if (type == ns_word_field) {
    size_t end= pos;
    while (end < stem.size () && !is_space_byte (stem[end])) end++;
    if (end == pos) return false;
    lens.push_back (end - pos);
    return true;
  }

  if (type == ns_int_field || type == ns_pos_int_field) {
    size_t end= pos;
    if (type == ns_int_field && stem[end] == '-') end++;
    size_t digits= end;
    while (end < stem.size () && std::isdigit ((unsigned char) stem[end]))
      end++;
    if (end == digits) return false;
    if (type == ns_pos_int_field) {
      long long v= std::strtoll (stem.substr (pos, end - pos).c_str (), nullptr,
                                 10);
      if (v <= 0) return false;
    }
    lens.push_back (end - pos);
    return true;
  }

  if (type == ns_roman_field) {
    size_t end= pos;
    while (end < stem.size () && roman_value (stem[end]) != 0) end++;
    if (end == pos) return false;
    for (size_t len=end - pos; len>0; len--)
      lens.push_back (len);
    return true;
  }

  for (size_t end= stem.size (); end>pos; end--)
    lens.push_back (end - pos);
  return true;
}

struct match_candidate {
  std::vector<std::string> captures;
  std::vector<ns_field_type> types;
};

static void
match_backtrack (const std::vector<template_token>& toks,
                 const std::string& stem, size_t tok_i, size_t pos,
                 match_candidate cur, std::vector<match_candidate>& out) {
  if (out.size () > 256) return;
  if (tok_i == toks.size ()) {
    if (pos == stem.size ()) out.push_back (cur);
    return;
  }
  const template_token& tok= toks[tok_i];
  if (!tok.field) {
    if (stem.compare (pos, tok.literal.size (), tok.literal) == 0)
      match_backtrack (toks, stem, tok_i + 1, pos + tok.literal.size (), cur,
                       out);
    return;
  }

  std::vector<size_t> lens;
  if (!match_field_lengths (tok.type, stem, pos, lens)) return;
  for (size_t len: lens) {
    std::string cap= stem.substr (pos, len);
    if (tok.type == ns_roman_field && parse_roman_value (cap) <= 0) continue;
    match_candidate next= cur;
    next.captures.push_back (cap);
    next.types.push_back (tok.type);
    match_backtrack (toks, stem, tok_i + 1, pos + len, next, out);
  }
}

static bool
better_candidate (const match_candidate& a, const match_candidate& b) {
  size_t n= std::min (a.captures.size (), b.captures.size ());
  for (size_t i=0; i<n; i++) {
    if (a.captures[i].size () != b.captures[i].size ())
      return a.captures[i].size () > b.captures[i].size ();
    if (a.captures[i] != b.captures[i])
      return a.captures[i] < b.captures[i];
  }
  return a.captures.size () < b.captures.size ();
}

static bool
match_stem (const athena_namespace_definition& ns, const std::string& stem,
            athena_namespace_match& out, string& error) {
  std::vector<template_token> toks;
  if (!parse_template (ns.templ, toks, error)) return false;
  std::vector<match_candidate> matches;
  match_backtrack (toks, stem, 0, 0, match_candidate (), matches);
  if (matches.empty ()) return false;
  std::sort (matches.begin (), matches.end (), better_candidate);

    out.stem= std_to_tm (stem);
    out.ambiguous= matches.size () > 1;
    for (size_t i=0; i<matches[0].captures.size (); i++) {
      out.captures << std_to_tm (matches[0].captures[i]);
      out.capture_types << std_to_tm (field_type_name (matches[0].types[i]));
    }
  return true;
}

static std::string
stem_for_file (url u) {
  std::string p= tm_to_std (concretize (u));
  std::filesystem::path fp (p);
  return fp.stem ().string ();
}

static bool
is_ath_file (url u) {
  return suffix (u) == "ath";
}

extern "C" {
enum AthenaNsFieldType {
  ATHENA_NS_STRING,
  ATHENA_NS_WORD,
  ATHENA_NS_CHAR,
  ATHENA_NS_INT,
  ATHENA_NS_POS_INT,
  ATHENA_NS_ROMAN
};

typedef struct {
  const char* text;
  int         type;
  long long   integer;
  int         roman;
} AthenaNsField;
}

extern "C" int
athena_ns_strcmp (const char* a, const char* b) {
  if (a == nullptr) a= "";
  if (b == nullptr) b= "";
  return std::strcmp (a, b);
}

extern "C" int
athena_ns_strcasecmp (const char* a, const char* b) {
  if (a == nullptr) a= "";
  if (b == nullptr) b= "";
  return strcasecmp (a, b);
}

extern "C" int
athena_ns_cmp_int (long long a, long long b) {
  return (a > b) - (a < b);
}

extern "C" int
athena_ns_roman_value (const char* s) {
  return parse_roman_value (s == nullptr ? std::string () : std::string (s));
}

extern "C" int
athena_ns_cmp_roman (int a, int b) {
  return (a > b) - (a < b);
}

typedef int (*ns_compare_fn) (int, const AthenaNsField*, const AthenaNsField*);

struct sorter_cache_entry {
  TCCState*     state= nullptr;
  ns_compare_fn fn= nullptr;
  time_t        mtime= 0;
  std::string   error;
};

static std::map<std::string, sorter_cache_entry> sorter_cache;

static std::string
read_file_std (const std::string& path) {
  std::ifstream in (path, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf ();
  return ss.str ();
}

static time_t
mtime_for_path (const std::string& path) {
  struct stat st;
  if (stat (path.c_str (), &st) != 0) return 0;
  return st.st_mtime;
}

static std::string
resolve_vault_relative_path (string rel) {
  std::string p= tm_to_std (rel);
  if (p.empty ()) return "";
  std::filesystem::path fp (p);
  if (fp.is_absolute ()) return fp.string ();
  url u= vault_get_root () * url (rel);
  return tm_to_std (concretize (u));
}

static void
tcc_error_cb (void* opaque, const char* msg) {
  std::string* error= static_cast<std::string*> (opaque);
  if (!error->empty ()) *error += "\n";
  *error += msg == nullptr ? "unknown libtcc error" : msg;
}

static ns_compare_fn
load_sorter (string sorter_path, string& error) {
  std::string path= resolve_vault_relative_path (sorter_path);
  if (path.empty ()) return nullptr;
  time_t mt= mtime_for_path (path);

  sorter_cache_entry& ent= sorter_cache[path];
  if (ent.fn != nullptr && ent.mtime == mt) return ent.fn;
  if (!ent.error.empty () && ent.mtime == mt) {
    error= std_to_tm (ent.error);
    return nullptr;
  }
  if (ent.state != nullptr) {
    tcc_delete (ent.state);
    ent.state= nullptr;
    ent.fn= nullptr;
    ent.error.clear ();
  }
  ent.mtime= mt;

  std::ifstream probe (path);
  if (!probe.good ()) {
    ent.error= "Cannot open sorter source: " + path;
    error= std_to_tm (ent.error);
    return nullptr;
  }
  std::string source= read_file_std (path);
  static const char* abi=
    "typedef enum {\n"
    "  ATHENA_NS_STRING,\n"
    "  ATHENA_NS_WORD,\n"
    "  ATHENA_NS_CHAR,\n"
    "  ATHENA_NS_INT,\n"
    "  ATHENA_NS_POS_INT,\n"
    "  ATHENA_NS_ROMAN\n"
    "} AthenaNsFieldType;\n"
    "typedef struct {\n"
    "  const char* text;\n"
    "  int type;\n"
    "  long long integer;\n"
    "  int roman;\n"
    "} AthenaNsField;\n"
    "int athena_ns_strcmp(const char*, const char*);\n"
    "int athena_ns_strcasecmp(const char*, const char*);\n"
    "int athena_ns_cmp_int(long long, long long);\n"
    "int athena_ns_cmp_roman(int, int);\n"
    "int athena_ns_roman_value(const char*);\n";

  std::string errors;
  TCCState* s= tcc_new ();
  if (s == nullptr) {
    ent.error= "Cannot initialize libtcc.";
    error= std_to_tm (ent.error);
    return nullptr;
  }
  tcc_set_error_func (s, &errors, tcc_error_cb);
  tcc_set_output_type (s, TCC_OUTPUT_MEMORY);
  std::string program= std::string (abi) + "\n" + source;
  if (tcc_compile_string (s, program.c_str ()) < 0) {
    ent.error= errors.empty () ? "Sorter compilation failed." : errors;
    error= std_to_tm (ent.error);
    tcc_delete (s);
    return nullptr;
  }

  tcc_add_symbol (s, "athena_ns_strcmp", (void*) athena_ns_strcmp);
  tcc_add_symbol (s, "athena_ns_strcasecmp", (void*) athena_ns_strcasecmp);
  tcc_add_symbol (s, "athena_ns_cmp_int", (void*) athena_ns_cmp_int);
  tcc_add_symbol (s, "athena_ns_cmp_roman", (void*) athena_ns_cmp_roman);
  tcc_add_symbol (s, "athena_ns_roman_value", (void*) athena_ns_roman_value);

#ifdef TCC_RELOCATE_AUTO
  if (tcc_relocate (s, TCC_RELOCATE_AUTO) < 0) {
#else
  if (tcc_relocate (s) < 0) {
#endif
    ent.error= errors.empty () ? "Sorter relocation failed." : errors;
    error= std_to_tm (ent.error);
    tcc_delete (s);
    return nullptr;
  }
  void* sym= tcc_get_symbol (s, "athena_ns_compare");
  if (sym == nullptr) {
    ent.error= "Sorter must define athena_ns_compare.";
    error= std_to_tm (ent.error);
    tcc_delete (s);
    return nullptr;
  }
  ent.state= s;
  ent.fn= reinterpret_cast<ns_compare_fn> (sym);
  ent.error.clear ();
  return ent.fn;
}

static AthenaNsField
to_c_field (const athena_namespace_match& m, int i) {
  AthenaNsField f;
  f.text= as_charp (m.captures[i]);
  string type= m.capture_types[i];
  f.type= ATHENA_NS_STRING;
  if (type == "word") f.type= ATHENA_NS_WORD;
  else if (type == "char") f.type= ATHENA_NS_CHAR;
  else if (type == "int") f.type= ATHENA_NS_INT;
  else if (type == "positive-int") f.type= ATHENA_NS_POS_INT;
  else if (type == "roman") f.type= ATHENA_NS_ROMAN;
  f.integer= std::strtoll (as_charp (m.captures[i]), nullptr, 10);
  f.roman= athena_ns_roman_value (f.text);
  return f;
}

static int
compare_with_sorter (ns_compare_fn fn, const athena_namespace_match& a,
                     const athena_namespace_match& b) {
  if (fn == nullptr) return 0;
  int n= std::min (N(a.captures), N(b.captures));
  std::vector<AthenaNsField> aa, bb;
  for (int i=0; i<n; i++) {
    aa.push_back (to_c_field (a, i));
    bb.push_back (to_c_field (b, i));
  }
  return fn (n, aa.data (), bb.data ());
}

static tree
text (const std::string& s) {
  return tree (std_to_tm (s));
}

static tree
line (const std::string& s) {
  return compound ("paragraph*", text (s));
}

static tree
line_tm (string s) {
  return compound ("paragraph*", tree (s));
}

static tree
document_for_body (tree body) {
  tree doc (DOCUMENT);
  doc << compound ("TeXmacs", TEXMACS_COMPAT_VERSION);
  doc << compound ("style", tuple ("generic"));
  doc << compound ("body", body);
  return doc;
}

static tree
error_page (const std::string& title, const std::string& message) {
  tree body (DOCUMENT);
  body << compound ("section*", text (title));
  body << line (message);
  return document_for_body (body);
}

} // anonymous namespace

std::vector<athena_namespace_definition>
athena_namespaces_list () {
  std::vector<athena_namespace_definition> out;
  if (!ns_db_exists ()) return out;

  string error;
  ns_sqlite_connection cx;
  if (!cx.open (false, error)) return out;

  sqlite3_stmt* st= nullptr;
  if (!prepare_sql (cx.db,
        "SELECT name FROM namespaces ORDER BY name;",
        &st, error)) return out;
  while (true) {
    int status= sqlite3_step (st);
    if (status == SQLITE_DONE) break;
    if (status != SQLITE_ROW) {
      sqlite3_finalize (st);
      return out;
    }
    athena_namespace_definition ns;
    string name= column_tm_string (st, 0);
    string err;
    if (get_namespace_from_db (cx.db, name, ns, err)) out.push_back (ns);
  }
  sqlite3_finalize (st);
  return out;
}

bool
athena_namespace_get (string name, athena_namespace_definition& out) {
  if (!ns_db_exists () || name == "") return false;
  string error;
  ns_sqlite_connection cx;
  if (!cx.open (false, error)) return false;
  return get_namespace_from_db (cx.db, name, out, error);
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
  string kind= canonical_kind (ns.kind);
  if ((kind == "semi-concrete" || kind == "concrete") && ns.templ == "") {
    error= "Concrete namespaces need a filename template.";
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
      "(name, kind, template, sorter_trivial, sorter_path, style_path) "
      "VALUES(?, ?, ?, ?, ?, ?) "
      "ON CONFLICT(name) DO UPDATE SET "
      "  kind=excluded.kind,"
      "  template=excluded.template,"
      "  sorter_trivial=excluded.sorter_trivial,"
      "  sorter_path=excluded.sorter_path,"
      "  style_path=excluded.style_path;",
      { ns.name, kind, ns.templ, ns.sorter_trivial ? "1" : "0",
        ns.sorter_path, ns.style_path },
      error) &&
    exec_prepared (cx.db,
      "DELETE FROM namespace_parents WHERE child=?;",
      { ns.name }, error);

  for (int i=0; ok && i<N(ns.parents); i++)
    ok= exec_prepared (cx.db,
      "INSERT OR REPLACE INTO namespace_parents"
      "(child, parent, source, ord) VALUES(?, ?, 'declared', ?);",
      { ns.name, ns.parents[i], std_to_tm (std::to_string (i)) }, error);

  for (int i=0; ok && i<N(ns.derived_parents); i++)
    ok= exec_prepared (cx.db,
      "INSERT OR REPLACE INTO namespace_parents"
      "(child, parent, source, ord) VALUES(?, ?, 'derived', ?);",
      { ns.name, ns.derived_parents[i], std_to_tm (std::to_string (i)) },
      error);

  for (int i=0; i<N(ns.derived_parents); i++) {
    if (!ok) break;
    ok= upsert_relation_decision (cx.db, ns.derived_parents[i], ns.name,
                                  "allow", "derived", error);
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
  return ok;
}

std::vector<athena_namespace_relation>
athena_namespace_relations_list () {
  std::vector<athena_namespace_relation> out;
  if (!ns_db_exists ()) return out;

  string error;
  ns_sqlite_connection cx;
  if (!cx.open (false, error)) return out;

  sqlite3_stmt* st= nullptr;
  if (!prepare_sql (cx.db,
        "SELECT parent, child, decision, source "
        "FROM relation_decisions ORDER BY parent, child;",
        &st, error)) return out;
  while (true) {
    int status= sqlite3_step (st);
    if (status == SQLITE_DONE) break;
    if (status != SQLITE_ROW) {
      sqlite3_finalize (st);
      return out;
    }
    athena_namespace_relation r;
    r.parent= column_tm_string (st, 0);
    r.child= column_tm_string (st, 1);
    r.decision= column_tm_string (st, 2);
    r.source= column_tm_string (st, 3);
    if (r.parent != "" && r.child != "") out.push_back (r);
  }
  sqlite3_finalize (st);
  return out;
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
  return upsert_relation_decision (cx.db, parent, child, decision, source,
                                   error);
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
  return exec_prepared (cx.db,
    "DELETE FROM relation_decisions WHERE parent=? AND child=?;",
    { parent, child }, error);
}

bool
athena_namespace_validate_relation (string parent, string child, bool ask_user,
                                    string& error) {
  if (parent == child) return true;
  if (!vault_active ()) {
    error= "No active vault.";
    return false;
  }

  athena_namespace_definition child_ns;
  if (athena_namespace_get (child, child_ns)) {
    if (has_string (child_ns.parents, parent) ||
        has_string (child_ns.derived_parents, parent)) {
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

std::vector<athena_namespace_match>
athena_namespace_members (string name, string& error) {
  std::vector<athena_namespace_match> out;
  if (!vault_active ()) {
    error= "No active vault.";
    return out;
  }
  athena_namespace_definition ns;
  if (!athena_namespace_get (name, ns)) {
    error= "Unknown namespace: " * name;
    return out;
  }
  if (ns.kind == "abstract") return out;

  std::vector<template_token> toks;
  if (!parse_template (ns.templ, toks, error)) return out;

  array<url> files= vault_get_all_files ();
  for (int i=0; i<N(files); i++) {
    if (!is_ath_file (files[i])) continue;
    athena_namespace_match m;
    string err;
    if (match_stem (ns, stem_for_file (files[i]), m, err)) {
      m.file= files[i];
      out.push_back (m);
    }
    else if (err != "") {
      error= err;
      return out;
    }
  }

  if (ns.sorter_trivial || ns.sorter_path != "") {
    string sort_error;
    ns_compare_fn fn= ns.sorter_trivial ? nullptr :
      load_sorter (ns.sorter_path, sort_error);
    if (sort_error != "") error= sort_error;
    std::stable_sort (out.begin (), out.end (),
               [fn] (const athena_namespace_match& a,
                     const athena_namespace_match& b) {
                 return compare_with_sorter (fn, a, b) < 0;
               });
  }
  else {
    std::sort (out.begin (), out.end (),
               [] (const athena_namespace_match& a,
                   const athena_namespace_match& b) {
                 return std::strcmp (as_charp (a.stem), as_charp (b.stem)) < 0;
               });
  }
  return out;
}

tree
athena_namespace_info_page (string tmfs_name) {
  if (!vault_active ())
    return error_page ("Namespace", "No active vault.");

  std::vector<string> path= split_tmfs_path (tmfs_name);
  if (path.empty ())
    return error_page ("Namespace", "No namespace specified.");

  for (size_t i=0; i + 1<path.size (); i++) {
    string error;
    if (!athena_namespace_validate_relation (path[i], path[i + 1], true,
                                             error)) {
      return error_page ("Namespace Relation", tm_to_std (error));
    }
  }

  string name= path.back ();
  athena_namespace_definition ns;
  if (!athena_namespace_get (name, ns))
    return error_page ("Namespace", "Unknown namespace: " + tm_to_std (name));

  string error;
  std::vector<athena_namespace_match> members=
    athena_namespace_members (name, error);

  tree body (DOCUMENT);
  body << compound ("section*", tree ("Namespace " * name));
  body << line_tm ("Kind: " * ns.kind);
  body << line_tm ("Template: " * (ns.templ == "" ? "<none>" : ns.templ));
  body << line_tm ("Parents: " *
                   (N(ns.parents) == 0 ? string ("<none>") :
                    join_list (ns.parents)));
  body << line_tm ("Derived parents: " *
                   (N(ns.derived_parents) == 0 ? string ("<none>") :
                    join_list (ns.derived_parents)));
  if (ns.sorter_trivial)
    body << line_tm ("Sorter: trivial");
  else if (ns.sorter_path != "")
    body << line_tm ("Sorter: " * ns.sorter_path);
  if (ns.style_path != "")
    body << line_tm ("Style: " * ns.style_path);
  if (error != "")
    body << line_tm ("Sorter warning: " * error);

  body << compound ("subsection*", tree ("Members"));
  if (members.empty ()) body << line ("No matching .ath files.");
  for (const athena_namespace_match& m: members) {
    tree link= compound ("hlink", tree (m.stem), tree (concretize (m.file)));
    tree c (CONCAT);
    c << link;
    if (N(m.captures) > 0) {
      c << tree ("  ");
      c << tree ("[");
      for (int i=0; i<N(m.captures); i++) {
        if (i != 0) c << tree (", ");
        c << tree (m.capture_types[i] * "=" * m.captures[i]);
      }
      c << tree ("]");
    }
    if (m.ambiguous) c << tree ("  (ambiguous)");
    body << compound ("paragraph*", c);
  }

  return document_for_body (body);
}

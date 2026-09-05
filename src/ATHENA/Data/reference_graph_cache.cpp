/******************************************************************************
* MODULE     : reference_graph_cache.cpp
* DESCRIPTION: Incremental Vault document-reference graph cache
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "ATHENA/Data/reference_graph_cache.hpp"

#include "analyze.hpp"
#include "convert.hpp"
#include "file.hpp"
#include "vault.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <queue>
#include <set>
#include <tuple>

namespace fs = std::filesystem;

namespace {

constexpr int reference_cache_schema_version= 1;

std::string
tm_std (string value) {
  return std::string (as_charp (value), (size_t) N(value));
}

string
std_tm (const std::string& value) {
  return string (value.data (), (int) value.size ());
}

std::string
tree_text (tree value) {
  return is_atomic (value) ? tm_std (value->label) :
                             tm_std (tree_as_string (value));
}

int
hex_value (char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return -1;
}

std::string
percent_decode (const std::string& value) {
  std::string out;
  out.reserve (value.size ());
  for (size_t i=0; i<value.size (); ++i) {
    if (value[i] == '%' && i + 2 < value.size ()) {
      int hi= hex_value (value[i + 1]);
      int lo= hex_value (value[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back ((char) ((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    out.push_back (value[i]);
  }
  return out;
}

bool
wikilink_uuid (const std::string& destination, std::string& uuid) {
  const std::string prefix= "tmfs://wikilink/";
  if (destination.compare (0, prefix.size (), prefix) != 0) return false;
  size_t end= destination.find ('/', prefix.size ());
  std::string encoded= destination.substr (
    prefix.size (), end == std::string::npos ? std::string::npos :
                                               end - prefix.size ());
  uuid= percent_decode (encoded);
  return !uuid.empty ();
}

void
collect_references (
  tree document, std::set<std::pair<std::string,std::string>>& out)
{
  if (is_atomic (document)) return;
  if ((is_func (document, TRANSCLUDE) ||
       is_compound (document, "transclude")) && N(document) >= 1) {
    std::string uuid= tree_text (document[0]);
    if (!uuid.empty ()) out.insert ({uuid, "transclusion"});
  }
  else if ((is_func (document, HLINK) ||
            is_compound (document, "hlink") ||
            is_compound (document, "cardlink")) && N(document) >= 2) {
    std::string uuid;
    if (wikilink_uuid (tree_text (document[1]), uuid))
      out.insert ({uuid, "wikilink"});
  }
  for (int i=0; i<N(document); ++i) collect_references (document[i], out);
}

std::string
sqlite_message (sqlite3* db, const std::string& operation) {
  return operation + ": " + (db == nullptr ? "database is not open" :
                              sqlite3_errmsg (db));
}

bool
exec_sql (sqlite3* db, const char* sql, std::string& error) {
  char* message= nullptr;
  int status= sqlite3_exec (db, sql, nullptr, nullptr, &message);
  if (status == SQLITE_OK) return true;
  error= message == nullptr ? sqlite_message (db, "SQLite statement failed") :
                              std::string (message);
  sqlite3_free (message);
  return false;
}

bool
prepare (sqlite3* db, const char* sql, sqlite3_stmt** statement,
         std::string& error) {
  if (sqlite3_prepare_v2 (db, sql, -1, statement, nullptr) == SQLITE_OK)
    return true;
  error= sqlite_message (db, "Could not prepare reference-cache statement");
  return false;
}

bool
bind_text (sqlite3* db, sqlite3_stmt* statement, int index,
           const std::string& value, std::string& error) {
  if (sqlite3_bind_text (statement, index, value.data (), (int) value.size (),
                         SQLITE_TRANSIENT) == SQLITE_OK)
    return true;
  error= sqlite_message (db, "Could not bind reference-cache value");
  return false;
}

std::string
column_text (sqlite3_stmt* statement, int column) {
  const unsigned char* value= sqlite3_column_text (statement, column);
  int bytes= sqlite3_column_bytes (statement, column);
  return value == nullptr ? std::string () :
                            std::string ((const char*) value, (size_t) bytes);
}

std::string
file_signature (const fs::path& path) {
  std::error_code ec;
  uintmax_t size= fs::file_size (path, ec);
  if (ec) return "missing";
  auto modified= fs::last_write_time (path, ec);
  if (ec) return "missing";
  return std::to_string (modified.time_since_epoch ().count ()) + ":" +
         std::to_string (size);
}

bool
initialize_schema (sqlite3* db, std::string& error) {
  sqlite3_stmt* statement= nullptr;
  if (!prepare (db, "PRAGMA user_version;", &statement, error)) return false;
  int version= sqlite3_step (statement) == SQLITE_ROW ?
               sqlite3_column_int (statement, 0) : 0;
  sqlite3_finalize (statement);
  if (version != 0 && version != reference_cache_schema_version) {
    if (!exec_sql (db,
      "DROP TABLE IF EXISTS document_references;"
      "DROP TABLE IF EXISTS documents;"
      "DROP TABLE IF EXISTS metadata;", error)) return false;
  }
  return exec_sql (db,
    "CREATE TABLE IF NOT EXISTS metadata("
    " key TEXT PRIMARY KEY, value TEXT NOT NULL);"
    "CREATE TABLE IF NOT EXISTS documents("
    " path TEXT PRIMARY KEY, modified TEXT NOT NULL, size INTEGER NOT NULL);"
    "CREATE TABLE IF NOT EXISTS document_references("
    " source_path TEXT NOT NULL, uuid TEXT NOT NULL, kind TEXT NOT NULL,"
    " target_path TEXT, PRIMARY KEY(source_path,uuid,kind),"
    " FOREIGN KEY(source_path) REFERENCES documents(path) ON DELETE CASCADE);"
    "CREATE INDEX IF NOT EXISTS document_references_target_idx"
    " ON document_references(target_path);"
    "PRAGMA user_version=1;", error);
}

bool
metadata_value (sqlite3* db, const std::string& key, std::string& value,
                std::string& error) {
  sqlite3_stmt* statement= nullptr;
  if (!prepare (db, "SELECT value FROM metadata WHERE key=?1;", &statement,
                error)) return false;
  bool ok= bind_text (db, statement, 1, key, error);
  if (ok && sqlite3_step (statement) == SQLITE_ROW)
    value= column_text (statement, 0);
  else if (ok) value.clear ();
  sqlite3_finalize (statement);
  return ok;
}

bool
set_metadata_value (sqlite3* db, const std::string& key,
                    const std::string& value, std::string& error) {
  sqlite3_stmt* statement= nullptr;
  if (!prepare (db,
      "INSERT INTO metadata(key,value) VALUES(?1,?2)"
      " ON CONFLICT(key) DO UPDATE SET value=excluded.value;",
      &statement, error)) return false;
  bool ok= bind_text (db, statement, 1, key, error) &&
           bind_text (db, statement, 2, value, error) &&
           sqlite3_step (statement) == SQLITE_DONE;
  if (!ok && error.empty ())
    error= sqlite_message (db, "Could not update reference-cache metadata");
  sqlite3_finalize (statement);
  return ok;
}

std::string
resolved_target (const std::string& uuid) {
  tree node= vault_get_node (std_tm (uuid));
  if (!is_func (node, TUPLE) || N(node) < 1) return std::string ();
  return tree_text (node[0]);
}

bool
refresh_target_paths (sqlite3* db, std::string& error) {
  sqlite3_stmt* select= nullptr;
  if (!prepare (db, "SELECT DISTINCT uuid FROM document_references;",
                &select, error)) return false;
  std::vector<std::string> uuids;
  while (sqlite3_step (select) == SQLITE_ROW)
    uuids.push_back (column_text (select, 0));
  sqlite3_finalize (select);

  sqlite3_stmt* update= nullptr;
  if (!prepare (db,
      "UPDATE document_references SET target_path=?1 WHERE uuid=?2;",
      &update, error)) return false;
  for (const std::string& uuid: uuids) {
    std::string target= resolved_target (uuid);
    if (target.empty ()) sqlite3_bind_null (update, 1);
    else if (!bind_text (db, update, 1, target, error)) {
      sqlite3_finalize (update);
      return false;
    }
    if (!bind_text (db, update, 2, uuid, error) ||
        sqlite3_step (update) != SQLITE_DONE) {
      if (error.empty ())
        error= sqlite_message (db, "Could not resolve cached reference");
      sqlite3_finalize (update);
      return false;
    }
    sqlite3_reset (update);
    sqlite3_clear_bindings (update);
  }
  sqlite3_finalize (update);
  return true;
}

bool
cached_document_fingerprint (sqlite3* db, const std::string& path,
                             std::string& modified, uintmax_t& size,
                             bool& found, std::string& error) {
  sqlite3_stmt* statement= nullptr;
  if (!prepare (db,
      "SELECT modified,size FROM documents WHERE path=?1;", &statement,
      error)) return false;
  bool ok= bind_text (db, statement, 1, path, error);
  found= false;
  if (ok && sqlite3_step (statement) == SQLITE_ROW) {
    modified= column_text (statement, 0);
    size= (uintmax_t) sqlite3_column_int64 (statement, 1);
    found= true;
  }
  sqlite3_finalize (statement);
  return ok;
}

bool
replace_document (sqlite3* db, const fs::path& absolute,
                  const std::string& relative, const std::string& modified,
                  uintmax_t size, std::string& error) {
  string source;
  if (load_string (url_system (std_tm (absolute.string ())), source, false)) {
    error= "Could not read ATHENA document " + relative;
    return false;
  }
  tree document;
  try {
    document= texmacs_document_to_tree (source);
  }
  catch (...) {
    error= "Could not parse ATHENA document " + relative;
    return false;
  }
  if (is_func (document, _ERROR)) {
    error= "Could not parse ATHENA document " + relative;
    return false;
  }
  std::vector<AthenaDocumentReference> references=
    athena_collect_document_references (document);

  sqlite3_stmt* documentStatement= nullptr;
  if (!prepare (db,
      "INSERT INTO documents(path,modified,size) VALUES(?1,?2,?3)"
      " ON CONFLICT(path) DO UPDATE SET modified=excluded.modified,"
      " size=excluded.size;", &documentStatement, error)) return false;
  bool ok= bind_text (db, documentStatement, 1, relative, error) &&
           bind_text (db, documentStatement, 2, modified, error);
  if (ok) sqlite3_bind_int64 (documentStatement, 3, (sqlite3_int64) size);
  if (ok && sqlite3_step (documentStatement) != SQLITE_DONE) {
    error= sqlite_message (db, "Could not update cached document");
    ok= false;
  }
  sqlite3_finalize (documentStatement);
  if (!ok) return false;

  sqlite3_stmt* remove= nullptr;
  if (!prepare (db, "DELETE FROM document_references WHERE source_path=?1;",
                &remove, error)) return false;
  ok= bind_text (db, remove, 1, relative, error) &&
      sqlite3_step (remove) == SQLITE_DONE;
  sqlite3_finalize (remove);
  if (!ok) {
    if (error.empty ())
      error= sqlite_message (db, "Could not clear cached references");
    return false;
  }

  sqlite3_stmt* insert= nullptr;
  if (!prepare (db,
      "INSERT INTO document_references(source_path,uuid,kind,target_path)"
      " VALUES(?1,?2,?3,?4);", &insert, error)) return false;
  for (const AthenaDocumentReference& reference: references) {
    std::string target= resolved_target (reference.uuid);
    ok= bind_text (db, insert, 1, relative, error) &&
        bind_text (db, insert, 2, reference.uuid, error) &&
        bind_text (db, insert, 3, reference.kind, error);
    if (ok) {
      if (target.empty ()) sqlite3_bind_null (insert, 4);
      else ok= bind_text (db, insert, 4, target, error);
    }
    if (!ok || sqlite3_step (insert) != SQLITE_DONE) {
      if (error.empty ())
        error= sqlite_message (db, "Could not cache document reference");
      sqlite3_finalize (insert);
      return false;
    }
    sqlite3_reset (insert);
    sqlite3_clear_bindings (insert);
  }
  sqlite3_finalize (insert);
  return true;
}

bool
refresh_documents (sqlite3* db, const fs::path& root,
                   const std::function<void(size_t,size_t)>& progress,
                   std::string& error) {
  array<url> files= vault_get_all_files ();
  std::vector<fs::path> absoluteFiles;
  for (int i=0; i<N(files); ++i) {
    fs::path path (tm_std (concretize (files[i])));
    std::string extension= path.extension ().string ();
    std::transform (extension.begin (), extension.end (), extension.begin (),
                    [] (unsigned char ch) { return (char) std::tolower (ch); });
    if (extension == ".ath") absoluteFiles.push_back (path);
  }
  std::sort (absoluteFiles.begin (), absoluteFiles.end ());

  std::set<std::string> live;
  size_t done= 0;
  for (const fs::path& absolute: absoluteFiles) {
    std::error_code ec;
    fs::path relativePath= fs::relative (absolute, root, ec).lexically_normal ();
    if (ec || relativePath.empty () || *relativePath.begin () == "..") {
      error= "Vault document is outside the Vault root: " + absolute.string ();
      return false;
    }
    std::string relative= relativePath.generic_string ();
    live.insert (relative);
    uintmax_t size= fs::file_size (absolute, ec);
    if (ec) continue;
    auto modifiedTime= fs::last_write_time (absolute, ec);
    if (ec) continue;
    std::string modified=
      std::to_string (modifiedTime.time_since_epoch ().count ());
    std::string cachedModified;
    uintmax_t cachedSize= 0;
    bool found= false;
    if (!cached_document_fingerprint (db, relative, cachedModified,
                                      cachedSize, found, error)) return false;
    if (!found || cachedModified != modified || cachedSize != size)
      if (!replace_document (db, absolute, relative, modified, size, error))
        return false;
    done++;
    if (progress) progress (done, absoluteFiles.size ());
  }

  sqlite3_stmt* select= nullptr;
  if (!prepare (db, "SELECT path FROM documents;", &select, error)) return false;
  std::vector<std::string> stale;
  while (sqlite3_step (select) == SQLITE_ROW) {
    std::string path= column_text (select, 0);
    if (live.find (path) == live.end ()) stale.push_back (path);
  }
  sqlite3_finalize (select);
  sqlite3_stmt* remove= nullptr;
  if (!prepare (db, "DELETE FROM documents WHERE path=?1;", &remove,
                error)) return false;
  for (const std::string& path: stale) {
    if (!bind_text (db, remove, 1, path, error) ||
        sqlite3_step (remove) != SQLITE_DONE) {
      if (error.empty ())
        error= sqlite_message (db, "Could not prune reference cache");
      sqlite3_finalize (remove);
      return false;
    }
    sqlite3_reset (remove);
    sqlite3_clear_bindings (remove);
  }
  sqlite3_finalize (remove);
  return true;
}

bool
direct_edges (sqlite3* db, const std::string& source,
              std::vector<AthenaReferenceGraphEdge>& edges,
              std::string& error) {
  sqlite3_stmt* statement= nullptr;
  if (!prepare (db,
      "SELECT target_path,source_path,kind FROM document_references"
      " WHERE source_path=?1 AND target_path IS NOT NULL"
      " ORDER BY target_path,kind;", &statement, error)) return false;
  if (!bind_text (db, statement, 1, source, error)) {
    sqlite3_finalize (statement);
    return false;
  }
  while (sqlite3_step (statement) == SQLITE_ROW)
    edges.push_back ({column_text (statement, 0),
                      column_text (statement, 1),
                      column_text (statement, 2)});
  sqlite3_finalize (statement);
  return true;
}

} // namespace

std::vector<AthenaDocumentReference>
athena_collect_document_references (tree document) {
  std::set<std::pair<std::string,std::string>> found;
  collect_references (document, found);
  std::vector<AthenaDocumentReference> references;
  for (const auto& item: found)
    references.push_back ({item.first, item.second});
  return references;
}

bool
athena_reference_graph_query (
  const std::string& sourceRelativePath, int maxDepth,
  std::vector<AthenaReferenceGraphEdge>& edges,
  const std::function<void(size_t,size_t)>& progress,
  std::string& error)
{
  edges.clear ();
  error.clear ();
  if (!vault_active ()) {
    error= "No active Vault.";
    return false;
  }
  fs::path root (tm_std (concretize (vault_get_root ())));
  fs::path cacheDir= root / ".athena";
  std::error_code ec;
  fs::create_directories (cacheDir, ec);
  if (ec) {
    error= "Could not create reference graph cache directory: " + ec.message ();
    return false;
  }

  sqlite3* db= nullptr;
  fs::path cachePath= cacheDir / "reference-graph.sqlite";
  if (sqlite3_open_v2 (cachePath.string ().c_str (), &db,
                       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                       nullptr) != SQLITE_OK) {
    error= sqlite_message (db, "Could not open reference graph cache");
    if (db != nullptr) sqlite3_close (db);
    return false;
  }
  sqlite3_busy_timeout (db, 5000);
  bool ok= exec_sql (db, "PRAGMA foreign_keys=ON;", error) &&
           initialize_schema (db, error) &&
           exec_sql (db, "BEGIN IMMEDIATE;", error);
  if (!ok) {
    sqlite3_close (db);
    return false;
  }

  std::string mapSignature= file_signature (
    fs::path (tm_std (concretize (vault_get_map_db ()))));
  std::string previousMapSignature;
  ok= metadata_value (db, "map_signature", previousMapSignature, error) &&
      refresh_documents (db, root, progress, error);
  if (ok && previousMapSignature != mapSignature)
    ok= refresh_target_paths (db, error) &&
        set_metadata_value (db, "map_signature", mapSignature, error);
  if (ok) ok= exec_sql (db, "COMMIT;", error);
  else {
    std::string ignored;
    exec_sql (db, "ROLLBACK;", ignored);
  }
  if (!ok) {
    sqlite3_close (db);
    return false;
  }

  if (maxDepth == 1)
    ok= direct_edges (db, sourceRelativePath, edges, error);
  else {
    std::queue<std::string> pending;
    std::set<std::string> visited;
    std::set<std::tuple<std::string,std::string,std::string>> unique;
    pending.push (sourceRelativePath);
    std::queue<int> depths;
    depths.push (0);
    while (ok && !pending.empty ()) {
      std::string source= pending.front ();
      pending.pop ();
      int depth= depths.front ();
      depths.pop ();
      if (!visited.insert (source).second) continue;
      if (maxDepth > 0 && depth >= maxDepth) continue;
      std::vector<AthenaReferenceGraphEdge> direct;
      ok= direct_edges (db, source, direct, error);
      for (const AthenaReferenceGraphEdge& edge: direct) {
        auto key= std::make_tuple (edge.referenced_path,
                                   edge.referencing_path, edge.kind);
        if (unique.insert (key).second) edges.push_back (edge);
        if (visited.find (edge.referenced_path) == visited.end ()) {
          pending.push (edge.referenced_path);
          depths.push (depth + 1);
        }
      }
    }
  }
  sqlite3_close (db);
  return ok;
}

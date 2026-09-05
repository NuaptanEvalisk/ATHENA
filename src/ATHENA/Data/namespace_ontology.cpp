/******************************************************************************
* MODULE     : namespace_ontology.cpp
* DESCRIPTION: Cached namespace ontology maintained by a background worker
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
*******************************************************************************/

#include "namespace_ontology.hpp"

#include "namespaces_private.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <memory>
#include <set>
#include <sstream>
#include <thread>
#include <utility>

namespace fs= std::filesystem;

namespace {

struct CachedDirectory {
  std::string parent;
  int64_t     mtime_ns= 0;
};

struct CachedFile {
  std::string parent;
  std::string stem;
};

struct CachedMatch {
  std::string              file;
  std::string              stem;
  std::vector<std::string> captures;
  std::vector<std::string> capture_types;
  bool                     ambiguous= false;
};

struct NativeNamespace {
  std::string name;
  std::string kind;
  std::string templ;
  bool sorter_trivial= false;
  std::string sorter_path;
  std::string style_path;
  std::string initial_content_path;
  std::string homepage_path;
  std::vector<std::string> parents;
  std::vector<std::string> derived_parents;
};

struct NativeRelation {
  std::string parent;
  std::string child;
  std::string decision;
  std::string source;
};

struct PersistentState {
  bool loaded= false;
  int64_t namespace_db_mtime_ns= INT64_MIN;
  std::map<std::string,CachedDirectory> directories;
  std::map<std::string,CachedFile> files;
  std::map<std::string,std::vector<CachedMatch>> direct_matches;
  std::string match_fingerprint;
  std::map<std::string,std::vector<std::string>> all_children;
  std::map<std::string,std::vector<std::string>> visible_children;
  std::map<std::string,std::vector<std::string>> folded_children;
  std::string hierarchy_fingerprint;
};

struct InventoryDelta {
  std::set<std::string> upsert_directories;
  std::set<std::string> removed_directories;
  std::set<std::string> upsert_files;
  std::set<std::string> removed_files;

  bool changed () const {
    return !upsert_directories.empty () || !removed_directories.empty () ||
           !upsert_files.empty () || !removed_files.empty ();
  }

  std::set<std::string> affected_files () const {
    std::set<std::string> out= upsert_files;
    out.insert (removed_files.begin (), removed_files.end ());
    return out;
  }
};

struct OntologySnapshot {
  std::vector<NativeNamespace> namespaces;
  std::vector<NativeRelation> relations;
  std::map<std::string,std::vector<CachedMatch>> direct_matches;
  std::map<std::string,std::vector<std::string>> all_children;
  std::map<std::string,std::vector<std::string>> visible_children;
  std::map<std::string,std::vector<std::string>> folded_children;
  std::map<std::string,size_t> namespace_indices;
  fs::path root;
};

static std::string
tm_to_std_string (string value) {
  return std::string (as_charp (value), (size_t) N(value));
}

static string
std_to_tm_string (const std::string& value) {
  return string (value.data (), (int) value.size ());
}

static std::string
url_path (url value) {
  return tm_to_std_string (concretize (value));
}

static bool
exec_sql (sqlite3* db, const char* sql, std::string& error) {
  char* message= nullptr;
  int status= sqlite3_exec (db, sql, nullptr, nullptr, &message);
  if (status == SQLITE_OK) return true;
  error= message == nullptr ? sqlite3_errmsg (db) : message;
  sqlite3_free (message);
  return false;
}

class SqliteConnection {
public:
  sqlite3* db= nullptr;

  ~SqliteConnection () {
    if (db != nullptr) sqlite3_close (db);
  }

  bool open (const std::string& path, std::string& error) {
    std::error_code directory_error;
    fs::create_directories (fs::path (path).parent_path (), directory_error);
    if (directory_error) {
      error= "Could not create namespace cache directory: " +
             directory_error.message ();
      return false;
    }
    int status= sqlite3_open_v2 (
      path.c_str (), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (status != SQLITE_OK) {
      error= db == nullptr ? "Could not open namespace cache database" :
        sqlite3_errmsg (db);
      return false;
    }
    sqlite3_busy_timeout (db, 5000);
    static const char* schema=
      "CREATE TABLE IF NOT EXISTS meta ("
      "  key TEXT PRIMARY KEY,"
      "  value TEXT NOT NULL"
      ");"
      "CREATE TABLE IF NOT EXISTS namespace_cache_directories ("
      "  path TEXT PRIMARY KEY,"
      "  parent TEXT NOT NULL,"
      "  mtime_ns INTEGER NOT NULL"
      ");"
      "CREATE INDEX IF NOT EXISTS namespace_cache_directories_parent_idx "
      "  ON namespace_cache_directories(parent);"
      "CREATE TABLE IF NOT EXISTS namespace_cache_files ("
      "  path TEXT PRIMARY KEY,"
      "  parent TEXT NOT NULL,"
      "  stem TEXT NOT NULL"
      ");"
      "CREATE INDEX IF NOT EXISTS namespace_cache_files_parent_idx "
      "  ON namespace_cache_files(parent);"
      "CREATE TABLE IF NOT EXISTS namespace_cache_matches ("
      "  namespace TEXT NOT NULL,"
      "  file TEXT NOT NULL,"
      "  stem TEXT NOT NULL,"
      "  ambiguous INTEGER NOT NULL DEFAULT 0,"
      "  PRIMARY KEY(namespace, file)"
      ");"
      "CREATE INDEX IF NOT EXISTS namespace_cache_matches_namespace_idx "
      "  ON namespace_cache_matches(namespace);"
      "CREATE INDEX IF NOT EXISTS namespace_cache_matches_file_idx "
      "  ON namespace_cache_matches(file);"
      "CREATE TABLE IF NOT EXISTS namespace_cache_match_captures ("
      "  namespace TEXT NOT NULL,"
      "  file TEXT NOT NULL,"
      "  ord INTEGER NOT NULL,"
      "  value TEXT NOT NULL,"
      "  type TEXT NOT NULL,"
      "  PRIMARY KEY(namespace, file, ord)"
      ");"
      "CREATE INDEX IF NOT EXISTS namespace_cache_captures_file_idx "
      "  ON namespace_cache_match_captures(file);"
      "CREATE TABLE IF NOT EXISTS namespace_cache_children ("
      "  parent TEXT NOT NULL,"
      "  child TEXT NOT NULL,"
      "  folded INTEGER NOT NULL DEFAULT 0,"
      "  PRIMARY KEY(parent, child)"
      ");"
      "CREATE INDEX IF NOT EXISTS namespace_cache_children_parent_idx "
      "  ON namespace_cache_children(parent, folded, child);";
    return exec_sql (db, "PRAGMA foreign_keys=ON;", error) &&
           exec_sql (db, schema, error);
  }
};

static bool
prepare (sqlite3* db, const char* sql, sqlite3_stmt** statement,
         std::string& error) {
  if (sqlite3_prepare_v2 (db, sql, -1, statement, nullptr) == SQLITE_OK)
    return true;
  error= sqlite3_errmsg (db);
  return false;
}

static bool
bind_text (sqlite3_stmt* statement, int index, const std::string& value,
           std::string& error) {
  if (sqlite3_bind_text (statement, index, value.data (), (int) value.size (),
                         SQLITE_TRANSIENT) == SQLITE_OK)
    return true;
  error= sqlite3_errmsg (sqlite3_db_handle (statement));
  return false;
}

static std::string
column_text (sqlite3_stmt* statement, int column) {
  const unsigned char* value= sqlite3_column_text (statement, column);
  int length= sqlite3_column_bytes (statement, column);
  return value == nullptr ? std::string () :
    std::string ((const char*) value, (size_t) length);
}

static bool
load_persistent_state (sqlite3* db, PersistentState& state,
                       std::string& error) {
  state.directories.clear ();
  state.files.clear ();
  state.direct_matches.clear ();
  state.match_fingerprint.clear ();
  state.all_children.clear ();
  state.visible_children.clear ();
  state.folded_children.clear ();
  state.hierarchy_fingerprint.clear ();

  sqlite3_stmt* statement= nullptr;
  if (!prepare (db,
        "SELECT path, parent, mtime_ns FROM namespace_cache_directories;",
        &statement, error))
    return false;
  while (true) {
    int status= sqlite3_step (statement);
    if (status == SQLITE_DONE) break;
    if (status != SQLITE_ROW) {
      error= sqlite3_errmsg (db);
      sqlite3_finalize (statement);
      return false;
    }
    state.directories[column_text (statement, 0)]= {
      column_text (statement, 1), sqlite3_column_int64 (statement, 2)};
  }
  sqlite3_finalize (statement);

  if (!prepare (db,
        "SELECT path, parent, stem FROM namespace_cache_files;",
        &statement, error))
    return false;
  while (true) {
    int status= sqlite3_step (statement);
    if (status == SQLITE_DONE) break;
    if (status != SQLITE_ROW) {
      error= sqlite3_errmsg (db);
      sqlite3_finalize (statement);
      return false;
    }
    state.files[column_text (statement, 0)]= {
      column_text (statement, 1), column_text (statement, 2)};
  }
  sqlite3_finalize (statement);

  std::map<std::pair<std::string,std::string>,size_t> match_indices;
  if (!prepare (
        db,
        "SELECT namespace, file, stem, ambiguous "
        "FROM namespace_cache_matches ORDER BY namespace, file;",
        &statement, error))
    return false;
  while (true) {
    int status= sqlite3_step (statement);
    if (status == SQLITE_DONE) break;
    if (status != SQLITE_ROW) {
      error= sqlite3_errmsg (db);
      sqlite3_finalize (statement);
      return false;
    }
    std::string name= column_text (statement, 0);
    CachedMatch match;
    match.file= column_text (statement, 1);
    match.stem= column_text (statement, 2);
    match.ambiguous= sqlite3_column_int (statement, 3) != 0;
    std::vector<CachedMatch>& matches= state.direct_matches[name];
    match_indices[{name, match.file}]= matches.size ();
    matches.push_back (std::move (match));
  }
  sqlite3_finalize (statement);

  if (!prepare (
        db,
        "SELECT namespace, file, value, type "
        "FROM namespace_cache_match_captures "
        "ORDER BY namespace, file, ord;",
        &statement, error))
    return false;
  while (true) {
    int status= sqlite3_step (statement);
    if (status == SQLITE_DONE) break;
    if (status != SQLITE_ROW) {
      error= sqlite3_errmsg (db);
      sqlite3_finalize (statement);
      return false;
    }
    std::string name= column_text (statement, 0);
    std::string file= column_text (statement, 1);
    auto found= match_indices.find ({name, file});
    auto group= state.direct_matches.find (name);
    if (found == match_indices.end () || group == state.direct_matches.end () ||
        found->second >= group->second.size ())
      continue;
    CachedMatch& match= group->second[found->second];
    match.captures.push_back (column_text (statement, 2));
    match.capture_types.push_back (column_text (statement, 3));
  }
  sqlite3_finalize (statement);

  if (!prepare (db,
        "SELECT value FROM meta WHERE key='namespace-match-fingerprint';",
        &statement, error))
    return false;
  int status= sqlite3_step (statement);
  if (status == SQLITE_ROW)
    state.match_fingerprint= column_text (statement, 0);
  else if (status != SQLITE_DONE) {
    error= sqlite3_errmsg (db);
    sqlite3_finalize (statement);
    return false;
  }
  sqlite3_finalize (statement);

  if (!prepare (
        db,
        "SELECT parent, child, folded FROM namespace_cache_children "
        "ORDER BY parent, child;",
        &statement, error))
    return false;
  while (true) {
    int child_status= sqlite3_step (statement);
    if (child_status == SQLITE_DONE) break;
    if (child_status != SQLITE_ROW) {
      error= sqlite3_errmsg (db);
      sqlite3_finalize (statement);
      return false;
    }
    std::string parent= column_text (statement, 0);
    std::string child= column_text (statement, 1);
    bool folded= sqlite3_column_int (statement, 2) != 0;
    state.all_children[parent].push_back (child);
    (folded ? state.folded_children[parent] :
              state.visible_children[parent]).push_back (child);
  }
  sqlite3_finalize (statement);

  if (!prepare (db,
        "SELECT value FROM meta WHERE key='namespace-hierarchy-fingerprint';",
        &statement, error))
    return false;
  status= sqlite3_step (statement);
  if (status == SQLITE_ROW)
    state.hierarchy_fingerprint= column_text (statement, 0);
  else if (status != SQLITE_DONE) {
    error= sqlite3_errmsg (db);
    sqlite3_finalize (statement);
    return false;
  }
  sqlite3_finalize (statement);
  state.loaded= true;
  return true;
}

static int64_t
directory_mtime_ns (const fs::path& path, std::error_code& error) {
  fs::file_time_type time= fs::last_write_time (path, error);
  if (error) return 0;
  return std::chrono::duration_cast<std::chrono::nanoseconds> (
           time.time_since_epoch ()).count ();
}

static std::string
relative_path (const fs::path& root, const fs::path& path) {
  std::error_code error;
  fs::path relative= fs::relative (path, root, error);
  if (error || relative == fs::path (".")) return "";
  return relative.generic_string ();
}

static void
remove_cached_subtree (const std::string& path, PersistentState& state,
                       InventoryDelta& delta) {
  std::string prefix= path.empty () ? "" : path + "/";
  for (auto it= state.files.begin (); it != state.files.end (); ) {
    if (it->first == path || (!prefix.empty () &&
                              it->first.compare (0, prefix.size (), prefix) == 0)) {
      delta.removed_files.insert (it->first);
      delta.upsert_files.erase (it->first);
      it= state.files.erase (it);
    }
    else ++it;
  }
  for (auto it= state.directories.begin (); it != state.directories.end (); ) {
    if (it->first == path || (!prefix.empty () &&
                              it->first.compare (0, prefix.size (), prefix) == 0)) {
      delta.removed_directories.insert (it->first);
      delta.upsert_directories.erase (it->first);
      it= state.directories.erase (it);
    }
    else ++it;
  }
}

static bool
scan_file_inventory (const fs::path& root, bool force,
                     PersistentState& state, InventoryDelta& delta,
                     std::string& error) {
  delta= InventoryDelta {};
  if (!fs::is_directory (root)) {
    error= "Vault root is not a directory: " + root.string ();
    return false;
  }
  if (state.directories.find ("") == state.directories.end ()) {
    state.directories[""]= {"", 0};
    delta.upsert_directories.insert ("");
    force= true;
  }

  std::vector<std::string> pending {""};
  std::set<std::string> visited;
  while (!pending.empty ()) {
    std::string relative= std::move (pending.back ());
    pending.pop_back ();
    if (!visited.insert (relative).second) continue;

    fs::path absolute= relative.empty () ? root : root / fs::path (relative);
    std::error_code stat_error;
    if (!fs::is_directory (absolute, stat_error) || stat_error) {
      if (relative.empty ()) {
        error= "Could not inspect vault root: " + stat_error.message ();
        return false;
      }
      remove_cached_subtree (relative, state, delta);
      continue;
    }

    int64_t mtime= directory_mtime_ns (absolute, stat_error);
    if (stat_error) {
      error= "Could not read directory timestamp for " + absolute.string () +
             ": " + stat_error.message ();
      return false;
    }
    auto cached= state.directories.find (relative);
    bool revisit= force || cached == state.directories.end () ||
                  cached->second.mtime_ns != mtime;

    if (revisit) {
      std::set<std::string> child_directories;
      std::map<std::string,CachedFile> child_files;
      std::error_code iteration_error;
      fs::directory_iterator iterator (
        absolute, fs::directory_options::skip_permission_denied,
        iteration_error);
      if (iteration_error) {
        error= "Could not visit vault directory " + absolute.string () +
               ": " + iteration_error.message ();
        return false;
      }
      for (const fs::directory_entry& entry: iterator) {
        std::string name= entry.path ().filename ().string ();
        if (name.empty () || name[0] == '.') continue;
        std::error_code type_error;
        if (entry.is_symlink (type_error)) continue;
        if (entry.is_directory (type_error)) {
          std::string child= relative_path (root, entry.path ());
          if (!child.empty ()) child_directories.insert (child);
          continue;
        }
        if (type_error || !entry.is_regular_file (type_error)) continue;
        std::string extension= entry.path ().extension ().string ();
        if (extension != ".ath") continue;
        std::string path= relative_path (root, entry.path ());
        if (path.empty ()) continue;
        child_files[path]= {relative, entry.path ().stem ().string ()};
      }

      std::vector<std::string> removed_directories;
      for (const auto& item: state.directories)
        if (!item.first.empty () && item.second.parent == relative &&
            child_directories.find (item.first) == child_directories.end ())
          removed_directories.push_back (item.first);
      for (const std::string& removed: removed_directories)
        remove_cached_subtree (removed, state, delta);

      for (auto it= state.files.begin (); it != state.files.end (); ) {
        if (it->second.parent == relative &&
            child_files.find (it->first) == child_files.end ()) {
          delta.removed_files.insert (it->first);
          delta.upsert_files.erase (it->first);
          it= state.files.erase (it);
        }
        else ++it;
      }
      for (const auto& item: child_files) {
        auto old= state.files.find (item.first);
        if (old == state.files.end () ||
            old->second.parent != item.second.parent ||
            old->second.stem != item.second.stem) {
          state.files[item.first]= item.second;
          delta.upsert_files.insert (item.first);
          delta.removed_files.erase (item.first);
        }
      }
      for (const std::string& child: child_directories) {
        auto old= state.directories.find (child);
        if (old == state.directories.end ()) {
          state.directories[child]= {relative, 0};
          delta.upsert_directories.insert (child);
          delta.removed_directories.erase (child);
        }
        else if (old->second.parent != relative) {
          old->second.parent= relative;
          delta.upsert_directories.insert (child);
        }
      }
      CachedDirectory updated {cached == state.directories.end () ? "" :
                               cached->second.parent, mtime};
      if (!relative.empty ()) updated.parent=
        fs::path (relative).parent_path ().generic_string ();
      state.directories[relative]= updated;
      delta.upsert_directories.insert (relative);
      delta.removed_directories.erase (relative);
    }

    for (const auto& item: state.directories)
      if (!item.first.empty () && item.second.parent == relative)
        pending.push_back (item.first);
  }
  return true;
}

static bool
persist_inventory (sqlite3* db, const PersistentState& state,
                   const InventoryDelta& delta, std::string& error) {
  if (!exec_sql (db, "BEGIN IMMEDIATE;", error)) return false;
  bool ok= true;
  sqlite3_stmt* delete_directory_statement= nullptr;
  sqlite3_stmt* delete_file_statement= nullptr;
  sqlite3_stmt* directory_statement= nullptr;
  sqlite3_stmt* file_statement= nullptr;
  if (ok) ok= prepare (db,
    "DELETE FROM namespace_cache_files WHERE path=?;",
    &delete_file_statement, error);
  if (ok) ok= prepare (db,
    "DELETE FROM namespace_cache_directories WHERE path=?;",
    &delete_directory_statement, error);
  if (ok) ok= prepare (
    db,
    "INSERT INTO namespace_cache_directories(path, parent, mtime_ns) "
    "VALUES(?, ?, ?) ON CONFLICT(path) DO UPDATE SET "
    "parent=excluded.parent, mtime_ns=excluded.mtime_ns;",
    &directory_statement, error);
  if (ok) ok= prepare (
    db, "INSERT INTO namespace_cache_files(path, parent, stem) VALUES(?, ?, ?) "
    "ON CONFLICT(path) DO UPDATE SET "
    "parent=excluded.parent, stem=excluded.stem;",
    &file_statement, error);

  for (const std::string& path: delta.removed_files) {
    if (!ok) break;
    sqlite3_reset (delete_file_statement);
    sqlite3_clear_bindings (delete_file_statement);
    ok= bind_text (delete_file_statement, 1, path, error) &&
        sqlite3_step (delete_file_statement) == SQLITE_DONE;
    if (!ok && error.empty ()) error= sqlite3_errmsg (db);
  }
  for (const std::string& path: delta.removed_directories) {
    if (!ok) break;
    sqlite3_reset (delete_directory_statement);
    sqlite3_clear_bindings (delete_directory_statement);
    ok= bind_text (delete_directory_statement, 1, path, error) &&
        sqlite3_step (delete_directory_statement) == SQLITE_DONE;
    if (!ok && error.empty ()) error= sqlite3_errmsg (db);
  }
  for (const std::string& path: delta.upsert_directories) {
    if (!ok) break;
    auto item= state.directories.find (path);
    if (item == state.directories.end ()) continue;
    sqlite3_reset (directory_statement);
    sqlite3_clear_bindings (directory_statement);
    ok= bind_text (directory_statement, 1, item->first, error) &&
        bind_text (directory_statement, 2, item->second.parent, error) &&
        sqlite3_bind_int64 (directory_statement, 3,
                            item->second.mtime_ns) == SQLITE_OK &&
        sqlite3_step (directory_statement) == SQLITE_DONE;
    if (!ok && error.empty ()) error= sqlite3_errmsg (db);
  }
  for (const std::string& path: delta.upsert_files) {
    if (!ok) break;
    auto item= state.files.find (path);
    if (item == state.files.end ()) continue;
    sqlite3_reset (file_statement);
    sqlite3_clear_bindings (file_statement);
    ok= bind_text (file_statement, 1, item->first, error) &&
        bind_text (file_statement, 2, item->second.parent, error) &&
        bind_text (file_statement, 3, item->second.stem, error) &&
        sqlite3_step (file_statement) == SQLITE_DONE;
    if (!ok && error.empty ()) error= sqlite3_errmsg (db);
  }
  if (delete_directory_statement != nullptr)
    sqlite3_finalize (delete_directory_statement);
  if (delete_file_statement != nullptr)
    sqlite3_finalize (delete_file_statement);
  if (directory_statement != nullptr) sqlite3_finalize (directory_statement);
  if (file_statement != nullptr) sqlite3_finalize (file_statement);
  if (ok) ok= exec_sql (db, "COMMIT;", error);
  if (!ok) {
    std::string ignored;
    exec_sql (db, "ROLLBACK;", ignored);
  }
  return ok;
}

static void
fingerprint_text (uint64_t& hash, const std::string& text) {
  for (unsigned char byte: text) {
    hash ^= (uint64_t) byte;
    hash *= UINT64_C (1099511628211);
  }
  hash ^= UINT64_C (255);
  hash *= UINT64_C (1099511628211);
}

static std::string
matching_fingerprint (
  const std::vector<NativeNamespace>& namespaces) {
  uint64_t hash= UINT64_C (1469598103934665603);
  fingerprint_text (hash, "athena-namespace-match-v1");
  for (const NativeNamespace& ns: namespaces) {
    fingerprint_text (hash, ns.name);
    fingerprint_text (hash, ns.kind);
    fingerprint_text (hash, ns.templ);
  }
  std::ostringstream out;
  out << std::hex << hash;
  return out.str ();
}

static bool
append_file_matches (
  const std::string& file_path, const CachedFile& file,
  const std::vector<NativeNamespace>& namespaces,
  PersistentState& state, std::string& error) {
  for (const NativeNamespace& ns: namespaces) {
    if (ns.kind == "abstract" || ns.templ == "") continue;
    std::vector<std::string> captures;
    std::vector<std::string> capture_types;
    bool ambiguous= false;
    std::string match_error;
    if (athena_namespaces::match_stem_std (
          ns.templ, file.stem, captures, capture_types, ambiguous,
          match_error)) {
      CachedMatch cached;
      cached.file= file_path;
      cached.stem= file.stem;
      cached.ambiguous= ambiguous;
      cached.captures= std::move (captures);
      cached.capture_types= std::move (capture_types);
      state.direct_matches[ns.name].push_back (std::move (cached));
    }
    else if (!match_error.empty ()) {
      error= match_error;
      return false;
    }
  }
  return true;
}

static bool
rebuild_matches (const std::vector<NativeNamespace>& namespaces,
                 PersistentState& state, std::string& error) {
  state.direct_matches.clear ();
  for (const auto& file: state.files)
    if (!append_file_matches (file.first, file.second, namespaces, state,
                              error))
      return false;
  state.match_fingerprint= matching_fingerprint (namespaces);
  return true;
}

static bool
update_matches (const std::vector<NativeNamespace>& namespaces,
                const std::set<std::string>& affected_files,
                PersistentState& state, std::string& error) {
  for (auto& group: state.direct_matches) {
    std::vector<CachedMatch>& matches= group.second;
    matches.erase (
      std::remove_if (matches.begin (), matches.end (),
        [&affected_files] (const CachedMatch& match) {
          return affected_files.find (match.file) != affected_files.end ();
        }),
      matches.end ());
  }
  for (const std::string& path: affected_files) {
    auto file= state.files.find (path);
    if (file != state.files.end () &&
        !append_file_matches (file->first, file->second, namespaces, state,
                              error))
      return false;
  }
  state.match_fingerprint= matching_fingerprint (namespaces);
  return true;
}

static bool
persist_matches (sqlite3* db, const PersistentState& state,
                 const std::set<std::string>* affected_files,
                 std::string& error) {
  if (!exec_sql (db, "BEGIN IMMEDIATE;", error)) return false;
  bool full= affected_files == nullptr;
  bool ok= true;
  if (full)
    ok= exec_sql (db, "DELETE FROM namespace_cache_match_captures;", error) &&
        exec_sql (db, "DELETE FROM namespace_cache_matches;", error);
  sqlite3_stmt* delete_capture_statement= nullptr;
  sqlite3_stmt* delete_match_statement= nullptr;
  sqlite3_stmt* match_statement= nullptr;
  sqlite3_stmt* capture_statement= nullptr;
  if (ok && !full) ok= prepare (
    db, "DELETE FROM namespace_cache_match_captures WHERE file=?;",
    &delete_capture_statement, error);
  if (ok && !full) ok= prepare (
    db, "DELETE FROM namespace_cache_matches WHERE file=?;",
    &delete_match_statement, error);
  if (ok) ok= prepare (
    db,
    "INSERT INTO namespace_cache_matches(namespace, file, stem, ambiguous) "
    "VALUES(?, ?, ?, ?);", &match_statement, error);
  if (ok) ok= prepare (
    db,
    "INSERT INTO namespace_cache_match_captures"
    "(namespace, file, ord, value, type) VALUES(?, ?, ?, ?, ?);",
    &capture_statement, error);
  if (ok && !full)
    for (const std::string& file: *affected_files) {
      sqlite3_reset (delete_capture_statement);
      sqlite3_clear_bindings (delete_capture_statement);
      ok= bind_text (delete_capture_statement, 1, file, error) &&
          sqlite3_step (delete_capture_statement) == SQLITE_DONE;
      if (!ok) break;
      sqlite3_reset (delete_match_statement);
      sqlite3_clear_bindings (delete_match_statement);
      ok= bind_text (delete_match_statement, 1, file, error) &&
          sqlite3_step (delete_match_statement) == SQLITE_DONE;
      if (!ok && error.empty ()) error= sqlite3_errmsg (db);
    }
  for (const auto& group: state.direct_matches) {
    for (const CachedMatch& match: group.second) {
      if (!ok) break;
      if (!full && affected_files->find (match.file) == affected_files->end ())
        continue;
      sqlite3_reset (match_statement);
      sqlite3_clear_bindings (match_statement);
      ok= bind_text (match_statement, 1, group.first, error) &&
          bind_text (match_statement, 2, match.file, error) &&
          bind_text (match_statement, 3, match.stem, error) &&
          sqlite3_bind_int (match_statement, 4,
                            match.ambiguous ? 1 : 0) == SQLITE_OK &&
          sqlite3_step (match_statement) == SQLITE_DONE;
      if (!ok && error.empty ()) error= sqlite3_errmsg (db);
      for (size_t i=0; ok && i<match.captures.size (); ++i) {
        sqlite3_reset (capture_statement);
        sqlite3_clear_bindings (capture_statement);
        std::string type= i < match.capture_types.size () ?
          match.capture_types[i] : "";
        ok= bind_text (capture_statement, 1, group.first, error) &&
            bind_text (capture_statement, 2, match.file, error) &&
            sqlite3_bind_int (capture_statement, 3, (int) i) == SQLITE_OK &&
            bind_text (capture_statement, 4, match.captures[i], error) &&
            bind_text (capture_statement, 5, type, error) &&
            sqlite3_step (capture_statement) == SQLITE_DONE;
        if (!ok && error.empty ()) error= sqlite3_errmsg (db);
      }
    }
    if (!ok) break;
  }
  if (delete_capture_statement != nullptr)
    sqlite3_finalize (delete_capture_statement);
  if (delete_match_statement != nullptr)
    sqlite3_finalize (delete_match_statement);
  if (match_statement != nullptr) sqlite3_finalize (match_statement);
  if (capture_statement != nullptr) sqlite3_finalize (capture_statement);
  if (ok) {
    sqlite3_stmt* meta_statement= nullptr;
    ok= prepare (
      db,
      "INSERT INTO meta(key, value) VALUES('namespace-match-fingerprint', ?) "
      "ON CONFLICT(key) DO UPDATE SET value=excluded.value;",
      &meta_statement, error);
    if (ok) {
      ok= bind_text (meta_statement, 1, state.match_fingerprint, error) &&
          sqlite3_step (meta_statement) == SQLITE_DONE;
      if (!ok && error.empty ()) error= sqlite3_errmsg (db);
    }
    if (meta_statement != nullptr) sqlite3_finalize (meta_statement);
  }
  if (ok) ok= exec_sql (db, "COMMIT;", error);
  if (!ok) {
    std::string ignored;
    exec_sql (db, "ROLLBACK;", ignored);
  }
  return ok;
}

static bool
namespace_is_child_of (const NativeNamespace& child,
                       const std::string& parent) {
  for (const std::string& candidate: child.parents)
    if (candidate == parent) return true;
  for (const std::string& candidate: child.derived_parents)
    if (candidate == parent) return true;
  return false;
}

static athena_namespace_match
materialize_match (const fs::path& root, const CachedMatch& cached) {
  athena_namespace_match match;
  match.file= url_system (std_to_tm_string ((root / cached.file).string ()));
  match.stem= std_to_tm_string (cached.stem);
  match.ambiguous= cached.ambiguous;
  for (size_t i=0; i<cached.captures.size (); ++i) {
    match.captures << std_to_tm_string (cached.captures[i]);
    match.capture_types << std_to_tm_string (
      i < cached.capture_types.size () ? cached.capture_types[i] : "");
  }
  return match;
}

static athena_namespace_definition
materialize_namespace (const NativeNamespace& native) {
  athena_namespace_definition ns;
  ns.name= std_to_tm_string (native.name);
  ns.kind= std_to_tm_string (native.kind);
  ns.templ= std_to_tm_string (native.templ);
  ns.sorter_trivial= native.sorter_trivial;
  ns.sorter_path= std_to_tm_string (native.sorter_path);
  ns.style_path= std_to_tm_string (native.style_path);
  ns.initial_content_path= std_to_tm_string (native.initial_content_path);
  ns.homepage_path= std_to_tm_string (native.homepage_path);
  for (const std::string& parent: native.parents)
    ns.parents << std_to_tm_string (parent);
  for (const std::string& parent: native.derived_parents)
    ns.derived_parents << std_to_tm_string (parent);
  return ns;
}

static athena_namespace_relation
materialize_relation (const NativeRelation& native) {
  athena_namespace_relation relation;
  relation.parent= std_to_tm_string (native.parent);
  relation.child= std_to_tm_string (native.child);
  relation.decision= std_to_tm_string (native.decision);
  relation.source= std_to_tm_string (native.source);
  return relation;
}

static void
collect_members (
  const std::string& name, bool descendants,
  const std::vector<NativeNamespace>& namespaces,
  const std::map<std::string,std::vector<CachedMatch>>& direct,
  const fs::path& root, std::set<std::string>& visiting,
  std::set<std::string>& seen_files,
  std::vector<CachedMatch>& out) {
  (void) root;
  if (!visiting.insert (name).second) return;
  auto matches= direct.find (name);
  if (matches != direct.end ())
    for (const CachedMatch& cached: matches->second)
      if (seen_files.insert (cached.file).second)
        out.push_back (cached);

  if (descendants)
    for (const NativeNamespace& child: namespaces)
      if (namespace_is_child_of (child, name))
        collect_members (child.name, true, namespaces,
                         direct, root, visiting, seen_files, out);
  visiting.erase (name);
}

static void
build_snapshot_index (OntologySnapshot& snapshot) {
  snapshot.namespace_indices.clear ();
  for (size_t i=0; i<snapshot.namespaces.size (); ++i)
    snapshot.namespace_indices[snapshot.namespaces[i].name]= i;
}

static bool
load_native_namespace_rows (sqlite3* db,
                            std::vector<NativeNamespace>& namespaces,
                            std::string& error) {
  namespaces.clear ();
  sqlite3_stmt* statement= nullptr;
  if (!prepare (
        db,
        "SELECT name, kind, template, sorter_trivial, sorter_path, style_path, "
        "initial_content_path, homepage_path FROM namespaces ORDER BY name;",
        &statement, error))
    return false;
  while (true) {
    int status= sqlite3_step (statement);
    if (status == SQLITE_DONE) break;
    if (status != SQLITE_ROW) {
      error= sqlite3_errmsg (db);
      sqlite3_finalize (statement);
      return false;
    }
    NativeNamespace ns;
    ns.name= column_text (statement, 0);
    ns.kind= column_text (statement, 1);
    ns.templ= column_text (statement, 2);
    ns.sorter_trivial= sqlite3_column_int (statement, 3) != 0;
    ns.sorter_path= column_text (statement, 4);
    ns.style_path= column_text (statement, 5);
    ns.initial_content_path= column_text (statement, 6);
    ns.homepage_path= column_text (statement, 7);
    namespaces.push_back (std::move (ns));
  }
  sqlite3_finalize (statement);
  return true;
}

static bool
read_meta (sqlite3* db, const char* key, std::string& value,
           std::string& error) {
  value.clear ();
  sqlite3_stmt* statement= nullptr;
  if (!prepare (db, "SELECT value FROM meta WHERE key=?;", &statement,
                error))
    return false;
  bool ok= bind_text (statement, 1, key, error);
  int status= ok ? sqlite3_step (statement) : SQLITE_ERROR;
  if (ok && status == SQLITE_ROW) value= column_text (statement, 0);
  else if (ok && status != SQLITE_DONE) {
    error= sqlite3_errmsg (db);
    ok= false;
  }
  sqlite3_finalize (statement);
  return ok;
}

static bool
write_meta (sqlite3* db, const char* key, const std::string& value,
            std::string& error) {
  sqlite3_stmt* statement= nullptr;
  if (!prepare (
        db,
        "INSERT INTO meta(key, value) VALUES(?, ?) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value;",
        &statement, error))
    return false;
  bool ok= bind_text (statement, 1, key, error) &&
           bind_text (statement, 2, value, error) &&
           sqlite3_step (statement) == SQLITE_DONE;
  if (!ok && error.empty ()) error= sqlite3_errmsg (db);
  sqlite3_finalize (statement);
  return ok;
}

static std::string
derived_fingerprint (const std::vector<NativeNamespace>& namespaces) {
  uint64_t hash= UINT64_C (1469598103934665603);
  fingerprint_text (hash, "athena-namespace-derived-v1");
  for (const NativeNamespace& ns: namespaces) {
    fingerprint_text (hash, ns.name);
    fingerprint_text (hash, ns.kind);
    fingerprint_text (hash, ns.templ);
  }
  std::ostringstream out;
  out << std::hex << hash;
  return out.str ();
}

static bool
refresh_native_derived_parents (sqlite3* db, bool& changed,
                                std::string& error) {
  changed= false;
  if (!exec_sql (db, "BEGIN IMMEDIATE;", error)) return false;

  std::vector<NativeNamespace> namespaces;
  std::string previous;
  bool ok= load_native_namespace_rows (db, namespaces, error) &&
           read_meta (db, "derived-source-fingerprint", previous, error);
  std::string fingerprint= derived_fingerprint (namespaces);
  if (ok && fingerprint != previous) {
    ok= exec_sql (db,
          "DELETE FROM namespace_parents WHERE source='derived';",
          error) &&
        exec_sql (db,
          "DELETE FROM relation_decisions WHERE source='derived';",
          error);
    sqlite3_stmt* parent_statement= nullptr;
    sqlite3_stmt* relation_statement= nullptr;
    if (ok) ok= prepare (
      db,
      "INSERT OR REPLACE INTO namespace_parents"
      "(child, parent, source, ord) VALUES(?, ?, 'derived', ?);",
      &parent_statement, error);
    if (ok) ok= prepare (
      db,
      "INSERT INTO relation_decisions(parent, child, decision, source) "
      "VALUES(?, ?, 'allow', 'derived') ON CONFLICT(parent, child) DO UPDATE "
      "SET decision=excluded.decision, source=excluded.source;",
      &relation_statement, error);

    for (const NativeNamespace& child: namespaces) {
      if (!ok) break;
      if (child.kind == "abstract" || child.templ.empty ()) continue;
      int ord= 0;
      for (const NativeNamespace& parent: namespaces) {
        if (child.name == parent.name || parent.kind == "abstract" ||
            parent.templ.empty ())
          continue;
        bool derives= false;
        if (!athena_namespaces::template_derives_from_std (
              child.templ, parent.templ, derives, error)) {
          ok= false;
          break;
        }
        if (!derives) continue;
        sqlite3_reset (parent_statement);
        sqlite3_clear_bindings (parent_statement);
        ok= bind_text (parent_statement, 1, child.name, error) &&
            bind_text (parent_statement, 2, parent.name, error) &&
            sqlite3_bind_int (parent_statement, 3, ord++) == SQLITE_OK &&
            sqlite3_step (parent_statement) == SQLITE_DONE;
        if (!ok && error.empty ()) error= sqlite3_errmsg (db);
        if (!ok) break;
        sqlite3_reset (relation_statement);
        sqlite3_clear_bindings (relation_statement);
        ok= bind_text (relation_statement, 1, parent.name, error) &&
            bind_text (relation_statement, 2, child.name, error) &&
            sqlite3_step (relation_statement) == SQLITE_DONE;
        if (!ok && error.empty ()) error= sqlite3_errmsg (db);
      }
    }
    if (parent_statement != nullptr) sqlite3_finalize (parent_statement);
    if (relation_statement != nullptr) sqlite3_finalize (relation_statement);
    if (ok) ok= write_meta (db, "derived-source-fingerprint", fingerprint,
                            error);
    changed= ok;
  }

  if (ok) ok= exec_sql (db, "COMMIT;", error);
  if (!ok) {
    std::string ignored;
    exec_sql (db, "ROLLBACK;", ignored);
  }
  return ok;
}

static bool
load_native_namespace_snapshot (sqlite3* db,
                                std::vector<NativeNamespace>& namespaces,
                                std::vector<NativeRelation>& relations,
                                std::string& error) {
  relations.clear ();
  if (!load_native_namespace_rows (db, namespaces, error)) return false;
  std::map<std::string,size_t> indices;
  for (size_t i=0; i<namespaces.size (); ++i)
    indices[namespaces[i].name]= i;

  sqlite3_stmt* statement= nullptr;
  if (!prepare (
        db,
        "SELECT child, parent, source FROM namespace_parents "
        "ORDER BY child, source, ord, parent;",
        &statement, error))
    return false;
  while (true) {
    int status= sqlite3_step (statement);
    if (status == SQLITE_DONE) break;
    if (status != SQLITE_ROW) {
      error= sqlite3_errmsg (db);
      sqlite3_finalize (statement);
      return false;
    }
    auto found= indices.find (column_text (statement, 0));
    if (found == indices.end ()) continue;
    std::string parent= column_text (statement, 1);
    std::string source= column_text (statement, 2);
    if (source == "declared")
      namespaces[found->second].parents.push_back (parent);
    else if (source == "derived")
      namespaces[found->second].derived_parents.push_back (parent);
  }
  sqlite3_finalize (statement);

  if (!prepare (
        db,
        "SELECT parent, child, decision, source FROM relation_decisions "
        "ORDER BY parent, child;",
        &statement, error))
    return false;
  while (true) {
    int status= sqlite3_step (statement);
    if (status == SQLITE_DONE) break;
    if (status != SQLITE_ROW) {
      error= sqlite3_errmsg (db);
      sqlite3_finalize (statement);
      return false;
    }
    NativeRelation relation {column_text (statement, 0),
                             column_text (statement, 1),
                             column_text (statement, 2),
                             column_text (statement, 3)};
    if (!relation.parent.empty () && !relation.child.empty ())
      relations.push_back (std::move (relation));
  }
  sqlite3_finalize (statement);
  return true;
}

static bool
read_namespace_database (const std::string& path,
                         std::vector<NativeNamespace>& namespaces,
                         std::vector<NativeRelation>& relations,
                         std::string& error) {
  namespaces.clear ();
  relations.clear ();
  if (!fs::exists (path)) return true;

  sqlite3* db= nullptr;
  int status= sqlite3_open_v2 (path.c_str (), &db, SQLITE_OPEN_READWRITE,
                               nullptr);
  if (status != SQLITE_OK) {
    error= db == nullptr ? "Could not open namespace database" :
      sqlite3_errmsg (db);
    if (db != nullptr) sqlite3_close (db);
    return false;
  }
  sqlite3_busy_timeout (db, 5000);
  bool derived_changed= false;
  bool ok= exec_sql (db, "PRAGMA foreign_keys=ON;", error) &&
           refresh_native_derived_parents (db, derived_changed, error) &&
           load_native_namespace_snapshot (db, namespaces, relations, error);
  sqlite3_close (db);
  return ok;
}

static std::string
hierarchy_fingerprint (const std::vector<NativeNamespace>& namespaces) {
  uint64_t hash= UINT64_C (1469598103934665603);
  fingerprint_text (hash, "athena-namespace-hierarchy-v1");
  for (const NativeNamespace& ns: namespaces) {
    fingerprint_text (hash, ns.name);
    for (const std::string& parent: ns.parents) {
      fingerprint_text (hash, "declared");
      fingerprint_text (hash, parent);
    }
    for (const std::string& parent: ns.derived_parents) {
      fingerprint_text (hash, "derived");
      fingerprint_text (hash, parent);
    }
  }
  std::ostringstream out;
  out << std::hex << hash;
  return out.str ();
}

static void
collect_reachable (
  const std::string& name,
  const std::map<std::string,std::set<std::string>>& adjacency,
  std::set<std::string>& visiting, std::set<std::string>& reachable) {
  if (!visiting.insert (name).second) return;
  auto children= adjacency.find (name);
  if (children != adjacency.end ())
    for (const std::string& child: children->second)
      if (reachable.insert (child).second)
        collect_reachable (child, adjacency, visiting, reachable);
  visiting.erase (name);
}

static void
build_hierarchy_cache (const std::vector<NativeNamespace>& namespaces,
                       PersistentState& state) {
  state.all_children.clear ();
  state.visible_children.clear ();
  state.folded_children.clear ();

  std::map<std::string,std::set<std::string>> adjacency;
  for (const NativeNamespace& child: namespaces) {
    for (const std::string& parent: child.parents)
      if (parent != child.name) adjacency[parent].insert (child.name);
    for (const std::string& parent: child.derived_parents)
      if (parent != child.name) adjacency[parent].insert (child.name);
  }

  std::map<std::string,std::set<std::string>> reachable;
  for (const NativeNamespace& ns: namespaces) {
    std::set<std::string> visiting;
    collect_reachable (ns.name, adjacency, visiting, reachable[ns.name]);
  }

  for (const auto& group: adjacency) {
    const std::string& parent= group.first;
    for (const std::string& child: group.second) {
      bool folded= false;
      for (const std::string& sibling: group.second) {
        if (sibling == child) continue;
        auto descendants= reachable.find (sibling);
        if (descendants != reachable.end () &&
            descendants->second.find (child) != descendants->second.end ()) {
          folded= true;
          break;
        }
      }
      state.all_children[parent].push_back (child);
      (folded ? state.folded_children[parent] :
                state.visible_children[parent]).push_back (child);
    }
  }
  state.hierarchy_fingerprint= hierarchy_fingerprint (namespaces);
}

static bool
persist_hierarchy_cache (sqlite3* db, const PersistentState& state,
                         std::string& error) {
  if (!exec_sql (db, "BEGIN IMMEDIATE;", error)) return false;
  bool ok= exec_sql (db, "DELETE FROM namespace_cache_children;", error);
  sqlite3_stmt* statement= nullptr;
  if (ok) ok= prepare (
    db,
    "INSERT INTO namespace_cache_children(parent, child, folded) "
    "VALUES(?, ?, ?);",
    &statement, error);
  for (const auto& group: state.all_children) {
    if (!ok) break;
    std::set<std::string> folded;
    auto folded_group= state.folded_children.find (group.first);
    if (folded_group != state.folded_children.end ())
      folded.insert (folded_group->second.begin (), folded_group->second.end ());
    for (const std::string& child: group.second) {
      sqlite3_reset (statement);
      sqlite3_clear_bindings (statement);
      ok= bind_text (statement, 1, group.first, error) &&
          bind_text (statement, 2, child, error) &&
          sqlite3_bind_int (statement, 3,
                            folded.find (child) != folded.end () ? 1 : 0) ==
            SQLITE_OK &&
          sqlite3_step (statement) == SQLITE_DONE;
      if (!ok && error.empty ()) error= sqlite3_errmsg (db);
      if (!ok) break;
    }
  }
  if (statement != nullptr) sqlite3_finalize (statement);
  if (ok) ok= write_meta (db, "namespace-hierarchy-fingerprint",
                          state.hierarchy_fingerprint, error);
  if (ok) ok= exec_sql (db, "COMMIT;", error);
  if (!ok) {
    std::string ignored;
    exec_sql (db, "ROLLBACK;", ignored);
  }
  return ok;
}

class NamespaceOntologyService {
public:
  ~NamespaceOntologyService () { stop (); }

  void start (url root_url, url db_url) {
    stop ();
    std::lock_guard<std::mutex> lock (mutex_);
    root_= url_path (root_url);
    namespace_db_path_= url_path (db_url);
    db_path_= (fs::path (root_) / ".athena" /
               "namespace-ontology.sqlite").string ();
    active_= !root_.empty () && !db_path_.empty () &&
             !namespace_db_path_.empty ();
    stopping_= false;
    requested_generation_= 1;
    published_generation_= 0;
    failed_generation_= 0;
    force_files_= false;
    snapshot_ready_= false;
    snapshot_.reset ();
    last_error_.clear ();
    if (active_) worker_= std::thread ([this] () { run (); });
  }

  void stop () {
    std::thread worker;
    {
      std::lock_guard<std::mutex> lock (mutex_);
      if (!worker_.joinable ()) {
        active_= false;
        snapshot_ready_= false;
        snapshot_.reset ();
        return;
      }
      stopping_= true;
      condition_.notify_all ();
      worker= std::move (worker_);
    }
    worker.join ();
    std::lock_guard<std::mutex> lock (mutex_);
    active_= false;
    stopping_= false;
    snapshot_ready_= false;
    snapshot_.reset ();
    root_.clear ();
    db_path_.clear ();
    namespace_db_path_.clear ();
  }

  void invalidate (bool force_files) {
    std::lock_guard<std::mutex> lock (mutex_);
    if (!active_) return;
    ++requested_generation_;
    force_files_= force_files_ || force_files;
    condition_.notify_all ();
  }

  bool refresh (bool force_files, string& error) {
    invalidate (force_files);
    std::shared_ptr<const OntologySnapshot> ignored;
    if (snapshot (ignored, error)) return true;
    if (error == "") error= "Namespace ontology refresh failed.";
    return false;
  }

  athena_namespace_ontology_status status (string& error) {
    std::lock_guard<std::mutex> lock (mutex_);
    if (!active_ || stopping_) return athena_namespace_ontology_inactive;
    if (snapshot_ready_ && published_generation_ >= requested_generation_)
      return athena_namespace_ontology_ready;
    if (failed_generation_ >= requested_generation_) {
      error= std_to_tm_string (last_error_);
      return athena_namespace_ontology_failed;
    }
    return athena_namespace_ontology_building;
  }

  bool snapshot (std::shared_ptr<const OntologySnapshot>& out, string& error) {
    std::unique_lock<std::mutex> lock (mutex_);
    if (!active_) return false;
    uint64_t wanted= requested_generation_;
    condition_.wait (lock, [this, wanted] () {
      return !active_ || stopping_ || published_generation_ >= wanted ||
             failed_generation_ >= wanted;
    });
    if (!active_ || stopping_) return false;
    if (!snapshot_ready_ || published_generation_ < wanted) {
      error= std_to_tm_string (last_error_);
      return false;
    }
    out= snapshot_;
    return true;
  }

private:
  void run () {
    PersistentState state;
    uint64_t handled_generation= 0;
    while (true) {
      uint64_t target_generation= 0;
      bool force_files= false;
      std::string root;
      std::string db_path;
      std::string namespace_db_path;
      std::shared_ptr<const OntologySnapshot> previous;
      {
        std::unique_lock<std::mutex> lock (mutex_);
        if (handled_generation >= requested_generation_)
          condition_.wait_for (lock, std::chrono::seconds (3), [this,
                               handled_generation] () {
            return stopping_ || requested_generation_ > handled_generation;
          });
        if (stopping_) break;
        target_generation= requested_generation_;
        force_files= force_files_;
        force_files_= false;
        root= root_;
        db_path= db_path_;
        namespace_db_path= namespace_db_path_;
        previous= snapshot_ready_ ? snapshot_ : nullptr;
      }

      std::shared_ptr<const OntologySnapshot> next;
      std::string error;
      bool ok= build (fs::path (root), db_path, namespace_db_path,
                      force_files, previous, state, next, error);
      {
        std::lock_guard<std::mutex> lock (mutex_);
        handled_generation= target_generation;
        if (ok) {
          snapshot_= std::move (next);
          snapshot_ready_= true;
          published_generation_= target_generation;
          last_error_.clear ();
        }
        else {
          failed_generation_= target_generation;
          last_error_= error;
          state= PersistentState {};
        }
        condition_.notify_all ();
      }
    }
  }

  static bool build (const fs::path& root, const std::string& db_path,
                     const std::string& namespace_db_path, bool force_files,
                     const std::shared_ptr<const OntologySnapshot>& previous,
                     PersistentState& state,
                     std::shared_ptr<const OntologySnapshot>& result,
                     std::string& error) {
    std::error_code namespace_stat_error;
    int64_t namespace_db_mtime= 0;
    if (fs::exists (namespace_db_path, namespace_stat_error))
      namespace_db_mtime=
        directory_mtime_ns (namespace_db_path, namespace_stat_error);
    if (namespace_stat_error) {
      error= "Could not inspect namespace database timestamp: " +
             namespace_stat_error.message ();
      return false;
    }

    bool namespaces_changed= previous == nullptr ||
      state.namespace_db_mtime_ns != namespace_db_mtime;
    std::vector<NativeNamespace> namespaces;
    std::vector<NativeRelation> relations;
    if (namespaces_changed) {
      if (!read_namespace_database (namespace_db_path, namespaces, relations,
                                    error))
        return false;
      namespace_stat_error.clear ();
      namespace_db_mtime= fs::exists (namespace_db_path, namespace_stat_error) ?
        directory_mtime_ns (namespace_db_path, namespace_stat_error) : 0;
      if (namespace_stat_error) {
        error= "Could not refresh namespace database timestamp: " +
               namespace_stat_error.message ();
        return false;
      }
      state.namespace_db_mtime_ns= namespace_db_mtime;
    }

    const std::vector<NativeNamespace>& active_namespaces=
      namespaces_changed ? namespaces : previous->namespaces;

    SqliteConnection connection;
    if (!connection.open (db_path, error)) return false;
    if (!state.loaded && !load_persistent_state (connection.db, state, error))
      return false;

    InventoryDelta delta;
    if (!scan_file_inventory (root, force_files, state, delta, error))
      return false;

    std::string fingerprint= matching_fingerprint (active_namespaces);
    bool full_rebuild= force_files || state.match_fingerprint != fingerprint;
    std::set<std::string> affected_files= delta.affected_files ();
    bool matches_changed= full_rebuild || !affected_files.empty ();
    if (full_rebuild) {
      if (!rebuild_matches (active_namespaces, state, error) ||
          !persist_matches (connection.db, state, nullptr, error))
        return false;
    }
    else if (!affected_files.empty ()) {
      if (!update_matches (active_namespaces, affected_files, state, error) ||
          !persist_matches (connection.db, state, &affected_files, error))
        return false;
    }
    if (delta.changed () &&
        !persist_inventory (connection.db, state, delta, error))
      return false;

    std::string hierarchy_hash= hierarchy_fingerprint (active_namespaces);
    bool hierarchy_changed= force_files ||
      state.hierarchy_fingerprint != hierarchy_hash;
    if (hierarchy_changed) {
      build_hierarchy_cache (active_namespaces, state);
      if (!persist_hierarchy_cache (connection.db, state, error)) return false;
    }

    if (matches_changed || hierarchy_changed || namespaces_changed ||
        previous == nullptr) {
      std::shared_ptr<OntologySnapshot> next= std::make_shared<OntologySnapshot> ();
      if (namespaces_changed) {
        next->namespaces= std::move (namespaces);
        next->relations= std::move (relations);
      }
      else {
        next->namespaces= previous->namespaces;
        next->relations= previous->relations;
      }
      next->direct_matches= state.direct_matches;
      next->all_children= state.all_children;
      next->visible_children= state.visible_children;
      next->folded_children= state.folded_children;
      next->root= root;
      build_snapshot_index (*next);
      result= std::move (next);
    }
    else result= previous;
    return true;
  }

  std::mutex mutex_;
  std::condition_variable condition_;
  std::thread worker_;
  bool active_= false;
  bool stopping_= false;
  bool force_files_= false;
  bool snapshot_ready_= false;
  uint64_t requested_generation_= 0;
  uint64_t published_generation_= 0;
  uint64_t failed_generation_= 0;
  std::string root_;
  std::string db_path_;
  std::string namespace_db_path_;
  std::string last_error_;
  std::shared_ptr<const OntologySnapshot> snapshot_;
};

NamespaceOntologyService&
service () {
  static NamespaceOntologyService instance;
  return instance;
}

} // namespace

void
athena_namespace_ontology_start (url vault_root, url namespace_db) {
  service ().start (vault_root, namespace_db);
}

void
athena_namespace_ontology_stop () {
  service ().stop ();
}

void
athena_namespace_ontology_invalidate (bool force_files) {
  service ().invalidate (force_files);
}

bool
athena_namespace_ontology_refresh (bool force_files, string& error) {
  return service ().refresh (force_files, error);
}

athena_namespace_ontology_status
athena_namespace_ontology_get_status (string& error) {
  return service ().status (error);
}

bool
athena_namespace_ontology_namespaces (
  std::vector<athena_namespace_definition>& out) {
  std::shared_ptr<const OntologySnapshot> snapshot;
  string error;
  if (!service ().snapshot (snapshot, error)) return false;
  out.clear ();
  out.reserve (snapshot->namespaces.size ());
  for (const NativeNamespace& ns: snapshot->namespaces)
    out.push_back (materialize_namespace (ns));
  return true;
}

bool
athena_namespace_ontology_namespace (string name,
                                     athena_namespace_definition& out) {
  std::shared_ptr<const OntologySnapshot> snapshot;
  string error;
  if (!service ().snapshot (snapshot, error)) return false;
  auto found= snapshot->namespace_indices.find (tm_to_std_string (name));
  if (found == snapshot->namespace_indices.end () ||
      found->second >= snapshot->namespaces.size ())
    return false;
  out= materialize_namespace (snapshot->namespaces[found->second]);
  return true;
}

bool
athena_namespace_ontology_relations (
  std::vector<athena_namespace_relation>& out) {
  std::shared_ptr<const OntologySnapshot> snapshot;
  string error;
  if (!service ().snapshot (snapshot, error)) return false;
  out.clear ();
  out.reserve (snapshot->relations.size ());
  for (const NativeRelation& relation: snapshot->relations)
    out.push_back (materialize_relation (relation));
  return true;
}

bool
athena_namespace_ontology_members (
  string name, std::vector<athena_namespace_match>& out, string& error) {
  std::shared_ptr<const OntologySnapshot> snapshot;
  if (!service ().snapshot (snapshot, error)) return false;
  std::string key= tm_to_std_string (name);
  auto found= snapshot->namespace_indices.find (key);
  if (found == snapshot->namespace_indices.end () ||
      found->second >= snapshot->namespaces.size ()) {
    error= "Unknown namespace: " * name;
    return false;
  }
  const NativeNamespace& ns= snapshot->namespaces[found->second];
  std::set<std::string> visiting;
  std::set<std::string> seen_files;
  std::vector<CachedMatch> cached;
  collect_members (key, ns.kind == "abstract", snapshot->namespaces,
                   snapshot->direct_matches, snapshot->root, visiting,
                   seen_files, cached);
  out.clear ();
  out.reserve (cached.size ());
  for (const CachedMatch& match: cached)
    out.push_back (materialize_match (snapshot->root, match));
  return true;
}

bool
athena_namespace_ontology_children (
  string name, bool simplified, array<string>& visible, array<string>& folded,
  string& error) {
  std::shared_ptr<const OntologySnapshot> snapshot;
  if (!service ().snapshot (snapshot, error)) return false;
  std::string key= tm_to_std_string (name);
  if (snapshot->namespace_indices.find (key) ==
      snapshot->namespace_indices.end ()) {
    error= "Unknown namespace: " * name;
    return false;
  }
  visible= array<string> ();
  folded= array<string> ();
  const auto& visible_map= simplified ? snapshot->visible_children :
                                        snapshot->all_children;
  auto visible_group= visible_map.find (key);
  if (visible_group != visible_map.end ())
    for (const std::string& child: visible_group->second)
      visible << std_to_tm_string (child);
  if (simplified) {
    auto folded_group= snapshot->folded_children.find (key);
    if (folded_group != snapshot->folded_children.end ())
      for (const std::string& child: folded_group->second)
        folded << std_to_tm_string (child);
  }
  return true;
}

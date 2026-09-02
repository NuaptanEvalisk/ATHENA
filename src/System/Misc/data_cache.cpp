
/******************************************************************************
* MODULE     : data_cache.cpp
* DESCRIPTION: utilities for caching data
* COPYRIGHT  : (C) 2005  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "data_cache.hpp"
#include "file.hpp"
#include "convert.hpp"
#include "iterator.hpp"
#include "tm_timer.hpp"
#include <mutex>

/******************************************************************************
* Caching routines
******************************************************************************/

static thread_local hashmap<tree,tree> cache_data ("?");
static thread_local hashset<string> cache_loaded;
static thread_local hashset<string> cache_loading;
static thread_local hashset<string> cache_changed;
static thread_local hashmap<string,bool> cache_valid (false);

// Cache objects contain legacy owner-affine trees.  Keep those objects local to
// each BufferActor; only serialize access to the persistent files themselves.
static std::recursive_mutex cache_disk_mutex;

void
cache_set (string buffer, tree key, tree t) {
  cache_load (buffer);
  tree ckey= tuple (buffer, key);
  if (cache_data[ckey] != t) {
    cache_data (ckey)= t;
    cache_changed->insert (buffer);
  }
}

void
cache_reset (string buffer, tree key) {
  cache_load (buffer);
  tree ckey= tuple (buffer, key);
  cache_data->reset (ckey);
  cache_changed->insert (buffer);
}

bool
is_cached (string buffer, tree key) {
  cache_load (buffer);
  tree ckey= tuple (buffer, key);
  return cache_data->contains (ckey);
}

tree
cache_get (string buffer, tree key) {
  cache_load (buffer);
  tree ckey= tuple (buffer, key);
  return cache_data [ckey];
}

bool
is_up_to_date (url dir) {
  string name_dir= concretize (dir);
  if (cache_valid->contains (name_dir)) return cache_valid [name_dir];
  int l= last_modified (dir, false);
  if (is_cached ("validate_cache.scm", name_dir)) {
    int r= as_int (cache_get ("validate_cache.scm", name_dir) -> label);
    if (l == r) {
      bool ok= (l != (- (int) (((unsigned int) (-1)) >> 1)));
      cache_valid (name_dir)= ok;
      return ok;
    }
    //cout << name_dir << " no longer up to date " << r << " -> " << l << "\n";
  }
  //else cout << name_dir << " not up to date " << l << "\n";
  cache_set ("validate_cache.scm", name_dir, as_string (l));
  cache_valid (name_dir)= false;
  // FIXME: we should explicitly remove all data concerning files in 'dir'
  // from the various caches.  Indeed, at a next run of TeXmacs, the directory
  // will be regarded as up to date, but the other caches may still contain
  // outdated data.  Careful: invalidating the cache lines should not
  // give rise to a performance penaly (e.g. go through all entries of
  // 'cache_data', or reloading unchanged files many times).
  // See also 'declare_out_of_date'.
  return false;
}

bool
is_recursively_up_to_date (url dir) {
  if (!is_up_to_date (dir)) return false;
  bool error_flag;
  array<string> a= read_directory (dir, error_flag);
  for (int i=0; i<N(a); i++)
    if (url (a[i]) != url_here () && url (a[i]) != url_parent ())
      if (N(a[i])>0 && a[i][0] != '.')
        if (is_directory (dir * a[i]))
          if (!is_recursively_up_to_date (dir * a[i]))
            return false;
  return true;
}

void
declare_out_of_date (url dir) {
  //cout << "out of date: " << dir << "\n";
  string name_dir= concretize (dir);
  int l= last_modified (dir, false);
  cache_set ("validate_cache.scm", name_dir, as_string (l));
  cache_valid (name_dir)= false;
  // FIXME: see 'FIXME' in 'is_up_to_date'.
}

/******************************************************************************
* Which files should be stored in the cache?
******************************************************************************/

static thread_local url texmacs_path (url_none ());
static thread_local url texmacs_doc_path (url_none ());
static thread_local url texmacs_home_path (url_none ());

static thread_local string texmacs_path_string;
static thread_local string texmacs_doc_path_string;
static thread_local string texmacs_home_path_string;
static thread_local string texmacs_font_path_string;
static thread_local bool cache_paths_initialized= false;

static void
cache_initialize_paths () {
  if (cache_paths_initialized) return;
  texmacs_path= url_system ("$ATHENA_PATH");
  if (get_env ("ATHENA_HOME_PATH") == "")
    texmacs_home_path= url_system ("$HOME/.ATHENA");
  else texmacs_home_path= url_system ("$ATHENA_HOME_PATH");
  if (get_env ("ATHENA_DOC_PATH") == "")
    texmacs_doc_path= url_system ("$ATHENA_PATH/doc");
  else texmacs_doc_path= url_system ("$ATHENA_DOC_PATH");

  texmacs_path_string= concretize (texmacs_path);
  texmacs_home_path_string= concretize (texmacs_home_path);
  texmacs_doc_path_string= concretize (texmacs_doc_path);
  texmacs_font_path_string= concretize (texmacs_home_path * "fonts/");
  cache_paths_initialized= true;
}

bool
do_cache_dir (string name) {
  cache_initialize_paths ();
  return
    starts (name, texmacs_path_string) ||
    starts (name, texmacs_doc_path_string);
}

bool
do_cache_stat (string name) {
  cache_initialize_paths ();
  return
    starts (name, texmacs_path_string) ||
    starts (name, texmacs_font_path_string) ||
    starts (name, texmacs_doc_path_string);
}

bool
do_cache_stat_fail (string name) {
  cache_initialize_paths ();
  return
    !ends (name, ".ts") &&
    (starts (name, texmacs_path_string) ||
     starts (name, texmacs_doc_path_string));
}

bool
do_cache_file (string name) {
  cache_initialize_paths ();
  return
    !ends (name, ".ts") && !ends (name, ".css") &&
    (starts (name, texmacs_path_string) ||
     starts (name, texmacs_font_path_string));
}

bool
do_cache_doc (string name) {
  cache_initialize_paths ();
  return starts (name, texmacs_doc_path_string);
}

/******************************************************************************
* Saving and loading the cache to/from disk
******************************************************************************/

void
cache_save (string buffer) {
  if (cache_changed->contains (buffer)) {
    cache_initialize_paths ();
    std::lock_guard<std::recursive_mutex> disk_guard (cache_disk_mutex);
    url cache_dir= texmacs_home_path * url ("system/cache");
    if (!is_directory (cache_dir)) mkdir (cache_dir);

    url cache_file= cache_dir * url (buffer);
    string cached;
    iterator<tree> it= iterate (cache_data);
    if (buffer == "file_cache" || buffer == "doc_cache") {
      while (it->busy ()) {
        tree ckey= it->next ();
        if (ckey[0] == buffer) {
          cached << ckey[1]->label << "\n";
          cached << cache_data [ckey]->label << "\n";
          cached << "%-%-tm-cache-%-%\n";
        }
      }
    }
    else {
      cached << "(tuple\n";
      while (it->busy ()) {
        tree ckey= it->next ();
        if (ckey[0] == buffer) {
          cached << tree_to_scheme (ckey[1]) << " ";
          cached << tree_to_scheme (cache_data [ckey]) << "\n";
        }
      }
      cached << ")";
    }
    if (!save_string (cache_file, cached))
      cache_changed->remove (buffer);
  }
}

void
cache_load (string buffer) {
  if (!cache_loaded->contains (buffer) &&
      !cache_loading->contains (buffer)) {
    cache_loading->insert (buffer);
    cache_initialize_paths ();
    std::lock_guard<std::recursive_mutex> disk_guard (cache_disk_mutex);
    url cache_file = texmacs_home_path * url ("system/cache/" * buffer);
    //cout << "cache_file "<< cache_file << LF;
    string cached;
    if (!load_string (cache_file, cached, false)) {
      if (buffer == "file_cache" || buffer == "doc_cache") {
        int i=0, n= N(cached);
        while (i<n) {
          int start= i;
          while (i<n && cached[i] != '\n') i++;
          string key= cached (start, i);
          i++; start= i;
          while (i<n && (cached[i] != '\n' ||
                         !test (cached, i+1, "%-%-tm-cache-%-%"))) i++;
          string im= cached (start, i);
          i++;
          while (i<n && cached[i] != '\n') i++;
          i++;
          //cout << "key= " << key << "\n----------------------\n";
          //cout << "im= " << im << "\n----------------------\n";
          cache_data (tuple (buffer, key))= im;
        }
      }
      else {
        tree t= scheme_to_tree (cached);
        for (int i=0; i<N(t)-1; i+=2)
          cache_data (tuple (buffer, t[i]))= t[i+1];
      }
    }
    cache_loading->remove (buffer);
    cache_loaded->insert (buffer);
  }
}

void
cache_memorize () {
  cache_save ("file_cache");
  cache_save ("doc_cache");
  cache_save ("dir_cache.scm");
  cache_save ("stat_cache.scm");
  cache_save ("font_cache.scm");
  cache_save ("font_path_cache_v2.scm");
  cache_save ("font_file_index_v2.scm");
  cache_save ("validate_cache.scm");
}

void
cache_refresh () {
  cache_data   = hashmap<tree,tree> ("?");
  cache_loaded = hashset<string> ();
  cache_loading= hashset<string> ();
  cache_changed= hashset<string> ();
  cache_load ("file_cache");
  cache_load ("dir_cache.scm");
  cache_load ("stat_cache.scm");
  cache_load ("font_cache.scm");
  cache_load ("font_path_cache_v2.scm");
  cache_load ("font_file_index_v2.scm");
  cache_load ("validate_cache.scm");
}

void
cache_initialize () {
  cache_initialize_paths ();

  // Version 1 used structural keys before tree labels were initialized.
  remove (texmacs_home_path * "system/cache/font_path_cache.scm");
  remove (texmacs_home_path * "system/cache/font_file_index.scm");

  bench_start ("load persistent data caches");
  cache_refresh ();
  bench_cumul ("load persistent data caches");
}

void
cache_validate_font_directories () {
  bench_start ("validate font cache directories");
  if (is_recursively_up_to_date (texmacs_path * "fonts/type1") &&
      is_recursively_up_to_date (texmacs_path * "fonts/truetype") &&
      is_recursively_up_to_date (texmacs_home_path * "fonts/type1") &&
      is_recursively_up_to_date (texmacs_home_path * "fonts/truetype"));
  else remove (texmacs_home_path * "fonts/error" * url_wildcard ("*"));
  bench_cumul ("validate font cache directories");
}

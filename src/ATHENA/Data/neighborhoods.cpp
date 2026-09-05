/******************************************************************************
* MODULE     : neighborhoods.cpp
* DESCRIPTION: ATHENA document neighborhood service
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "neighborhoods.hpp"

#include "boot.hpp"
#include "namespaces.hpp"
#include "namespaces_private.hpp"
#include "new_buffer.hpp"
#include "scheme.hpp"
#include "vault.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

using athena_namespaces::std_to_tm;
using athena_namespaces::tm_to_std;

namespace {

static std::map<std::string,std::string> selected_neighborhoods;

struct vault_ath_file {
  url         file;
  fs::path    path;
  std::string key;
  std::string stem;
};

struct namespace_containment_cache {
  std::map<std::string,std::set<std::string> > direct_parents;
  std::map<std::string,std::set<std::string> > ancestor_memo;
  std::set<std::string> visiting;

  namespace_containment_cache (
    const namespace_records<athena_namespace_definition>& definitions) {
    for (const athena_namespace_definition& ns: definitions) {
      std::string name= tm_to_std (ns.name);
      std::set<std::string>& parents= direct_parents[name];
      for (int i=0; i<(int) ns.parents.size (); i++)
        parents.insert (tm_to_std (ns.parents[i]));
      for (int i=0; i<(int) ns.derived_parents.size (); i++)
        parents.insert (tm_to_std (ns.derived_parents[i]));
    }
  }

  const std::set<std::string>& ancestors (const std::string& child) {
    std::map<std::string,std::set<std::string> >::iterator memo=
      ancestor_memo.find (child);
    if (memo != ancestor_memo.end ()) return memo->second;

    std::set<std::string>& out= ancestor_memo[child];
    if (visiting.find (child) != visiting.end ()) return out;
    visiting.insert (child);

    std::map<std::string,std::set<std::string> >::iterator direct=
      direct_parents.find (child);
    if (direct != direct_parents.end ()) {
      for (const std::string& parent: direct->second) {
        out.insert (parent);
        const std::set<std::string>& more= ancestors (parent);
        out.insert (more.begin (), more.end ());
      }
    }

    visiting.erase (child);
    return out;
  }

  bool contains (string parent, string child) {
    std::string parent_key= tm_to_std (parent);
    std::string child_key= tm_to_std (child);
    const std::set<std::string>& all= ancestors (child_key);
    return all.find (parent_key) != all.end ();
  }
};

static fs::path
canonical_path (const fs::path& path) {
  std::error_code ec;
  fs::path out= fs::weakly_canonical (path, ec);
  if (!ec) return out;
  out= fs::absolute (path, ec);
  if (!ec) return out.lexically_normal ();
  return path.lexically_normal ();
}

static fs::path
url_path (url u) {
  return canonical_path (fs::path (tm_to_std (concretize (u))));
}

static string
tm_path (const fs::path& path) {
  return std_to_tm (path.string ());
}

static bool
path_descends (const fs::path& child, const fs::path& parent) {
  fs::path rel= child.lexically_relative (parent);
  if (rel.empty ()) return child == parent;
  if (rel.is_absolute ()) return false;
  for (const fs::path& part: rel)
    if (part == "..") return false;
  return true;
}

static std::string
key_for_path (const fs::path& path) {
  return path.string ();
}

static std::string
key_for_url (url u) {
  return key_for_path (url_path (u));
}

static std::vector<vault_ath_file>
vault_ath_files () {
  std::vector<vault_ath_file> out;
  array<url> files= vault_get_all_files ();
  for (int i=0; i<N(files); i++) {
    if (suffix (files[i]) != "ath") continue;
    vault_ath_file entry;
    entry.file= files[i];
    entry.path= url_path (files[i]);
    entry.key= key_for_path (entry.path);
    entry.stem= entry.path.stem ().string ();
    out.push_back (entry);
  }
  return out;
}

static string
display_for_file (url u) {
  fs::path path= url_path (u);
  std::string stem= path.stem ().string ();
  if (stem.empty ()) stem= path.filename ().string ();
  return std_to_tm (stem);
}

static bool
append_unique_entry (std::vector<athena_neighborhood_entry>& out,
                     std::set<std::string>& seen, url file, string display) {
  fs::path path= url_path (file);
  std::string key= key_for_path (path);
  if (seen.find (key) != seen.end ()) return false;
  seen.insert (key);

  athena_neighborhood_entry entry;
  entry.file= file;
  entry.display= display;
  entry.canonical_path= tm_path (path);
  out.push_back (entry);
  return true;
}

static bool
file_is_current (const athena_neighborhood_entry& entry,
                 const std::string& current_key) {
  return tm_to_std (entry.canonical_path) == current_key;
}

static int
find_current_index (const std::vector<athena_neighborhood_entry>& files,
                    const std::string& current_key) {
  for (int i=0; i<(int) files.size (); i++)
    if (file_is_current (files[i], current_key)) return i;
  return -1;
}

static bool
current_is_vault_ath_file (url file, std::string& current_key,
                           const std::vector<vault_ath_file>& files,
                           string& error) {
  if (!vault_active ()) {
    error= "No active vault.";
    return false;
  }
  if (is_none (file) || suffix (file) != "ath") {
    error= "The current buffer is not a vault .ath document.";
    return false;
  }

  fs::path current= url_path (file);
  fs::path root= url_path (vault_get_root ());
  if (!path_descends (current, root)) {
    error= "The current document is outside the active vault.";
    return false;
  }

  current_key= key_for_path (current);
  for (const vault_ath_file& entry: files)
    if (entry.key == current_key)
      return true;

  error= "The current document is not in the active vault file index.";
  return false;
}

static athena_neighborhood_row
path_row (const std::string& current_key, url current,
          const std::vector<vault_ath_file>& all) {
  athena_neighborhood_row row;
  row.kind= ATHENA_NEIGHBORHOOD_PATH;
  row.key= "path";
  row.name= "Path";
  row.current_index= -1;

  fs::path current_dir= url_path (current).parent_path ();
  std::vector<vault_ath_file> files;
  for (const vault_ath_file& entry: all)
    if (entry.path.parent_path () == current_dir) files.push_back (entry);

  std::sort (files.begin (), files.end (),
             [] (const vault_ath_file& a, const vault_ath_file& b) {
    std::string an= a.path.filename ().string ();
    std::string bn= b.path.filename ().string ();
    if (an != bn) return an < bn;
    return a.key < b.key;
  });

  std::set<std::string> seen;
  for (const vault_ath_file& file: files)
    append_unique_entry (row.files, seen, file.file, display_for_file (file.file));
  row.current_index= find_current_index (row.files, current_key);
  return row;
}

static athena_neighborhood_row
namespace_row (const athena_namespace_definition& ns,
               const namespace_records<athena_namespace_match>& matches,
               const std::string& current_key, string warning) {
  athena_neighborhood_row row;
  row.kind= ATHENA_NEIGHBORHOOD_NAMESPACE;
  row.namespace_name= ns.name;
  row.key= "ns:" * ns.name;
  row.name= "NS: " * ns.name;
  row.current_index= -1;
  row.warning= warning;

  std::set<std::string> seen;
  for (const athena_namespace_match& match: matches) {
    string display= match.stem == "" ? display_for_file (match.file_url ())
                                     : match.stem;
    append_unique_entry (row.files, seen, match.file_url (), display);
  }
  row.current_index= find_current_index (row.files, current_key);
  return row;
}

static std::vector<athena_neighborhood_row>
namespace_rows (const std::string& current_key, const std::string& current_stem) {
  std::vector<athena_neighborhood_row> rows;
  namespace_records<athena_namespace_definition> defs_v= athena_namespaces_list ();
  namespace_containment_cache contains (defs_v);

  struct containing_namespace {
    const athena_namespace_definition* ns;
    namespace_records<athena_namespace_match> matches;
    string warning;
  };
  std::vector<containing_namespace> containing;

  for (const athena_namespace_definition& ns: defs_v) {
    if (athena_namespaces::canonical_kind (ns.kind) == "abstract") continue;
    athena_namespace_match current_match;
    string error;
    if (!athena_namespace_match_stem (ns, std_to_tm (current_stem),
                                      current_match, error))
      continue;
    namespace_records<athena_namespace_match> matches=
      athena_namespace_members (ns.name, error);
    containing_namespace entry;
    entry.ns= &ns;
    entry.matches= matches;
    entry.warning= error;
    containing.push_back (entry);
  }

  for (const containing_namespace& candidate: containing) {
    bool has_containing_child= false;
    for (const containing_namespace& other: containing) {
      if (other.ns->name == candidate.ns->name) continue;
      if (contains.contains (candidate.ns->name, other.ns->name)) {
        has_containing_child= true;
        break;
      }
    }
    if (!has_containing_child)
      rows.push_back (namespace_row (*candidate.ns, candidate.matches,
                                     current_key, candidate.warning));
  }

  std::sort (rows.begin (), rows.end (),
             [] (const athena_neighborhood_row& a,
                 const athena_neighborhood_row& b) {
               return tm_to_std (a.name) < tm_to_std (b.name);
             });
  return rows;
}

static int
row_index_for_key (const athena_neighborhood_set& set, string key) {
  for (int i=0; i<(int) set.rows.size (); i++)
    if (set.rows[i].key == key) return i;
  return -1;
}

static void
choose_selected_row (athena_neighborhood_set& set) {
  if (set.rows.empty ()) {
    set.selected_row= -1;
    set.selected_key= "";
    return;
  }

  std::string file_key= tm_to_std (set.canonical_path);
  std::map<std::string,std::string>::iterator saved=
    selected_neighborhoods.find (file_key);
  if (saved != selected_neighborhoods.end ()) {
    int index= row_index_for_key (set, std_to_tm (saved->second));
    if (index >= 0) {
      set.selected_row= index;
      set.selected_key= set.rows[index].key;
      return;
    }
  }

  string pref= get_preference ("vault preferred initial neighborhood",
                               "namespace");
  if (pref == "path") {
    int index= row_index_for_key (set, "path");
    if (index >= 0) {
      set.selected_row= index;
      set.selected_key= set.rows[index].key;
      return;
    }
  }
  else {
    for (int i=0; i<(int) set.rows.size (); i++)
      if (set.rows[i].kind == ATHENA_NEIGHBORHOOD_NAMESPACE) {
        set.selected_row= i;
        set.selected_key= set.rows[i].key;
        return;
      }
  }

  set.selected_row= 0;
  set.selected_key= set.rows[0].key;
}

static bool
remember_selection (url file, string key) {
  string error;
  std::string current_key;
  std::vector<vault_ath_file> files= vault_ath_files ();
  if (!current_is_vault_ath_file (file, current_key, files, error))
    return false;
  selected_neighborhoods[current_key]= tm_to_std (key);
  return true;
}

} // namespace

athena_neighborhood_set::athena_neighborhood_set ()
  : valid (false), selected_row (-1) {}

athena_neighborhood_set
athena_neighborhoods_for_file (url file) {
  athena_neighborhood_set set;
  set.current_file= file;

  std::string current_key;
  string error;
  std::vector<vault_ath_file> files= vault_ath_files ();
  if (!current_is_vault_ath_file (file, current_key, files, error)) {
    set.error= error;
    return set;
  }

  set.valid= true;
  set.canonical_path= std_to_tm (current_key);
  std::string current_stem;
  for (const vault_ath_file& entry: files)
    if (entry.key == current_key) {
      current_stem= entry.stem;
      break;
    }
  athena_neighborhood_row path= path_row (current_key, file, files);
  if (path.current_index >= 0) set.rows.push_back (path);

  std::vector<athena_neighborhood_row> ns_rows=
    namespace_rows (current_key, current_stem);
  for (const athena_neighborhood_row& row: ns_rows)
    if (row.current_index >= 0) set.rows.push_back (row);

  if (set.rows.empty ()) {
    set.valid= false;
    set.error= "No neighborhoods for the current document.";
    return set;
  }
  choose_selected_row (set);
  return set;
}

athena_neighborhood_set
athena_current_neighborhoods () {
  return athena_neighborhoods_for_file (get_current_buffer_safe ());
}

bool
athena_neighborhood_select (url file, string key) {
  athena_neighborhood_set set= athena_neighborhoods_for_file (file);
  if (!set.valid) return false;
  if (row_index_for_key (set, key) < 0) return false;
  return remember_selection (file, key);
}

bool
athena_neighborhood_select_row (url file, int row) {
  athena_neighborhood_set set= athena_neighborhoods_for_file (file);
  if (!set.valid || row < 0 || row >= (int) set.rows.size ()) return false;
  return remember_selection (file, set.rows[row].key);
}

bool
athena_neighborhood_neighbor (url file, int direction, url& out,
                              string& message) {
  out= url_none ();
  if (direction == 0) return false;
  direction= direction < 0 ? -1 : 1;

  athena_neighborhood_set set= athena_neighborhoods_for_file (file);
  if (!set.valid) {
    message= set.error;
    return false;
  }
  if (set.selected_row < 0 || set.selected_row >= (int) set.rows.size ()) {
    message= "No selected neighborhood.";
    return false;
  }

  const athena_neighborhood_row& row= set.rows[set.selected_row];
  int next= row.current_index + direction;
  if (row.current_index < 0 || next < 0 || next >= (int) row.files.size ()) {
    message= direction < 0 ? "No left neighbor." : "No right neighbor.";
    return false;
  }

  out= row.files[next].file;
  message= row.name;
  return true;
}

bool
athena_neighborhood_current_neighbor (int direction, url& out,
                                      string& message) {
  return athena_neighborhood_neighbor (get_current_buffer_safe (), direction,
                                      out, message);
}

bool
athena_neighborhood_cycle (url file, string& message) {
  athena_neighborhood_set set= athena_neighborhoods_for_file (file);
  if (!set.valid) {
    message= set.error;
    return false;
  }
  if (set.rows.empty ()) {
    message= "No neighborhoods for the current document.";
    return false;
  }

  int next= set.selected_row < 0 ? 0 : (set.selected_row + 1) %
                                      (int) set.rows.size ();
  if (!remember_selection (file, set.rows[next].key)) return false;
  message= "Neighborhood: " * set.rows[next].name;
  return true;
}

bool
athena_neighborhood_cycle_current (string& message) {
  return athena_neighborhood_cycle (get_current_buffer_safe (), message);
}

void
athena_neighborhood_clear_session () {
  selected_neighborhoods.clear ();
}

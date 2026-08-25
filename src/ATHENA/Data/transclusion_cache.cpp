/******************************************************************************
* MODULE     : transclusion_cache.cpp
* DESCRIPTION: Cached structural resolution of Vault transclusions
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "ATHENA/Data/transclusion_cache.hpp"

#include "ATHENA/Data/vault.hpp"
#include "analyze.hpp"
#include "convert.hpp"
#include "converter.hpp"
#include "file.hpp"
#include "scheme.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace fs= std::filesystem;

namespace {

using TreePath= std::vector<int>;

struct SourceTreeCacheEntry {
  string signature;
  tree document;
  tree body;
  std::map<std::string,TreePath> anchors;
  uint64_t last_use= 0;
};

struct ResolvedCacheEntry {
  tree content;
  uint64_t last_use= 0;
};

std::map<std::string,SourceTreeCacheEntry> source_cache;
std::map<std::string,ResolvedCacheEntry> resolved_cache;
std::map<std::string,ResolvedCacheEntry> display_cache;
std::string cached_vault_root;
uint64_t cache_clock= 0;

std::string
to_std (string value) {
  return std::string (as_charp (value), (size_t) N(value));
}

string
to_tm (const std::string& value) {
  return string (value.data (), (int) value.size ());
}

string
tree_text (tree value) {
  return is_atomic (value) ? value->label : tree_as_string (value);
}

string
source_signature (const fs::path& path) {
  std::error_code ec;
  uintmax_t size= fs::file_size (path, ec);
  if (ec) return "missing";
  fs::file_time_type modified= fs::last_write_time (path, ec);
  if (ec) return "missing";
  return to_tm (std::to_string (modified.time_since_epoch ().count ()) + ":" +
                std::to_string (size));
}

void
prune_source_cache () {
  constexpr size_t limit= 128;
  while (source_cache.size () > limit) {
    auto oldest= source_cache.begin ();
    for (auto it= source_cache.begin (); it != source_cache.end (); ++it)
      if (it->second.last_use < oldest->second.last_use) oldest= it;
    source_cache.erase (oldest);
  }
}

void
prune_resolved_cache () {
  constexpr size_t limit= 1024;
  while (resolved_cache.size () > limit) {
    auto oldest= resolved_cache.begin ();
    for (auto it= resolved_cache.begin (); it != resolved_cache.end (); ++it)
      if (it->second.last_use < oldest->second.last_use) oldest= it;
    resolved_cache.erase (oldest);
  }
}

void
prune_display_cache () {
  constexpr size_t limit= 1024;
  while (display_cache.size () > limit) {
    auto oldest= display_cache.begin ();
    for (auto it= display_cache.begin (); it != display_cache.end (); ++it)
      if (it->second.last_use < oldest->second.last_use) oldest= it;
    display_cache.erase (oldest);
  }
}

void
index_anchors (tree t, TreePath& path,
               std::map<std::string,TreePath>& anchors) {
  if (is_atomic (t)) return;
  if (is_func (t, LABEL) && N(t) >= 1 && is_atomic (t[0])) {
    std::string label= to_std (tree_text (t[0]));
    if (anchors.find (label) == anchors.end ()) anchors[label]= path;
  }
  for (int i=0; i<N(t); ++i) {
    path.push_back (i);
    index_anchors (t[i], path, anchors);
    path.pop_back ();
  }
}

SourceTreeCacheEntry*
cached_source (url source_url, string signature) {
  std::string path= to_std (concretize (source_url));
  auto found= source_cache.find (path);
  if (found != source_cache.end () && found->second.signature == signature) {
    found->second.last_use= ++cache_clock;
    return &found->second;
  }

  string serialized;
  if (load_string (source_url, serialized, false)) return nullptr;
  tree document= texmacs_document_to_tree (serialized);
  if (is_func (document, _ERROR)) return nullptr;

  SourceTreeCacheEntry entry;
  entry.signature= signature;
  entry.document= document;
  entry.body= extract (document, "body");
  if (!is_func (entry.body, DOCUMENT)) entry.body= tree (DOCUMENT, entry.body);
  TreePath path_buffer;
  index_anchors (entry.document, path_buffer, entry.anchors);
  entry.last_use= ++cache_clock;
  source_cache[path]= entry;
  prune_source_cache ();
  return &source_cache[path];
}

tree
subtree_at (tree t, const TreePath& path, size_t start= 0) {
  for (size_t i=start; i<path.size (); ++i) {
    int child= path[i];
    if (is_atomic (t) || child < 0 || child >= N(t)) return UNINIT;
    t= t[child];
  }
  return t;
}

size_t
common_prefix_length (const TreePath& left, const TreePath& right) {
  size_t n= std::min (left.size (), right.size ());
  size_t i= 0;
  while (i < n && left[i] == right[i]) ++i;
  return i;
}

bool
heading_anchor_id (const std::string& value) {
  return value.size () >= 3 && value[0] == 'H' && value[1] >= '1' &&
         value[1] <= '6' && value[2] == ' ';
}

bool
heading_node (tree t) {
  if (is_atomic (t)) return false;
  static const char* labels[]= {
    "section", "section*", "subsection", "subsection*",
    "subsubsection", "subsubsection*", "paragraph", "paragraph*",
    "subparagraph", "subparagraph*"
  };
  for (const char* label: labels)
    if (is_compound (t, label) && N(t) > 0 && tree_text (t[0]) != "")
      return true;
  return false;
}

tree
extract_range (const SourceTreeCacheEntry& source, const std::string& begin,
               const std::string& end) {
  if (begin.empty () && end.empty ()) return copy (source.body);

  auto begin_it= source.anchors.find (begin);
  auto end_it= end.empty () ? source.anchors.end () : source.anchors.find (end);
  if (begin_it == source.anchors.end () ||
      (!end.empty () && end_it == source.anchors.end ())) return UNINIT;

  const TreePath& begin_path= begin_it->second;
  if (!end.empty () && begin == end && heading_anchor_id (begin) &&
      !begin_path.empty ()) {
    TreePath parent_path (begin_path.begin (), begin_path.end () - 1);
    tree parent= subtree_at (source.document, parent_path);
    for (int i= begin_path.back () + 1; i<N(parent); ++i) {
      tree child= parent[i];
      if (is_atomic (child) && trim_spaces (tree_text (child)) == "") continue;
      if (heading_node (child)) return tree (DOCUMENT, copy (child));
      break;
    }
  }

  size_t prefix_length;
  TreePath parent_path;
  int first;
  int last;
  if (end.empty ()) {
    if (begin_path.empty ()) return UNINIT;
    parent_path.assign (begin_path.begin (), begin_path.end () - 1);
    first= begin_path.back ();
    tree parent= subtree_at (source.document, parent_path);
    last= N(parent) - 1;
  }
  else {
    const TreePath& end_path= end_it->second;
    prefix_length= common_prefix_length (begin_path, end_path);
    if (prefix_length >= begin_path.size () || prefix_length >= end_path.size ())
      return UNINIT;
    parent_path.assign (begin_path.begin (), begin_path.begin () +
                        (ptrdiff_t) prefix_length);
    first= begin_path[prefix_length];
    last= end_path[prefix_length];
  }

  tree parent= subtree_at (source.document, parent_path);
  if (parent == UNINIT || is_atomic (parent) || first < 0 || last < first ||
      last >= N(parent)) return UNINIT;
  tree result (DOCUMENT);
  for (int i=first; i<=last; ++i) result << copy (parent[i]);
  return result;
}

bool
absolute_asset_path (const string& path) {
  return path == "" || starts (path, "/") || starts (path, "~") ||
         starts (path, "$") || occurs ("://", path);
}

string
rebase_asset_path (const string& path, url source_dir) {
  if (absolute_asset_path (path)) return path;
  url absolute= source_dir * url_unix (cork_to_utf8 (path));
  return utf8_to_cork (as_system_string (absolute));
}

tree
strip_labels_and_rebase_images (tree t, url source_dir) {
  if (is_atomic (t)) return copy (t);
  if (is_func (t, LABEL)) return tree (CONCAT);
  tree result (L(t));
  for (int i=0; i<N(t); ++i) {
    if (i == 0 && is_func (t, IMAGE) && is_atomic (t[i]))
      result << tree (rebase_asset_path (t[i]->label, source_dir));
    else result << strip_labels_and_rebase_images (t[i], source_dir);
  }
  return result;
}

tree
repair_error (tree t, string message) {
  string uuid= N(t) > 0 ? tree_text (t[0]) : "";
  string file= N(t) > 1 ? tree_text (t[1]) : "";
  string begin= N(t) > 2 ? tree_text (t[2]) : "";
  string end= N(t) > 3 ? tree_text (t[3]) : "";
  string command= "(vault-transclude-repair " * scm_quote (uuid) * " " *
                  scm_quote (file) * " " * scm_quote (begin) * " " *
                  scm_quote (end) * ")";
  tree body (CONCAT);
  body << compound ("bold", "Broken Transclusion: ")
       << tree (message * " ")
       << tree (ACTION, "Repair", command);
  return tree (WITH, "color", "red", body);
}

tree
display_tree (tree transclusion, const AthenaTransclusionResolution& resolved) {
  if (!resolved.ok) return resolved.content;
  url absolute_source=
    vault_get_root () * url_unix (resolved.source_relative_path);
  string filename= as_string (tail (absolute_source));
  string begin= tree_text (transclusion[2]);
  string source_url= as_string (absolute_source);
  string command= "(vault-jump-to-source " * scm_quote (source_url) * " " *
                  scm_quote (begin) * ")";

  tree source_line (CONCAT);
  source_line << tree (ACTION, "[Source: " * filename * "]", command);
  tree document (DOCUMENT);
  document << tree (WITH, "font-size", "0.8", "color", "blue", source_line);
  if (is_func (resolved.content, DOCUMENT)) document << A(resolved.content);
  else document << resolved.content;

  tree compact= tree (WITH, "par-par-sep", "0fn", "par-sep", "0fn",
                      document);
  tree ornamented= compound ("ornamented", compact);
  tree styled (WITH);
  styled << "ornament-color"
         << get_preference ("vault transclusion color", "#f8f8f8")
         << "ornament-shape" << "rectangular"
         << "ornament-border" << "1ln"
         << "ornament-vpadding" << "0.25spc"
         << "padding-above" << "0.15fn"
         << "padding-below" << "0.15fn"
         << "large-padding-above" << "0.2fn"
         << "large-padding-below" << "0.2fn"
         << ornamented;
  return styled;
}

} // namespace

void
athena_clear_transclusion_caches () {
  source_cache.clear ();
  resolved_cache.clear ();
  display_cache.clear ();
  cached_vault_root.clear ();
}

AthenaTransclusionResolution
athena_resolve_transclusion_content (tree transclusion) {
  AthenaTransclusionResolution result;
  if (N(transclusion) != 4) {
    result.content= repair_error (transclusion, "Malformed transclusion.");
    return result;
  }
  if (!vault_active ()) {
    result.content= repair_error (transclusion, "No Vault is open.");
    return result;
  }

  std::string root= to_std (concretize (vault_get_root ()));
  if (root != cached_vault_root) {
    athena_clear_transclusion_caches ();
    cached_vault_root= root;
  }

  string uuid= tree_text (transclusion[0]);
  tree node= vault_get_node (uuid);
  if (!is_func (node, TUPLE) || N(node) < 3) {
    result.content= repair_error (transclusion, "UUID not in database.");
    return result;
  }

  string relative_path= tree_text (node[0]);
  string begin= tree_text (node[1]);
  string end= tree_text (node[2]);
  url source_url= vault_get_root () * url_unix (relative_path);
  fs::path source_path (to_std (concretize (source_url)));
  string signature= source_signature (source_path);
  if (signature == "missing") {
    result.content= repair_error (transclusion, "Target file missing.");
    return result;
  }

  result.cache_key= to_tm (root) * "\x1f" * uuid * "\x1f" * relative_path *
                    "\x1f" * begin * "\x1f" * end * "\x1f" * signature;
  result.source_relative_path= relative_path;
  std::string key= to_std (result.cache_key);
  auto cached= resolved_cache.find (key);
  if (cached != resolved_cache.end ()) {
    cached->second.last_use= ++cache_clock;
    result.content= cached->second.content;
    result.ok= true;
    return result;
  }

  SourceTreeCacheEntry* source= cached_source (source_url, signature);
  if (source == nullptr) {
    result.content= repair_error (transclusion, "Target file could not be read.");
    return result;
  }
  tree extracted= extract_range (*source, to_std (begin), to_std (end));
  if (extracted == UNINIT) {
    result.content= repair_error (transclusion, "Anchors not found in target.");
    return result;
  }
  tree transformed= strip_labels_and_rebase_images (extracted, head (source_url));
  resolved_cache[key]= {transformed, ++cache_clock};
  prune_resolved_cache ();
  result.content= transformed;
  result.ok= true;
  return result;
}

tree
athena_resolve_transclusion_display (tree transclusion, string* cache_key) {
  AthenaTransclusionResolution result=
    athena_resolve_transclusion_content (transclusion);
  if (!result.ok) {
    if (cache_key != nullptr) *cache_key= result.cache_key;
    return result.content;
  }
  string color= get_preference ("vault transclusion color", "#f8f8f8");
  string display_key= result.cache_key * "\x1f" * color;
  if (cache_key != nullptr) *cache_key= display_key;
  std::string key= to_std (display_key);
  auto cached= display_cache.find (key);
  if (cached != display_cache.end ()) {
    cached->second.last_use= ++cache_clock;
    return cached->second.content;
  }
  tree display= display_tree (transclusion, result);
  display_cache[key]= {display, ++cache_clock};
  prune_display_cache ();
  return display;
}

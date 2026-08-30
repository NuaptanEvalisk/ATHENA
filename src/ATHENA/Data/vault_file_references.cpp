/******************************************************************************
* MODULE     : vault_file_references.cpp
* DESCRIPTION: Structural local-file references in ATHENA documents
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "ATHENA/Data/vault_file_references.hpp"

#include <system_error>

namespace fs = std::filesystem;

namespace {

std::string
to_std (string s) {
  return std::string (as_charp (s), (size_t) N(s));
}

string
to_tm (const std::string& s) {
  return string (s.data (), (int) s.size ());
}

fs::path
replace_prefix (const fs::path& path, const fs::path& old_prefix,
                const fs::path& new_prefix) {
  if (!athena_vault_path_at_or_below (path, old_prefix)) return path;
  fs::path normalized_path= path.lexically_normal ();
  fs::path normalized_old= old_prefix.lexically_normal ();
  fs::path suffix;
  auto pi= normalized_path.begin ();
  for (auto ri= normalized_old.begin (); ri != normalized_old.end (); ++ri)
    ++pi;
  for (; pi != normalized_path.end (); ++pi) suffix /= *pi;
  return (new_prefix / suffix).lexically_normal ();
}

bool
rewrite_value (const std::string& value, const fs::path& source_after,
               const fs::path& target_after, std::string& result) {
  if (!athena_vault_is_local_file_reference (value)) return false;
  bool file_url= value.compare (0, 7, "file://") == 0;
  std::string path_text= file_url ? value.substr (7) : value;
  bool relative= fs::path (path_text).is_relative ();
  fs::path output;
  if (relative) {
    std::error_code ec;
    output= fs::relative (target_after, source_after.parent_path (), ec);
    if (ec) output= target_after.lexically_relative (source_after.parent_path ());
    if (output.empty ()) output= ".";
  }
  else output= target_after;
  std::string next= output.generic_string ();
  if (relative && path_text.rfind ("./", 0) == 0 && next.rfind ("./", 0) != 0)
    next= "./" + next;
  result= file_url ? "file://" + next : next;
  return result != value;
}

void
collect_references (tree t, const fs::path& source,
                    std::vector<AthenaVaultFileReference>& references) {
  if (is_atomic (t)) return;
  int path_index= -1;
  bool has_path= athena_vault_file_reference_argument (t, path_index);
  for (int i=0; i<N(t); ++i) {
    if (has_path && i == path_index && is_atomic (t[i])) {
      std::string value= to_std (tree_as_string (t[i]));
      if (athena_vault_is_local_file_reference (value))
        references.push_back (
          {value, athena_vault_resolve_file_reference (source, value)});
    }
    collect_references (t[i], source, references);
  }
}

void
collect_image_references (tree t, const fs::path& source,
                          std::vector<AthenaVaultFileReference>& references) {
  if (is_atomic (t)) return;
  if ((is_func (t, IMAGE) || is_compound (t, "image")) && N(t) >= 1 &&
      is_atomic (t[0])) {
    std::string value= to_std (tree_as_string (t[0]));
    if (athena_vault_is_local_file_reference (value))
      references.push_back (
        {value, athena_vault_resolve_file_reference (source, value)});
  }
  for (int i=0; i<N(t); ++i)
    collect_image_references (t[i], source, references);
}

tree
rewrite_map (tree t, const fs::path& source_before,
             const fs::path& source_after,
             const std::unordered_map<std::string, fs::path>& renames,
             size_t& replacements) {
  if (is_atomic (t)) return copy (t);
  tree result (L(t));
  int path_index= -1;
  bool has_path= athena_vault_file_reference_argument (t, path_index);
  for (int i=0; i<N(t); ++i) {
    if (has_path && i == path_index && is_atomic (t[i])) {
      std::string old_value= to_std (tree_as_string (t[i]));
      fs::path resolved=
        athena_vault_resolve_file_reference (source_before, old_value);
      auto hit= renames.find (resolved.generic_string ());
      std::string new_value;
      if (hit != renames.end () &&
          rewrite_value (old_value, source_after, hit->second, new_value)) {
        result << tree (to_tm (new_value));
        ++replacements;
        continue;
      }
    }
    result << rewrite_map (t[i], source_before, source_after, renames,
                           replacements);
  }
  return result;
}

} // namespace

bool
athena_vault_file_reference_argument (tree t, int& index) {
  if ((is_func (t, IMAGE) || is_compound (t, "image")) && N(t) >= 1)
    { index= 0; return true; }
  if ((is_func (t, HLINK) || is_compound (t, "hlink")) && N(t) >= 2)
    { index= 1; return true; }
  if (is_compound (t, "cardlink") && N(t) >= 2)
    { index= 1; return true; }
  if ((is_func (t, INCLUDE) || is_compound (t, "include")) && N(t) >= 1)
    { index= 0; return true; }
  if ((is_compound (t, "sound") || is_compound (t, "video") ||
       is_compound (t, "animation")) && N(t) >= 1)
    { index= 0; return true; }
  return false;
}

bool
athena_vault_is_local_file_reference (const std::string& value) {
  if (value.empty () || value[0] == '#' || value[0] == '$') return false;
  size_t scheme= value.find ("://");
  if (scheme != std::string::npos && value.compare (0, 7, "file://") != 0)
    return false;
  return value.compare (0, 7, "tmfs://") != 0 &&
         value.compare (0, 7, "http://") != 0 &&
         value.compare (0, 8, "https://") != 0 &&
         value.compare (0, 7, "mailto:") != 0;
}

fs::path
athena_vault_normalized_path (const fs::path& path) {
  std::error_code ec;
  fs::path absolute= fs::absolute (path, ec).lexically_normal ();
  fs::path canonical= fs::weakly_canonical (absolute, ec);
  return ec ? absolute : canonical;
}

bool
athena_vault_path_at_or_below (const fs::path& path, const fs::path& parent) {
  fs::path p= path.lexically_normal ();
  fs::path root= parent.lexically_normal ();
  auto pi= p.begin ();
  auto ri= root.begin ();
  for (; ri != root.end (); ++ri, ++pi)
    if (pi == p.end () || *pi != *ri) return false;
  return true;
}

fs::path
athena_vault_resolve_file_reference (const fs::path& source,
                                     const std::string& value) {
  if (!athena_vault_is_local_file_reference (value)) return fs::path ();
  std::string path_text=
    value.compare (0, 7, "file://") == 0 ? value.substr (7) : value;
  fs::path written (path_text);
  fs::path resolved= written.is_relative () ? source.parent_path () / written :
                                              written;
  return athena_vault_normalized_path (resolved);
}

void
athena_vault_collect_file_references (
  tree document, const fs::path& source,
  std::vector<AthenaVaultFileReference>& references) {
  collect_references (document, athena_vault_normalized_path (source), references);
}

void
athena_vault_collect_image_file_references (
  tree document, const fs::path& source,
  std::vector<AthenaVaultFileReference>& references) {
  collect_image_references (
    document, athena_vault_normalized_path (source), references);
}

tree
athena_vault_rewrite_file_reference_map (
  tree document, const fs::path& source_before, const fs::path& source_after,
  const std::unordered_map<std::string, fs::path>& renames,
  size_t& replacements) {
  replacements= 0;
  return rewrite_map (document, athena_vault_normalized_path (source_before),
                      athena_vault_normalized_path (source_after), renames,
                      replacements);
}

tree
athena_vault_rewrite_file_references (
  tree document, const fs::path& source_before, const fs::path& source_after,
  const fs::path& renamed_before, const fs::path& renamed_after,
  size_t& replacements) {
  fs::path old_path= athena_vault_normalized_path (renamed_before);
  fs::path new_path= athena_vault_normalized_path (renamed_after);
  std::unordered_map<std::string, fs::path> renames;
  std::vector<AthenaVaultFileReference> references;
  athena_vault_collect_file_references (document, source_before, references);
  for (const AthenaVaultFileReference& reference: references)
    if (athena_vault_path_at_or_below (reference.resolved_path, old_path))
      renames[reference.resolved_path.generic_string ()]=
        replace_prefix (reference.resolved_path, old_path, new_path);
  return athena_vault_rewrite_file_reference_map (
    document, source_before, source_after, renames, replacements);
}

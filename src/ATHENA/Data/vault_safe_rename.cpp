/******************************************************************************
* MODULE     : vault_safe_rename.cpp
* DESCRIPTION: Transactional, reference-aware Vault renaming
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "ATHENA/Data/vault_safe_rename.hpp"

#include "ATHENA/Data/new_buffer.hpp"
#include "ATHENA/Data/vault.hpp"
#include "ATHENA/Data/vault_file_references.hpp"
#include "ATHENA/Data/vault_map_sqlite.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"
#include "analyze.hpp"
#include "file.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <system_error>
#include <unordered_set>

namespace fs = std::filesystem;

namespace {

struct DocumentRewrite {
  fs::path before;
  fs::path after;
  fs::path stage_before;
  fs::path backup;
  tree rewritten;
  size_t replacements= 0;
};

std::string tm_std (string s) {
  return std::string (as_charp (s), (size_t) N(s));
}

string std_tm (const std::string& s) {
  return string (s.data (), (int) s.size ());
}

fs::path normalized_absolute (const fs::path& path) {
  std::error_code ec;
  fs::path absolute= fs::absolute (path, ec).lexically_normal ();
  fs::path canonical= fs::weakly_canonical (absolute, ec);
  return ec ? absolute : canonical;
}

bool path_at_or_below (const fs::path& path, const fs::path& parent) {
  fs::path p= path.lexically_normal ();
  fs::path root= parent.lexically_normal ();
  auto pi= p.begin ();
  auto ri= root.begin ();
  for (; ri != root.end (); ++ri, ++pi)
    if (pi == p.end () || *pi != *ri) return false;
  return true;
}

fs::path replace_prefix (const fs::path& path, const fs::path& old_prefix,
                         const fs::path& new_prefix) {
  if (!path_at_or_below (path, old_prefix)) return path;
  fs::path normalized_path= path.lexically_normal ();
  fs::path normalized_old= old_prefix.lexically_normal ();
  fs::path suffix;
  auto pi= normalized_path.begin ();
  for (auto ri= normalized_old.begin (); ri != normalized_old.end (); ++ri)
    ++pi;
  for (; pi != normalized_path.end (); ++pi) suffix /= *pi;
  return (new_prefix / suffix).lexically_normal ();
}

bool document_extension (const fs::path& path) {
  std::string extension= path.extension ().string ();
  std::transform (extension.begin (), extension.end (), extension.begin (),
                  [] (unsigned char c) { return (char) std::tolower (c); });
  return extension == ".ath" || extension == ".tm";
}

bool ignored_scan_path (const fs::path& relative) {
  if (relative.empty ()) return false;
  std::string first= relative.begin ()->string ();
  return first == ".backup" || first == ".athena" || first == ".git";
}

bool raw_file_contains (const fs::path& path,
                        const std::vector<std::string>& needles,
                        bool& readable) {
  std::ifstream input (path, std::ios::binary);
  readable= (bool) input;
  if (!readable) return false;
  std::ostringstream buffer;
  buffer << input.rdbuf ();
  std::string contents= buffer.str ();
  for (const std::string& needle: needles)
    if (!needle.empty () && contents.find (needle) != std::string::npos)
      return true;
  return false;
}

std::string operation_id () {
  auto now= std::chrono::high_resolution_clock::now ().time_since_epoch ();
  return std::to_string (
    std::chrono::duration_cast<std::chrono::nanoseconds> (now).count ());
}

bool rename_replace (const fs::path& stage, const fs::path& destination,
                     const fs::path& backup, std::string& error) {
  std::error_code ec;
  fs::create_directories (backup.parent_path (), ec);
  if (ec) { error= "Could not create safe rename backup: " + ec.message (); return false; }
  fs::copy_file (destination, backup, fs::copy_options::overwrite_existing, ec);
  if (ec) { error= "Could not back up " + destination.string () + ": " + ec.message (); return false; }
  fs::rename (stage, destination, ec);
  if (!ec) return true;
  fs::remove (destination, ec);
  ec.clear ();
  fs::rename (stage, destination, ec);
  if (ec) {
    std::string install_error= ec.message ();
    std::error_code restore_error;
    fs::copy_file (backup, destination, fs::copy_options::overwrite_existing,
                   restore_error);
    error= "Could not install rewritten document: " + install_error;
    if (restore_error)
      error += "; additionally could not restore its backup: " +
               restore_error.message ();
    return false;
  }
  return true;
}

} // namespace

struct VaultSafeRenamePlan::Impl {
  fs::path root;
  std::string map_relative_path;
  std::vector<DocumentRewrite> rewrites;
  std::vector<fs::path> open_buffers;
};

tree
vault_safe_rename_rewrite_tree (tree document, const fs::path& source_before,
                                const fs::path& source_after,
                                const fs::path& renamed_before,
                                const fs::path& renamed_after,
                                size_t& replacements) {
  replacements= 0;
  return athena_vault_rewrite_file_references (
    document, source_before, source_after, renamed_before, renamed_after,
    replacements);
}

bool
vault_safe_rename_plan (const fs::path& source_arg, const fs::path& target_arg,
                        VaultSafeRenamePlan& plan, std::string& error) {
  plan= VaultSafeRenamePlan ();
  if (!vault_active ()) { error= "No active Vault."; return false; }
  fs::path root= normalized_absolute (fs::path (tm_std (concretize (vault_get_root ()))));
  std::error_code status_error;
  fs::file_status source_status= fs::symlink_status (source_arg, status_error);
  if (status_error) {
    error= "Could not inspect the rename source: " + status_error.message ();
    return false;
  }
  if (fs::is_symlink (source_status)) {
    error= "Safe rename does not operate on symbolic links.";
    return false;
  }
  fs::path source= normalized_absolute (source_arg);
  fs::path target_absolute= fs::absolute (target_arg).lexically_normal ();
  fs::path target= normalized_absolute (target_absolute.parent_path ()) /
                   target_absolute.filename ();
  if (!path_at_or_below (source, root) || !path_at_or_below (target, root) ||
      source == root) {
    error= "Safe rename paths must remain inside the active Vault.";
    return false;
  }
  if (source.parent_path () != target.parent_path ()) {
    error= "Safe rename changes a name in place; it does not move items "
           "between directories.";
    return false;
  }
  if (!fs::exists (source)) { error= "The source no longer exists."; return false; }
  if (fs::exists (target)) { error= "The destination already exists."; return false; }

  AthenaVaultfileInfo info;
  if (!athena_vaultfile_read (root, info, error)) return false;
  std::vector<fs::path> protected_paths= {
    root / "Vaultfile.json", root / info.map_path,
    root / info.namespace_db_path, root / info.rag_index_path,
    root / ".backup", root / ".athena"};
  for (const fs::path& protected_path: protected_paths)
    if (source == normalized_absolute (protected_path) ||
        path_at_or_below (normalized_absolute (protected_path), source) ||
        path_at_or_below (source, normalized_absolute (protected_path))) {
      error= "Vault infrastructure cannot be renamed.";
      return false;
    }

  plan.source= source;
  plan.target= target;
  plan.old_relative_path= source.lexically_relative (root).generic_string ();
  plan.new_relative_path= target.lexically_relative (root).generic_string ();
  plan.is_directory= fs::is_directory (source);
  plan.impl= std::make_shared<VaultSafeRenamePlan::Impl> ();
  plan.impl->root= root;
  plan.impl->map_relative_path= info.map_path;

  if (plan.is_directory) {
    plan.filesystem_entries= 1;
    std::error_code ec;
    for (fs::recursive_directory_iterator it (source, ec), end;
         !ec && it != end; it.increment (ec)) ++plan.filesystem_entries;
    if (ec) {
      error= "Could not inspect the source directory: " + ec.message ();
      return false;
    }
  }
  else plan.filesystem_entries= 1;

  AthenaVaultMapSqlite map;
  if (!map.open (root / info.map_path, true, error) ||
      !map.count_path_rename (plan.old_relative_path, plan.is_directory,
                              plan.map_rows, error))
    return false;

  bool scan_references= plan.is_directory || !document_extension (source);
  if (scan_references) {
    std::vector<std::string> needles= {
      source.filename ().string (), plan.old_relative_path,
      source.generic_string ()};
    std::error_code ec;
    for (fs::recursive_directory_iterator it (root, ec), end;
         !ec && it != end; it.increment (ec)) {
      if (it->is_directory (ec)) {
        fs::path rel= it->path ().lexically_relative (root);
        if (ignored_scan_path (rel)) it.disable_recursion_pending ();
        continue;
      }
      if (!it->is_regular_file (ec) || !document_extension (it->path ())) continue;
      bool readable= false;
      if (!raw_file_contains (it->path (), needles, readable)) {
        if (!readable) {
          error= "Could not read Vault document while scanning references: " +
                 it->path ().string ();
          return false;
        }
        continue;
      }
      ++plan.candidate_documents;
      tree document= import_tree (url_system (std_tm (it->path ().string ())),
                                  "texmacs");
      if (document == "error") {
        error= "Could not parse candidate Vault document: " +
               it->path ().string ();
        return false;
      }
      fs::path after= plan.is_directory ?
        replace_prefix (it->path (), source, target) : it->path ();
      size_t replacements= 0;
      tree rewritten= vault_safe_rename_rewrite_tree (
        document, it->path (), after, source, target, replacements);
      if (replacements == 0) continue;
      DocumentRewrite item;
      item.before= it->path ();
      item.after= after;
      item.rewritten= rewritten;
      item.replacements= replacements;
      plan.impl->rewrites.push_back (std::move (item));
      ++plan.rewritten_documents;
      plan.rewritten_references += replacements;
    }
    if (ec) { error= "Could not scan Vault documents: " + ec.message (); return false; }
  }

  std::unordered_set<std::string> rewrite_paths_set;
  for (const DocumentRewrite& rewrite: plan.impl->rewrites)
    rewrite_paths_set.insert (rewrite.before.string ());
  array<url> buffers= get_all_buffers ();
  for (int i=0; i<N(buffers); ++i) {
    if (is_rooted_tmfs (buffers[i]) || is_rooted_web (buffers[i])) continue;
    fs::path path= normalized_absolute (fs::path (tm_std (concretize (buffers[i]))));
    bool affected= path == source || (plan.is_directory && path_at_or_below (path, source));
    bool rewritten= rewrite_paths_set.find (path.string ()) != rewrite_paths_set.end ();
    if (!affected && !rewritten) continue;
    plan.impl->open_buffers.push_back (path);
    ++plan.affected_open_buffers;
    if (buffer_modified (buffers[i])) plan.modified_buffers.push_back (path.string ());
  }
  return true;
}

bool
vault_safe_rename_execute (VaultSafeRenamePlan& plan, std::string& error) {
  if (plan.impl == nullptr) { error= "Invalid safe rename plan."; return false; }
  if (!plan.modified_buffers.empty ()) {
    error= "Save the modified affected documents before renaming.";
    return false;
  }
  if (!fs::exists (plan.source) || fs::exists (plan.target)) {
    error= "Vault contents changed after the rename was planned.";
    return false;
  }
  std::string id= operation_id ();
  fs::path backup_root= plan.impl->root / ".backup" / "safe-rename" / id;
  auto remove_stages= [&] () {
    for (const DocumentRewrite& rewrite: plan.impl->rewrites) {
      std::error_code ignored;
      fs::remove (rewrite.stage_before, ignored);
      if (plan.is_directory)
        fs::remove (replace_prefix (rewrite.stage_before, plan.source,
                                    plan.target), ignored);
    }
  };
  for (DocumentRewrite& rewrite: plan.impl->rewrites) {
    rewrite.stage_before= rewrite.before;
    rewrite.stage_before += ".athena-safe-rename-" + id + ".tmp";
    rewrite.backup= backup_root /
      rewrite.after.lexically_relative (plan.impl->root);
    if (export_tree (rewrite.rewritten,
                     url_system (std_tm (rewrite.stage_before.string ())),
                     "texmacs")) {
      error= "Could not stage rewritten document " + rewrite.before.string ();
      remove_stages ();
      return false;
    }
  }

  AthenaVaultMapSqlite map;
  if (!map.open (plan.impl->root / plan.impl->map_relative_path, true, error)) {
    remove_stages ();
    return false;
  }
  AthenaVaultMapRenameOperation operation;
  operation.operation_id= id;
  operation.old_path= plan.old_relative_path;
  operation.new_path= plan.new_relative_path;
  operation.is_directory= plan.is_directory;
  operation.phase= "prepared";
  if (!map.prepare_path_rename (operation, error)) {
    remove_stages ();
    return false;
  }

  std::error_code ec;
  fs::rename (plan.source, plan.target, ec);
  if (ec) {
    error= "Could not rename Vault item: " + ec.message ();
    std::string ignored;
    map.finish_path_rename (id, ignored);
    remove_stages ();
    return false;
  }

  std::vector<DocumentRewrite*> installed;
  for (DocumentRewrite& rewrite: plan.impl->rewrites) {
    fs::path stage= plan.is_directory ?
      replace_prefix (rewrite.stage_before, plan.source, plan.target) :
      rewrite.stage_before;
    if (!rename_replace (stage, rewrite.after, rewrite.backup, error)) {
      for (DocumentRewrite* done: installed) {
        std::error_code ignored;
        fs::copy_file (done->backup, done->after,
                       fs::copy_options::overwrite_existing, ignored);
      }
      fs::rename (plan.target, plan.source, ec);
      std::string ignored;
      map.finish_path_rename (id, ignored);
      remove_stages ();
      return false;
    }
    installed.push_back (&rewrite);
  }

  size_t changed= 0;
  if (!map.apply_path_rename (id, changed, error)) {
    for (DocumentRewrite* done: installed) {
      std::error_code ignored;
      fs::copy_file (done->backup, done->after,
                     fs::copy_options::overwrite_existing, ignored);
    }
    fs::rename (plan.target, plan.source, ec);
    std::string ignored;
    map.finish_path_rename (id, ignored);
    remove_stages ();
    return false;
  }

  for (const fs::path& old_buffer: plan.impl->open_buffers) {
    fs::path new_buffer= plan.is_directory ?
      replace_prefix (old_buffer, plan.source, plan.target) :
      (old_buffer == plan.source ? plan.target : old_buffer);
    url old_url= url_system (std_tm (old_buffer.string ()));
    url new_url= url_system (std_tm (new_buffer.string ()));
    if (old_buffer != new_buffer) rename_buffer (old_url, new_url);
    auto rewrite= std::find_if (
      plan.impl->rewrites.begin (), plan.impl->rewrites.end (),
      [&] (const DocumentRewrite& item) { return item.before == old_buffer; });
    if (rewrite != plan.impl->rewrites.end ())
      set_buffer_tree (new_url, rewrite->rewritten);
  }
  if (!map.finish_path_rename (id, error)) return false;
  return true;
}

bool
vault_safe_rename_recover (const fs::path& root,
                           const std::string& map_relative_path,
                           std::string& error) {
  AthenaVaultMapSqlite map;
  if (!map.open (root / map_relative_path, true, error)) return false;
  std::vector<AthenaVaultMapRenameOperation> operations;
  if (!map.pending_path_renames (operations, error)) return false;
  for (const auto& operation: operations) {
    fs::path old_path= root / operation.old_path;
    fs::path new_path= root / operation.new_path;
    bool old_exists= fs::exists (old_path);
    bool new_exists= fs::exists (new_path);
    if (old_exists && !new_exists) {
      std::string suffix= ".athena-safe-rename-" + operation.operation_id +
                          ".tmp";
      std::error_code ec;
      for (fs::recursive_directory_iterator it (root, ec), end;
           !ec && it != end; it.increment (ec)) {
        if (!it->is_regular_file (ec)) continue;
        std::string path= it->path ().string ();
        if (path.size () > suffix.size () &&
            path.compare (path.size () - suffix.size (), suffix.size (),
                          suffix) == 0)
          fs::remove (it->path (), ec);
      }
      if (!map.finish_path_rename (operation.operation_id, error)) return false;
      continue;
    }
    if (!old_exists && new_exists) {
      std::string suffix= ".athena-safe-rename-" + operation.operation_id + ".tmp";
      std::error_code ec;
      for (fs::recursive_directory_iterator it (root, ec), end;
           !ec && it != end; it.increment (ec)) {
        if (!it->is_regular_file (ec)) continue;
        std::string path= it->path ().string ();
        if (path.size () <= suffix.size () ||
            path.compare (path.size () - suffix.size (), suffix.size (), suffix) != 0)
          continue;
        fs::path destination (path.substr (0, path.size () - suffix.size ()));
        fs::path backup= root / ".backup" / "safe-rename" /
          operation.operation_id / destination.lexically_relative (root);
        if (!rename_replace (it->path (), destination, backup, error)) return false;
      }
      size_t changed= 0;
      if (!map.apply_path_rename (operation.operation_id, changed, error) ||
          !map.finish_path_rename (operation.operation_id, error))
        return false;
      continue;
    }
    error= "Safe rename recovery found an ambiguous filesystem state for " +
           operation.old_path + " -> " + operation.new_path;
    return false;
  }
  return true;
}

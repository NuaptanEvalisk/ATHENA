/******************************************************************************
* MODULE     : vault_maintenance_pass_images.cpp
* DESCRIPTION: Transactional normalization of referenced Vault assets
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "ATHENA/Data/vault_maintenance_internal.hpp"
#include "ATHENA/Data/vault_file_references.hpp"

#include "convert.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct ParsedDocument {
  fs::path path;
  tree document;
};

struct DocumentRewrite {
  fs::path path;
  fs::path stage;
  fs::path backup;
  std::string serialized;
  size_t replacements= 0;
};

bool
document_asset_extension (const fs::path& path) {
  static const std::unordered_set<std::string> extensions= {
    ".ath", ".tm", ".ts", ".tp", ".stm"};
  return extensions.find (lower_copy (path.extension ().string ())) !=
         extensions.end ();
}

bool
protected_asset_path (const fs::path& root, const fs::path& path,
                      const std::unordered_set<std::string>& infrastructure) {
  fs::path relative= path.lexically_relative (root);
  if (relative.empty ()) return true;
  std::string first= relative.begin ()->string ();
  if (first == ".backup" || first == ".athena" || first == ".git" ||
      is_orphan_dir_name (first))
    return true;
  return is_vault_infrastructure_path (path, infrastructure);
}

bool
parse_documents (const fs::path& root, std::vector<ParsedDocument>& documents,
                 std::vector<AthenaVaultFileReference>& references) {
  std::vector<fs::path> paths= scan_documents (root);
  documents.reserve (paths.size ());
  for (size_t i=0; i<paths.size (); ++i) {
    print_progress (i + 1, paths.size (), "Scanning asset references",
                    paths[i].filename ().string ());
    std::string text;
    if (!read_file_bytes (paths[i], text)) {
      finish_progress ();
      log_error ("failed to read document " + paths[i].string ());
      return false;
    }
    tree document;
    try { document= texmacs_document_to_tree (std_to_tm (text)); }
    catch (...) { document= tree (_ERROR, "parse failed"); }
    if (is_func (document, _ERROR)) {
      finish_progress ();
      log_error ("failed to parse document while collecting assets: " +
                 paths[i].string ());
      return false;
    }
    athena_vault_collect_file_references (document, paths[i], references);
    documents.push_back ({paths[i], document});
  }
  finish_progress ();
  return true;
}

bool
build_rename_plan (const fs::path& root,
                   const std::vector<AthenaVaultFileReference>& references,
                   std::vector<RenamePlan>& plans) {
  std::vector<fs::path> assets;
  std::unordered_set<std::string> seen;
  std::unordered_set<std::string> infrastructure;
  std::string infrastructure_error;
  if (!collect_vault_infrastructure_paths (
        root, infrastructure, infrastructure_error)) {
    log_error ("could not read Vaultfile infrastructure paths: " +
               infrastructure_error);
    return false;
  }
  fs::path normalized_root= athena_vault_normalized_path (root);
  auto add_asset= [&] (const fs::path& path) {
    std::error_code ec;
    fs::file_status status= fs::symlink_status (path, ec);
    if (ec || !fs::is_regular_file (status) || fs::is_symlink (status) ||
        !athena_vault_path_at_or_below (path, normalized_root) ||
        document_asset_extension (path) ||
        protected_asset_path (root, path, infrastructure) ||
        has_canonical_asset_name (path))
      return;
    if (seen.insert (path.generic_string ()).second) assets.push_back (path);
  };
  for (const AthenaVaultFileReference& reference: references)
    add_asset (reference.resolved_path);
  // Preserve ATHENA's established treatment of image/PDF files as managed
  // assets even before they acquire a structural reference. Other file types
  // become managed only when a document actually refers to them.
  for (const fs::path& legacy_asset: scan_asset_files (root))
    if (is_image_extension (legacy_asset)) add_asset (legacy_asset);
  std::sort (assets.begin (), assets.end ());

  std::unordered_set<std::string> reserved;
  for (const fs::path& old_path: assets) {
    fs::path target;
    do {
      target= old_path.parent_path () /
              ("asset-" + generate_uuid_v4 () + canonical_extension (old_path));
    } while (fs::exists (target) ||
             reserved.find (path_key (target)) != reserved.end ());
    plans.push_back ({old_path, target, old_path.filename ().string ()});
    reserved.insert (path_key (target));
  }
  return true;
}

bool
prepare_rewrites (const std::vector<ParsedDocument>& documents,
                  const std::vector<RenamePlan>& plans,
                  std::vector<DocumentRewrite>& rewrites,
                  size_t& replacements) {
  std::unordered_map<std::string, fs::path> rename_map;
  for (const RenamePlan& plan: plans)
    rename_map[plan.old_path.generic_string ()]= plan.new_path;
  replacements= 0;
  for (const ParsedDocument& parsed: documents) {
    size_t count= 0;
    tree rewritten= athena_vault_rewrite_file_reference_map (
      parsed.document, parsed.path, parsed.path, rename_map, count);
    if (count == 0) continue;
    string serialized_tm= tree_to_texmacs (rewritten);
    std::string serialized (as_charp (serialized_tm), (size_t) N(serialized_tm));
    tree validation;
    try { validation= texmacs_document_to_tree (serialized_tm); }
    catch (...) { validation= tree (_ERROR, "parse failed"); }
    if (is_func (validation, _ERROR)) {
      log_error ("refusing malformed document after asset reference rewrite: " +
                 parsed.path.string ());
      return false;
    }
    rewrites.push_back ({parsed.path, {}, {}, serialized, count});
    replacements += count;
  }
  return true;
}

void
remove_stages (const std::vector<DocumentRewrite>& rewrites) {
  for (const DocumentRewrite& rewrite: rewrites) {
    std::error_code ignored;
    fs::remove (rewrite.stage, ignored);
  }
}

void
rollback_assets (const std::vector<RenamePlan>& plans, size_t renamed) {
  while (renamed > 0) {
    --renamed;
    std::error_code ignored;
    fs::rename (plans[renamed].new_path, plans[renamed].old_path, ignored);
  }
}

bool
commit_transaction (const std::vector<RenamePlan>& plans,
                    std::vector<DocumentRewrite>& rewrites) {
  std::string id= generate_uuid_v4 ();
  for (DocumentRewrite& rewrite: rewrites) {
    rewrite.stage= rewrite.path;
    rewrite.stage += ".athena-asset-normalize-" + id + ".tmp";
    rewrite.backup= rewrite.path;
    rewrite.backup += ".athena-asset-normalize-" + id + ".bak";
    if (!write_file_bytes (rewrite.stage, rewrite.serialized)) {
      log_error ("failed to stage rewritten document " + rewrite.path.string ());
      remove_stages (rewrites);
      return false;
    }
  }

  size_t renamed= 0;
  for (; renamed<plans.size (); ++renamed) {
    print_progress (renamed + 1, plans.size (), "Renaming assets",
                    plans[renamed].old_name);
    std::error_code ec;
    fs::rename (plans[renamed].old_path, plans[renamed].new_path, ec);
    if (ec) {
      finish_progress ();
      log_error ("failed to rename asset " + plans[renamed].old_path.string () +
                 ": " + ec.message ());
      rollback_assets (plans, renamed);
      remove_stages (rewrites);
      return false;
    }
  }
  finish_progress ();

  size_t installed= 0;
  for (; installed<rewrites.size (); ++installed) {
    DocumentRewrite& rewrite= rewrites[installed];
    std::error_code ec;
    fs::rename (rewrite.path, rewrite.backup, ec);
    if (!ec) fs::rename (rewrite.stage, rewrite.path, ec);
    if (!ec) continue;

    if (fs::exists (rewrite.backup)) {
      std::error_code ignored;
      fs::remove (rewrite.path, ignored);
      fs::rename (rewrite.backup, rewrite.path, ignored);
    }
    while (installed > 0) {
      --installed;
      DocumentRewrite& done= rewrites[installed];
      std::error_code ignored;
      fs::remove (done.path, ignored);
      fs::rename (done.backup, done.path, ignored);
    }
    rollback_assets (plans, renamed);
    remove_stages (rewrites);
    log_error ("failed to install rewritten document " +
               rewrite.path.string () + ": " + ec.message ());
    return false;
  }

  for (const DocumentRewrite& rewrite: rewrites) {
    std::error_code ignored;
    fs::remove (rewrite.backup, ignored);
    log_info ("updated " + std::to_string (rewrite.replacements) +
              " asset reference(s) in " + rewrite.path.string ());
  }
  return true;
}

} // namespace

VaultMaintenancePassResult
vault_maintenance_pass_normalize_assets (VaultMaintenanceContext& ctx) {
  std::vector<ParsedDocument> documents;
  std::vector<AthenaVaultFileReference> references;
  if (!parse_documents (ctx.root, documents, references))
    return VaultMaintenancePassResult::failure ("asset reference scan failed");

  std::vector<RenamePlan> plans;
  if (!build_rename_plan (ctx.root, references, plans))
    return VaultMaintenancePassResult::failure (
      "could not determine protected Vault infrastructure");
  log_info ("planned " + std::to_string (plans.size ()) +
            " referenced asset rename(s)");
  if (plans.empty ()) return VaultMaintenancePassResult::success ();

  std::vector<DocumentRewrite> rewrites;
  size_t replacements= 0;
  if (!prepare_rewrites (documents, plans, rewrites, replacements))
    return VaultMaintenancePassResult::failure ("asset reference rewrite failed");
  if (!commit_transaction (plans, rewrites))
    return VaultMaintenancePassResult::failure ("asset transaction failed");

  ctx.summary.asset_renames= plans.size ();
  ctx.summary.asset_reference_updates= replacements;
  log_info ("renamed " + std::to_string (plans.size ()) +
            " asset(s) and updated " + std::to_string (replacements) +
            " reference(s)");
  return VaultMaintenancePassResult::success ();
}

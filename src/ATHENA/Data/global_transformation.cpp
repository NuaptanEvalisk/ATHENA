/******************************************************************************
* MODULE     : global_transformation.cpp
* DESCRIPTION: Transactional Scheme-driven transformations over a Vault
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "ATHENA/Data/global_transformation.hpp"

#include "ATHENA/Data/vault_maintenance_internal.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"
#include "convert.hpp"

#include <algorithm>
#include <filesystem>
#include <system_error>
#include <unordered_set>

namespace fs= std::filesystem;

namespace {

void
remove_stages (const std::vector<AthenaGlobalTransformationRewrite>& rewrites,
               const std::string& id) {
  for (const AthenaGlobalTransformationRewrite& rewrite: rewrites) {
    fs::path stage= rewrite.path;
    stage += ".athena-global-transform-" + id + ".tmp";
    std::error_code ignored;
    fs::remove (stage, ignored);
  }
}

bool
serialize_document (const tree& document, std::string& serialized) {
  string source= tree_to_texmacs (document);
  tree validation;
  try { validation= texmacs_document_to_tree (source); }
  catch (...) { validation= tree (_ERROR, "parse failed"); }
  if (is_func (validation, _ERROR)) return false;
  serialized.assign (as_charp (source), (size_t) N(source));
  return true;
}

bool
read_current (const fs::path& path, std::string& source,
              std::string& error) {
  if (!read_file_bytes (path, source)) {
    error= "Could not read " + path.string ();
    return false;
  }
  return true;
}

} // namespace

bool
athena_global_transformation_prepare (
    const fs::path& input_root,
    const AthenaGlobalTransformationCallback& transform,
    const AthenaGlobalTransformationProgress& progress,
    AthenaGlobalTransformationPlan& plan, std::string& error) {
  error.clear ();
  plan= AthenaGlobalTransformationPlan {};
  plan.root= normalize_root (input_root);
  if (plan.root.empty () || !fs::is_directory (plan.root)) {
    error= "The active Vault root is not a directory";
    return false;
  }

  std::unordered_set<std::string> infrastructure;
  if (!collect_vault_infrastructure_paths (plan.root, infrastructure, error))
    return false;
  AthenaVaultfileInfo info;
  if (fs::exists (plan.root / "Vaultfile.json") &&
      !athena_vaultfile_read (plan.root, info, error)) return false;
  if (!info.maintenance_summary_path.empty ())
    infrastructure.insert (
      path_key (plan.root / fs::u8path (info.maintenance_summary_path)));

  std::vector<fs::path> documents= scan_ath_documents (plan.root);
  documents.erase (
    std::remove_if (documents.begin (), documents.end (), [&] (const fs::path& p) {
      return is_vault_infrastructure_path (p, infrastructure);
    }),
    documents.end ());
  plan.scanned= documents.size ();
  plan.rewrites.reserve (documents.size ());

  for (size_t index= 0; index<documents.size (); ++index) {
    const fs::path& path= documents[index];
    fs::path relative= path.lexically_relative (plan.root);
    std::string relative_text= relative.generic_u8string ();
    if (progress && !progress (index, documents.size (), relative_text)) {
      error= "cancelled";
      return false;
    }

    std::error_code status_error;
    if (fs::is_symlink (fs::symlink_status (path, status_error))) {
      error= "Refusing to rewrite symbolic-link document: " + path.string ();
      return false;
    }
    std::string source;
    if (!read_current (path, source, error)) return false;
    tree document;
    try { document= texmacs_document_to_tree (std_to_tm (source)); }
    catch (...) { document= tree (_ERROR, "parse failed"); }
    if (is_func (document, _ERROR)) {
      error= "Could not parse " + relative_text;
      return false;
    }

    tree transformed;
    if (!transform (relative_text, document, transformed, error)) {
      if (error.empty ())
        error= "Transformation failed for " + relative_text;
      return false;
    }
    if (transformed == document) continue;

    AthenaGlobalTransformationRewrite rewrite;
    rewrite.path= path;
    rewrite.relative_path= relative;
    rewrite.original= std::move (source);
    rewrite.transformed= transformed;
    if (!serialize_document (transformed, rewrite.serialized)) {
      error= "Transformation returned an invalid document for " + relative_text;
      return false;
    }
    plan.rewrites.push_back (std::move (rewrite));
  }
  if (progress && !progress (documents.size (), documents.size (), "")) {
    error= "cancelled";
    return false;
  }
  return true;
}

bool
athena_global_transformation_commit (AthenaGlobalTransformationPlan& plan,
                                     std::string& error) {
  error.clear ();
  if (plan.rewrites.empty ()) return true;
  const std::string id= generate_uuid_v4 ();
  plan.backup_root=
    plan.root / ".backup" / "global-transformations" / id;

  for (const AthenaGlobalTransformationRewrite& rewrite: plan.rewrites) {
    std::string current;
    if (!read_current (rewrite.path, current, error)) return false;
    if (current != rewrite.original) {
      error= "Document changed while the transformation was being prepared: " +
             rewrite.relative_path.generic_u8string ();
      return false;
    }
  }

  for (const AthenaGlobalTransformationRewrite& rewrite: plan.rewrites) {
    fs::path backup= plan.backup_root / rewrite.relative_path;
    fs::path stage= rewrite.path;
    stage += ".athena-global-transform-" + id + ".tmp";
    std::error_code ec;
    fs::create_directories (backup.parent_path (), ec);
    if (!ec)
      fs::copy_file (rewrite.path, backup, fs::copy_options::none, ec);
    if (ec || !write_file_bytes (stage, rewrite.serialized)) {
      remove_stages (plan.rewrites, id);
      error= "Could not stage " + rewrite.relative_path.generic_u8string ();
      return false;
    }
    std::error_code permission_error;
    fs::permissions (stage, fs::status (rewrite.path, permission_error).permissions (),
                     permission_error);
  }

  for (const AthenaGlobalTransformationRewrite& rewrite: plan.rewrites) {
    std::string current;
    if (!read_current (rewrite.path, current, error) ||
        current != rewrite.original) {
      remove_stages (plan.rewrites, id);
      if (error.empty ())
        error= "Document changed while transformed files were being staged: " +
               rewrite.relative_path.generic_u8string ();
      return false;
    }
  }

  size_t installed= 0;
  for (; installed<plan.rewrites.size (); ++installed) {
    const AthenaGlobalTransformationRewrite& rewrite= plan.rewrites[installed];
    fs::path stage= rewrite.path;
    stage += ".athena-global-transform-" + id + ".tmp";
    fs::path rollback= rewrite.path;
    rollback += ".athena-global-transform-" + id + ".bak";
    std::error_code ec;
    fs::rename (rewrite.path, rollback, ec);
    bool original_moved= !ec;
    if (original_moved) fs::rename (stage, rewrite.path, ec);
    if (!ec) continue;

    bool rollback_ok= true;
    if (original_moved) {
      std::error_code rollback_error;
      fs::remove (rewrite.path, rollback_error);
      rollback_error.clear ();
      fs::rename (rollback, rewrite.path, rollback_error);
      if (rollback_error) rollback_ok= false;
    }
    while (installed > 0) {
      --installed;
      const AthenaGlobalTransformationRewrite& done= plan.rewrites[installed];
      fs::path done_rollback= done.path;
      done_rollback += ".athena-global-transform-" + id + ".bak";
      std::error_code rollback_error;
      fs::remove (done.path, rollback_error);
      rollback_error.clear ();
      fs::rename (done_rollback, done.path, rollback_error);
      if (rollback_error) rollback_ok= false;
    }
    remove_stages (plan.rewrites, id);
    error= rollback_ok
      ? "Could not install transformed documents; original files were restored"
      : "Could not install transformed documents and rollback was incomplete; "
        "original copies remain in " + plan.backup_root.string ();
    return false;
  }

  for (const AthenaGlobalTransformationRewrite& rewrite: plan.rewrites) {
    fs::path rollback= rewrite.path;
    rollback += ".athena-global-transform-" + id + ".bak";
    std::error_code ignored;
    fs::remove (rollback, ignored);
  }
  return true;
}

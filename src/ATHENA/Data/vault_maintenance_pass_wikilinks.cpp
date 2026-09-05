/******************************************************************************
* MODULE     : vault_maintenance_pass_wikilinks.cpp
* DESCRIPTION: Remove redundant block wikilinks after artifactization
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include "ATHENA/Data/vault_maintenance_internal.hpp"

#include "ATHENA/Data/artifacts.hpp"
#include "ATHENA/Data/redundant_wikilinks.hpp"
#include "ATHENA/Data/vault_map_sqlite.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"
#include "convert.hpp"

#include <filesystem>
#include <algorithm>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct DocumentRewrite {
  fs::path path;
  fs::path stage;
  fs::path backup;
  std::string serialized;
};

bool
in_configured_subtree (const fs::path& root, const fs::path& path,
                       const std::string& configured) {
  fs::path subtree= fs::path (configured).lexically_normal ();
  if (subtree.empty () || subtree.is_absolute ()) return false;
  for (const fs::path& part: subtree)
    if (part == "." || part == "..") return false;
  fs::path relative= path.lexically_relative (root);
  auto expected= subtree.begin ();
  auto actual= relative.begin ();
  for (; expected != subtree.end (); ++expected, ++actual)
    if (actual == relative.end () || *actual != *expected) return false;
  return true;
}

bool
read_document (const fs::path& path, tree& document) {
  std::string source;
  if (!read_file_bytes (path, source)) return false;
  try { document= texmacs_document_to_tree (std_to_tm (source)); }
  catch (...) { document= tree (_ERROR, "parse failed"); }
  return !is_func (document, _ERROR);
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

void
remove_stages (const std::vector<DocumentRewrite>& rewrites) {
  for (const DocumentRewrite& rewrite: rewrites) {
    std::error_code ignored;
    fs::remove (rewrite.stage, ignored);
  }
}

bool
commit_rewrites (std::vector<DocumentRewrite>& rewrites) {
  std::string id= generate_uuid_v4 ();
  for (DocumentRewrite& rewrite: rewrites) {
    rewrite.stage= rewrite.path;
    rewrite.stage += ".athena-dewikilink-" + id + ".tmp";
    rewrite.backup= rewrite.path;
    rewrite.backup += ".athena-dewikilink-" + id + ".bak";
    if (!write_file_bytes (rewrite.stage, rewrite.serialized)) {
      remove_stages (rewrites);
      return false;
    }
  }

  size_t installed= 0;
  for (; installed<rewrites.size (); ++installed) {
    DocumentRewrite& rewrite= rewrites[installed];
    std::error_code error;
    fs::rename (rewrite.path, rewrite.backup, error);
    if (!error) fs::rename (rewrite.stage, rewrite.path, error);
    if (!error) continue;
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
    remove_stages (rewrites);
    return false;
  }
  for (const DocumentRewrite& rewrite: rewrites) {
    std::error_code ignored;
    fs::remove (rewrite.backup, ignored);
  }
  return true;
}

} // namespace

VaultMaintenancePassResult
vault_maintenance_pass_remove_redundant_wikilinks (
    VaultMaintenanceContext& ctx) {
  if (!ctx.summary.redundant_block_wikilink_removal_enabled)
    return VaultMaintenancePassResult::success ("disabled");

  AthenaVaultfileInfo vaultfile;
  std::string error;
  if (!athena_vaultfile_read (ctx.root, vaultfile, error))
    return VaultMaintenancePassResult::failure (error);
  std::string map_relative;
  if (!athena_vault_map_prepare (
        vaultfile.map_path, map_relative, error))
    return VaultMaintenancePassResult::failure (error);
  AthenaVaultMapSqlite map;
  if (!map.open (ctx.root / map_relative, false, error))
    return VaultMaintenancePassResult::failure (error);
  std::vector<AthenaVaultMapNode> map_nodes;
  if (!map.read_all (map_nodes, error))
    return VaultMaintenancePassResult::failure (error);

  std::vector<AthenaArtifactRecord> artifacts;
  if (!athena_artifacts_query (ctx.root, artifacts, error))
    return VaultMaintenancePassResult::failure (error);

  std::vector<fs::path> paths= scan_ath_documents (ctx.root);
  if (!vaultfile.maintenance_summary_path.empty ())
    paths.erase (
      std::remove_if (
        paths.begin (), paths.end (), [&] (const fs::path& path) {
          return in_configured_subtree (
            ctx.root, path, vaultfile.maintenance_summary_path);
        }),
      paths.end ());
  std::vector<AthenaRedundantWikilinkDocument> documents;
  documents.reserve (paths.size ());
  for (size_t i=0; i<paths.size (); ++i) {
    print_progress (i + 1, paths.size (), "Scanning redundant wikilinks",
                    paths[i].filename ().string ());
    tree document;
    if (!read_document (paths[i], document)) {
      finish_progress ();
      return VaultMaintenancePassResult::failure (
        "could not parse " + compact_log_path (paths[i]));
    }
    documents.push_back (
      {paths[i].lexically_relative (ctx.root).generic_string (), document, 0});
  }
  finish_progress ();

  AthenaRedundantWikilinkStats stats;
  athena_remove_redundant_block_wikilinks (
    documents, map_nodes, artifacts, stats);

  std::vector<DocumentRewrite> rewrites;
  for (size_t i=0; i<documents.size (); ++i) {
    if (documents[i].removals == 0) continue;
    std::string serialized;
    if (!serialize_document (documents[i].document, serialized))
      return VaultMaintenancePassResult::failure (
        "refusing malformed rewrite for " + compact_log_path (paths[i]));
    rewrites.push_back ({paths[i], {}, {}, std::move (serialized)});
  }
  if (!commit_rewrites (rewrites))
    return VaultMaintenancePassResult::failure (
      "could not install redundant wikilink rewrites");

  ctx.summary.redundant_wikilink_files_scanned= stats.files_scanned;
  ctx.summary.redundant_block_wikilinks_scanned=
    stats.block_wikilinks_scanned;
  ctx.summary.redundant_wikilink_full_matches= stats.full_text_matches;
  ctx.summary.redundant_wikilinks_removed= stats.links_removed;
  ctx.summary.redundant_wikilink_files_changed= stats.files_changed;
  ctx.summary.redundant_wikilink_unverified_targets= stats.unverified_targets;
  log_info ("redundant wikilinks: removed " +
            std::to_string (stats.links_removed) + " block wikilink(s) in " +
            std::to_string (stats.files_changed) + " file(s); scanned " +
            std::to_string (stats.block_wikilinks_scanned) +
            " block wikilink(s), unverified targets " +
            std::to_string (stats.unverified_targets));
  return VaultMaintenancePassResult::success ();
}

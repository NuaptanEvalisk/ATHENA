/******************************************************************************
* MODULE     : vault_maintenance_pass_missing_images.cpp
* DESCRIPTION: Structural scan for missing local images in ATHENA documents
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "ATHENA/Data/vault_maintenance_internal.hpp"
#include "ATHENA/Data/vault_file_references.hpp"

#include "convert.hpp"

#include <filesystem>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace fs = std::filesystem;

VaultMaintenancePassResult
vault_maintenance_pass_scan_missing_images (VaultMaintenanceContext& ctx) {
  ctx.summary.missing_image_scan_enabled= true;
  ctx.summary.missing_image_files_scanned= 0;
  ctx.summary.local_image_references_scanned= 0;
  ctx.summary.missing_images.clear ();

  std::vector<fs::path> documents= scan_ath_documents (ctx.root);
  std::set<std::tuple<std::string, std::string, std::string>> missing;
  for (size_t i=0; i<documents.size (); ++i) {
    const fs::path& document_path= documents[i];
    print_progress (i + 1, documents.size (), "Scanning for missing images",
                    document_path.filename ().string ());
    std::string text;
    if (!read_file_bytes (document_path, text)) {
      finish_progress ();
      return VaultMaintenancePassResult::failure (
        "could not read " + compact_log_path (document_path));
    }

    tree document;
    try { document= texmacs_document_to_tree (std_to_tm (text)); }
    catch (...) { document= tree (_ERROR, "parse failed"); }
    if (is_func (document, _ERROR)) {
      finish_progress ();
      return VaultMaintenancePassResult::failure (
        "could not parse " + compact_log_path (document_path));
    }

    std::vector<AthenaVaultFileReference> references;
    athena_vault_collect_image_file_references (
      document, document_path, references);
    ctx.summary.missing_image_files_scanned++;
    ctx.summary.local_image_references_scanned+= references.size ();
    for (const AthenaVaultFileReference& reference: references) {
      std::error_code ec;
      if (fs::is_regular_file (reference.resolved_path, ec) && !ec) continue;
      missing.insert ({document_path.generic_string (), reference.value,
                       reference.resolved_path.generic_string ()});
    }
  }
  finish_progress ();

  for (const auto& entry: missing)
    ctx.summary.missing_images.push_back (
      {fs::path (std::get<0> (entry)), std::get<1> (entry),
       fs::path (std::get<2> (entry))});

  if (!ctx.summary.missing_images.empty ())
    ctx.warnings.push_back (
      "Missing image scan found " +
      std::to_string (ctx.summary.missing_images.size ()) +
      " missing local image reference(s).");
  return VaultMaintenancePassResult::success (
    "scanned " + std::to_string (documents.size ()) +
    " .ath file(s); found " +
    std::to_string (ctx.summary.missing_images.size ()) +
    " missing image(s)");
}

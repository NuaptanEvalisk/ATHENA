/******************************************************************************
* MODULE     : vault_maintenance_pass_materials.cpp
* DESCRIPTION: Vault maintenance pass for managed Material attachments
* COPYRIGHT  : (C) 2026  Felix
*******************************************************************************/

#include "ATHENA/Data/vault_maintenance_internal.hpp"

#include "ATHENA/Data/materials.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"

#include <filesystem>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

static bool
valid_vault_relative_path (const fs::path& path) {
  if (path.empty () || path.is_absolute ()) return false;
  for (const fs::path& part: path)
    if (part == "..") return false;
  return true;
}

VaultMaintenancePassResult
vault_maintenance_pass_maintain_materials (VaultMaintenanceContext& ctx) {
  AthenaVaultfileInfo info;
  std::string error;
  if (!athena_vaultfile_read (ctx.root, info, error))
    return VaultMaintenancePassResult::failure (
      "failed to read Materials paths from Vaultfile.json: " + error);

  fs::path database_rel= fs::u8path (info.materials_db_path);
  if (!valid_vault_relative_path (database_rel))
    return VaultMaintenancePassResult::failure (
      "Materials database path is not vault-relative");

  fs::path database= (ctx.root / database_rel).lexically_normal ();
  std::error_code ec;
  bool database_exists= fs::is_regular_file (database, ec);
  if (ec == std::errc::no_such_file_or_directory) ec.clear ();
  if (ec)
    return VaultMaintenancePassResult::failure (
      "could not inspect Materials database: " + ec.message ());
  if (!database_exists) {
    log_info ("materials: no existing database; maintenance skipped");
    return VaultMaintenancePassResult::success ("no Materials database");
  }

  ctx.summary.materials_database_present= true;
  MaterialsStore store;
  if (!store.open (ctx.root, info, error))
    return VaultMaintenancePassResult::failure (
      "failed to open Materials database: " + error);

  MaterialFilenameMaintenanceResult result;
  if (!store.canonicalize_filenames (result, error))
    return VaultMaintenancePassResult::failure (
      "failed to canonicalize Material attachments: " + error);

  ctx.summary.material_attachments_renamed= (size_t) result.renamed;
  ctx.summary.material_attachments_unchanged= (size_t) result.unchanged;
  ctx.summary.material_attachments_missing= (size_t) result.missing;
  ctx.summary.missing_material_attachments= result.missing_files;

  for (const fs::path& path: result.unreferenced_files) {
    ec.clear ();
    bool removed= fs::remove (path, ec);
    if (ec)
      return VaultMaintenancePassResult::failure (
        "failed to purge unreferenced Material file " + path.string () +
        ": " + ec.message ());
    if (removed) ctx.summary.material_files_purged++;
  }

  if (result.missing > 0)
    ctx.warnings.push_back (
      "Materials: " + std::to_string (result.missing) +
      " referenced attachment(s) are missing.");

  std::string message=
    "renamed " + std::to_string (result.renamed) +
    ", unchanged " + std::to_string (result.unchanged) +
    ", missing " + std::to_string (result.missing) +
    ", purged " + std::to_string (ctx.summary.material_files_purged);
  log_info ("materials: " + message);
  return VaultMaintenancePassResult::success (message);
}

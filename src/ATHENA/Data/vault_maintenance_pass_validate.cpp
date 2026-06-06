/******************************************************************************
* MODULE     : vault_maintenance_pass_validate.cpp
* DESCRIPTION: Vault maintenance root validation pass
* COPYRIGHT  : (C) 2026  Felix
******************************************************************************/

#include "ATHENA/Data/vault_maintenance_internal.hpp"

#include <filesystem>

namespace fs = std::filesystem;

VaultMaintenancePassResult
vault_maintenance_pass_validate_root (VaultMaintenanceContext& ctx) {
  log_info ("vault root: " + ctx.root.string ());

  if (!fs::exists (ctx.root) || !fs::is_directory (ctx.root)) {
    log_error ("vault root is not a directory");
    return VaultMaintenancePassResult::failure ("vault root is not a directory");
  }
  if (!fs::exists (ctx.root / "Vaultfile")) {
    std::string message = "missing Vaultfile in " + ctx.root.string ();
    log_error (message);
    return VaultMaintenancePassResult::failure (message);
  }
  return VaultMaintenancePassResult::success ();
}

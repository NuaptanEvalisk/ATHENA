/******************************************************************************
* MODULE     : vault_maintenance_pass_dispatch_backups.cpp
* DESCRIPTION: Run maintenance-triggered one-way vault backup dispatchers
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include "ATHENA/Data/vault_backup_dispatcher.hpp"
#include "ATHENA/Data/vault_maintenance_passes.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"

VaultMaintenancePassResult
vault_maintenance_pass_dispatch_backups (VaultMaintenanceContext& ctx) {
  AthenaVaultfileInfo info;
  std::string error;
  if (!athena_vaultfile_read (ctx.root, info, error))
    return VaultMaintenancePassResult::failure (
      "could not read Vaultfile.json: " + error);

  size_t count= 0;
  for (const AthenaBackupDispatcher& dispatcher: info.backup_dispatchers) {
    if (dispatcher.trigger != "maintenance") continue;
    vault_maintenance_log_info (
      "backup dispatcher: synchronizing to " + dispatcher.destination);
    if (!athena_backup_dispatch_run (
          ctx.root, dispatcher.destination, error))
      return VaultMaintenancePassResult::failure (error);
    ++count;
  }
  ctx.summary.backup_dispatchers_run= count;
  if (count == 0)
    return VaultMaintenancePassResult::success (
      "no maintenance-triggered backup dispatchers configured");
  return VaultMaintenancePassResult::success (
    "completed " + std::to_string (count) + " backup dispatcher(s)");
}

/******************************************************************************
* MODULE     : vault_maintenance.cpp
* DESCRIPTION: Headless maintenance operations for ATHENA vaults
* COPYRIGHT  : (C) 2026  Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "ATHENA/Data/vault_maintenance.hpp"
#include "ATHENA/Data/vault_maintenance_internal.hpp"
#include "ATHENA/Data/vault_maintenance_passes.hpp"

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

bool
vault_maintenance_run (string vault_dir) {
  VaultMaintenanceContext ctx;
  ctx.root = normalize_root (fs::path (tm_to_std (vault_dir)));

  static const VaultMaintenancePass passes[] = {
    {"validate-root", "Validate vault root",
     vault_maintenance_pass_validate_root},
    {"load-preferences", "Load vault maintenance preferences",
     vault_maintenance_pass_load_preferences},
    {"full-backup", "Create full compressed backup",
     vault_maintenance_pass_create_backup},
    {"read-policies", "Read maintenance policy preferences",
     vault_maintenance_pass_read_policy_preferences},
    {"normalize-images", "Normalize image filenames and references",
     vault_maintenance_pass_normalize_images},
    {"anchor-enunciations", "Anchor enunciations",
     vault_maintenance_pass_anchor_enunciations},
    {"collect-orphans", "Collect orphan assets",
     vault_maintenance_pass_collect_orphans},
    {"purge-retained-data", "Purge old retained data",
     vault_maintenance_pass_purge_retained_data},
    {"summary", "Print maintenance summary",
     vault_maintenance_pass_print_summary}
  };

  for (const VaultMaintenancePass& pass : passes) {
    log_info (std::string ("pass start: ") + pass.id + " (" +
              pass.description + ")");
    VaultMaintenancePassResult result = pass.run (ctx);
    if (!result.ok) {
      std::string message = result.message.empty () ? "failed" : result.message;
      log_error (std::string ("pass failed: ") + pass.id + ": " + message);
      return false;
    }
    log_info (std::string ("pass success: ") + pass.id);
  }

  return true;
}

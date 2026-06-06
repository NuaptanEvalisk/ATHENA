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
    {"read-policies", "Read maintenance policy preferences",
     vault_maintenance_pass_read_policy_preferences},
    {"full-backup", "Create full compressed backup",
     vault_maintenance_pass_create_backup},
    {"health-check", "Check ATHENA document readability",
     vault_maintenance_pass_health_check},
    {"normalize-images", "Normalize image filenames and references",
     vault_maintenance_pass_normalize_images},
    {"anchor-structures", "Anchor structures",
     vault_maintenance_pass_anchor_enunciations},
    {"collect-orphans", "Collect orphan assets",
     vault_maintenance_pass_collect_orphans},
    {"purge-retained-data", "Purge old retained data",
     vault_maintenance_pass_purge_retained_data},
    {"summary", "Print maintenance summary",
     vault_maintenance_pass_print_summary}
  };

  size_t pass_count = sizeof (passes) / sizeof (passes[0]);
  for (size_t i=0; i<pass_count; i++) {
    const VaultMaintenancePass& pass = passes[i];
    log_info (std::string ("pass start: ") + pass.id + " (" +
              pass.description + ")");
    size_t warning_count = ctx.warnings.size ();
    VaultMaintenancePassResult result = pass.run (ctx);
    std::string message = result.message.empty () ? "ok" : result.message;
    bool produced_warning = ctx.warnings.size () > warning_count;
    if (std::string (pass.id) != "summary")
      ctx.pass_records.push_back (
        {pass.id, pass.description, result.ok ? "success" : "failed",
         message, produced_warning});
    if (!result.ok) {
      message = result.message.empty () ? "failed" : result.message;
      log_error (std::string ("pass failed: ") + pass.id + ": " + message);
      for (size_t j=i + 1; j<pass_count; j++) {
        if (std::string (passes[j].id) == "summary") continue;
        ctx.pass_records.push_back (
          {passes[j].id, passes[j].description, "not-run",
           "not run because an earlier pass failed", false});
      }
      if (std::string (pass.id) != "summary" &&
          !vault_maintenance_write_summary_page (ctx, false, pass.id, message))
        log_error ("summary: failed to write failure summary page");
      return false;
    }
    log_info (std::string ("pass success: ") + pass.id);
  }

  return true;
}

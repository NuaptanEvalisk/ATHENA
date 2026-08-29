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
#include "ATHENA/Features/athena_features.hpp"
#include "ATHENA/Data/vault_maintenance_internal.hpp"
#include "ATHENA/Data/vault_maintenance_passes.hpp"

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

bool
vault_maintenance_run (string vault_dir, bool check_only) {
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
    {"normalize-assets", "Normalize referenced asset names and references",
     vault_maintenance_pass_normalize_assets},
#if ATHENA_ENABLE_PERSON_SUBSYSTEM
    {"normalize-person-names", "Tag recognized person names",
     vault_maintenance_pass_normalize_person_names},
#endif
    {"anchor-structures", "Anchor structures",
     vault_maintenance_pass_anchor_enunciations},
    {"build-artifacts", "Build semantic artifact indexes",
     vault_maintenance_pass_build_artifacts},
    {"remove-redundant-block-wikilinks",
     "Remove block wikilinks superseded by radioactive links",
     vault_maintenance_pass_remove_redundant_wikilinks},
    {"update-tocs", "Update tables of contents",
     vault_maintenance_pass_update_tables_of_contents},
    {"continuous-rag", "Update Continuous RAG index",
     vault_maintenance_pass_continuous_rag},
    {"collect-orphans", "Collect orphan assets",
     vault_maintenance_pass_collect_orphans},
    {"purge-retained-data", "Purge old retained data",
     vault_maintenance_pass_purge_retained_data},
    {"generate-websites", "Generate maintenance websites",
     vault_maintenance_pass_generate_websites},
    {"dispatch-backups", "Dispatch vault backup mirrors",
     vault_maintenance_pass_dispatch_backups},
    {"summary", "Print maintenance summary",
     vault_maintenance_pass_print_summary}
  };

  static const VaultMaintenancePass check_only_passes[] = {
    {"validate-root", "Validate vault root",
     vault_maintenance_pass_validate_root},
    {"health-check", "Check ATHENA document readability",
     vault_maintenance_pass_health_check}
  };

  const VaultMaintenancePass* active_passes =
    check_only ? check_only_passes : passes;
  size_t pass_count =
    check_only ? sizeof (check_only_passes) / sizeof (check_only_passes[0])
               : sizeof (passes) / sizeof (passes[0]);
  if (check_only)
    log_info ("check-only mode: running health check without backup, "
              "mutation, preference loading, or summary generation");
  for (size_t i=0; i<pass_count; i++) {
    const VaultMaintenancePass& pass = active_passes[i];
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
        if (std::string (active_passes[j].id) == "summary") continue;
        ctx.pass_records.push_back (
          {active_passes[j].id, active_passes[j].description, "not-run",
           "not run because an earlier pass failed", false});
      }
      if (!check_only && std::string (pass.id) != "summary" &&
          !vault_maintenance_write_summary_page (ctx, false, pass.id, message))
        log_error ("summary: failed to write failure summary page");
      return false;
    }
    log_info (std::string ("pass success: ") + pass.id);
  }

  return true;
}

bool
vault_rag_delegation_run (string vault_dir) {
  VaultMaintenanceContext ctx;
  ctx.root = normalize_root (fs::path (tm_to_std (vault_dir)));

  static const VaultMaintenancePass setup_passes[] = {
    {"validate-root", "Validate vault root",
     vault_maintenance_pass_validate_root},
    {"load-preferences", "Load vault maintenance preferences",
     vault_maintenance_pass_load_preferences},
    {"read-policies", "Read maintenance policy preferences",
     vault_maintenance_pass_read_policy_preferences}
  };

  log_info ("delegated RAG: running embedding-only maintenance");
  for (const VaultMaintenancePass& pass: setup_passes) {
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

  // This explicit command never performs local embedding as a fallback. It is
  // intended for unattended delegation tests and server-side deployments.
  ctx.summary.rag_update_enabled = true;
  ctx.summary.rag_delegation_enabled = true;
  ctx.summary.rag_fallback_policy = "fail-maintenance";

  log_info ("pass start: continuous-rag (Delegate incremental embedding)");
  VaultMaintenancePassResult result =
    vault_maintenance_pass_continuous_rag (ctx);
  if (!result.ok) {
    std::string message = result.message.empty () ? "failed" : result.message;
    log_error ("pass failed: continuous-rag: " + message);
    return false;
  }
  log_info ("pass success: continuous-rag: " +
            (result.message.empty () ? std::string ("ok") : result.message));
  return true;
}

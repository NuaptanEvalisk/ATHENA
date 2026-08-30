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
#include "ATHENA/Data/vaultfile_json.hpp"
#include "ATHENA/Data/websites.hpp"
#include "sys_utils.hpp"

#include <algorithm>
#include <filesystem>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

typedef bool (*VaultMaintenancePassEnabled) (
  const VaultMaintenanceContext&);

struct RegisteredVaultMaintenancePass {
  VaultMaintenancePass pass;
  bool setup;
  VaultMaintenancePassEnabled enabled;
};

bool
pass_always_enabled (const VaultMaintenanceContext&) {
  return true;
}

bool
redundant_wikilinks_enabled (const VaultMaintenanceContext& ctx) {
  return ctx.summary.redundant_block_wikilink_removal_enabled;
}

bool
toc_update_enabled (const VaultMaintenanceContext& ctx) {
  return ctx.summary.toc_update_enabled;
}

bool
continuous_rag_enabled (const VaultMaintenanceContext& ctx) {
  return ctx.summary.rag_update_enabled;
}

bool
orphan_collection_enabled (const VaultMaintenanceContext& ctx) {
  return ctx.summary.orphan_collection_enabled;
}

bool
maintenance_websites_enabled (const VaultMaintenanceContext& ctx) {
  std::vector<athena_website_entry> websites;
  std::string error;
  if (!athena_websites_load (ctx.root.string (), websites, error))
    return true;
  return std::any_of (
    websites.begin (), websites.end (), [] (const auto& website) {
      return website.regenerate == "maintenance";
    });
}

bool
maintenance_dispatchers_enabled (const VaultMaintenanceContext& ctx) {
  AthenaVaultfileInfo info;
  std::string error;
  if (!athena_vaultfile_read (ctx.root, info, error)) return true;
  return std::any_of (
    info.backup_dispatchers.begin (), info.backup_dispatchers.end (),
    [] (const auto& dispatcher) {
      return dispatcher.trigger == "maintenance";
    });
}

const std::vector<RegisteredVaultMaintenancePass>&
registered_passes () {
  static const std::vector<RegisteredVaultMaintenancePass> passes= {
    {{"validate-root", "Validate vault root",
      vault_maintenance_pass_validate_root}, true, pass_always_enabled},
    {{"load-preferences", "Load vault maintenance preferences",
      vault_maintenance_pass_load_preferences}, true, pass_always_enabled},
    {{"read-policies", "Read maintenance policy preferences",
      vault_maintenance_pass_read_policy_preferences}, true,
      pass_always_enabled},
    {{"full-backup", "Create full compressed backup",
      vault_maintenance_pass_create_backup}, false, pass_always_enabled},
    {{"health-check", "Check ATHENA document readability",
      vault_maintenance_pass_health_check}, false, pass_always_enabled},
    {{"normalize-assets", "Normalize referenced asset names and references",
      vault_maintenance_pass_normalize_assets}, false, pass_always_enabled},
    {{"scan-missing-images", "Scan for missing images",
      vault_maintenance_pass_scan_missing_images}, false,
      pass_always_enabled},
#if ATHENA_ENABLE_PERSON_SUBSYSTEM
    {{"normalize-person-names", "Tag recognized person names",
      vault_maintenance_pass_normalize_person_names}, false,
      pass_always_enabled},
#endif
    {{"anchor-structures", "Anchor structures",
      vault_maintenance_pass_anchor_enunciations}, false,
      pass_always_enabled},
    {{"build-artifacts", "Build semantic artifact indexes",
      vault_maintenance_pass_build_artifacts}, false, pass_always_enabled},
    {{"remove-redundant-block-wikilinks",
      "Remove block wikilinks superseded by radioactive links",
      vault_maintenance_pass_remove_redundant_wikilinks}, false,
      redundant_wikilinks_enabled},
    {{"update-tocs", "Update tables of contents",
      vault_maintenance_pass_update_tables_of_contents}, false,
      toc_update_enabled},
    {{"continuous-rag", "Update Continuous RAG index",
      vault_maintenance_pass_continuous_rag}, false,
      continuous_rag_enabled},
    {{"collect-orphans", "Collect orphan assets",
      vault_maintenance_pass_collect_orphans}, false,
      orphan_collection_enabled},
    {{"purge-retained-data", "Purge old retained data",
      vault_maintenance_pass_purge_retained_data}, false,
      pass_always_enabled},
    {{"generate-websites", "Generate maintenance websites",
      vault_maintenance_pass_generate_websites}, false,
      maintenance_websites_enabled},
    {{"dispatch-backups", "Dispatch vault backup mirrors",
      vault_maintenance_pass_dispatch_backups}, false,
      maintenance_dispatchers_enabled},
    {{"summary", "Print maintenance summary",
      vault_maintenance_pass_print_summary}, false, pass_always_enabled}
  };
  return passes;
}

std::set<std::string>
skipped_passes_from_environment () {
  std::set<std::string> skipped;
  std::string configured= tm_to_std (
    get_env ("ATHENA_VAULT_MAINTENANCE_SKIP_PASSES"));
  std::stringstream stream (configured);
  std::string id;
  while (std::getline (stream, id, ',')) {
    id= trim_copy (id);
    if (!id.empty ()) skipped.insert (id);
  }
  return skipped;
}

VaultMaintenancePassResult
run_registered_pass (VaultMaintenanceContext& ctx,
                     const RegisteredVaultMaintenancePass& registered) {
  const VaultMaintenancePass& pass= registered.pass;
  log_info (std::string ("pass start: ") + pass.id + " (" +
            pass.description + ")");
  size_t warning_count= ctx.warnings.size ();
  VaultMaintenancePassResult result= pass.run (ctx);
  std::string message= result.message.empty () ? "ok" : result.message;
  bool produced_warning= ctx.warnings.size () > warning_count;
  if (std::string (pass.id) != "summary")
    ctx.pass_records.push_back (
      {pass.id, pass.description, result.ok ? "success" : "failed",
       message, produced_warning});
  if (result.ok)
    log_info (std::string ("pass success: ") + pass.id);
  return result;
}

bool
initialize_maintenance_context (VaultMaintenanceContext& ctx,
                                std::string& error) {
  for (const auto& registered: registered_passes ()) {
    if (!registered.setup) break;
    VaultMaintenancePassResult result= run_registered_pass (ctx, registered);
    if (!result.ok) {
      error= result.message.empty () ? "failed" : result.message;
      return false;
    }
  }
  return true;
}

} // namespace

bool
vault_maintenance_run (string vault_dir, bool check_only) {
  VaultMaintenanceContext ctx;
  ctx.root = normalize_root (fs::path (tm_to_std (vault_dir)));

  static const VaultMaintenancePass check_only_passes[] = {
    {"validate-root", "Validate vault root",
     vault_maintenance_pass_validate_root},
    {"health-check", "Check ATHENA document readability",
     vault_maintenance_pass_health_check}
  };

  if (check_only) {
    log_info ("check-only mode: running health check without backup, "
              "mutation, preference loading, or summary generation");
    for (const VaultMaintenancePass& pass: check_only_passes) {
      RegisteredVaultMaintenancePass registered {
        pass, true, pass_always_enabled};
      VaultMaintenancePassResult result= run_registered_pass (ctx, registered);
      if (!result.ok) {
        std::string message= result.message.empty () ? "failed": result.message;
        log_error (std::string ("pass failed: ") + pass.id + ": " + message);
        return false;
      }
    }
    return true;
  }

  std::set<std::string> skipped= skipped_passes_from_environment ();
  const auto& passes= registered_passes ();
  for (size_t i=0; i<passes.size (); ++i) {
    const RegisteredVaultMaintenancePass& registered= passes[i];
    const VaultMaintenancePass& pass= registered.pass;
    if (!registered.setup && !registered.enabled (ctx)) {
      log_info (std::string ("pass skipped: ") + pass.id +
                " (disabled by current configuration)");
      continue;
    }
    if (!registered.setup && skipped.count (pass.id) != 0) {
      log_info (std::string ("pass skipped: ") + pass.id +
                " (disabled in Maintenance Setup)");
      if (std::string (pass.id) != "summary")
        ctx.pass_records.push_back (
          {pass.id, pass.description, "skipped",
           "disabled in Maintenance Setup", false});
      continue;
    }
    VaultMaintenancePassResult result= run_registered_pass (ctx, registered);
    if (!result.ok) {
      std::string message= result.message.empty () ? "failed": result.message;
      log_error (std::string ("pass failed: ") + pass.id + ": " + message);
      for (size_t j=i + 1; j<passes.size (); ++j) {
        const RegisteredVaultMaintenancePass& pending_registered= passes[j];
        const VaultMaintenancePass& pending= pending_registered.pass;
        if (std::string (pending.id) == "summary") continue;
        if (!pending_registered.setup && !pending_registered.enabled (ctx))
          continue;
        if (!pending_registered.setup && skipped.count (pending.id) != 0)
          continue;
        ctx.pass_records.push_back (
          {pending.id, pending.description, "not-run",
           "not run because an earlier pass failed", false});
      }
      if (!check_only && std::string (pass.id) != "summary" &&
          !vault_maintenance_write_summary_page (ctx, false, pass.id, message))
        log_error ("summary: failed to write failure summary page");
      return false;
    }
  }

  return true;
}

bool
vault_maintenance_plan (
  string vault_dir, std::vector<VaultMaintenancePlanEntry>& entries,
  std::string& error) {
  entries.clear ();
  error.clear ();
  VaultMaintenanceContext ctx;
  ctx.root= normalize_root (fs::path (tm_to_std (vault_dir)));
  if (!initialize_maintenance_context (ctx, error)) return false;
  for (const auto& registered: registered_passes ())
    if (!registered.setup && registered.enabled (ctx))
      entries.push_back (
        {registered.pass.id, registered.pass.description});
  return true;
}

bool
vault_rag_delegation_run (string vault_dir) {
  VaultMaintenanceContext ctx;
  ctx.root = normalize_root (fs::path (tm_to_std (vault_dir)));
  log_info ("delegated RAG: running embedding-only maintenance");
  std::string setup_error;
  if (!initialize_maintenance_context (ctx, setup_error)) {
    log_error ("delegated RAG setup failed: " + setup_error);
    return false;
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

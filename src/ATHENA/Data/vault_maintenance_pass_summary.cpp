/******************************************************************************
* MODULE     : vault_maintenance_pass_summary.cpp
* DESCRIPTION: Vault maintenance summary pass
* COPYRIGHT  : (C) 2026  Felix
******************************************************************************/

#include "ATHENA/Data/vault_maintenance_internal.hpp"

#include <string>

VaultMaintenancePassResult
vault_maintenance_pass_print_summary (VaultMaintenanceContext& ctx) {
  VaultMaintenanceSummary& summary = ctx.summary;
  log_info ("summary: backup archive " + summary.backup_archive.string ());
  if (summary.backup_limit == VAULT_BACKUP_LIMIT_UNLIMITED)
    log_info ("summary: full backup retention Unlimited; purged " +
              std::to_string (summary.backups_purged) + " old backup(s)");
  else
    log_info ("summary: full backup retention " +
              std::to_string (summary.backup_limit) + "; purged " +
              std::to_string (summary.backups_purged) + " old backup(s)");
  log_info ("summary: pre-save history retention " +
            manual_save_retention_label (summary.manual_save_retention_seconds) +
            "; purged " +
            std::to_string (summary.manual_save_histories_purged) +
            " old history folder(s)");
  log_info ("summary: renamed " + std::to_string (summary.image_renames) +
            " image file(s), updated " +
            std::to_string (summary.image_reference_updates) +
            " image reference(s)");
  log_info ("summary: health-checked " +
            std::to_string (summary.health_files_scanned) +
            " .ath file(s); unreadable " +
            std::to_string (summary.health_files_failed));
  log_info ("summary: anchored " +
            std::to_string (summary.anchor_enunciations_wrapped) +
            " enunciation(s) and " +
            std::to_string (summary.anchor_headings_added) +
            " heading(s) in " +
            std::to_string (summary.anchor_files_changed) + " of " +
            std::to_string (summary.anchor_files_scanned) +
            " .ath file(s); removed " +
            std::to_string (summary.anchor_dead_pairs_removed) +
            " dead anchor pair(s); updated " +
            std::to_string (summary.anchor_stale_structures_updated) +
            " stale anchor structure(s); rewrote " +
            std::to_string (summary.anchor_map_references_updated) +
            " map reference(s); failures " +
            std::to_string (summary.anchor_failures));
  if (summary.orphan_collection_enabled) {
    std::string where = summary.orphan_dir.empty ()
                        ? std::string ("")
                        : (" into " + summary.orphan_dir.string ());
    log_info ("summary: collected " +
              std::to_string (summary.orphan_assets_collected) +
              " orphan asset(s)" + where);
  }
  else log_info ("summary: orphan asset collection disabled");
  log_info ("complete");
  return VaultMaintenancePassResult::success ();
}

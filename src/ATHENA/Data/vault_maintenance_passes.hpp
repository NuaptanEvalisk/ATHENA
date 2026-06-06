/******************************************************************************
* MODULE     : vault_maintenance_passes.hpp
* DESCRIPTION: Pass API for ATHENA vault maintenance
* COPYRIGHT  : (C) 2026  Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef VAULT_MAINTENANCE_PASSES_HPP
#define VAULT_MAINTENANCE_PASSES_HPP

#include <cstddef>
#include <filesystem>
#include <string>

struct VaultMaintenanceSummary {
  std::filesystem::path backup_archive;
  int backup_limit = -1;
  size_t backups_purged = 0;
  long long manual_save_retention_seconds = -1;
  size_t manual_save_histories_purged = 0;
  size_t image_renames = 0;
  size_t image_reference_updates = 0;
  size_t health_files_scanned = 0;
  size_t health_files_failed = 0;
  size_t anchor_files_scanned = 0;
  size_t anchor_files_changed = 0;
  size_t anchor_enunciations_wrapped = 0;
  size_t anchor_headings_added = 0;
  size_t anchor_dead_pairs_removed = 0;
  size_t anchor_failures = 0;
  bool orphan_collection_enabled = false;
  size_t orphan_assets_collected = 0;
  std::filesystem::path orphan_dir;
};

struct VaultMaintenanceContext {
  std::filesystem::path root;
  VaultMaintenanceSummary summary;
};

struct VaultMaintenancePassResult {
  bool ok;
  std::string message;

  static VaultMaintenancePassResult success (std::string message= "") {
    return {true, message};
  }

  static VaultMaintenancePassResult failure (std::string message) {
    return {false, message};
  }
};

typedef VaultMaintenancePassResult (*VaultMaintenancePassFunction) (
  VaultMaintenanceContext&);

struct VaultMaintenancePass {
  const char* id;
  const char* description;
  VaultMaintenancePassFunction run;
};

void vault_maintenance_log_info (const std::string& message);
void vault_maintenance_log_error (const std::string& message);

VaultMaintenancePassResult vault_maintenance_pass_validate_root (
  VaultMaintenanceContext& ctx);
VaultMaintenancePassResult vault_maintenance_pass_load_preferences (
  VaultMaintenanceContext& ctx);
VaultMaintenancePassResult vault_maintenance_pass_create_backup (
  VaultMaintenanceContext& ctx);
VaultMaintenancePassResult vault_maintenance_pass_health_check (
  VaultMaintenanceContext& ctx);
VaultMaintenancePassResult vault_maintenance_pass_read_policy_preferences (
  VaultMaintenanceContext& ctx);
VaultMaintenancePassResult vault_maintenance_pass_normalize_images (
  VaultMaintenanceContext& ctx);
VaultMaintenancePassResult vault_maintenance_pass_anchor_enunciations (
  VaultMaintenanceContext& ctx);
VaultMaintenancePassResult vault_maintenance_pass_collect_orphans (
  VaultMaintenanceContext& ctx);
VaultMaintenancePassResult vault_maintenance_pass_purge_retained_data (
  VaultMaintenanceContext& ctx);
VaultMaintenancePassResult vault_maintenance_pass_print_summary (
  VaultMaintenanceContext& ctx);

#endif // VAULT_MAINTENANCE_PASSES_HPP

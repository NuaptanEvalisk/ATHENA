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
#include <vector>

struct VaultMaintenanceCollectedOrphan {
  std::filesystem::path collected_path;
  std::filesystem::path original_path;
};

struct VaultMaintenanceMissingImage {
  std::filesystem::path document_path;
  std::string reference;
  std::filesystem::path resolved_path;
};

struct VaultMaintenanceSummary {
  std::filesystem::path backup_archive;
  int backup_limit = -1;
  size_t backups_purged = 0;
  long long manual_save_retention_seconds = -1;
  size_t manual_save_histories_purged = 0;
  size_t asset_renames = 0;
  size_t asset_reference_updates = 0;
  bool materials_database_present = false;
  size_t material_attachments_renamed = 0;
  size_t material_attachments_unchanged = 0;
  size_t material_attachments_missing = 0;
  size_t material_files_purged = 0;
  std::vector<std::filesystem::path> missing_material_attachments;
  bool missing_image_scan_enabled = false;
  size_t missing_image_files_scanned = 0;
  size_t local_image_references_scanned = 0;
  std::vector<VaultMaintenanceMissingImage> missing_images;
  size_t health_files_scanned = 0;
  size_t health_files_failed = 0;
  size_t person_files_scanned = 0;
  size_t person_files_changed = 0;
  size_t person_names_wrapped = 0;
  size_t anchor_files_scanned = 0;
  size_t anchor_files_changed = 0;
  size_t anchor_enunciations_wrapped = 0;
  size_t anchor_headings_added = 0;
  size_t anchor_stale_structures_updated = 0;
  size_t anchor_map_references_updated = 0;
  size_t anchor_dead_pairs_removed = 0;
  size_t anchor_failures = 0;
  int anchor_reader_processes = -1;
  size_t artifact_documents_seen = 0;
  size_t artifact_documents_changed = 0;
  size_t artifact_documents_deleted = 0;
  size_t artifact_enunciations = 0;
  size_t artifact_bold_texts = 0;
  size_t artifacts_indexed = 0;
  bool artifact_delegation_enabled = false;
  bool artifact_delegation_attempted = false;
  bool artifact_delegation_succeeded = false;
  bool redundant_block_wikilink_removal_enabled = false;
  size_t redundant_wikilink_files_scanned = 0;
  size_t redundant_block_wikilinks_scanned = 0;
  size_t redundant_wikilink_full_matches = 0;
  size_t redundant_wikilinks_removed = 0;
  size_t redundant_wikilink_files_changed = 0;
  size_t redundant_wikilink_unverified_targets = 0;
  bool toc_update_enabled = false;
  size_t toc_files_scanned = 0;
  size_t toc_files_containing_toc = 0;
  size_t toc_files_updated = 0;
  size_t toc_failures = 0;
  int toc_worker_processes = 0;
  bool rag_update_enabled = false;
  bool rag_delegation_enabled = false;
  bool rag_delegation_attempted = false;
  bool rag_delegation_succeeded = false;
  bool rag_local_fallback_used = false;
  std::string rag_fallback_policy = "continue";
  std::string delegation_server;
  std::string rag_result;
  int rag_documents_before = 0;
  int rag_documents_after = 0;
  int rag_chunks_before = 0;
  int rag_chunks_after = 0;
  bool orphan_collection_enabled = false;
  size_t orphan_assets_collected = 0;
  std::filesystem::path orphan_dir;
  std::vector<VaultMaintenanceCollectedOrphan> collected_orphans;
  bool generate_summary_page = false;
  int summary_keep_count = -1;
  std::filesystem::path summary_dir;
  std::filesystem::path summary_file;
  size_t summaries_purged = 0;
  size_t backup_dispatchers_run = 0;
};

struct VaultMaintenancePassRecord {
  std::string id;
  std::string description;
  std::string status;
  std::string message;
  bool produced_warning = false;
};

struct VaultMaintenanceContext {
  std::filesystem::path root;
  std::string vault_name;
  std::vector<std::string> warnings;
  std::vector<VaultMaintenancePassRecord> pass_records;
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
VaultMaintenancePassResult vault_maintenance_pass_maintain_materials (
  VaultMaintenanceContext& ctx);
VaultMaintenancePassResult vault_maintenance_pass_read_policy_preferences (
  VaultMaintenanceContext& ctx);
VaultMaintenancePassResult vault_maintenance_pass_normalize_assets (
  VaultMaintenanceContext& ctx);
VaultMaintenancePassResult vault_maintenance_pass_scan_missing_images (
  VaultMaintenanceContext& ctx);
VaultMaintenancePassResult vault_maintenance_pass_normalize_person_names (
  VaultMaintenanceContext& ctx);
VaultMaintenancePassResult vault_maintenance_pass_anchor_enunciations (
  VaultMaintenanceContext& ctx);
VaultMaintenancePassResult vault_maintenance_pass_promote_evaluation_bars (
  VaultMaintenanceContext& ctx);
VaultMaintenancePassResult vault_maintenance_pass_build_artifacts (
  VaultMaintenanceContext& ctx);
VaultMaintenancePassResult vault_maintenance_pass_remove_redundant_wikilinks (
  VaultMaintenanceContext& ctx);
VaultMaintenancePassResult vault_maintenance_pass_update_tables_of_contents (
  VaultMaintenanceContext& ctx);
VaultMaintenancePassResult vault_maintenance_pass_continuous_rag (
  VaultMaintenanceContext& ctx);
VaultMaintenancePassResult vault_maintenance_pass_collect_orphans (
  VaultMaintenanceContext& ctx);
VaultMaintenancePassResult vault_maintenance_pass_purge_retained_data (
  VaultMaintenanceContext& ctx);
VaultMaintenancePassResult vault_maintenance_pass_generate_websites (
  VaultMaintenanceContext& ctx);
VaultMaintenancePassResult vault_maintenance_pass_dispatch_backups (
  VaultMaintenanceContext& ctx);
VaultMaintenancePassResult vault_maintenance_pass_print_summary (
  VaultMaintenanceContext& ctx);
bool vault_maintenance_write_summary_page (
  VaultMaintenanceContext& ctx, bool success,
  const std::string& failure_pass= "", const std::string& failure_message= "");

#endif // VAULT_MAINTENANCE_PASSES_HPP

/******************************************************************************
* MODULE     : vault_maintenance_pass_preferences.cpp
* DESCRIPTION: Vault maintenance preference loading passes
* COPYRIGHT  : (C) 2026  Felix
******************************************************************************/

#include "ATHENA/Data/vault_maintenance_internal.hpp"

#include "ATHENA/Data/vaultfile_json.hpp"
#include "boot.hpp"
#include "scheme.hpp"
#include "sys_utils.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static int
backup_limit_preference () {
  std::string pref =
    trim_copy (tm_to_std (get_preference ("vault max full backups", "Unlimited")));
  std::string low = lower_copy (pref);
  if (pref.empty () || low == "unlimited") return VAULT_BACKUP_LIMIT_UNLIMITED;
  try {
    size_t pos = 0;
    int value = std::stoi (pref, &pos);
    if (pos == pref.size () && value >= 1) return value;
  }
  catch (...) {}
  log_info ("invalid backup retention preference '" + pref +
            "'; using Unlimited");
  return VAULT_BACKUP_LIMIT_UNLIMITED;
}

static bool
collect_orphan_assets_preference () {
  return get_preference ("vault collect orphan assets", "off") == "on";
}

static bool
update_tables_of_contents_preference () {
  return get_preference ("vault maintenance update table of contents", "off") ==
         "on";
}

static std::string
rag_fallback_preference () {
  std::string value= lower_copy (trim_copy (tm_to_std (get_preference (
    "vault maintenance rag delegation fallback", "continue"))));
  if (value == "fail-maintenance" || value == "continue" || value == "local")
    return value;
  log_info ("invalid RAG delegation fallback preference '" + value +
            "'; using continue");
  return "continue";
}

static bool
generate_summary_page_preference () {
  return get_preference ("vault generate maintenance summary page", "off") ==
         "on";
}

static int
summary_keep_count_preference () {
  std::string pref = trim_copy (tm_to_std (
    get_preference ("vault maintenance summaries to keep", "All")));
  std::string low = lower_copy (pref);
  if (pref.empty () || low == "all") return -1;
  try {
    size_t pos = 0;
    int value = std::stoi (pref, &pos);
    if (pos == pref.size () && value >= 1) return value;
  }
  catch (...) {}
  log_info ("invalid maintenance summary retention preference '" + pref +
            "'; using All");
  return -1;
}

static long long
manual_save_retention_preference () {
  std::string pref = trim_copy (tm_to_std (
    get_preference ("vault pre-save history preservation", "1 week")));
  std::string low = lower_copy (pref);
  if (pref.empty () || low == "unlimited")
    return VAULT_MANUAL_SAVE_RETENTION_UNLIMITED;
  if (low == "1 hour") return 60LL * 60LL;
  if (low == "6 hours") return 6LL * 60LL * 60LL;
  if (low == "1 day") return 24LL * 60LL * 60LL;
  if (low == "3 days") return 3LL * 24LL * 60LL * 60LL;
  if (low == "1 week") return 7LL * 24LL * 60LL * 60LL;
  if (low == "1 month") return 30LL * 24LL * 60LL * 60LL;
  log_info ("invalid pre-save history preservation preference '" + pref +
            "'; using Unlimited");
  return VAULT_MANUAL_SAVE_RETENTION_UNLIMITED;
}

static int
anchor_reader_processes_preference () {
  std::string pref = trim_copy (tm_to_std (
    get_preference ("vault maintenance anchor reader processes", "Unlimited")));
  std::string low = lower_copy (pref);
  if (pref.empty () || low == "unlimited") return -1;
  try {
    size_t pos = 0;
    int value = std::stoi (pref, &pos);
    if (pos == pref.size () && value >= 1) return value;
  }
  catch (...) {}
  log_info ("invalid anchor reader process preference '" + pref +
            "'; using Unlimited");
  return -1;
}

static std::string
preferences_json_rel (const std::string& rel) {
  if (rel.empty ()) return "vprefs.json";
  if (ends_with (rel, ".json")) return rel;
  if (ends_with (rel, ".scm")) return rel.substr (0, rel.size () - 4) + ".json";
  return rel + ".json";
}

static bool
valid_vault_relative_path (const std::string& rel) {
  if (rel.empty ()) return true;
  fs::path p (rel);
  if (p.is_absolute ()) return false;
  for (const fs::path& part : p) {
    if (part == "..") return false;
  }
  return true;
}

static void
read_vaultfile_metadata (VaultMaintenanceContext& ctx) {
  ctx.vault_name = ctx.root.filename ().string ();
  ctx.summary.summary_dir.clear ();

  AthenaVaultfileInfo info;
  std::string error;
  if (!athena_vaultfile_read (ctx.root, info, error)) {
    log_error ("preferences: " + error);
    return;
  }
  if (!info.name.empty ()) ctx.vault_name = info.name;

  std::string rel = trim_copy (info.maintenance_summary_path);
  if (rel.empty ()) return;
  if (!valid_vault_relative_path (rel)) {
    ctx.warnings.push_back (
      "Vaultfile maintenance summary folder is not a vault-relative path; "
      "using vault root for this summary.");
    return;
  }
  ctx.summary.summary_dir = fs::path (rel);
}

static bool
load_vault_preferences_if_enabled (const fs::path& root) {
  std::string requested = lower_copy (tm_to_std (
    get_env ("ATHENA_VAULT_MAINTENANCE_TAKE_PREFS")));
  bool take_prefs = requested == "on" ||
                    (requested.empty () &&
                     get_preference ("vault take preferences with vault", "off") == "on");
  if (!take_prefs) {
    log_info ("preferences: using system preferences");
    return true;
  }

  AthenaVaultfileInfo info;
  std::string vaultfile_error;
  if (!athena_vaultfile_read (root, info, vaultfile_error)) {
    log_error ("failed to read Vaultfile.json for vault preferences: " +
               vaultfile_error);
    return false;
  }

  std::string prefs_rel = info.preferences_path;
  std::string json_rel = preferences_json_rel (prefs_rel);
  std::string legacy_rel = prefs_rel.empty () ? "vprefs.scm" : prefs_rel;
  fs::path prefs_path = root / json_rel;
  fs::path legacy_path = root / legacy_rel;

  if (prefs_rel != json_rel) {
    info.preferences_path = json_rel;
    if (athena_vaultfile_write (root, info, vaultfile_error))
      log_info ("preferences: updated Vaultfile.json preferences path to " +
                json_rel);
    else
      log_error ("failed to update Vaultfile.json preferences path: " +
                 vaultfile_error);
  }

  if (!fs::exists (prefs_path) && fs::exists (legacy_path)) {
    load_user_preferences (url (legacy_path.string ().c_str ()));
  }

  if (!fs::exists (prefs_path)) {
    log_info ("preferences: vault preferences enabled, but " +
              prefs_path.string () + " does not exist; using system preferences");
    return true;
  }

  load_user_preferences (url (prefs_path.string ().c_str ()));
  log_info ("preferences: loaded vault preferences from " + prefs_path.string ());
  return true;
}



VaultMaintenancePassResult
vault_maintenance_pass_load_preferences (VaultMaintenanceContext& ctx) {
  if (load_vault_preferences_if_enabled (ctx.root))
    return VaultMaintenancePassResult::success ();
  return VaultMaintenancePassResult::failure ("failed to load vault preferences");
}

VaultMaintenancePassResult
vault_maintenance_pass_read_policy_preferences (VaultMaintenanceContext& ctx) {
  ctx.summary.backup_limit = backup_limit_preference ();
  ctx.summary.manual_save_retention_seconds =
    manual_save_retention_preference ();
  ctx.summary.anchor_reader_processes = anchor_reader_processes_preference ();
  ctx.summary.toc_update_enabled = update_tables_of_contents_preference ();
  ctx.summary.rag_update_enabled =
    get_preference ("vault maintenance continuous rag", "off") == "on";
  ctx.summary.rag_delegation_enabled =
    get_preference ("rag delegation enabled", "off") == "on";
  ctx.summary.artifact_delegation_enabled =
    get_preference ("artifact definition span delegation enabled", "off") ==
    "on";
  ctx.summary.redundant_block_wikilink_removal_enabled =
    get_preference (
      "vault maintenance remove redundant block wikilinks", "off") == "on";
  ctx.summary.rag_fallback_policy = rag_fallback_preference ();
  ctx.summary.delegation_server = trim_copy (tm_to_std (
    get_preference ("delegation server", "")));
  ctx.summary.orphan_collection_enabled = collect_orphan_assets_preference ();
  ctx.summary.generate_summary_page = generate_summary_page_preference ();
  ctx.summary.summary_keep_count = summary_keep_count_preference ();
  read_vaultfile_metadata (ctx);
  return VaultMaintenancePassResult::success ();
}

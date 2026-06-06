/******************************************************************************
* MODULE     : vault_maintenance_pass_preferences.cpp
* DESCRIPTION: Vault maintenance preference loading passes
* COPYRIGHT  : (C) 2026  Felix
******************************************************************************/

#include "ATHENA/Data/vault_maintenance_internal.hpp"

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

static std::vector<std::string>
parse_vaultfile_strings (const std::string& text) {
  std::vector<std::string> values;
  for (size_t i=0; i<text.size (); i++) {
    if (text[i] != '"') continue;
    i++;
    std::string value;
    while (i < text.size ()) {
      char c = text[i++];
      if (c == '\\' && i < text.size ()) {
        value.push_back (text[i++]);
        continue;
      }
      if (c == '"') break;
      value.push_back (c);
    }
    values.push_back (value);
  }
  return values;
}

static std::string
scheme_quote_string (const std::string& text) {
  std::string out = "\"";
  for (char c: text) {
    if (c == '\\' || c == '"') out.push_back ('\\');
    out.push_back (c);
  }
  out.push_back ('"');
  return out;
}

static std::string
preferences_json_rel (const std::string& rel) {
  if (rel.empty ()) return "vprefs.json";
  if (ends_with (rel, ".json")) return rel;
  if (ends_with (rel, ".scm")) return rel.substr (0, rel.size () - 4) + ".json";
  return rel + ".json";
}

static bool
write_vaultfile_preferences_path (const fs::path& vault_file,
                                  const std::vector<std::string>& fields,
                                  const std::string& prefs_rel) {
  if (fields.size () < 2) return false;
  std::string ns_rel = fields.size () >= 4 && !fields[3].empty ()
                       ? fields[3] : "ns.sqlite";
  std::string startup_page = fields.size () >= 5 ? fields[4] : "";
  std::string one_time_startup_page = fields.size () >= 6 ? fields[5] : "";
  std::string summary_dir = fields.size () >= 7 ? fields[6] : "";
  std::string text = "(" + scheme_quote_string (fields[0]) +
                     " " + scheme_quote_string (fields[1]) +
                     " " + scheme_quote_string (prefs_rel) +
                     " " + scheme_quote_string (ns_rel) +
                     " " + scheme_quote_string (startup_page) +
                     " " + scheme_quote_string (one_time_startup_page) +
                     " " + scheme_quote_string (summary_dir) +
                     ")\n";
  return write_file_bytes (vault_file, text);
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

  std::string text;
  fs::path vault_file = ctx.root / "Vaultfile";
  if (!read_file_bytes (vault_file, text)) return;

  std::vector<std::string> fields = parse_vaultfile_strings (text);
  if (fields.size () >= 2 && fields.size () < 7) {
    std::string prefs_rel = fields.size () >= 3 ? fields[2] : "";
    if (write_vaultfile_preferences_path (vault_file, fields, prefs_rel))
      log_info ("preferences: normalized Vaultfile to 7 fields");
    else
      log_error ("failed to normalize Vaultfile to 7 fields");
  }
  if (fields.size () >= 1 && !fields[0].empty ())
    ctx.vault_name = fields[0];

  std::string rel = fields.size () >= 7 ? trim_copy (fields[6]) : "";
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

  std::string text;
  fs::path vault_file = root / "Vaultfile";
  if (!read_file_bytes (vault_file, text)) {
    log_error ("failed to read Vaultfile for vault preferences");
    return false;
  }

  std::vector<std::string> fields = parse_vaultfile_strings (text);
  std::string prefs_rel = fields.size () >= 3 ? fields[2] : "";
  std::string json_rel = preferences_json_rel (prefs_rel);
  std::string legacy_rel = prefs_rel.empty () ? "vprefs.scm" : prefs_rel;
  fs::path prefs_path = root / json_rel;
  fs::path legacy_path = root / legacy_rel;

  if (fields.size () >= 2 && prefs_rel != json_rel) {
    if (write_vaultfile_preferences_path (vault_file, fields, json_rel))
      log_info ("preferences: updated Vaultfile preferences path to " +
                json_rel);
    else
      log_error ("failed to update Vaultfile preferences path");
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
  ctx.summary.orphan_collection_enabled = collect_orphan_assets_preference ();
  ctx.summary.generate_summary_page = generate_summary_page_preference ();
  ctx.summary.summary_keep_count = summary_keep_count_preference ();
  read_vaultfile_metadata (ctx);
  return VaultMaintenancePassResult::success ();
}

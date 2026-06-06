/******************************************************************************
* MODULE     : vault_maintenance_pass_summary.cpp
* DESCRIPTION: Vault maintenance summary pass
* COPYRIGHT  : (C) 2026  Felix
******************************************************************************/

#include "ATHENA/Data/vault_maintenance_internal.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static std::string
summary_retention_label (int count) {
  return count < 0 ? std::string ("All") : std::to_string (count);
}

static std::string
backup_retention_label (int count) {
  return count == VAULT_BACKUP_LIMIT_UNLIMITED ? std::string ("Unlimited")
                                               : std::to_string (count);
}

static std::string
safe_summary_name (const std::string& name) {
  std::string out;
  for (unsigned char c : name) {
    if (std::isalnum (c) || c == ' ' || c == '-' || c == '_' || c == '.')
      out.push_back ((char) c);
    else out.push_back ('_');
  }
  out = trim_copy (out);
  while (!out.empty () && out.front () == '.') out.erase (out.begin ());
  while (!out.empty () && out.back () == '.') out.pop_back ();
  return out.empty () ? std::string ("Vault") : out;
}

static std::string
tm_escape_text (const std::string& text) {
  std::string out;
  out.reserve (text.size ());
  for (char c : text) {
    if (c == '\\' || c == '<' || c == '>' || c == '|') out.push_back ('\\');
    out.push_back (c);
  }
  return out;
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

static bool
has_warning_with_prefix (const VaultMaintenanceContext& ctx,
                         const std::string& prefix) {
  for (const std::string& warning : ctx.warnings)
    if (starts_with (warning, prefix)) return true;
  return false;
}

static void
add_warning_once (VaultMaintenanceContext& ctx, const std::string& warning) {
  if (std::find (ctx.warnings.begin (), ctx.warnings.end (), warning) !=
      ctx.warnings.end ())
    return;
  ctx.warnings.push_back (warning);
  log_info ("summary warning: " + warning);
}

static fs::path
summary_directory (VaultMaintenanceContext& ctx) {
  if (ctx.summary.summary_dir.empty ()) {
    if (!has_warning_with_prefix (
          ctx, "Vaultfile maintenance summary folder is not") &&
        !has_warning_with_prefix (
          ctx, "Vaultfile maintenance summary folder is empty"))
      add_warning_once (
        ctx, "Vaultfile maintenance summary folder is empty; writing this "
             "summary to the vault root.");
    return ctx.root;
  }
  return ctx.root / ctx.summary.summary_dir;
}

static bool
summary_filename_matches (const std::string& name, const std::string& prefix) {
  return starts_with (name, prefix) && ends_with (name, ".ath");
}

static fs::path
unique_summary_file (const fs::path& dir, const std::string& prefix) {
  std::string stamp = timestamp_string ();
  fs::path candidate = dir / (prefix + stamp + ".ath");
  if (!fs::exists (candidate)) return candidate;
  for (int i=2; i<1000; i++) {
    candidate = dir / (prefix + stamp + "-" + std::to_string (i) + ".ath");
    if (!fs::exists (candidate)) return candidate;
  }
  return candidate;
}

static std::string
summary_relative_path (const VaultMaintenanceContext& ctx) {
  std::error_code ec;
  fs::path rel = fs::relative (ctx.summary.summary_file, ctx.root, ec);
  if (!ec && !rel.empty () && !rel.is_absolute ()) {
    bool escapes = false;
    for (const fs::path& part : rel) {
      if (part == "..") {
        escapes = true;
        break;
      }
    }
    if (!escapes) return rel.generic_string ();
  }

  if (ctx.summary.summary_dir.empty ())
    return ctx.summary.summary_file.filename ().generic_string ();
  return (ctx.summary.summary_dir /
          ctx.summary.summary_file.filename ()).generic_string ();
}

static bool
write_one_time_startup_page (VaultMaintenanceContext& ctx) {
  std::string text;
  fs::path vault_file = ctx.root / "Vaultfile";
  if (!read_file_bytes (vault_file, text)) {
    log_error ("summary: failed to read Vaultfile while setting one-time "
               "startup page");
    return false;
  }

  std::vector<std::string> fields = parse_vaultfile_strings (text);
  if (fields.size () < 2) {
    log_error ("summary: invalid Vaultfile while setting one-time startup page");
    return false;
  }
  while (fields.size () < 7) {
    if (fields.size () == 2) fields.push_back ("");
    else if (fields.size () == 3) fields.push_back ("ns.sqlite");
    else fields.push_back ("");
  }
  if (fields[3].empty ()) fields[3] = "ns.sqlite";

  std::string rel = summary_relative_path (ctx);
  fields[5] = rel;

  std::string out = "(" + scheme_quote_string (fields[0]) +
                    " " + scheme_quote_string (fields[1]) +
                    " " + scheme_quote_string (fields[2]) +
                    " " + scheme_quote_string (fields[3]) +
                    " " + scheme_quote_string (fields[4]) +
                    " " + scheme_quote_string (fields[5]) +
                    " " + scheme_quote_string (fields[6]) +
                    ")\n";
  if (!write_file_bytes (vault_file, out)) {
    log_error ("summary: failed to write Vaultfile while setting one-time "
               "startup page");
    return false;
  }
  log_info ("summary: set one-time startup page to " + rel);
  return true;
}

static bool
purge_old_summary_files (VaultMaintenanceContext& ctx, const fs::path& dir,
                         const std::string& prefix) {
  ctx.summary.summaries_purged = 0;
  int keep = ctx.summary.summary_keep_count;
  if (keep < 0) return true;

  std::vector<std::pair<fs::file_time_type, fs::path> > files;
  std::error_code ec;
  if (!fs::exists (dir, ec)) return true;
  for (fs::directory_iterator it (dir, ec), end; !ec && it != end;
       it.increment (ec)) {
    if (!it->is_regular_file (ec)) continue;
    std::string name = it->path ().filename ().string ();
    if (!summary_filename_matches (name, prefix)) continue;
    std::error_code time_ec;
    fs::file_time_type t = fs::last_write_time (it->path (), time_ec);
    if (time_ec) {
      log_info ("summary: skipped old summary page with unreadable timestamp " +
                compact_log_path (it->path ()));
      continue;
    }
    files.push_back ({t, it->path ()});
  }
  if (ec) {
    log_error ("summary: failed to inspect old summary pages: " + ec.message ());
    return false;
  }

  size_t allowed_existing = keep > 0 ? (size_t) keep - 1 : 0;
  if (files.size () <= allowed_existing) return true;
  std::sort (files.begin (), files.end (),
             [] (const auto& a, const auto& b) {
    if (a.first != b.first) return a.first < b.first;
    return a.second.string () < b.second.string ();
  });

  size_t remove_count = files.size () - allowed_existing;
  for (size_t i=0; i<remove_count; i++) {
    std::error_code remove_ec;
    fs::remove (files[i].second, remove_ec);
    if (remove_ec) {
      log_error ("summary: failed to purge old summary page " +
                 compact_log_path (files[i].second) + ": " +
                 remove_ec.message ());
      return false;
    }
    ctx.summary.summaries_purged++;
  }
  return true;
}

static void
append_item (std::ostringstream& out, const std::string& text) {
  out << "  <item>" << tm_escape_text (text) << "\n";
}

static void
append_block_title (std::ostringstream& out, const std::string& text) {
  out << "<strong|" << tm_escape_text (text) << ">\n\n";
}

static std::string
summary_document_text (VaultMaintenanceContext& ctx, bool success,
                       const std::string& failure_pass,
                       const std::string& failure_message) {
  VaultMaintenanceSummary& summary = ctx.summary;
  std::ostringstream out;
  out << "<TeXmacs|2.1.4>\n\n";
  out << "<style|generic>\n\n";
  out << "<\\body>\n";
  out << "<strong|Vault Maintenance Summary>\n\n";

  append_block_title (out, "Overview");
  out << "<\\itemize>\n";
  append_item (out, "Vault: " + ctx.vault_name);
  append_item (out, "Root: " + ctx.root.string ());
  append_item (out, "Generated: " + timestamp_string ());
  append_item (out, "Status: " + std::string (success ? "Success" : "Failure"));
  append_item (out, "Summary retention: " +
                    summary_retention_label (summary.summary_keep_count));
  append_item (out, "Old summaries purged: " +
                    std::to_string (summary.summaries_purged));
  if (!success) {
    append_item (out, "Failed pass: " + failure_pass);
    append_item (out, "Failure: " + failure_message);
  }
  out << "</itemize>\n\n";

  if (!ctx.warnings.empty ()) {
    append_block_title (out, "Warnings");
    out << "<\\itemize>\n";
    for (const std::string& warning : ctx.warnings) append_item (out, warning);
    out << "</itemize>\n\n";
  }

  append_block_title (out, "Passes");
  out << "<\\itemize>\n";
  for (const VaultMaintenancePassRecord& record : ctx.pass_records) {
    std::string line = record.id + " - " +
                       (record.ok ? std::string ("success")
                                  : std::string ("failure"));
    if (!record.message.empty () && record.message != "ok")
      line += ": " + record.message;
    append_item (out, line);
  }
  out << "</itemize>\n\n";

  append_block_title (out, "Work Performed");
  out << "<\\itemize>\n";
  append_item (out, "Backup archive: " + summary.backup_archive.string ());
  append_item (out, "Full backup retention: " +
                    backup_retention_label (summary.backup_limit) +
                    "; purged " + std::to_string (summary.backups_purged) +
                    " old backup(s)");
  append_item (out, "Pre-save history retention: " +
                    manual_save_retention_label (
                      summary.manual_save_retention_seconds) +
                    "; purged " +
                    std::to_string (summary.manual_save_histories_purged) +
                    " old history folder(s)");
  append_item (out, "Image normalization: renamed " +
                    std::to_string (summary.image_renames) +
                    " image file(s), updated " +
                    std::to_string (summary.image_reference_updates) +
                    " image reference(s)");
  append_item (out, "Health check: scanned " +
                    std::to_string (summary.health_files_scanned) +
                    " .ath file(s), unreadable " +
                    std::to_string (summary.health_files_failed));
  append_item (out, "Anchors: wrapped " +
                    std::to_string (summary.anchor_enunciations_wrapped) +
                    " enunciation(s), added " +
                    std::to_string (summary.anchor_headings_added) +
                    " heading anchor(s), updated " +
                    std::to_string (summary.anchor_stale_structures_updated) +
                    " stale structure(s), rewrote " +
                    std::to_string (summary.anchor_map_references_updated) +
                    " map reference(s), removed " +
                    std::to_string (summary.anchor_dead_pairs_removed) +
                    " dead pair(s), failures " +
                    std::to_string (summary.anchor_failures));
  if (summary.orphan_collection_enabled)
    append_item (out, "Orphan assets: collected " +
                      std::to_string (summary.orphan_assets_collected) +
                      " asset(s) into " + summary.orphan_dir.string ());
  else append_item (out, "Orphan assets: collection disabled");
  out << "</itemize>\n\n";

  out << "</body>\n";
  return out.str ();
}

bool
vault_maintenance_write_summary_page (VaultMaintenanceContext& ctx,
                                      bool success,
                                      const std::string& failure_pass,
                                      const std::string& failure_message) {
  if (!ctx.summary.generate_summary_page) return true;

  if (ctx.vault_name.empty ()) ctx.vault_name = ctx.root.filename ().string ();
  std::string prefix = safe_summary_name (ctx.vault_name) +
                       "_Maintenance_Summary_";
  fs::path dir = summary_directory (ctx);

  std::error_code ec;
  fs::create_directories (dir, ec);
  if (ec) {
    log_error ("summary: failed to create summary folder " +
               compact_log_path (dir) + ": " + ec.message ());
    return false;
  }

  if (!purge_old_summary_files (ctx, dir, prefix)) return false;
  ctx.summary.summary_file = unique_summary_file (dir, prefix);

  std::string text = summary_document_text (ctx, success, failure_pass,
                                            failure_message);
  if (!write_file_bytes (ctx.summary.summary_file, text)) {
    log_error ("summary: failed to write summary page " +
               compact_log_path (ctx.summary.summary_file));
    return false;
  }
  log_info ("summary: wrote page " +
            compact_log_path (ctx.summary.summary_file));
  if (!write_one_time_startup_page (ctx)) return false;
  return true;
}

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
  if (summary.generate_summary_page &&
      !vault_maintenance_write_summary_page (ctx, true))
    return VaultMaintenancePassResult::failure (
      "failed to write maintenance summary page");
  log_info ("complete");
  return VaultMaintenancePassResult::success ();
}

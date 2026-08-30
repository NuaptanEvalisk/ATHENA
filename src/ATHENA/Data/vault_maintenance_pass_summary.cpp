/******************************************************************************
* MODULE     : vault_maintenance_pass_summary.cpp
* DESCRIPTION: Vault maintenance summary pass
* COPYRIGHT  : (C) 2026  Felix
******************************************************************************/

#include "ATHENA/Data/vault_maintenance_internal.hpp"
#include "ATHENA/Features/athena_features.hpp"

#include "ATHENA/Data/vaultfile_json.hpp"
#include "boot.hpp"
#include "scheme.hpp"

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

static std::string
tm_text (const std::string& text) {
  return tm_escape_text (text);
}

static std::string
tm_verbatim (const std::string& text) {
  return "<verbatim|" + tm_escape_text (text) + ">";
}

static std::string
tm_strong (const std::string& text) {
  return "<strong|" + tm_escape_text (text) + ">";
}

static std::string
tm_hlink (const std::string& label, const std::string& target) {
  return "<hlink|" + tm_escape_text (label) + "|" + tm_escape_text (target) + ">";
}

static std::string
tm_colored (const std::string& color, const std::string& body) {
  return "<with|color|" + color + "|" + body + ">";
}

static std::string
tm_cell (const std::string& body) {
  return "<cell|" + body + ">";
}

static std::string
tm_row (std::initializer_list<std::string> cells) {
  std::string out = "<row";
  for (const std::string& cell : cells)
    out += "|" + tm_cell (cell);
  out += ">";
  return out;
}

static std::string
tm_table (const std::vector<std::string>& rows) {
  std::string out =
    "<tabular|<tformat|<twith|table-width|1par>|"
    "<twith|table-hmode|min>|"
    "<cwith|1|1|1|-1|cell-background|#ececec>|"
    "<cwith|1|1|1|-1|cell-bborder|1ln>|"
    "<cwith|1|-1|1|-1|cell-hyphen|t>|"
    "<cwith|1|-1|1|-1|cell-lsep|0.6em>|"
    "<cwith|1|-1|1|-1|cell-rsep|0.6em>|"
    "<cwith|1|-1|1|-1|cell-tsep|0.35em>|"
    "<cwith|1|-1|1|-1|cell-bsep|0.35em>|<table";
  for (const std::string& row : rows)
    out += "|" + row;
  out += ">>>";
  return out;
}

static std::string
vault_startup_page_target (VaultMaintenanceContext& ctx,
                           std::string& label) {
  AthenaVaultfileInfo info;
  std::string error;
  if (athena_vaultfile_read (ctx.root, info, error) &&
      !trim_copy (info.startup_page).empty ()) {
    label = "Startup page";
    std::string target = trim_copy (info.startup_page);
    if (starts_with (target, "tmfs://") || starts_with (target, "file://"))
      return target;
    fs::path p (target);
    if (p.is_absolute ()) return p.generic_string ();
    return (ctx.root / p).generic_string ();
  }
  label = "Homepage";
  return "tmfs://welcome/home";
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
  AthenaVaultfileInfo info;
  std::string error;
  if (!athena_vaultfile_read (ctx.root, info, error)) {
    log_error ("summary: invalid Vaultfile.json while setting one-time "
               "startup page: " + error);
    return false;
  }

  std::string rel = summary_relative_path (ctx);
  info.one_time_startup_page = rel;

  if (!athena_vaultfile_write (ctx.root, info, error)) {
    log_error ("summary: failed to write Vaultfile.json while setting "
               "one-time startup page: " + error);
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

static std::string
status_label (const VaultMaintenancePassRecord& record) {
  if (record.status == "not-run")
    return tm_colored ("#cc6600", tm_strong ("Did not run"));
  if (record.status == "failed")
    return tm_colored ("#c00000", tm_strong ("Failed"));
  if (record.produced_warning)
    return tm_colored ("#b58900", tm_strong ("Warning"));
  return tm_colored ("#008000", tm_strong ("Success"));
}

static std::string
summary_document_text (VaultMaintenanceContext& ctx, bool success,
                       const std::string& failure_pass,
                       const std::string& failure_message) {
  VaultMaintenanceSummary& summary = ctx.summary;
  std::vector<std::string> overview_rows = {
    tm_row ({tm_strong ("Field"), tm_strong ("Value")}),
    tm_row ({tm_text ("Vault"), tm_verbatim (ctx.vault_name)}),
    tm_row ({tm_text ("Root"), tm_verbatim (ctx.root.string ())}),
    tm_row ({tm_text ("Generated"), tm_verbatim (timestamp_string ())}),
    tm_row ({tm_text ("Summary path"),
             tm_verbatim (summary.summary_file.string ())}),
    tm_row ({tm_text ("Status"),
             success ? tm_colored ("#008000", tm_strong ("Success"))
                     : tm_colored ("#c00000", tm_strong ("Failure"))}),
    tm_row ({tm_text ("Summary retention"),
             tm_verbatim (summary_retention_label (summary.summary_keep_count))}),
    tm_row ({tm_text ("Old summaries purged"),
             tm_verbatim (std::to_string (summary.summaries_purged))})
  };
  if (!success) {
    overview_rows.push_back (
      tm_row ({tm_text ("Failed pass"), tm_verbatim (failure_pass)}));
    overview_rows.push_back (
      tm_row ({tm_text ("Failure"), tm_text (failure_message)}));
  }

  std::vector<std::string> pass_rows = {
    tm_row ({tm_strong ("Pass"), tm_strong ("Description"),
             tm_strong ("Status"), tm_strong ("Message")})
  };
  for (const VaultMaintenancePassRecord& record : ctx.pass_records) {
    pass_rows.push_back (
      tm_row ({tm_verbatim (record.id), tm_text (record.description),
               status_label (record),
               record.message.empty () || record.message == "ok"
                 ? tm_text ("")
                 : tm_text (record.message)}));
  }

  std::vector<std::string> work_rows = {
    tm_row ({tm_strong ("Area"), tm_strong ("Result")}),
    tm_row ({tm_text ("Backup archive"),
             tm_verbatim (summary.backup_archive.string ())}),
    tm_row ({tm_text ("Full backup retention"),
             tm_text (backup_retention_label (summary.backup_limit) +
                      "; purged " + std::to_string (summary.backups_purged) +
                      " old backup(s)")}),
    tm_row ({tm_text ("Pre-save history retention"),
             tm_text (manual_save_retention_label (
                        summary.manual_save_retention_seconds) +
                      "; purged " +
                      std::to_string (summary.manual_save_histories_purged) +
                      " old history folder(s)")}),
    tm_row ({tm_text ("Asset normalization"),
             tm_text ("renamed " + std::to_string (summary.asset_renames) +
                      " asset file(s), updated " +
                      std::to_string (summary.asset_reference_updates) +
                      " asset reference(s)")}),
    tm_row ({tm_text ("Materials"),
             summary.materials_database_present
               ? tm_text (
                   "renamed " +
                   std::to_string (summary.material_attachments_renamed) +
                   " attachment(s), unchanged " +
                   std::to_string (summary.material_attachments_unchanged) +
                   ", missing " +
                   std::to_string (summary.material_attachments_missing) +
                   ", purged " +
                   std::to_string (summary.material_files_purged) +
                   " unreferenced file(s)")
               : tm_text ("no Materials database; skipped")}),
    tm_row ({tm_text ("Missing images"),
             summary.missing_image_scan_enabled
               ? tm_text (
                   "scanned " +
                   std::to_string (summary.missing_image_files_scanned) +
                   " .ath file(s) and " +
                   std::to_string (summary.local_image_references_scanned) +
                   " local image reference(s); missing " +
                   std::to_string (summary.missing_images.size ()))
               : tm_text ("scan skipped")}),
    tm_row ({tm_text ("Health check"),
             tm_text ("scanned " +
                      std::to_string (summary.health_files_scanned) +
                      " .ath file(s), unreadable " +
                      std::to_string (summary.health_files_failed))}),
#if ATHENA_ENABLE_PERSON_SUBSYSTEM
    tm_row ({tm_text ("Person names"),
             tm_text ("wrapped " +
                      std::to_string (summary.person_names_wrapped) +
                      " occurrence(s) in " +
                      std::to_string (summary.person_files_changed) + " of " +
                      std::to_string (summary.person_files_scanned) +
                      " .ath file(s)")}),
#endif
    tm_row ({tm_text ("Anchors"),
             tm_text ("wrapped " +
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
                      std::to_string (summary.anchor_failures))})
  };
  work_rows.push_back (
    tm_row ({tm_text ("Artifacts"),
             tm_text ("examined " +
                      std::to_string (summary.artifact_documents_seen) +
                      " document(s), rebuilt " +
                      std::to_string (summary.artifact_documents_changed) +
                      ", removed " +
                      std::to_string (summary.artifact_documents_deleted) +
                      ", indexed " +
                      std::to_string (summary.artifacts_indexed) +
                      " artifact(s): " +
                      std::to_string (summary.artifact_enunciations) +
                      " enunciation(s), " +
                      std::to_string (summary.artifact_bold_texts) +
                      " bold-text definition(s)" +
                      (summary.artifact_delegation_attempted ?
                        (summary.artifact_delegation_succeeded ?
                          ", delegated range selection succeeded":
                          ", delegated range selection failed"):
                        ", local range selection"))}));
  if (summary.redundant_block_wikilink_removal_enabled)
    work_rows.push_back (
      tm_row ({tm_text ("Redundant block wikilinks"),
               tm_text ("scanned " +
                        std::to_string (
                          summary.redundant_block_wikilinks_scanned) +
                        " block wikilink(s) in " +
                        std::to_string (
                          summary.redundant_wikilink_files_scanned) +
                        " file(s), full radioactive matches " +
                        std::to_string (
                          summary.redundant_wikilink_full_matches) +
                        ", removed " +
                        std::to_string (
                          summary.redundant_wikilinks_removed) +
                        " in " +
                        std::to_string (
                          summary.redundant_wikilink_files_changed) +
                        " changed file(s), unverified targets " +
                        std::to_string (
                          summary.redundant_wikilink_unverified_targets))}));
  else
    work_rows.push_back (
      tm_row ({tm_text ("Redundant block wikilinks"),
               tm_text ("removal disabled")}));
  if (summary.toc_update_enabled)
    work_rows.push_back (
      tm_row ({tm_text ("Tables of contents"),
               tm_text ("found " +
                        std::to_string (summary.toc_files_containing_toc) +
                        " ToC document(s) among " +
                        std::to_string (summary.toc_files_scanned) +
                        " scanned, changed " +
                        std::to_string (summary.toc_files_updated) +
                        ", failures " +
                        std::to_string (summary.toc_failures) +
                        ", workers " +
                        std::to_string (summary.toc_worker_processes))}));
  else
    work_rows.push_back (
      tm_row ({tm_text ("Tables of contents"),
               tm_text ("update disabled")}));
  if (summary.rag_update_enabled) {
    std::string mode= summary.rag_delegation_succeeded
                        ? "delegated"
                        : (summary.rag_local_fallback_used
                             ? "local fallback":
                             (summary.rag_delegation_attempted
                                ? "delegation unavailable": "local"));
    work_rows.push_back (
      tm_row ({tm_text ("Continuous RAG"),
               tm_text ("mode " + mode + ", documents " +
                        std::to_string (summary.rag_documents_before) + " -> " +
                        std::to_string (summary.rag_documents_after) +
                        ", chunks " +
                        std::to_string (summary.rag_chunks_before) + " -> " +
                        std::to_string (summary.rag_chunks_after) +
                        ", result: " + summary.rag_result)}));
  }
  else
    work_rows.push_back (
      tm_row ({tm_text ("Continuous RAG"), tm_text ("update disabled")}));
  if (summary.orphan_collection_enabled)
    work_rows.push_back (
      tm_row ({tm_text ("Orphan assets"),
               tm_text ("collected " +
                        std::to_string (summary.orphan_assets_collected) +
                        " asset(s) into ") +
                 tm_verbatim (summary.orphan_dir.string ())}));
  else
    work_rows.push_back (
      tm_row ({tm_text ("Orphan assets"), tm_text ("collection disabled")}));

  std::vector<std::string> orphan_rows;
  if (!summary.collected_orphans.empty ()) {
    orphan_rows.push_back (
      tm_row ({tm_strong ("Collected orphan"), tm_strong ("Original path")}));
    for (const VaultMaintenanceCollectedOrphan& orphan:
         summary.collected_orphans) {
      std::string collected = orphan.collected_path.string ();
      orphan_rows.push_back (
        tm_row ({tm_hlink (orphan.collected_path.filename ().string (),
                           collected),
                 tm_verbatim (orphan.original_path.string ())}));
    }
  }

  std::vector<std::string> missing_image_rows;
  if (!summary.missing_images.empty ()) {
    missing_image_rows.push_back (
      tm_row ({tm_strong ("Document"), tm_strong ("Image reference"),
               tm_strong ("Resolved path")}));
    for (const VaultMaintenanceMissingImage& missing:
         summary.missing_images) {
      std::error_code ec;
      fs::path relative= fs::relative (missing.document_path, ctx.root, ec);
      std::string document_label=
        !ec && !relative.empty () && !relative.is_absolute ()
          ? relative.generic_string ()
          : missing.document_path.generic_string ();
      missing_image_rows.push_back (
        tm_row ({tm_hlink (document_label,
                           missing.document_path.generic_string ()),
                 tm_verbatim (missing.reference),
                 tm_verbatim (missing.resolved_path.generic_string ())}));
    }
  }

  std::vector<std::string> missing_material_rows;
  if (!summary.missing_material_attachments.empty ()) {
    missing_material_rows.push_back (
      tm_row ({tm_strong ("Missing Material attachment")}));
    for (const fs::path& path: summary.missing_material_attachments)
      missing_material_rows.push_back (
        tm_row ({tm_verbatim (path.generic_string ())}));
  }

  std::string startup_label;
  std::string startup_target = vault_startup_page_target (ctx, startup_label);

  std::ostringstream body;
  body << "  <section|Overview>\n\n";
  body << "  " << tm_table (overview_rows) << "\n\n";

  if (!ctx.warnings.empty ()) {
    std::vector<std::string> warning_rows = {
      tm_row ({tm_strong ("Warnings")})
    };
    for (const std::string& warning : ctx.warnings)
      warning_rows.push_back (
        tm_row ({tm_colored ("#b58900", tm_text (warning))}));
    body << "  <section|Warnings>\n\n";
    body << "  " << tm_table (warning_rows) << "\n\n";
  }

  body << "  <section|Passes>\n\n";
  body << "  " << tm_table (pass_rows) << "\n\n";
  body << "  <section|Work Performed>\n\n";
  body << "  " << tm_table (work_rows) << "\n\n";
  if (!orphan_rows.empty ()) {
    body << "  <section|Collected Orphans>\n\n";
    body << "  " << tm_table (orphan_rows) << "\n\n";
  }
  if (!missing_image_rows.empty ()) {
    body << "  <section|Missing Images>\n\n";
    body << "  " << tm_table (missing_image_rows) << "\n\n";
  }
  if (!missing_material_rows.empty ()) {
    body << "  <section|Missing Material Attachments>\n\n";
    body << "  " << tm_table (missing_material_rows) << "\n\n";
  }
  body << "  <section|Next Step>\n\n";
  body << "  <cardlink|" << tm_escape_text (startup_label)
       << "|" << tm_escape_text (startup_target) << ">\n\n";

  std::string font = trim_copy (
    tm_to_std (get_preference ("vault preferred font", "")));

  std::ostringstream out;
  out << "<TeXmacs|2.1.4>\n\n";
  out << "<style|generic>\n\n";
  out << "<\\body>\n";
  out << "<doc-data|<doc-title|Vault Maintenance Summary>>\n\n";
  if (!font.empty ()) {
    out << "<\\with|font|" << tm_escape_text (font) << ">\n";
    out << body.str ();
    out << "</with>\n";
  }
  else out << body.str ();
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
  log_info ("summary: renamed " + std::to_string (summary.asset_renames) +
            " asset file(s), updated " +
            std::to_string (summary.asset_reference_updates) +
            " asset reference(s)");
  if (summary.materials_database_present)
    log_info ("summary: Materials renamed " +
              std::to_string (summary.material_attachments_renamed) +
              " attachment(s), unchanged " +
              std::to_string (summary.material_attachments_unchanged) +
              ", missing " +
              std::to_string (summary.material_attachments_missing) +
              ", purged " +
              std::to_string (summary.material_files_purged) +
              " unreferenced file(s)");
  else log_info ("summary: no Materials database; maintenance skipped");
  if (summary.missing_image_scan_enabled)
    log_info ("summary: missing-image scan checked " +
              std::to_string (summary.local_image_references_scanned) +
              " local image reference(s) in " +
              std::to_string (summary.missing_image_files_scanned) +
              " .ath file(s); missing " +
              std::to_string (summary.missing_images.size ()));
  else log_info ("summary: missing-image scan skipped");
  log_info ("summary: health-checked " +
            std::to_string (summary.health_files_scanned) +
            " .ath file(s); unreadable " +
            std::to_string (summary.health_files_failed));
#if ATHENA_ENABLE_PERSON_SUBSYSTEM
  log_info ("summary: wrapped " +
            std::to_string (summary.person_names_wrapped) +
            " person-name occurrence(s) in " +
            std::to_string (summary.person_files_changed) + " of " +
            std::to_string (summary.person_files_scanned) + " .ath file(s)");
#endif
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
  log_info ("summary: artifacts examined " +
            std::to_string (summary.artifact_documents_seen) +
            " document(s), rebuilt " +
            std::to_string (summary.artifact_documents_changed) +
            ", removed " +
            std::to_string (summary.artifact_documents_deleted) +
            ", indexed " +
            std::to_string (summary.artifacts_indexed) + " artifact(s): " +
            std::to_string (summary.artifact_enunciations) +
            " enunciation(s), " +
            std::to_string (summary.artifact_bold_texts) +
            " bold-text definition(s), range selection " +
            (summary.artifact_delegation_attempted ?
              (summary.artifact_delegation_succeeded ? "delegated":
                                                        "delegation failed"):
              "local"));
  if (summary.redundant_block_wikilink_removal_enabled)
    log_info ("summary: redundant block wikilinks scanned " +
              std::to_string (
                summary.redundant_block_wikilinks_scanned) +
              ", full radioactive matches " +
              std::to_string (summary.redundant_wikilink_full_matches) +
              ", removed " +
              std::to_string (summary.redundant_wikilinks_removed) +
              " in " +
              std::to_string (summary.redundant_wikilink_files_changed) +
              " changed file(s), unverified targets " +
              std::to_string (
                summary.redundant_wikilink_unverified_targets));
  else log_info ("summary: redundant block wikilink removal disabled");
  if (summary.toc_update_enabled)
    log_info ("summary: updated tables of contents in " +
              std::to_string (summary.toc_files_updated) + " of " +
              std::to_string (summary.toc_files_containing_toc) +
              " matching document(s), scanned " +
              std::to_string (summary.toc_files_scanned) +
              ", failures " + std::to_string (summary.toc_failures) +
              ", worker processes " +
              std::to_string (summary.toc_worker_processes));
  else log_info ("summary: table-of-contents update disabled");
  if (summary.rag_update_enabled) {
    std::string mode= summary.rag_delegation_succeeded
                        ? "delegated"
                        : (summary.rag_local_fallback_used
                             ? "local fallback":
                             (summary.rag_delegation_attempted
                                ? "delegation unavailable": "local"));
    log_info ("summary: Continuous RAG mode " + mode +
              ", documents " +
              std::to_string (summary.rag_documents_before) + " -> " +
              std::to_string (summary.rag_documents_after) + ", chunks " +
              std::to_string (summary.rag_chunks_before) + " -> " +
              std::to_string (summary.rag_chunks_after) + ", result: " +
              summary.rag_result);
  }
  else log_info ("summary: Continuous RAG update disabled");
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

/******************************************************************************
* MODULE     : vault_maintenance_pass_anchors.cpp
* DESCRIPTION: Vault maintenance enunciation anchor pass
* COPYRIGHT  : (C) 2026  Felix
******************************************************************************/

#include "ATHENA/Data/vault_maintenance_internal.hpp"

#include "Database/database.hpp"
#include "scheme.hpp"
#include "url.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static std::vector<std::string>
split_tabs (const std::string& text) {
  std::vector<std::string> parts;
  size_t begin = 0;
  for (size_t i=0; i<=text.size (); i++) {
    if (i == text.size () || text[i] == '\t') {
      parts.push_back (text.substr (begin, i - begin));
      begin = i + 1;
    }
  }
  return parts;
}

static size_t
parse_count (const std::string& text) {
  try {
    size_t pos = 0;
    unsigned long long value = std::stoull (text, &pos);
    if (pos == text.size ()) return (size_t) value;
  }
  catch (...) {}
  return 0;
}

static size_t
rewrite_map_anchor_references (const fs::path& root, const fs::path& doc,
                               const std::string& renames) {
  if (renames.empty ()) return 0;

  fs::path db_path = root / "map.tmdb";
  if (!fs::exists (db_path)) return 0;

  std::string rel_path = doc.lexically_relative (root).generic_string ();
  try {
    std::string result = tm_to_std (as_string (
      call ("vault-anchor-maintenance-rewrite-map",
            object (url_system (std_to_tm (db_path.string ()))),
            object (std_to_tm (rel_path)),
            object (std_to_tm (renames)))));
    size_t changed = parse_count (result);
    if (changed != 0) sync_databases ();
    return changed;
  }
  catch (...) {
    return 0;
  }
}

static bool
anchor_structures_in_vault (const fs::path& root, VaultMaintenanceSummary& summary) {
  std::vector<fs::path> docs = scan_ath_documents (root);
  summary.anchor_files_scanned = docs.size ();
  log_info ("anchor structures: scanning " + std::to_string (docs.size ()) +
            " .ath files");

  for (size_t i=0; i<docs.size (); i++) {
    print_progress (i + 1, docs.size (), "Anchoring structures",
                    docs[i].filename ().string ());
    std::string result;
    try {
      result = tm_to_std (as_string (
        call ("vault-anchor-maintenance-file",
              object (url_system (std_to_tm (docs[i].string ()))))));
    }
    catch (...) {
      finish_progress ();
      log_error ("anchor structures: Scheme failure for " +
                 compact_log_path (docs[i]));
      summary.anchor_failures++;
      return false;
    }

    std::vector<std::string> parts = split_tabs (result);
    if (parts.size () < 7) {
      finish_progress ();
      log_error ("anchor structures: malformed result for " +
                 compact_log_path (docs[i]));
      summary.anchor_failures++;
      return false;
    }

    size_t wrapped = parse_count (parts[1]);
    size_t removed = parse_count (parts[2]);
    size_t headings = parse_count (parts[3]);
    size_t updated = parse_count (parts[4]);
    bool changed = parts[5] == "1";
    std::string renames = parts.size () >= 8 ? parts[7] : "";
    if (parts[0] == "ok") {
      summary.anchor_enunciations_wrapped += wrapped;
      summary.anchor_headings_added += headings;
      summary.anchor_stale_structures_updated += updated;
      summary.anchor_dead_pairs_removed += removed;
      if (changed) {
        summary.anchor_files_changed++;
        size_t map_updates =
          rewrite_map_anchor_references (root, docs[i], renames);
        summary.anchor_map_references_updated += map_updates;
        log_info ("anchor structures: updated " + compact_log_path (docs[i]) +
                  " (wrapped " + std::to_string (wrapped) +
                  ", headings " + std::to_string (headings) +
                  ", updated " + std::to_string (updated) +
                  ", map references " + std::to_string (map_updates) +
                  ", removed " + std::to_string (removed) +
                  " dead pair(s))");
      }
    }
    else {
      finish_progress ();
      summary.anchor_failures++;
      log_error ("anchor structures: failed for " + compact_log_path (docs[i]) +
                 (parts[6].empty () ? std::string () : (": " + parts[6])));
      return false;
    }
  }
  finish_progress ();

  log_info ("anchor structures: changed " +
            std::to_string (summary.anchor_files_changed) + " file(s), wrapped " +
            std::to_string (summary.anchor_enunciations_wrapped) +
            " enunciation(s), added " +
            std::to_string (summary.anchor_headings_added) +
            " heading anchor(s), updated " +
            std::to_string (summary.anchor_stale_structures_updated) +
            " stale anchor structure(s), rewrote " +
            std::to_string (summary.anchor_map_references_updated) +
            " map reference(s), removed " +
            std::to_string (summary.anchor_dead_pairs_removed) +
            " dead anchor pair(s)");
  if (summary.anchor_failures != 0)
    log_info ("anchor structures: " +
              std::to_string (summary.anchor_failures) + " file(s) failed");
  return true;
}


VaultMaintenancePassResult
vault_maintenance_pass_anchor_enunciations (VaultMaintenanceContext& ctx) {
  if (anchor_structures_in_vault (ctx.root, ctx.summary))
    return VaultMaintenancePassResult::success ();
  return VaultMaintenancePassResult::failure ("structural anchoring failed");
}

/******************************************************************************
* MODULE     : vault_maintenance_pass_anchors.cpp
* DESCRIPTION: Vault maintenance enunciation anchor pass
* COPYRIGHT  : (C) 2026  Felix
******************************************************************************/

#include "ATHENA/Data/vault_maintenance_internal.hpp"

#include "scheme.hpp"

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
      log_error ("anchor structures: Scheme failure for " + docs[i].string ());
      summary.anchor_failures++;
      return false;
    }

    std::vector<std::string> parts = split_tabs (result);
    if (parts.size () < 6) {
      finish_progress ();
      log_error ("anchor structures: malformed result for " + docs[i].string ());
      summary.anchor_failures++;
      return false;
    }

    size_t wrapped = parse_count (parts[1]);
    size_t removed = parse_count (parts[2]);
    size_t headings = parse_count (parts[3]);
    bool changed = parts[4] == "1";
    if (parts[0] == "ok") {
      summary.anchor_enunciations_wrapped += wrapped;
      summary.anchor_headings_added += headings;
      summary.anchor_dead_pairs_removed += removed;
      if (changed) {
        summary.anchor_files_changed++;
        log_info ("anchor structures: updated " + docs[i].string () +
                  " (wrapped " + std::to_string (wrapped) +
                  ", headings " + std::to_string (headings) +
                  ", removed " + std::to_string (removed) +
                  " dead pair(s))");
      }
    }
    else {
      finish_progress ();
      summary.anchor_failures++;
      log_error ("anchor structures: failed for " + docs[i].string () +
                 (parts[5].empty () ? std::string () : (": " + parts[5])));
      return false;
    }
  }
  finish_progress ();

  log_info ("anchor structures: changed " +
            std::to_string (summary.anchor_files_changed) + " file(s), wrapped " +
            std::to_string (summary.anchor_enunciations_wrapped) +
            " enunciation(s), added " +
            std::to_string (summary.anchor_headings_added) +
            " heading anchor(s), removed " +
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

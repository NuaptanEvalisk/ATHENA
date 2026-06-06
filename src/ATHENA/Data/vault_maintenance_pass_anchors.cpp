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
anchor_enunciations_in_vault (const fs::path& root, VaultMaintenanceSummary& summary) {
  std::vector<fs::path> docs = scan_ath_documents (root);
  summary.anchor_files_scanned = docs.size ();
  log_info ("anchor enunciations: scanning " + std::to_string (docs.size ()) +
            " .ath files");

  for (size_t i=0; i<docs.size (); i++) {
    print_progress (i + 1, docs.size (), "Anchoring enunciations",
                    docs[i].filename ().string ());
    std::string result;
    try {
      result = tm_to_std (as_string (
        call ("vault-anchor-maintenance-file",
              object (url_system (std_to_tm (docs[i].string ()))))));
    }
    catch (...) {
      finish_progress ();
      log_error ("anchor enunciations: Scheme failure for " + docs[i].string ());
      summary.anchor_failures++;
      return false;
    }

    std::vector<std::string> parts = split_tabs (result);
    if (parts.size () < 5) {
      finish_progress ();
      log_error ("anchor enunciations: malformed result for " + docs[i].string ());
      summary.anchor_failures++;
      return false;
    }

    size_t wrapped = parse_count (parts[1]);
    size_t removed = parse_count (parts[2]);
    bool changed = parts[3] == "1";
    if (parts[0] == "ok") {
      summary.anchor_enunciations_wrapped += wrapped;
      summary.anchor_dead_pairs_removed += removed;
      if (changed) {
        summary.anchor_files_changed++;
        log_info ("anchor enunciations: updated " + docs[i].string () +
                  " (wrapped " + std::to_string (wrapped) +
                  ", removed " + std::to_string (removed) +
                  " dead pair(s))");
      }
    }
    else {
      finish_progress ();
      summary.anchor_failures++;
      log_error ("anchor enunciations: failed for " + docs[i].string () +
                 (parts[4].empty () ? std::string () : (": " + parts[4])));
      return false;
    }
  }
  finish_progress ();

  log_info ("anchor enunciations: changed " +
            std::to_string (summary.anchor_files_changed) + " file(s), wrapped " +
            std::to_string (summary.anchor_enunciations_wrapped) +
            " enunciation(s), removed " +
            std::to_string (summary.anchor_dead_pairs_removed) +
            " dead anchor pair(s)");
  if (summary.anchor_failures != 0)
    log_info ("anchor enunciations: " +
              std::to_string (summary.anchor_failures) + " file(s) failed");
  return true;
}


VaultMaintenancePassResult
vault_maintenance_pass_anchor_enunciations (VaultMaintenanceContext& ctx) {
  if (anchor_enunciations_in_vault (ctx.root, ctx.summary))
    return VaultMaintenancePassResult::success ();
  return VaultMaintenancePassResult::failure ("enunciation anchoring failed");
}

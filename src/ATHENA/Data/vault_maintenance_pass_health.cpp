/******************************************************************************
* MODULE     : vault_maintenance_pass_health.cpp
* DESCRIPTION: Vault maintenance document health check pass
* COPYRIGHT  : (C) 2026  Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "ATHENA/Data/vault_maintenance_internal.hpp"

#include "convert.hpp"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

static bool
is_legible_ath_file (const fs::path& path, std::string& reason) {
  std::string text;
  if (!read_file_bytes (path, text)) {
    reason = "failed to read";
    return false;
  }

  try {
    tree doc = texmacs_document_to_tree (std_to_tm (text));
    if (is_func (doc, _ERROR)) {
      reason = "malformed ATHENA document";
      return false;
    }
    return true;
  }
  catch (...) {
    reason = "parser exception";
    return false;
  }
}

VaultMaintenancePassResult
vault_maintenance_pass_health_check (VaultMaintenanceContext& ctx) {
  std::vector<fs::path> docs = scan_ath_documents (ctx.root);
  ctx.summary.health_files_scanned = docs.size ();
  ctx.summary.health_files_failed = 0;

  log_info ("health check: scanning " + std::to_string (docs.size ()) +
            " .ath file(s)");

  std::vector<std::pair<fs::path, std::string>> malformed;
  for (size_t i=0; i<docs.size (); i++) {
    print_progress (i + 1, docs.size (), "Checking documents",
                    docs[i].filename ().string ());
    std::string reason;
    if (!is_legible_ath_file (docs[i], reason))
      malformed.push_back ({docs[i], reason});
  }
  finish_progress ();

  ctx.summary.health_files_failed = malformed.size ();
  if (!malformed.empty ()) {
    log_error ("health check: found " + std::to_string (malformed.size ()) +
               " malformed .ath file(s)");
    for (const auto& entry : malformed)
      log_error ("health check: malformed file: " + entry.first.string () +
                 " (" + entry.second + ")");
    return VaultMaintenancePassResult::failure (
      "malformed ATHENA documents: " + std::to_string (malformed.size ()));
  }

  log_info ("health check: all " + std::to_string (docs.size ()) +
            " .ath file(s) are legible");
  return VaultMaintenancePassResult::success ();
}

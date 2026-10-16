/******************************************************************************
* MODULE     : vault_maintenance_pass_persons.cpp
* DESCRIPTION: Vault maintenance person-name normalization pass
* COPYRIGHT  : (C) 2026  Nuaptan
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "ATHENA/Data/person_names.hpp"
#include "ATHENA/Data/vault_maintenance_internal.hpp"

#include "convert.hpp"

#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

tree
normalize_person_document (tree document, int& wrapped,
                           const std::vector<string>& trusted_names) {
  if (is_func (document, DOCUMENT)) {
    for (int i=0; i<N(document); i++) {
      int body_index= -1;
      if (is_compound (document[i], "body", 1))
        body_index= 0;
      else if ((L(document[i]) == EXPAND || L(document[i]) == APPLY) &&
               N(document[i]) == 2 && document[i][0] == "body")
        body_index= 1;
      if (body_index < 0) continue;
      tree normalized= copy (document);
      normalized[i][body_index]= athena_normalize_person_names (
        document[i][body_index], wrapped, trusted_names);
      return normalized;
    }
  }
  return athena_normalize_person_names (document, wrapped, trusted_names);
}

std::vector<string>
learn_vault_person_names (const std::vector<MaintenanceDocument>& documents) {
  std::set<string> names;
  for (const auto& document: documents) {
    const auto local= athena_collect_person_names (document.document);
    names.insert (local.begin (), local.end ());
  }
  return std::vector<string> (names.begin (), names.end ());
}

} // namespace

VaultMaintenancePassResult
vault_maintenance_pass_normalize_person_names (VaultMaintenanceContext& ctx) {
  std::vector<fs::path> documents= scan_ath_documents (ctx.root);
  ctx.summary.person_files_scanned= documents.size ();
  std::vector<MaintenanceDocument> sources (documents.size ());
  std::vector<std::string> outputs (documents.size ());
  std::vector<int> counts (documents.size (), 0);
  std::string error;
  for (size_t i=0; i<documents.size (); ++i)
    if (!read_maintenance_document (documents[i], sources[i], error))
      return VaultMaintenancePassResult::failure (error);
  const auto vault_names= learn_vault_person_names (sources);

  for (size_t i=0; i<documents.size (); i++) {
    const fs::path& document= documents[i];
    print_progress (i + 1, documents.size (), "Normalizing person names",
                    document.filename ().string ());
    int wrapped= 0;
    tree normalized= normalize_person_document (sources[i].document, wrapped, vault_names);
    if (wrapped == 0) continue;
    if (!serialize_maintenance_document (sources[i], normalized, outputs[i], error)) {
      finish_progress ();
      return VaultMaintenancePassResult::failure (
        error);
    }
    counts[i]= wrapped;
  }
  finish_progress ();
  for (const auto& source: sources)
    if (!maintenance_document_current (source, error))
      return VaultMaintenancePassResult::failure (error);
  for (size_t i=0; i<sources.size (); ++i) {
    if (counts[i] == 0) continue;
    if (!commit_maintenance_document (sources[i], outputs[i], error))
      return VaultMaintenancePassResult::failure (error);
    ctx.summary.person_files_changed++;
    ctx.summary.person_names_wrapped += (size_t) counts[i];
  }

  log_info ("person names: wrapped " +
            std::to_string (ctx.summary.person_names_wrapped) +
            " occurrence(s) in " +
            std::to_string (ctx.summary.person_files_changed) + " of " +
            std::to_string (ctx.summary.person_files_scanned) + " file(s)");
  return VaultMaintenancePassResult::success ();
}

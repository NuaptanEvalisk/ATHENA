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

bool
read_person_document (const fs::path& path, tree& document) {
  std::string source;
  if (!read_file_bytes (path, source)) return false;
  try { document= texmacs_document_to_tree (std_to_tm (source)); }
  catch (...) { document= tree (_ERROR, "parse failed"); }
  return !is_func (document, _ERROR);
}

bool
write_person_document (const fs::path& path, tree document) {
  string serialized_tm= tree_to_texmacs (document);
  tree validation;
  try { validation= texmacs_document_to_tree (serialized_tm); }
  catch (...) { validation= tree (_ERROR, "parse failed"); }
  if (is_func (validation, _ERROR)) return false;
  std::string serialized (as_charp (serialized_tm),
                          (size_t) N(serialized_tm));
  return write_file_bytes (path, serialized);
}

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
learn_vault_person_names (const std::vector<fs::path>& documents) {
  std::set<string> names;
  for (const fs::path& document: documents) {
    std::string source;
    if (!read_file_bytes (document, source) ||
        source.find ("<person|") == std::string::npos)
      continue;
    tree parsed;
    if (read_person_document (document, parsed)) {
      std::vector<string> local= athena_collect_person_names (parsed);
      names.insert (local.begin (), local.end ());
    }
  }
  return std::vector<string> (names.begin (), names.end ());
}

} // namespace

VaultMaintenancePassResult
vault_maintenance_pass_normalize_person_names (VaultMaintenanceContext& ctx) {
  std::vector<fs::path> documents= scan_ath_documents (ctx.root);
  ctx.summary.person_files_scanned= documents.size ();
  std::vector<string> vault_names= learn_vault_person_names (documents);

  for (size_t i=0; i<documents.size (); i++) {
    const fs::path& document= documents[i];
    print_progress (i + 1, documents.size (), "Normalizing person names",
                    document.filename ().string ());
    tree parsed;
    if (!read_person_document (document, parsed)) {
      finish_progress ();
      return VaultMaintenancePassResult::failure (
        "could not parse " + compact_log_path (document));
    }
    int wrapped= 0;
    tree normalized= normalize_person_document (parsed, wrapped, vault_names);
    if (wrapped == 0) continue;
    if (!write_person_document (document, normalized)) {
      finish_progress ();
      return VaultMaintenancePassResult::failure (
        "could not write " + compact_log_path (document));
    }
    ctx.summary.person_files_changed++;
    ctx.summary.person_names_wrapped += (size_t) wrapped;
  }
  finish_progress ();

  log_info ("person names: wrapped " +
            std::to_string (ctx.summary.person_names_wrapped) +
            " occurrence(s) in " +
            std::to_string (ctx.summary.person_files_changed) + " of " +
            std::to_string (ctx.summary.person_files_scanned) + " file(s)");
  return VaultMaintenancePassResult::success ();
}

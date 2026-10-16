/******************************************************************************
* MODULE     : vault_maintenance_pass_evaluation_bars.cpp
* DESCRIPTION: Opt-in evaluation-bar conversion of vault ATHENA documents
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include "ATHENA/Data/evaluation_bars.hpp"
#include "ATHENA/Data/vault_maintenance_internal.hpp"
#include "Data/Convert/Xml/document_file_codec.hpp"
#include "convert.hpp"
#include "drd_std.hpp"
#include "tree_analyze.hpp"

namespace {

struct Rewrite {
  MaintenanceDocument source;
  std::string replacement;
};

} // namespace

VaultMaintenancePassResult
vault_maintenance_pass_promote_evaluation_bars (VaultMaintenanceContext& ctx) {
  auto paths= scan_ath_documents (ctx.root);
  std::vector<Rewrite> rewrites;
  int promoted= 0;
  // Validate the entire input set before replacing any document. This pass
  // runs in the headless maintenance process, never on a live BufferActor tree.
  for (size_t i=0; i<paths.size (); ++i) {
    print_progress (i + 1, paths.size (), "Promoting evaluation bars",
                    paths[i].filename ().string ());
    MaintenanceDocument source;
    std::string error;
    if (!read_maintenance_document (paths[i], source, error)) {
      finish_progress ();
      return VaultMaintenancePassResult::failure (
        error);
    }
    const tree document= source.document;
    int body_index= -1;
    if (is_func (document, DOCUMENT))
      for (int j=0; j<N(document); ++j)
        if (is_compound (document[j], "body", 1)) body_index= j;
    if (body_index < 0) {
      finish_progress ();
      return VaultMaintenancePassResult::failure (
        "could not parse document body: " + paths[i].string ());
    }
    with_drd scope (get_document_drd (document));
    string mode= "text";
    tree initial= extract (document, "initial");
    if (is_func (initial, COLLECTION))
      for (int j=0; j<N(initial); ++j)
        if (is_func (initial[j], ASSOCIATE, 2) && initial[j][0] == "mode" &&
            is_atomic (initial[j][1])) mode= initial[j][1]->label;
    int count= 0;
    tree body;
    try {
      body= athena_promote_evaluation_bars (
        document[body_index][0], count, mode,
        source.format == athena::document::document_source_format::xml_v3 ?
          athena::document::xml_format_version::v3 : athena::document::xml_format_version::v2);
    }
    catch (const std::exception& e) {
      finish_progress ();
      return VaultMaintenancePassResult::failure (paths[i].string () + ": " + e.what ());
    }
    if (count == 0) continue;
    tree rewritten= copy (document);
    rewritten[body_index][0]= body;
    std::string serialized;
    if (!serialize_maintenance_document (source, rewritten, serialized, error)) {
      finish_progress ();
      return VaultMaintenancePassResult::failure (
        error);
    }
    rewrites.push_back ({std::move (source), std::move (serialized)});
    promoted += count;
  }
  finish_progress ();

  std::string error;
  for (const auto& rewrite: rewrites)
    if (!maintenance_document_current (rewrite.source, error))
      return VaultMaintenancePassResult::failure (error);
  for (const auto& rewrite: rewrites)
    if (!commit_maintenance_document (rewrite.source, rewrite.replacement, error))
      return VaultMaintenancePassResult::failure (error);
  return VaultMaintenancePassResult::success (
    "promoted " + std::to_string (promoted) + " bar(s) in " +
    std::to_string (rewrites.size ()) + " of " +
    std::to_string (paths.size ()) + " ATHENA document(s)");
}

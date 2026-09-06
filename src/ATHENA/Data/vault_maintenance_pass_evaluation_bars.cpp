/******************************************************************************
* MODULE     : vault_maintenance_pass_evaluation_bars.cpp
* DESCRIPTION: Opt-in evaluation-bar conversion of vault ATHENA documents
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include "ATHENA/Data/evaluation_bars.hpp"
#include "ATHENA/Data/vault_maintenance_internal.hpp"
#include "convert.hpp"
#include "drd_std.hpp"
#include "tree_analyze.hpp"

#include <QSaveFile>

namespace {

struct Rewrite {
  std::filesystem::path path;
  std::string source;
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
    std::string source;
    if (!read_file_bytes (paths[i], source)) {
      finish_progress ();
      return VaultMaintenancePassResult::failure (
        "could not read " + paths[i].string ());
    }
    tree document;
    try { document= texmacs_document_to_tree (std_to_tm (source)); }
    catch (...) { document= tree (_ERROR, "parse failed"); }
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
    tree body= athena_promote_evaluation_bars (
      document[body_index][0], count, mode);
    if (count == 0) continue;
    tree rewritten= copy (document);
    rewritten[body_index][0]= body;
    string serialized= tree_to_texmacs (rewritten);
    tree validation= texmacs_document_to_tree (serialized);
    if (is_func (validation, _ERROR) || extract (validation, "body") != body) {
      finish_progress ();
      return VaultMaintenancePassResult::failure (
        "evaluation-bar rewrite failed round-trip validation: " +
        paths[i].string ());
    }
    rewrites.push_back ({paths[i], std::move (source), tm_to_std (serialized)});
    promoted += count;
  }
  finish_progress ();

  for (const auto& rewrite: rewrites) {
    std::string current;
    if (!read_file_bytes (rewrite.path, current) || current != rewrite.source)
      return VaultMaintenancePassResult::failure (
        "document changed during maintenance: " + rewrite.path.string ());
    QSaveFile file (QString::fromStdString (rewrite.path.string ()));
    if (!file.open (QIODevice::WriteOnly) ||
        file.write (rewrite.replacement.data (), rewrite.replacement.size ()) !=
          qint64 (rewrite.replacement.size ()) || !file.commit ())
      return VaultMaintenancePassResult::failure (
        "could not atomically replace " + rewrite.path.string ());
  }
  return VaultMaintenancePassResult::success (
    "promoted " + std::to_string (promoted) + " bar(s) in " +
    std::to_string (rewrites.size ()) + " of " +
    std::to_string (paths.size ()) + " ATHENA document(s)");
}

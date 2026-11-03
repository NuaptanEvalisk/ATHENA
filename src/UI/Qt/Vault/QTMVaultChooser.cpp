/******************************************************************************
* MODULE     : QTMVaultChooser.cpp
* DESCRIPTION: Qt vault link chooser facade
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "UI/Qt/Vault/QTMVaultChooser.hpp"
#include "UI/Qt/Vault/QTMVaultLinkFocus.hpp"
#include "UI/Qt/Vault/QTMVaultTransclusionWizard.hpp"
#include "UI/Qt/Vault/QTMVaultWikilinkWizard.hpp"
#include "App/Runtime/actor_transport.hpp"
#include "App/Runtime/buffer_actor.hpp"
#include "App/Runtime/execution_context.hpp"
#include "vault.hpp"
#include "Document/Model/compound_edit_batch.hpp"
#include "UI/Qt/Toolkit/qt_utilities.hpp"
#include <QApplication>
#include <memory>
#include <stdexcept>

namespace {

tree
runVaultChooser (bool transcludeMode) {
  if (!vault_active ()) return UNINIT;
  TeXmacsFocusSnapshot focusSnapshot= capture_texmacs_focus_snapshot ();
  tree result= transcludeMode ?
    qtm_vault_choose_transclusion (QApplication::activeWindow ()) :
    qtm_vault_choose_wikilink (QApplication::activeWindow ());
  restore_texmacs_focus_snapshot_later (focusSnapshot);
  return result;
}

} // namespace

void
vault_choose_link_native (bool transcludeMode, std::function<void(tree)> completion) {
  const auto* owner= current_execution_context ();
  if (!owner || !owner->editor)
    throw std::runtime_error ("Vault link insertion requires an owning editor");
  const auto actor= owner->actor_id;
  const auto view= owner->view_id;
  qt_post_to_main_thread ([actor, view, transcludeMode, completion= std::move (completion)] {
    tree result= runVaultChooser (transcludeMode);
    if (result == UNINIT) return;
    struct payload {
      athena_blob_id id;
      explicit payload (tree t): id (actor_tree_registry::instance ().store (std::move (t))) {}
      ~payload () { actor_tree_registry::instance ().discard (id); }
    };
    auto value= std::make_shared<payload> (std::move (result));
    athena::avd::submit_source_task (actor, view,
      [value, completion] (editor_rep&) {
        completion (actor_tree_registry::instance ().take (value->id));
      }, [] (std::string error) {
        if (!error.empty ()) std_warning << "Vault link insertion: " << string (error.c_str ()) << LF;
      });
  });
}

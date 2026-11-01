/******************************************************************************
* MODULE     : QTMVaultChooser.cpp
* DESCRIPTION: Qt vault link chooser facade
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMVaultChooser.hpp"
#include "QTMVaultLinkFocus.hpp"
#include "QTMVaultTransclusionWizard.hpp"
#include "QTMVaultWikilinkWizard.hpp"
#include "actor_transport.hpp"
#include "buffer_actor.hpp"
#include "guile_tm.hpp"
#include "object.hpp"
#include "scheme_execution_context.hpp"
#include "vault.hpp"
#include "ATHENA/Data/compound_edit_batch.hpp"
#include "qt_utilities.hpp"
#include <QApplication>
#include <QCoreApplication>
#include <QMetaObject>
#include <QThread>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

namespace {

struct VaultChooserRequest {
  bool transcludeMode= false;
  athena_actor_id actorId= ATHENA_NO_ACTOR;
  athena_view_id viewId= ATHENA_NO_VIEW;
  SchemeCapabilitySet capabilities= SCHEME_CAPABILITY_NONE;
  athena_scheme_handle_id completion= ATHENA_NO_SCHEME_HANDLE;

  ~VaultChooserRequest () {
    scheme_command_handle_release (completion);
  }
};

std::mutex vaultChooserRequestMutex;
std::unordered_map<athena_resource_id, std::unique_ptr<VaultChooserRequest>>
  vaultChooserRequests;
athena_resource_id nextVaultChooserRequestId= 1;

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

athena_resource_id
registerVaultChooserRequest (bool transcludeMode, object completion,
                             const SchemeExecutionContext& context) {
  auto request= std::make_unique<VaultChooserRequest> ();
  request->transcludeMode= transcludeMode;
  request->actorId= context.actor_id;
  request->viewId= context.view_id;
  request->capabilities= context.capabilities;
  request->completion=
    scheme_command_handle_acquire (object_to_tmscm (completion));

  std::lock_guard<std::mutex> guard (vaultChooserRequestMutex);
  athena_resource_id id= nextVaultChooserRequestId++;
  if (id == ATHENA_NO_RESOURCE) id= nextVaultChooserRequestId++;
  vaultChooserRequests.emplace (id, std::move (request));
  return id;
}

std::unique_ptr<VaultChooserRequest>
takeVaultChooserRequest (athena_resource_id id) {
  std::lock_guard<std::mutex> guard (vaultChooserRequestMutex);
  auto found= vaultChooserRequests.find (id);
  if (found == vaultChooserRequests.end ()) return nullptr;
  std::unique_ptr<VaultChooserRequest> request= std::move (found->second);
  vaultChooserRequests.erase (found);
  return request;
}

void
executeVaultChooserRequest (athena_resource_id id) {
  std::unique_ptr<VaultChooserRequest> request= takeVaultChooserRequest (id);
  if (request == nullptr) return;

  tree result= runVaultChooser (request->transcludeMode);
  athena_blob_id resultId=
    actor_tree_registry::instance ().store (std::move (result));
  actor_command_ticket ticket= buffer_actor::submit_to (
    request->actorId, actor_command_kind::invoke_scheme_handle_tree,
    request->viewId, resultId, ATHENA_NO_BLOB, request->capabilities,
    request->completion);
  if (ticket)
    request->completion= ATHENA_NO_SCHEME_HANDLE;
  else
    (void) actor_tree_registry::instance ().discard (resultId);
}

} // namespace

void
vault_choose_link (bool transcludeMode, object completion) {
  QCoreApplication* app= QCoreApplication::instance ();
  if (app == nullptr) return;

  const SchemeExecutionContext* context= current_scheme_execution_context ();
  if (context == nullptr || context->actor_id == ATHENA_NO_ACTOR ||
      context->view_id == ATHENA_NO_VIEW) {
    if (QThread::currentThread () == app->thread ())
      (void) call (completion, object (runVaultChooser (transcludeMode)));
    return;
  }

  athena_resource_id requestId=
    registerVaultChooserRequest (transcludeMode, completion, *context);
  bool invoked= QMetaObject::invokeMethod (
    app, [requestId] () { executeVaultChooserRequest (requestId); },
    Qt::QueuedConnection);
  if (!invoked) {
    (void) takeVaultChooserRequest (requestId);
  }
}

void
vault_choose_link_native (bool transcludeMode, std::function<void(tree)> completion) {
  const auto* owner= current_scheme_execution_context ();
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

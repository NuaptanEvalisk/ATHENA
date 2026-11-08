/******************************************************************************
* MODULE     : buffer_actor.cpp
* DESCRIPTION: Per-buffer execution owner and ID-only command mailbox
* COPYRIGHT  : (C) 2026  Nuaptan F. Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "cursor_history.hpp"
#include "Infrastructure/VirtualFS/tmfs.hpp"
#include "App/Runtime/buffer_actor.hpp"
#include "native_math_edit.hpp"
#include "view_zoom.hpp"
#include "Document/Model/buffer_export.hpp"
#include "Document/Persistence/document_persistence.hpp"
#include "App/Frontend/document_save_frontend.hpp"
#include "App/Frontend/application_lifecycle.hpp"
#include "Document/Convert/Xml/athena_document_xml.hpp"
#include "Document/NodeContracts/builtin_contracts.hpp"
#include "Domains/References/node_reference.hpp"
#include "App/Runtime/actor_lifetime.hpp"
#include "Infrastructure/Misc/crash_report.hpp"

#include "App/Runtime/actor_ui_bridge.hpp"
#include "App/Runtime/buffer_state.hpp"
#include "convert.hpp"
#include "App/Frontend/new_buffer.hpp"
#include "App/Frontend/new_view.hpp"
#include "Editor/editor.hpp"
#include "Editor/Engine/edit_typeset.hpp"
#include "Editor/Commands/edit_interface.hpp"
#include "App/Runtime/thumbnail_snapshot.hpp"
#include "file.hpp"
#include "font_domain.hpp"
#include "outline_snapshot.hpp"
#include "native_editor_actions.hpp"
#include "source_commands.hpp"
#include "presentation_commands.hpp"
#include "generic_frontend.hpp"
#include "Editor/Commands/selection_image_export.hpp"
#include "Editor/Commands/document_print.hpp"
#include "Editor/Commands/link_markup.hpp"
#include "structured_commands.hpp"
#include "new_style.hpp"
#include "Interop/Adapters/interop_document_source.hpp"
#include "Services/RAG/rag_realtime_generation.hpp"
#include "App/Runtime/tm_buffer.hpp"
#include "App/Window/tm_window.hpp"
#include "tm_ostream.hpp"

#ifdef QTTEXMACS
#include "QTMNodePropertiesDialog.hpp"
#include "QTMHodarium.hpp"
#include "node_metadata.hpp"
#include "QTMRenderService.hpp"
#include "UI/Qt/Document/QTMThumbnailsPane.hpp"
#include "qt_renderer.hpp"
#include <QJsonDocument>
#include <QPainter>
#endif

#include <cstdio>
#include <atomic>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <exception>
#include <unordered_map>
#include <utility>

namespace {

#ifdef QTTEXMACS
struct thumbnail_actor_manifest {
  std::vector<thumbnail_page_bounds> pages;
  std::uint32_t active= 0;
  std::uint32_t flags= 0;
};

thumbnail_actor_manifest
make_thumbnail_manifest (editor_rep* editor, edit_typeset_rep* typeset) {
  thumbnail_actor_manifest result;
  result.pages= typeset->thumbnail_page_geometry ();
  if (editor->get_init_string ("page-medium") != "beamer") return result;
  tree body= editor->the_buffer ();
  if (!is_document (body) || N (body) == 0) return result;
  tree screens= body[N (body)-1];
  if (!is_compound (screens, "screens") || N (screens) == 0)
    return result;
  result.flags= THUMBNAIL_MANIFEST_SLIDES;
  for (int i=0; i<N(screens); ++i)
    if (is_compound (screens[i], "shown", 1)) {
      result.active= static_cast<std::uint32_t> (i);
      break;
    }
  const auto visible= result.pages;
  result.pages.assign (N (screens), {});
  // Beamer screens share page geometry. The hidden slides do not have boxes
  // yet, but their provisional bounds let the pane schedule offscreen fills.
  if (!visible.empty ())
    std::fill (result.pages.begin (), result.pages.end (), visible[0]);
  return result;
}

std::vector<std::uint64_t>
thumbnail_slide_signatures (editor_rep* editor) {
  tree body= editor->the_buffer ();
  if (!is_document (body) || N (body) == 0) return {};
  tree screens= body[N (body)-1];
  if (!is_compound (screens, "screens")) return {};
  // Preamble and other document-level definitions affect every screen.
  std::uint64_t common= 1469598103934665603ULL;
  for (int i=0; i<N (body)-1; ++i)
    common= (common * 1099511628211ULL) ^
            static_cast<std::uint32_t> (hash (body[i]));
  std::vector<std::uint64_t> result;
  result.reserve (N (screens));
  for (int i=0; i<N (screens); ++i) {
    tree slide= screens[i];
    // Presentation navigation only changes these wrapper labels. The actual
    // content and its node metadata remain the authority for cache validity.
    if (N (slide) == 1 &&
        (is_compound (slide, "shown") ||
         is_compound (slide, "hidden") ||
         is_compound (slide, "hidden*") ||
         is_compound (slide, "slide")))
      slide= slide[0];
    const auto identity= (common * 1099511628211ULL) ^
                         static_cast<std::uint32_t> (hash (slide));
    result.push_back (identity == 0 ? 1 : identity);
  }
  return result;
}

std::uint64_t
thumbnail_slides_content_signature (const std::vector<std::uint64_t>& slides) {
  std::uint64_t result= 1469598103934665603ULL;
  for (const auto slide: slides)
    result= (result * 1099511628211ULL) ^ slide;
  return result == 0 ? 1 : result;
}

std::uint64_t
thumbnail_style_signature (editor_rep* editor) {
  std::uint64_t result= 1469598103934665603ULL;
  result= (result * 1099511628211ULL) ^ editor->runtime_view_id;
  new_data data;
  editor->get_data (data);
  result= (result * 1099511628211ULL) ^
          static_cast<std::uint32_t> (hash (data->style));
  for (const char* variable: {"page-type", "page-medium", "bg-color",
                              "color", "page-orientation", "page-width", "page-height"})
    result= (result * 1099511628211ULL) ^
             static_cast<std::uint32_t> (hash (editor->get_init_value (variable)));
  // Changing the Vault's white-background override affects every preview,
  // even though neither the document source nor its initial bg-color changes.
  result= (result * 1099511628211ULL) ^
          static_cast<std::uint32_t> (hash (effective_document_background (
            editor->get_init_value ("bg-color"))));
  return result == 0 ? 1 : result;
}

std::uint64_t
thumbnail_signature (std::uint64_t style_signature,
                     const thumbnail_actor_manifest& manifest,
                     std::uint64_t layout_revision,
                     std::uint64_t content_signature) {
  // An actual typeset pass increments this revision, including when page
  // boxes are updated in place. Cursor blinks do not invalidate thumbnails.
  std::uint64_t result= style_signature ^ layout_revision;
  result= (result * 1099511628211ULL) ^ content_signature;
  result= (result * 1099511628211ULL) ^ manifest.flags;
  result= (result * 1099511628211ULL) ^ manifest.active;
  for (const auto& page: manifest.pages)
    for (const auto value: {page.x1, page.y1, page.x2, page.y2})
      result= (result * 1099511628211ULL) ^ static_cast<std::uint64_t> (value);
  return result == 0 ? 1 : result;
}
#endif

std::mutex actor_registry_lock;
std::atomic<std::uint64_t> continuous_rag_save_sequence {1};
using actor_entry= actor_lifetime<buffer_actor>;
std::unordered_map<athena_actor_id, std::shared_ptr<actor_entry>> actor_registry;

bool
run_native_editor_command (editor_rep* editor,
                           native_editor_command_id command,
                           const string& argument= "") {
  if (editor == nullptr) return false;
  actor_editor_command_snapshot state= editor->editor_command_state_snapshot ();
  if (!state.valid ()) return false;

  const bool has_selection=
    state.selection_active () || state.graphics_selection_active ();
  actor_focus_toolbar_snapshot focus= editor->focus_toolbar_state_snapshot ();
  const bool genericFocus=
    focus.valid () &&
    !focus.has (ACTOR_FOCUS_TOOLBAR_BUFFER) &&
    !focus.has (ACTOR_FOCUS_TOOLBAR_CODE_CONTEXT) &&
    !focus.has (ACTOR_FOCUS_TOOLBAR_SCREENS_CONTEXT);
  switch (command) {
  case native_editor_command_id::undo:
    if (state.read_only () || state.undo_count == 0) return false;
    break;
  case native_editor_command_id::redo:
    if (state.read_only () || state.redo_count == 0) return false;
    break;
  case native_editor_command_id::copy:
    if (!has_selection) return false;
    break;
  case native_editor_command_id::cut:
    if (state.read_only () || !has_selection) return false;
    break;
  case native_editor_command_id::paste:
    if (state.read_only ()) return false;
    break;
  case native_editor_command_id::node_properties:
    if (!state.focus_node_available ()) return false;
    break;
  case native_editor_command_id::save:
  case native_editor_command_id::update_all:
    if (state.read_only ()) return false;
    break;
  case native_editor_command_id::math_correct_all:
    if (state.read_only () || !state.math_mode ()) return false;
    break;
  case native_editor_command_id::presentation_first:
  case native_editor_command_id::presentation_previous:
  case native_editor_command_id::presentation_next:
  case native_editor_command_id::presentation_last:
    if (!state.presentation_mode ()) return false;
    break;
  case native_editor_command_id::presentation_previous_screen:
  case native_editor_command_id::presentation_next_screen:
    if (!state.presentation_mode () || !state.screens_mode ()) return false;
    break;
  case native_editor_command_id::export_selection_image:
    if (!has_selection || N(argument) == 0) return false;
    break;
  case native_editor_command_id::focus_traverse_first:
  case native_editor_command_id::focus_traverse_previous:
  case native_editor_command_id::focus_traverse_next:
  case native_editor_command_id::focus_traverse_last:
    if (!genericFocus || !focus.has (ACTOR_FOCUS_TOOLBAR_CAN_MOVE))
      return false;
    break;
  case native_editor_command_id::focus_insert_left:
  case native_editor_command_id::focus_insert_right:
    if (!genericFocus ||
        !focus.has (ACTOR_FOCUS_TOOLBAR_CAN_INSERT_REMOVE) ||
        ((!focus.has (ACTOR_FOCUS_TOOLBAR_VERTICAL)) &&
         (!focus.has (ACTOR_FOCUS_TOOLBAR_HORIZONTAL) ||
          !focus.has (ACTOR_FOCUS_TOOLBAR_CAN_INSERT))))
      return false;
    break;
  case native_editor_command_id::focus_remove_left:
  case native_editor_command_id::focus_remove_right:
    if (!genericFocus ||
        !focus.has (ACTOR_FOCUS_TOOLBAR_CAN_INSERT_REMOVE) ||
        ((!focus.has (ACTOR_FOCUS_TOOLBAR_VERTICAL)) &&
         (!focus.has (ACTOR_FOCUS_TOOLBAR_HORIZONTAL) ||
          !focus.has (ACTOR_FOCUS_TOOLBAR_CAN_REMOVE))))
      return false;
    break;
  case native_editor_command_id::focus_insert_up:
  case native_editor_command_id::focus_insert_down:
  case native_editor_command_id::focus_remove_up:
  case native_editor_command_id::focus_remove_down:
    if (!genericFocus ||
        !focus.has (ACTOR_FOCUS_TOOLBAR_CAN_INSERT_REMOVE) ||
        !focus.has (ACTOR_FOCUS_TOOLBAR_VERTICAL))
      return false;
    break;
  case native_editor_command_id::focus_exit_left:
  case native_editor_command_id::focus_exit_right:
  case native_editor_command_id::focus_remove_tag:
    if (!genericFocus || !focus.has (ACTOR_FOCUS_TOOLBAR_CURSOR_INSIDE))
      return false;
    break;
  case native_editor_command_id::revert:
  case native_editor_command_id::close_document:
  case native_editor_command_id::save_as:
  case native_editor_command_id::preview:
  case native_editor_command_id::print:
  case native_editor_command_id::close_window:
  case native_editor_command_id::history_back:
  case native_editor_command_id::history_forward:
  case native_editor_command_id::print_to_file:
  case native_editor_command_id::print_page_selection:
  case native_editor_command_id::print_page_selection_to_file:
  case native_editor_command_id::export_pdf:
    break;
  default:
    return false;
  }

  editor->before_menu_action ();
  try {
    switch (command) {
    case native_editor_command_id::undo:
      editor->undo (0);
      break;
    case native_editor_command_id::redo:
      editor->redo (0);
      break;
    case native_editor_command_id::copy:
      editor->selection_copy ("primary");
      break;
    case native_editor_command_id::cut:
      editor->selection_cut ("primary");
      break;
    case native_editor_command_id::paste:
      editor->selection_paste ("primary");
      break;
    case native_editor_command_id::node_properties: {
#ifdef QTTEXMACS
      path focus= editor->focus_get ();
      if (!editor->test_subtree (focus) ||
          !node_properties_show (editor->the_subtree (focus))) {
        editor->cancel_menu_action ();
        editor->publish_editor_command_state ();
        return false;
      }
#else
      editor->cancel_menu_action ();
      editor->publish_editor_command_state ();
      return false;
#endif
      break;
    }
    case native_editor_command_id::save:
      document_save_manual (get_current_buffer ());
      break;
    case native_editor_command_id::revert:
      application_revert_buffer (get_current_buffer ());
      break;
    case native_editor_command_id::update_all:
      generic_update_document ("all");
      break;
    case native_editor_command_id::close_document:
      application_close_buffer ();
      break;
    case native_editor_command_id::save_as:
      document_save_dialog ();
      break;
    case native_editor_command_id::preview:
      document_preview ();
      break;
    case native_editor_command_id::print:
      document_print ();
      break;
    case native_editor_command_id::close_window:
      application_close_window ();
      break;
    case native_editor_command_id::history_back:
      cursor_history_backward ();
      break;
    case native_editor_command_id::history_forward:
      cursor_history_forward ();
      break;
    case native_editor_command_id::math_correct_all:
      math_correct_all ();
      break;
    case native_editor_command_id::presentation_first:
      presentation_operate_buffer ("first");
      break;
    case native_editor_command_id::presentation_previous_screen:
      presentation_navigate ("previous");
      break;
    case native_editor_command_id::presentation_previous:
      presentation_traverse_buffer ("previous");
      break;
    case native_editor_command_id::presentation_next:
      presentation_traverse_buffer ("next");
      break;
    case native_editor_command_id::presentation_next_screen:
      presentation_navigate ("next");
      break;
    case native_editor_command_id::presentation_last:
      presentation_operate_buffer ("last");
      break;
    case native_editor_command_id::print_to_file:
      document_print_file_dialog ();
      break;
    case native_editor_command_id::print_page_selection:
      document_print_pages_dialog ();
      break;
    case native_editor_command_id::print_page_selection_to_file:
      document_print_pages_file_dialog ();
      break;
    case native_editor_command_id::export_pdf:
      document_export_pdf_dialog ();
      break;
    case native_editor_command_id::export_selection_image:
      native_export_selection_as_image_dialog (argument);
      break;
    case native_editor_command_id::focus_traverse_first:
      generic_traverse_first ();
      break;
    case native_editor_command_id::focus_traverse_previous:
      generic_traverse_previous ();
      break;
    case native_editor_command_id::focus_traverse_next:
      generic_traverse_next ();
      break;
    case native_editor_command_id::focus_traverse_last:
      generic_traverse_last ();
      break;
    case native_editor_command_id::focus_insert_left:
      generic_structured_insert_left ();
      break;
    case native_editor_command_id::focus_insert_right:
      generic_structured_insert_right ();
      break;
    case native_editor_command_id::focus_insert_up:
      generic_structured_insert_up ();
      break;
    case native_editor_command_id::focus_insert_down:
      generic_structured_insert_down ();
      break;
    case native_editor_command_id::focus_remove_left:
      generic_structured_remove_left ();
      break;
    case native_editor_command_id::focus_remove_right:
      generic_structured_remove_right ();
      break;
    case native_editor_command_id::focus_remove_up:
      generic_structured_remove_up ();
      break;
    case native_editor_command_id::focus_remove_down:
      generic_structured_remove_down ();
      break;
    case native_editor_command_id::focus_exit_left:
      generic_structured_exit_left ();
      break;
    case native_editor_command_id::focus_exit_right:
      generic_structured_exit_right ();
      break;
    case native_editor_command_id::focus_remove_tag:
      editor->remove_structure_upwards ();
      break;
    default:
      editor->cancel_menu_action ();
      editor->publish_editor_command_state ();
      return false;
    }
    editor->after_menu_action ();
  }
  catch (...) {
    editor->cancel_menu_action ();
    editor->publish_editor_command_state ();
    throw;
  }
  editor->publish_editor_command_state ();
  return true;
}

std::unique_ptr<athena::document_node::source_identity_state>
replacement_node_identities (const tree& document, const tree& body) {
  auto next= std::make_unique<athena::document_node::source_identity_state> ();
  const auto contexts= prepare_document_source_contracts (document);
  const auto errors= next->initialize_complete (
    body, athena::contracts::builtin_contracts (),
    athena::document_node::standard_source_role, {}, contexts);
  if (!errors.empty ()) {
    std_warning << "Source replacement rejected: "
                << string (errors.front ().detail.c_str ()) << LF;
    return {};
  }
  return next;
}

std::unique_ptr<athena::document_node::source_identity_state>
prepare_replacement_node_identities (const tree& document, tree& body) {
  const auto contexts= prepare_document_source_contracts (document);
  auto assigned= athena::document_node::assign_detached_source_ids (
    body, athena::contracts::builtin_contracts (), athena::document_node::standard_source_role,
    [] (const athena::document_node::identity_request&) {
      return athena::node::new_id ();
    }, {}, contexts);
  if (!assigned.ok ()) {
    std_warning << "Source replacement rejected: "
                << string (assigned.diagnostics.front ().detail.c_str ()) << LF;
    return {};
  }
  body= *assigned.body;
  auto next= std::make_unique<athena::document_node::source_identity_state> ();
  next->adopt_complete_assignment (body, std::move (assigned.owners));
  return next;
}

actor_entry::lease
acquire_actor (athena_actor_id id) {
  std::shared_ptr<actor_entry> entry;
  {
    std::lock_guard<std::mutex> guard (actor_registry_lock);
    auto found= actor_registry.find (id);
    if (found == actor_registry.end ()) return {};
    entry= found->second;
  }
  return entry->acquire ();
}
athena_actor_id next_actor_id= 1;


void
report_unhandled_actor_exception (std::exception_ptr failure) noexcept {
  try {
    if (failure != nullptr) std::rethrow_exception (failure);
  }
  catch (const std::exception& error) {
    try {
      athena_spdlog_error (std::string ("buffer actor command failed: ") +
                           error.what ());
    }
    catch (...) {}
  }
  catch (...) {
    try { athena_spdlog_error ("buffer actor command failed"); }
    catch (...) {}
  }
}

athena_actor_id
register_actor (buffer_actor* actor) {
  std::lock_guard<std::mutex> guard (actor_registry_lock);
  athena_actor_id id= next_actor_id++;
  if (id == ATHENA_NO_ACTOR)
    FAILED ("buffer actor id space exhausted");
  actor_registry.emplace (id, std::make_shared<actor_entry> (actor));
  return id;
}

void
unregister_actor (athena_actor_id id) noexcept {
  std::shared_ptr<actor_entry> entry;
  {
    std::lock_guard<std::mutex> guard (actor_registry_lock);
    auto found= actor_registry.find (id);
    if (found == actor_registry.end ()) return;
    entry= found->second;
    actor_registry.erase (found);
  }
  // A raw lookup pointer could otherwise outlive this registry lock and race
  // the actor's destruction. Drain leases outside the global registry lock.
  entry->close ();
}

} // namespace

struct buffer_actor::implementation {
  struct actor_view {
    int legacy_number;
    editor instance;
    athena_resource_id render_connection;
    std::uint64_t buffer_generation;
    std::uint64_t frame_generation;
  };

  buffer_document_state state;
  std::unordered_map<athena_view_id, actor_view> views;
  std::unordered_map<athena_view_id, std::uint64_t> hybrid_activations;
  struct pending_callback {
    athena_view_id view;
    std::function<void()> action;
  };
  std::unordered_map<athena_continuation_id, pending_callback> callbacks;
  athena_continuation_id next_callback= 1;

  implementation (buffer_actor* actor, string name, string master,
                  string title, bool read_only, int last_save):
    state (actor, std::move (name), std::move (master), std::move (title),
           read_only, last_save), views () {}
};

static double
argument_double (std::uint64_t bits) noexcept {
  double result;
  std::memcpy (&result, &bits, sizeof (result));
  return result;
}

void
buffer_actor::publish_tmfs_title (editor_rep* preferred_editor) {
  if (!is_rooted_tmfs (impl_->state.name)) return;

  editor_rep* target= preferred_editor;
  if (target == nullptr || target->ui_endpoint == nullptr) {
    target= nullptr;
    for (auto& entry: impl_->views)
      if (entry.second.instance->ui_endpoint != nullptr) {
        target= entry.second.instance.operator -> ();
        break;
      }
  }
  if (target == nullptr) return;

  string title= tmfs_title (impl_->state.name);
  (void) target->publish_ui_text (
    actor_command_kind::ui_set_buffer_title, std::move (title));
}

athena_blob_id
actor_text_from_string (string text) {
  return actor_text_registry::instance ().store (std::move (text));
}

buffer_actor::buffer_actor (tm_buffer_rep* owner):
  id_ (register_actor (this)),
  initial_name_ (actor_text_from_string (copy (as_string (owner->buf->name)))),
  initial_master_ (
    actor_text_from_string (copy (as_string (owner->buf->master)))),
  initial_title_ (actor_text_from_string (copy (owner->buf->title))),
  initial_read_only_ (owner->buf->read_only),
  initial_last_save_ (static_cast<int> (owner->buf->last_save)), commands_ (128),
  owner_thread_ (), next_command_id_ (1), next_response_id_ (1),
  completed_command_id_ (0), completed_commands_ (0), accepting_ (true),
  started_ (false), impl_ () {}

buffer_actor::~buffer_actor () {
  unregister_actor (id_);
  shutdown ();
  athena::artifact::close (id_);
}

athena_actor_id
buffer_actor::id () const noexcept {
  return id_;
}

athena_continuation_id
buffer_actor::retain_native_callback (std::function<void()> callback) {
  ASSERT (is_owner_thread (), "native callbacks belong to their BufferActor");
  const auto id= impl_->next_callback++;
  impl_->callbacks.emplace (id, implementation::pending_callback {
    current_execution_context ()->view_id, std::move (callback)});
  return id;
}

void
buffer_actor::resolve_native_callback (athena_continuation_id id, bool run) {
  ASSERT (is_owner_thread (), "native callbacks resolve on their BufferActor");
  auto entry= impl_->callbacks.find (id);
  if (entry == impl_->callbacks.end ()) return;
  auto callback= std::move (entry->second);
  impl_->callbacks.erase (entry);
  const auto* context= current_execution_context ();
  if (run && callback.view == context->view_id &&
      (callback.view == ATHENA_NO_VIEW || context->editor != nullptr))
    callback.action ();
}

actor_command_ticket
buffer_actor::submit (
  actor_command_kind kind, athena_view_id view_id,
  athena_blob_id payload0, athena_blob_id payload1,
  ExecutionCapabilitySet capabilities, std::uint64_t argument0,
  std::uint64_t argument1, std::uint64_t argument2,
  std::uint64_t argument3, std::uint64_t argument4,
  std::uint64_t argument5, std::uint64_t argument6,
  std::uint64_t argument7) {
  return submit_with_response (
    kind, view_id, payload0, payload1, capabilities, ATHENA_NO_RESPONSE,
    argument0, argument1, argument2, argument3, argument4, argument5,
    argument6, argument7, true);
}

actor_command_ticket
buffer_actor::try_submit (
  actor_command_kind kind, athena_view_id view_id,
  athena_blob_id payload0, athena_blob_id payload1,
  ExecutionCapabilitySet capabilities, std::uint64_t argument0,
  std::uint64_t argument1, std::uint64_t argument2,
  std::uint64_t argument3, std::uint64_t argument4,
  std::uint64_t argument5, std::uint64_t argument6,
  std::uint64_t argument7) {
  return submit_with_response (
    kind, view_id, payload0, payload1, capabilities, ATHENA_NO_RESPONSE,
    argument0, argument1, argument2, argument3, argument4, argument5,
    argument6, argument7, false);
}

actor_command_ticket
buffer_actor::submit_with_response (
  actor_command_kind kind, athena_view_id view_id,
  athena_blob_id payload0, athena_blob_id payload1,
  ExecutionCapabilitySet capabilities, athena_response_id response_id,
  std::uint64_t argument0, std::uint64_t argument1,
  std::uint64_t argument2, std::uint64_t argument3,
  std::uint64_t argument4, std::uint64_t argument5,
  std::uint64_t argument6, std::uint64_t argument7,
  bool wait_for_slot) {
  if (kind == actor_command_kind::none || !ensure_started ())
    return actor_command_ticket {};

  std::unique_lock<std::mutex> submit_guard (submit_lock_, std::defer_lock);
  if (wait_for_slot) submit_guard.lock ();
  else if (!submit_guard.try_lock ()) return actor_command_ticket {};
  actor_command_transport::writable_command command;
  bool acquired= wait_for_slot ? commands_.acquire (command) :
    commands_.try_acquire (command);
  if (!acquired) return actor_command_ticket {};

  std::uint64_t command_id;
  {
    std::lock_guard<std::mutex> guard (state_lock_);
    if (!accepting_) {
      (void) commands_.discard (command.slot);
      return actor_command_ticket {};
    }
    command_id= next_command_id_++;
  }

  command.record->command_id= command_id;
  command.record->response_id= response_id;
  command.record->view_id= view_id;
  command.record->payload0= payload0;
  command.record->payload1= payload1;
  command.record->capabilities= capabilities;
  command.record->kind= kind;
  command.record->argument[0]= argument0;
  command.record->argument[1]= argument1;
  command.record->argument[2]= argument2;
  command.record->argument[3]= argument3;
  command.record->argument[4]= argument4;
  command.record->argument[5]= argument5;
  command.record->argument[6]= argument6;
  command.record->argument[7]= argument7;
  if (!commands_.publish (command.slot)) {
    (void) commands_.discard (command.slot);
    return actor_command_ticket {};
  }
  return actor_command_ticket {command_id, response_id};
}

actor_command_ticket
buffer_actor::submit_to (
  athena_actor_id actor_id, actor_command_kind kind, athena_view_id view_id,
  athena_blob_id payload0, athena_blob_id payload1,
  ExecutionCapabilitySet capabilities, std::uint64_t argument0,
  std::uint64_t argument1, std::uint64_t argument2,
  std::uint64_t argument3, std::uint64_t argument4,
  std::uint64_t argument5, std::uint64_t argument6,
  std::uint64_t argument7) {
  auto lifetime= acquire_actor (actor_id);
  buffer_actor* actor= lifetime.get ();
  return actor == nullptr ? actor_command_ticket {} : actor->submit (
    kind, view_id, payload0, payload1, capabilities, argument0, argument1,
    argument2, argument3, argument4, argument5, argument6, argument7);
}

bool
buffer_actor::try_submit_coalesced_to (
  athena_actor_id actor_id, actor_command_kind kind, athena_view_id view_id,
  std::uint64_t argument0, std::uint64_t argument1,
  std::uint64_t argument2, std::uint64_t argument3) {
  auto lifetime= acquire_actor (actor_id);
  buffer_actor* actor= lifetime.get ();
  actor_ui_endpoint* endpoint= find_actor_ui_endpoint (view_id);
  if (actor == nullptr || endpoint == nullptr) return false;
  if (!endpoint->begin_coalesced_command (kind)) return true;
  actor_command_ticket ticket= actor->try_submit (
    kind, view_id, ATHENA_NO_BLOB, ATHENA_NO_BLOB,
    EXECUTION_CAPABILITY_BUFFER, argument0, argument1, argument2, argument3);
  if (!ticket) endpoint->finish_coalesced_command (kind);
  return static_cast<bool> (ticket);
}

actor_command_ticket
buffer_actor::try_submit_to (
  athena_actor_id actor_id, actor_command_kind kind, athena_view_id view_id,
  athena_blob_id payload0, athena_blob_id payload1,
  ExecutionCapabilitySet capabilities, std::uint64_t argument0,
  std::uint64_t argument1, std::uint64_t argument2,
  std::uint64_t argument3, std::uint64_t argument4,
  std::uint64_t argument5, std::uint64_t argument6,
  std::uint64_t argument7) {
  auto lifetime= acquire_actor (actor_id);
  buffer_actor* actor= lifetime.get ();
  return actor == nullptr ? actor_command_ticket {} : actor->try_submit (
    kind, view_id, payload0, payload1, capabilities, argument0, argument1,
    argument2, argument3, argument4, argument5, argument6, argument7);
}

void
buffer_actor::notify_contracts_ready (athena_actor_id actor_id,
                                     athena_view_id view_id) {
  auto lifetime= acquire_actor (actor_id);
  buffer_actor* actor= lifetime.get ();
  if (actor == nullptr) return;
  {
    std::lock_guard<std::mutex> guard (actor->state_lock_);
    if (!actor->accepting_ || !actor->started_) return;
    if (!actor->contracts_ready_.insert (view_id).second) return;
  }
  actor->commands_.wake ();
}

bool
buffer_actor::invoke_on (
  athena_actor_id actor_id, actor_command_kind kind, athena_view_id view_id,
  athena_blob_id payload0, athena_blob_id payload1,
  actor_command_record* result, ExecutionCapabilitySet capabilities,
  std::uint64_t argument0, std::uint64_t argument1,
  std::uint64_t argument2, std::uint64_t argument3,
  std::uint64_t argument4, std::uint64_t argument5,
  std::uint64_t argument6, std::uint64_t argument7) {
  auto lifetime= acquire_actor (actor_id);
  buffer_actor* actor= lifetime.get ();
  return actor != nullptr && actor->invoke (
    kind, view_id, payload0, payload1, result, capabilities,
    argument0, argument1, argument2, argument3, argument4, argument5,
    argument6, argument7);
}

bool
buffer_actor::wait (actor_command_ticket ticket,
                    actor_command_record* result) {
  if (!ticket || ticket.response_id == ATHENA_NO_RESPONSE) return false;
  ASSERT (!is_owner_thread (), "buffer actor cannot synchronously await itself");
  std::unique_lock<std::mutex> guard (state_lock_);
  completed_condition_.wait (guard, [this, ticket] {
    auto found= responses_.find (ticket.response_id);
    return found == responses_.end () || found->second.ready || !started_;
  });
  auto found= responses_.find (ticket.response_id);
  if (found == responses_.end () || !found->second.ready) return false;
  if (result != nullptr) *result= found->second.record;
  responses_.erase (found);
  return true;
}

bool
buffer_actor::can_apply_saved_document (const std::string& expected_storage) const {
  ASSERT (is_owner_thread (), "remote source replacement outside its actor");
  const auto& state= impl_->state;
  if (!state.storage || state.storage_capture_failed ||
      state.storage_version != athena::document::xml_storage_version::v3 ||
      state.storage->source_sha256 () != expected_storage ||
      state.source_modified || state.source_autosave_modified ||
      !state.node_identities || state.node_identities->pending ()) return false;
  for (auto& view: impl_->views)
    if (view.second.instance->need_save (true) || view.second.instance->get_input_mode () != 0)
      return false;
  return true;
}

bool
buffer_actor::retire_saved_document (const std::string& expected_storage,
    const std::function<bool()>& publish) {
  if (!can_apply_saved_document (expected_storage)) return false;
  auto& state= impl_->state;
  const bool was_read_only= state.read_only;
  // GUI closure is asynchronous. Reject subsequently queued edits before it
  // can run; otherwise an input event could recreate the deleted document.
  state.read_only= true;
  if (!publish ()) { state.read_only= was_read_only; return false; }
  state.storage.reset ();
  state.artifacts.reset ();
  athena::artifact::close (id_);
  source_changed ();
  athena::node_reference::source_changed ();
  for (auto& view: impl_->views) {
    view.second.instance->notify_change (THE_ENVIRONMENT);
    if (view.second.instance->publish_ui (actor_command_kind::ui_close_buffer, id_))
      break;
  }
  return true;
}

bool
buffer_actor::apply_saved_document (const std::string& expected_storage,
    const std::string& target_storage, tree source,
    const std::function<std::unique_ptr<athena::document::document_file>()>& publish) {
  if (!can_apply_saved_document (expected_storage)) return false;
  auto& state= impl_->state;

  // Unlike a normal import, synchronization must not invent identities that
  // differ from the exact source bytes installed on the other devices.
  new_data data;
  tree body= detach_data (source, data);
  if (athena::node::id (body) !=
      athena::node::id (subtree (state.document, state.root_path))) return false;
  auto identities= replacement_node_identities (source, body);
  if (!identities) return false;
  auto storage= publish ();
  if (!storage) return false;
  if (storage->version () != athena::document::xml_storage_version::v3 ||
      storage->source_sha256 () != target_storage)
    throw std::runtime_error ("Remote document publication returned a different storage revision");

  state.source_envelope= std::move (source);
  // Live typesetting environments hold references to these map members.
  // Replace their values without destroying the owning new_data_rep.
  state.data->style= std::move (data->style);
  state.data->init= std::move (data->init);
  state.data->fin= std::move (data->fin);
  state.data->ref= std::move (data->ref);
  state.data->aux= std::move (data->aux);
  state.data->att= std::move (data->att);
  state.artifacts.reset ();
  athena::artifact::close (id_);
  set_document (state.document, state.root_path, std::move (body));
  state.node_identities= std::move (identities);
  state.storage= std::move (*storage);
  state.storage_capture_failed= false;
  source_changed ();
  state.last_save= last_modified (state.name);
  for (auto& view: impl_->views) {
    view.second.instance->set_data (state.data);
    view.second.instance->notify_save ();
    (void) view.second.instance->publish_ui (actor_command_kind::ui_mark_buffer_saved,
      static_cast<std::uint64_t> (state.last_save));
  }
  state.source_modified= state.source_autosave_modified= false;
  athena::node_reference::source_changed ();
  return true;
}

bool
buffer_actor::invoke (
  actor_command_kind kind, athena_view_id view_id,
  athena_blob_id payload0, athena_blob_id payload1,
  actor_command_record* result, ExecutionCapabilitySet capabilities,
  std::uint64_t argument0, std::uint64_t argument1,
  std::uint64_t argument2, std::uint64_t argument3,
  std::uint64_t argument4, std::uint64_t argument5,
  std::uint64_t argument6, std::uint64_t argument7) {
  if (is_owner_thread ()) {
    actor_command_record command;
    command.view_id= view_id;
    command.payload0= payload0;
    command.payload1= payload1;
    command.capabilities= capabilities;
    command.kind= kind;
    command.argument[0]= argument0;
    command.argument[1]= argument1;
    command.argument[2]= argument2;
    command.argument[3]= argument3;
    command.argument[4]= argument4;
    command.argument[5]= argument5;
    command.argument[6]= argument6;
    command.argument[7]= argument7;
    dispatch (command);
    if (result != nullptr) *result= command;
    return true;
  }

  athena_response_id response_id;
  {
    std::lock_guard<std::mutex> guard (state_lock_);
    if (!accepting_) return false;
    response_id= next_response_id_++;
    if (response_id == ATHENA_NO_RESPONSE)
      FAILED ("buffer actor response id space exhausted");
    responses_.emplace (response_id, response_state {});
  }
  actor_command_ticket ticket= submit_with_response (
    kind, view_id, payload0, payload1, capabilities, response_id,
    argument0, argument1, argument2, argument3, argument4, argument5,
    argument6, argument7, true);
  if (!ticket) {
    std::lock_guard<std::mutex> guard (state_lock_);
    responses_.erase (response_id);
    return false;
  }
  return wait (ticket, result);
}

bool
buffer_actor::wait_until_idle () {
  if (is_owner_thread ()) return true;
  return invoke (actor_command_kind::barrier);
}

void
buffer_actor::shutdown () {
  ASSERT (!is_owner_thread (), "buffer actor cannot join itself");
  bool joinable;
  {
    std::lock_guard<std::mutex> guard (state_lock_);
    if (!accepting_ && !worker_.joinable ()) return;
    accepting_= false;
    joinable= worker_.joinable ();
  }
  commands_.close ();
  if (joinable) worker_.join ();
}

bool
buffer_actor::ensure_started () {
  std::unique_lock<std::mutex> guard (state_lock_);
  if (!accepting_) return false;
  if (!worker_.joinable ()) {
    // The destructor joins this thread. Startup must not look itself up in
    // a registry from which concurrent teardown may already have removed it.
    worker_= std::thread ([this] { thread_entry (this); });
  }
  started_condition_.wait (guard, [this] {
    return started_ || !accepting_;
  });
  return started_ && accepting_;
}

bool
buffer_actor::is_owner_thread () const noexcept {
  std::lock_guard<std::mutex> guard (state_lock_);
  return started_ && owner_thread_ == std::this_thread::get_id ();
}

std::thread::id
buffer_actor::owner_thread () const noexcept {
  std::lock_guard<std::mutex> guard (state_lock_);
  return owner_thread_;
}

std::uint64_t
buffer_actor::completed_commands () const noexcept {
  std::lock_guard<std::mutex> guard (state_lock_);
  return completed_commands_;
}

url
buffer_actor::current_buffer_url () const {
  ASSERT (is_owner_thread (), "buffer URL accessed outside its actor");
  ASSERT (impl_ != nullptr, "buffer actor state is unavailable");
  return impl_->state.name;
}

editor_rep*
buffer_actor::current_editor (athena_view_id view_id) const noexcept {
  if (!is_owner_thread () || view_id == ATHENA_NO_VIEW) return nullptr;
  auto found= impl_->views.find (view_id);
  return found == impl_->views.end () ? nullptr :
    found->second.instance.operator -> ();
}

buffer_document_state*
buffer_actor::current_state () const noexcept {
  return is_owner_thread () && impl_ != nullptr ? &impl_->state : nullptr;
}

tree&
buffer_actor::current_source (athena_view_id view_id) {
  ASSERT (is_owner_thread (), "document source accessed outside its actor");
  ASSERT (impl_ != nullptr, "buffer actor state is unavailable");
  editor_rep* editor= current_editor (view_id);
  if (view_id != ATHENA_NO_VIEW && editor == nullptr)
    throw std::runtime_error ("STALE: source view is no longer attached");
  if (view_id == ATHENA_NO_VIEW && !impl_->views.empty ())
    throw std::invalid_argument ("A displayed document requires its source view");
  if (editor != nullptr) editor->get_data (impl_->state.data);
  refresh_interop_document_source (impl_->state.source_envelope,
    subtree (impl_->state.document, impl_->state.root_path), impl_->state.data);
  return impl_->state.source_envelope;
}

void
buffer_actor::commit_current_source (athena_view_id view_id) {
  source_changed ();
  ASSERT (is_owner_thread (), "document source committed outside its actor");
  auto& state= impl_->state;
  ASSERT (!state.read_only, "read-only document source cannot be committed");
  if (state.node_identities && state.node_identities->pending ()) {
    editor_rep* identity_editor= current_editor (view_id);
    if (identity_editor == nullptr && view_id == ATHENA_NO_VIEW &&
        !impl_->views.empty ())
      identity_editor= impl_->views.begin ()->second.instance.operator -> ();
    if (identity_editor == nullptr || !identity_editor->finish_node_identities ())
      throw std::runtime_error (
        "Active source identities could not be finalized after document edit");
  }
  new_data next;
  tree body= detach_data (state.source_envelope, next);
  tree& live= subtree (state.document, state.root_path);
  if (inside (live) != inside (body)) assign (live, body);
  const bool environment_changed= next->style != state.data->style ||
    next->init != state.data->init || next->fin != state.data->fin ||
    next->ref != state.data->ref || next->aux != state.data->aux || next->att != state.data->att;
  // Editors' environments borrow these member objects by reference. Replacing
  // the new_data owner would leave their ref/aux/att bindings dangling.
  state.data->style= next->style;
  state.data->init= next->init;
  state.data->fin= next->fin;
  state.data->ref= next->ref;
  state.data->aux= next->aux;
  state.data->att= next->att;
  for (auto& entry: impl_->views) {
    if (environment_changed) entry.second.instance->set_data (state.data);
    entry.second.instance->notify_change (THE_TREE);
    entry.second.instance->require_save ();
  }
  // Attached editors already track save/undo state in their archives. Keep a
  // fallback only for buffers edited through interop before any view exists.
  if (impl_->views.empty ()) {
    state.source_modified= state.source_autosave_modified= true;
  }
  athena::node_reference::source_changed ();
}

void
buffer_actor::invalidate_typesetting (path p) {
  ASSERT (is_owner_thread (), "typesetting invalidated outside its actor");
  for (auto& entry: impl_->views)
    entry.second.instance->typeset_invalidate (p);
}

url
buffer_actor::current_view_url (athena_view_id view_id) const {
  ASSERT (is_owner_thread (), "view URL accessed outside its actor");
  auto found= impl_->views.find (view_id);
  if (found != impl_->views.end ())
    return make_abstract_view_url (
      impl_->state.name, found->second.legacy_number);
  return url_none ();
}

void
buffer_actor::thread_entry (buffer_actor* actor) {
  athena_crash_register_thread (AthenaCrashThreadRole::BufferActor, actor->id ());
  athena_crash_set_execution (actor->id (), 0, 0);
  actor->run_loop ();
}

void
buffer_actor::run_loop () {
  font_domain fonts;
  font_domain_binding font_owner (fonts);
  string initial_name= actor_text_registry::instance ().take (initial_name_);
  string initial_master= actor_text_registry::instance ().take (initial_master_);
  string initial_title= actor_text_registry::instance ().take (initial_title_);
  initial_name_= ATHENA_NO_BLOB;
  initial_master_= ATHENA_NO_BLOB;
  initial_title_= ATHENA_NO_BLOB;
  impl_= std::make_unique<implementation> (
    this, std::move (initial_name), std::move (initial_master),
    std::move (initial_title), initial_read_only_, initial_last_save_);
  {
    std::lock_guard<std::mutex> guard (state_lock_);
    owner_thread_= std::this_thread::get_id ();
    started_= true;
  }
  started_condition_.notify_all ();

  while (true) {
    actor_command_transport::readable_command command;
    const auto received= commands_.wait_event (command);
    if (received == actor_command_transport::event::closed) break;

    if (received == actor_command_transport::event::wakeup) {
      publish_ready_contracts ();
      flush_hybrid_activations ();
      continue;
    }

    execute (*command.record);
    publish_ready_contracts ();
    actor_command_record completed= *command.record;
    std::uint64_t command_id= completed.command_id;
    {
      std::lock_guard<std::mutex> guard (state_lock_);
      if (completed.response_id != ATHENA_NO_RESPONSE) {
        auto found= responses_.find (completed.response_id);
        if (found != responses_.end ()) {
          found->second.record= completed;
          found->second.ready= true;
        }
      }
      completed_command_id_= command_id;
      ++completed_commands_;
    }
    (void) commands_.complete (command.slot);
    flush_hybrid_activations ();
    completed_condition_.notify_all ();
  }

  impl_.reset ();

  {
    std::lock_guard<std::mutex> guard (state_lock_);
    started_= false;
    owner_thread_= std::thread::id ();
  }
  started_condition_.notify_all ();
  completed_condition_.notify_all ();
}

void
buffer_actor::queue_hybrid_activation (athena_view_id view_id, std::uint64_t token) {
  ASSERT (is_owner_thread (), "hybrid activation belongs to its BufferActor");
  impl_->hybrid_activations.insert_or_assign (view_id, token);
}

void
buffer_actor::flush_hybrid_activations () {
  auto& pending= impl_->hybrid_activations;
  for (auto it= pending.begin (); it != pending.end ();) {
    if (!try_submit (actor_command_kind::resume_hybrid_activation, it->first,
        ATHENA_NO_BLOB, ATHENA_NO_BLOB, EXECUTION_CAPABILITY_BUFFER, it->second))
      // A full ring has queued work; a busy producer either publishes another
      // command or closes the actor. Retry after consumption, without spinning.
      break;
    it= pending.erase (it);
  }
}

void
buffer_actor::publish_ready_contracts () {
  std::unordered_set<athena_view_id> ready;
  {
    std::lock_guard<std::mutex> guard (state_lock_);
    ready.swap (contracts_ready_);
    if (!accepting_) return;
  }
  for (const auto view: ready) {
    actor_command_record signal;
    signal.view_id= view;
    signal.kind= actor_command_kind::contracts_ready;
    signal.capabilities= EXECUTION_CAPABILITY_BUFFER;
    // Signals have no mailbox ticket and do not advance command completion.
    execute (signal);
  }
}

void
buffer_actor::execute (actor_command_record& command) {
  current_font_domain ().synchronize_configuration ();
  editor_rep* editor= current_editor (command.view_id);
  athena::contracts::evaluation_state* contract_state= editor == nullptr ? nullptr : &editor->contract_state;
  ExecutionContext context (
    this, editor, contract_state, impl_ == nullptr ? nullptr : &impl_->state.document,
    id_, command.view_id,
    command.command_id, command.capabilities);
  std::exception_ptr failure;
  try {
    ExecutionScope scope (context);
    dispatch (command);
  }
  catch (...) { failure= std::current_exception (); }
  // Dispatch can destroy the view. Do not reuse its pre-dispatch pointer.
  editor= current_editor (command.view_id);
  if (editor != nullptr && editor->ui_endpoint != nullptr &&
      (command.kind == actor_command_kind::apply_changes ||
       command.kind == actor_command_kind::progressive_typeset ||
       command.kind == actor_command_kind::render_view ||
        command.kind == actor_command_kind::request_outline ||
        command.kind == actor_command_kind::request_thumbnail_manifest ||
       command.kind == actor_command_kind::user_scroll ||
       command.kind == actor_command_kind::cursor_blink))
    editor->ui_endpoint->finish_coalesced_command (command.kind);
  report_unhandled_actor_exception (failure);
}

void
buffer_actor::dispatch (actor_command_record& command) {
  editor_rep* editor= current_editor (command.view_id);
  switch (command.kind) {
  case actor_command_kind::barrier:
    break;
  case actor_command_kind::create_view: {
    if (command.view_id == ATHENA_NO_VIEW || editor != nullptr) break;
    class editor created= new_editor (get_server (), &impl_->state);
    created->runtime_view_id= command.view_id;
    created->ui_endpoint= find_actor_ui_endpoint (command.view_id);
    ASSERT (created->ui_endpoint != nullptr,
            "view was created without a UI endpoint");
    created->ui_endpoint->set_zoom_factor (created->handle_get_zoom_factor ());
    created->set_data (impl_->state.data);
    created->publish_editor_command_state ();
    impl_->views.emplace (
      command.view_id,
      implementation::actor_view {
        static_cast<int> (command.argument[0]), std::move (created), 0, 1, 0});
    break;
  }
  case actor_command_kind::destroy_view:
    impl_->hybrid_activations.erase (command.view_id);
    for (auto it= impl_->callbacks.begin (); it != impl_->callbacks.end ();)
      if (it->second.view == command.view_id) it= impl_->callbacks.erase (it);
      else ++it;
    if (editor != nullptr) {
      editor->buf= nullptr;
      impl_->views.erase (command.view_id);
    }
    break;
  case actor_command_kind::initialize_view:
    if (editor != nullptr) {
      initialize_current_view ();
      publish_tmfs_title (editor);
      editor->publish_editor_command_state ();
    }
    break;
  case actor_command_kind::apply_changes:
    if (editor != nullptr) editor->apply_changes ();
    break;
  case actor_command_kind::typeset_document:
    if (editor != nullptr) {
      SI x1, y1, x2, y2;
      editor->typeset (x1, y1, x2, y2);
      command.argument[0]= static_cast<std::uint64_t> (x1);
      command.argument[1]= static_cast<std::uint64_t> (y1);
      command.argument[2]= static_cast<std::uint64_t> (x2);
      command.argument[3]= static_cast<std::uint64_t> (y2);
    }
    break;
  case actor_command_kind::progressive_typeset:
    if (editor != nullptr) {
      // The pending suffix is part of this command's work, not an unrelated
      // future UI tick. Posting only a dirty notification could leave the
      // placeholder on screen indefinitely when no later apply is admitted.
      if (editor->schedule_progressive_typeset ()) editor->apply_changes ();
    }
    break;
  case actor_command_kind::init_style:
    if (editor != nullptr) editor->init_style ();
    break;
  case actor_command_kind::typeset_invalidate_all:
    if (editor != nullptr) editor->typeset_invalidate_all ();
    break;
  case actor_command_kind::key_press:
  case actor_command_kind::text_input:
    if (editor != nullptr) {
      string text= actor_text_registry::instance ().take (command.payload0);
      time_t time= static_cast<time_t> (command.argument[0]);
      if (command.kind == actor_command_kind::key_press)
        editor->handle_keypress (std::move (text), time);
      else editor->handle_text_input (std::move (text), time);
    }
    else
      (void) actor_text_registry::instance ().discard (command.payload0);
    break;
  case actor_command_kind::native_editor_command: {
    string argument;
    if (command.payload0 != ATHENA_NO_BLOB)
      argument= actor_text_registry::instance ().take (command.payload0);
    if (editor != nullptr) {
      if (editor->has_changed (THE_TREE | THE_ENVIRONMENT)) editor->apply_changes ();
      command.argument[0]= run_native_editor_command (
        editor,
        static_cast<native_editor_command_id> (command.argument[0]),
        argument) ? 1 : 0;
    }
    else command.argument[0]= 0;
    break;
  }
  case actor_command_kind::native_editor_action_json: {
    string encoded;
    if (command.payload0 != ATHENA_NO_BLOB)
      encoded= actor_text_registry::instance ().take (command.payload0);
    command.argument[0]= 0;
    if (editor == nullptr || N(encoded) == 0) break;
    if (editor->has_changed (THE_TREE | THE_ENVIRONMENT)) editor->apply_changes ();
    actor_editor_command_snapshot state= editor->editor_command_state_snapshot ();
    const std::uint32_t required=
      static_cast<std::uint32_t> (command.argument[1]);
    const std::uint32_t forbidden=
      static_cast<std::uint32_t> (command.argument[2]);
    const std::uint32_t any=
      static_cast<std::uint32_t> (command.argument[3]);
    if (!state.valid () ||
        (state.flags & required) != required ||
        (state.flags & forbidden) != 0 ||
        (any != 0 && (state.flags & any) == 0))
      break;
#ifdef QTTEXMACS
    QJsonParseError parse;
    QJsonDocument document= QJsonDocument::fromJson (
      QByteArray (encoded.data (), N(encoded)), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject ())
      break;
    QString validation;
    if (!native_editor_action_validate (document.object (), &validation))
      break;
    editor->before_menu_action ();
    try {
      native_editor_action_execute (editor, document.object ());
      editor->after_menu_action ();
      editor->publish_editor_command_state ();
      command.argument[0]= 1;
    }
    catch (...) {
      editor->cancel_menu_action ();
      editor->publish_editor_command_state ();
      throw;
    }
#endif
    break;
  }
  case actor_command_kind::request_personal_macro_items: {
    if (editor == nullptr || editor->ui_endpoint == nullptr) break;
    std::vector<actor_dynamic_menu_item_snapshot> items;
    try {
      array<string> values= source_personal_macros ();
      std::vector<string> flat;
      for (int i= 0; i < N(values); ++i) flat.push_back (values[i]);
      for (std::size_t i= 0; i + 2 < flat.size (); i += 3) {
        actor_dynamic_menu_item_snapshot item;
        item.group= std::string (
          flat[i].data (), static_cast<std::size_t> (N(flat[i])));
        item.label= std::string (
          flat[i + 1].data (),
          static_cast<std::size_t> (N(flat[i + 1])));
        item.key= std::string (
          flat[i + 2].data (),
          static_cast<std::size_t> (N(flat[i + 2])));
        items.push_back (std::move (item));
      }
    }
    catch (...) {
      items.clear ();
    }
    editor->ui_endpoint->update_personal_macro_items (std::move (items));
    break;
  }
  case actor_command_kind::keyboard_focus:
    if (editor != nullptr)
      editor->handle_keyboard_focus (
        command.argument[0] != 0, static_cast<time_t> (command.argument[1]));
    break;
  case actor_command_kind::completion_choice:
    if (editor != nullptr)
      editor->complete_choose (command.argument[0],
        static_cast<int> (static_cast<std::int64_t> (command.argument[1])),
        command.argument[2] != 0);
    break;
  case actor_command_kind::cursor_blink:
    if (editor != nullptr)
      editor->handle_cursor_blink (command.argument[0] != 0);
    break;
  case actor_command_kind::user_scroll:
    if (editor != nullptr)
      editor->handle_user_scroll (static_cast<time_t> (command.argument[0]));
    break;
  case actor_command_kind::mouse:
  {
    struct CompleteResizeMotion {
      editor_rep* editor;
      bool notify;
      ~CompleteResizeMotion () {
        if (notify && editor != nullptr && editor->ui_endpoint != nullptr)
          editor->ui_endpoint->publish (
            actor_command_kind::ui_image_resize_motion_complete);
      }
    } complete {editor, command.argument[5] != 0};
    if (editor != nullptr) {
      string kind= actor_text_registry::instance ().take (command.payload0);
      owned_actor_blob payload=
        actor_blob_registry::instance ().take (command.payload1);
      int count= static_cast<int> (command.argument[4]);
      std::size_t expected= static_cast<std::size_t> (max (count, 0)) *
                            sizeof (double);
      array<double> data;
      if (count > 0 && payload && payload.size () == expected)
        data= array<double> (reinterpret_cast<double*> (payload.data ()), count);
      editor->handle_mouse (
        std::move (kind), static_cast<SI> (command.argument[0]),
        static_cast<SI> (command.argument[1]),
        static_cast<int> (command.argument[2]),
        static_cast<time_t> (command.argument[3]), std::move (data));
    }
    else {
      (void) actor_text_registry::instance ().discard (command.payload0);
      (void) actor_blob_registry::instance ().discard (command.payload1);
    }
    break;
  }
  case actor_command_kind::set_native_drawing_tool:
    if (editor != nullptr)
      editor->set_native_drawing_tool (
        static_cast<native_drawing_tool> (command.argument[0]));
    break;
  case actor_command_kind::set_native_drawing_property:
    if (editor != nullptr)
      editor->set_native_drawing_property (
        static_cast<native_drawing_property> (command.argument[0]),
        command.argument[1]);
    break;
  case actor_command_kind::native_ink_stroke:
    if (editor != nullptr) {
      owned_actor_blob payload=
        actor_blob_registry::instance ().take (command.payload0);
      std::size_t count= static_cast<std::size_t> (command.argument[0]);
      std::size_t expected= count * sizeof (native_ink_sample);
      if (count > 0 && payload && payload.size () == expected) {
        native_drawing_tool tool=
          static_cast<native_drawing_tool> (command.argument[1]);
        const native_ink_sample* samples=
          reinterpret_cast<const native_ink_sample*> (payload.data ());
        if (tool == native_drawing_tool::shape)
          editor->commit_native_drawing_shape (
            static_cast<native_drawing_shape> (command.argument[2]),
            samples, count);
        else
          editor->commit_native_drawing_gesture (tool, samples, count);
      }
    }
    else
      (void) actor_blob_registry::instance ().discard (command.payload0);
    break;
  case actor_command_kind::native_drawing_recognition:
    if (editor != nullptr) {
      owned_actor_blob payload=
        actor_blob_registry::instance ().take (command.payload0);
      if (payload && payload.size () == sizeof (native_shape_recognition_result))
        editor->commit_native_drawing_recognition (
          *reinterpret_cast<const native_shape_recognition_result*> (
            payload.data ()));
    }
    else
      (void) actor_blob_registry::instance ().discard (command.payload0);
    break;
  case actor_command_kind::native_drawing_transform:
    if (editor != nullptr) {
      owned_actor_blob payload=
        actor_blob_registry::instance ().take (command.payload0);
      std::size_t count= static_cast<std::size_t> (command.argument[1]);
      std::size_t expected= count * sizeof (native_ink_sample);
      if (count >= 2 && payload && payload.size () == expected)
        editor->commit_native_drawing_transform (
          static_cast<native_drawing_transform> (command.argument[0]),
          reinterpret_cast<const native_ink_sample*> (payload.data ()), count);
    }
    else
      (void) actor_blob_registry::instance ().discard (command.payload0);
    break;
  case actor_command_kind::native_drawing_insert_space:
    if (editor != nullptr) {
      owned_actor_blob payload=
        actor_blob_registry::instance ().take (command.payload0);
      std::size_t count= static_cast<std::size_t> (command.argument[1]);
      std::size_t expected= count * sizeof (native_ink_sample);
      if (count >= 2 && payload && payload.size () == expected)
        editor->commit_native_drawing_insert_space (
          command.argument[0] != 0,
          reinterpret_cast<const native_ink_sample*> (payload.data ()), count);
    }
    else
      (void) actor_blob_registry::instance ().discard (command.payload0);
    break;
  case actor_command_kind::native_drawing_trim:
    if (editor != nullptr) editor->commit_native_drawing_trim ();
    break;
  case actor_command_kind::commutative_diagram_action: {
    string first, second;
    if (command.payload0 != ATHENA_NO_BLOB)
      first= actor_text_registry::instance ().take (command.payload0);
    if (command.payload1 != ATHENA_NO_BLOB)
      second= actor_text_registry::instance ().take (command.payload1);
    if (editor != nullptr)
      editor->commutative_diagram_action (
        static_cast<native_cd_action> (command.argument[0]),
        std::move (first), std::move (second));
    break;
  }
  case actor_command_kind::set_zoom:
    if (editor != nullptr)
      editor->handle_set_zoom_factor (argument_double (command.argument[0]));
    break;
  case actor_command_kind::change_zoom:
    if (editor != nullptr)
      view_change_zoom (argument_double (command.argument[0]));
    break;
  case actor_command_kind::zoom_by:
    if (editor != nullptr) {
      double factor= argument_double (command.argument[1]);
      if (command.argument[0] != 0) view_zoom_in (factor);
      else view_zoom_out (factor);
    }
    break;
  case actor_command_kind::viewport_changed:
    if (editor != nullptr)
      editor->handle_notify_resize (
        static_cast<SI> (command.argument[0]),
        static_cast<SI> (command.argument[1]),
        static_cast<SI> (command.argument[2]),
        command.argument[3] != 0,
        command.argument[4], command.argument[5]);
    break;
  case actor_command_kind::device_pixel_ratio_changed:
    if (editor != nullptr) {
      editor->suspend ();
      editor->resume ();
    }
    break;
  case actor_command_kind::center_message_state:
    if (editor != nullptr)
      editor->handle_center_message_state (command.argument[0] != 0);
    break;
  case actor_command_kind::render_view:
#ifdef QTTEXMACS
    if (editor != nullptr) {
      // Rendering and document mutation have one owner.  Bring the editor to
      // a coherent box-tree state here instead of relying on an independently
      // queued main-thread maintenance command to win the race.
      editor->apply_changes ();
      auto found= impl_->views.find (command.view_id);
      actor_ui_endpoint* endpoint= editor->ui_endpoint;
      if (found == impl_->views.end () || endpoint == nullptr ||
          found->second.render_connection == 0)
        break;
      actor_viewport_snapshot viewport= endpoint->viewport ();
      int width= static_cast<int> (viewport.render_width);
      int height= static_cast<int> (viewport.render_height);
      if (width <= 0 || height <= 0 ||
          !std::isfinite (viewport.render_pixel_ratio) ||
          viewport.render_pixel_ratio <= 0.0)
        break;
      render_damage damage {
        0, 0, static_cast<std::int64_t> (width),
        static_cast<std::int64_t> (height)};
      std::uint64_t frame_generation= ++found->second.frame_generation;
      std::unique_ptr<QTMRenderRecording> recording=
        qtm_begin_render_recording (
          found->second.render_connection, width, height,
          viewport.render_pixel_ratio, 0xff808080U,
          found->second.buffer_generation, frame_generation, damage,
          viewport.render_origin_x, viewport.render_origin_y);
      if (recording == nullptr) break;

      QPainter painter;
      qt_renderer_rep renderer (
        &painter, viewport.render_pixel_ratio, width, height, true);
      renderer.begin (recording->device ());
      renderer.w= width;
      renderer.h= height;
      renderer.set_origin (
        viewport.render_origin_x, viewport.render_origin_y);
      SI x1= 0, y1= 0;
      SI x2= static_cast<SI> (width), y2= static_cast<SI> (height);
      renderer.encode (x1, y1);
      renderer.encode (x2, y2);
      renderer.set_clipping (x1, y2, x2, y1);
      editor->handle_repaint (&renderer, x1, y2, x2, y1);
      renderer.end ();
      (void) recording->finish ();
    }
#endif
    break;
  case actor_command_kind::set_render_connection: {
    auto found= impl_->views.find (command.view_id);
    if (found != impl_->views.end ())
      found->second.render_connection= command.argument[0];
    break;
  }
  case actor_command_kind::run_native_continuation: {
    std::function<void()> continuation=
      actor_continuation_registry::instance ().take (command.argument[0]);
    if (continuation) continuation ();
    break;
  }
  case actor_command_kind::resolve_native_callback:
    resolve_native_callback (command.argument[0], command.argument[1] != 0);
    break;
  case actor_command_kind::suspend_view:
    if (editor != nullptr) editor->suspend ();
    break;
  case actor_command_kind::resume_view:
    if (editor != nullptr) editor->resume ();
    break;
  case actor_command_kind::rename_buffer: {
    string name= actor_text_registry::instance ().take (command.payload0);
    if (N (name) != 0) {
      source_changed ();
      impl_->state.name= url (std::move (name));
      impl_->state.master= impl_->state.name;
      impl_->state.storage.reset ();
      impl_->state.storage_capture_failed= false;
      for (auto& entry: impl_->views)
        entry.second.instance->notify_change (THE_ENVIRONMENT);
      publish_tmfs_title (editor);
    }
    break;
  }
  case actor_command_kind::capture_document_storage: {
    command.argument[0]= 1;
    impl_->state.storage_capture_failed= true;
    tree capture= actor_tree_registry::instance ().take (command.payload0);
    try {
      if (!is_func (capture, TUPLE, 3) || !is_atomic (capture[0]) ||
          !is_atomic (capture[1]) || !is_atomic (capture[2]))
        throw std::invalid_argument ("Invalid document storage capture payload");
      string source_text= capture[0]->label;
      string vault_text= capture[1]->label;
      string expected_text= capture[2]->label;
      std::filesystem::path source (
        std::string (source_text.data (), (std::size_t) N(source_text)));
      std::optional<std::filesystem::path> vault;
      if (N(vault_text) != 0)
        vault= std::filesystem::path (
          std::string (vault_text.data (), (std::size_t) N(vault_text)));
      auto storage= athena::document::document_file::capture (source, vault);
      const std::string expected (
        expected_text.data (), (std::size_t) N(expected_text));
      if (!expected.empty () && storage.source_sha256 () != expected)
        throw std::system_error (
          EAGAIN, std::generic_category (),
          "Document changed between import and storage capture");
      const auto version= storage.version ();
      if (version != athena::document::xml_storage_version::v1 &&
          !impl_->state.node_identities) {
        auto identities= replacement_node_identities (
          impl_->state.source_envelope,
          subtree (impl_->state.document, impl_->state.root_path));
        if (!identities)
          throw std::invalid_argument ("Node-model source does not have a complete identity baseline");
        impl_->state.node_identities= std::move (identities);
      }
      impl_->state.storage_version= version;
      impl_->state.storage= std::move (storage);
      impl_->state.storage_capture_failed= false;
      command.argument[0]= 0;
    }
    catch (const std::exception& error) {
      std_warning << "Could not capture document storage revision: "
                  << string (error.what ()) << LF;
      impl_->state.storage.reset ();
    }
    break;
  }
  case actor_command_kind::replace_document: {
    command.argument[0]= 1;
    tree document= actor_tree_registry::instance ().take (command.payload0);
    // Loading/reloading is a baseline replacement, not a normal content edit.
    // Preflight before touching the old envelope, body, editor data or index.
    const bool supplied_format= command.argument[1] != 0;
    const bool source_v2= command.argument[1] == 2;
    const bool source_v3= command.argument[1] == 3;
    std::unique_ptr<athena::document_node::source_identity_state> identities;
    // A metadata editor owns field content, not a second document identity
    // graph. Its final value is validated and committed by the source owner.
    if (!impl_->state.field_environment &&
        (source_v2 || source_v3 || (!supplied_format && impl_->state.node_identities))) {
      new_data projected;
      tree body= detach_data (document, projected);
      // Node-model XML permits anonymous source nodes. Complete their identities on
      // the detached baseline, retaining all persisted IDs and properties.
      identities= prepare_replacement_node_identities (document, body);
      if (!identities) break;
      document= change_doc_attr (document, "body", std::move (body));
    }
    impl_->state.source_envelope= document;
    source_changed ();
    impl_->state.artifacts.reset ();
    athena::artifact::close (id_);
    tree body= detach_data (document, impl_->state.data);
    set_document (
      impl_->state.document, impl_->state.root_path, std::move (body));
    if (supplied_format) {
      impl_->state.storage_version= source_v3 ?
        athena::document::xml_storage_version::v3 : source_v2 ?
        athena::document::xml_storage_version::v2 :
        athena::document::xml_storage_version::v1;
      impl_->state.node_identities= std::move (identities);
    }
    else if (identities) impl_->state.node_identities= std::move (identities);
    for (auto& entry: impl_->views) {
      entry.second.instance->set_data (impl_->state.data);
    }
    publish_tmfs_title (editor);
    athena::node_reference::source_changed ();
    command.argument[0]= 0;
    break;
  }
  case actor_command_kind::replace_body: {
    command.argument[0]= 1;
    tree body= actor_tree_registry::instance ().take (command.payload0);
    std::unique_ptr<athena::document_node::source_identity_state> identities;
    if (impl_->state.node_identities) {
      identities= prepare_replacement_node_identities (
        attach_data (body, impl_->state.data), body);
      if (!identities) break;
    }
    assign (subtree (impl_->state.document, impl_->state.root_path),
            std::move (body));
    source_changed ();
    if (identities) impl_->state.node_identities= std::move (identities);
    impl_->state.artifacts.reset ();
    athena::artifact::close (id_);
    athena::node_reference::source_changed ();
    command.argument[0]= 0;
    break;
  }
  case actor_command_kind::set_message: {
    tree left= actor_tree_registry::instance ().take (command.payload0);
    tree right= actor_tree_registry::instance ().take (command.payload1);
    if (editor != nullptr)
      editor->set_message (
        std::move (left), std::move (right), command.argument[0] != 0);
    break;
  }
  case actor_command_kind::recall_message:
    if (editor != nullptr) editor->recall_message ();
    break;
  case actor_command_kind::init_default: {
    string variable=
      actor_text_registry::instance ().take (command.payload0);
    if (editor != nullptr) editor->init_default (std::move (variable));
    break;
  }
  case actor_command_kind::set_buffer_read_only:
    impl_->state.read_only= command.argument[0] != 0;
    for (auto& entry: impl_->views)
      entry.second.instance->publish_editor_command_state ();
    break;
  case actor_command_kind::set_buffer_title:
    impl_->state.title=
      actor_text_registry::instance ().take (command.payload0);
    break;
  case actor_command_kind::snapshot_document: {
    switch (impl_->state.storage_version) {
    case athena::document::xml_storage_version::v1: command.argument[0]= 1; break;
    case athena::document::xml_storage_version::v2: command.argument[0]= 2; break;
    case athena::document::xml_storage_version::v3: command.argument[0]= 3; break;
    default: throw std::runtime_error ("Unknown document snapshot storage version");
    }
    bool no_aux= false;
    if (editor != nullptr) {
      editor->get_data (impl_->state.data);
      no_aux= !editor->get_save_aux ();
    }
    tree body= subtree (impl_->state.document, impl_->state.root_path);
    tree snapshot= copy (attach_data (body, impl_->state.data, no_aux));
    append_interop_source_attributes (snapshot, impl_->state.source_envelope, no_aux);
    command.payload0= actor_tree_registry::instance ().store (
      std::move (snapshot));
    break;
  }
  case actor_command_kind::snapshot_body: {
    tree snapshot= copy (
      subtree (impl_->state.document, impl_->state.root_path));
    command.payload0= actor_tree_registry::instance ().store (
      std::move (snapshot));
    break;
  }
  case actor_command_kind::document_history_snapshot: {
    command.argument[0]= 1;
    try {
      editor_rep* snapshot_editor= current_editor (command.view_id);
      if (snapshot_editor == nullptr && !impl_->views.empty ())
        snapshot_editor= impl_->views.begin ()->second.instance.operator -> ();
      if (impl_->state.storage_version !=
            athena::document::xml_storage_version::v1 &&
          !impl_->state.field_environment &&
          !impl_->state.node_identities)
        throw std::runtime_error (
          "Node-model document has no active source identity index");
      if (impl_->state.node_identities && impl_->state.node_identities->pending ()) {
        if (snapshot_editor == nullptr ||
            !snapshot_editor->finish_node_identities ())
          throw std::runtime_error (
            "Source identities must be finalized before history capture");
      }
      if (snapshot_editor != nullptr) snapshot_editor->get_data (impl_->state.data);
      refresh_interop_document_source (
        impl_->state.source_envelope,
        subtree (impl_->state.document, impl_->state.root_path),
        impl_->state.data);
      tree document= remove_doc_attr (impl_->state.source_envelope, "view");
      if (!is_headless ()) {
        tree body= subtree (impl_->state.document, impl_->state.root_path);
        tree links= native_get_link_locations (impl_->state.name, body);
        document= remove_doc_attr (document, "links");
        if (N(links) != 0) document << compound ("links", links);
      }
      std::string serialized=
        impl_->state.storage_version == athena::document::xml_storage_version::v3 ?
          athena::document::write_xml_v3 (document) :
        impl_->state.storage_version == athena::document::xml_storage_version::v2 ?
          athena::document::write_xml_v2 (document) :
          athena::document::write_xml (document);
      string bytes (serialized.data (), static_cast<int> (serialized.size ()));
      if (command.response_id == ATHENA_NO_RESPONSE && snapshot_editor != nullptr) {
        (void) snapshot_editor->publish_ui_text (
          actor_command_kind::ui_document_history_snapshot, std::move (bytes),
          command.argument[1], command.argument[2], 1);
      }
      else
        command.payload0= actor_text_registry::instance ().store (std::move (bytes));
      command.argument[0]= 0;
    }
    catch (const std::exception& error) {
      std_warning << "Could not capture document history snapshot for "
                  << impl_->state.name << ": " << string (error.what ()) << LF;
      if (command.response_id == ATHENA_NO_RESPONSE && editor != nullptr)
        (void) editor->publish_ui_text (
          actor_command_kind::ui_document_history_snapshot, string (),
          command.argument[1], command.argument[2], 0);
    }
    break;
  }
  case actor_command_kind::document_search_update:
  case actor_command_kind::document_search_navigate:
  case actor_command_kind::document_replace:
  case actor_command_kind::document_search_clear: {
    string query;
    if (command.kind == actor_command_kind::document_search_update ||
        command.kind == actor_command_kind::document_replace)
      query= actor_text_registry::instance ().take (command.payload0);
    if (editor == nullptr) break;
    int replaced= -2;
    if (command.kind == actor_command_kind::document_search_update)
      editor->document_search (tree (query), command.argument[1] != 0);
    else if (command.kind == actor_command_kind::document_search_navigate)
      editor->document_search_navigate (command.argument[1] != 0,
                                       command.argument[2] != 0);
    else if (command.kind == actor_command_kind::document_replace)
      replaced= editor->document_replace (tree (query), command.argument[1] != 0);
    else editor->document_search_clear ();
    if (editor->ui_endpoint != nullptr)
      editor->ui_endpoint->publish (
        actor_command_kind::ui_document_search_state, ATHENA_NO_BLOB,
        command.argument[0], editor->document_search_current (),
        editor->document_search_total (), replaced + 2);
    break;
  }
  case actor_command_kind::request_outline: {
    if (editor == nullptr || editor->ui_endpoint == nullptr) break;
    const tree& document= subtree (
      impl_->state.document, impl_->state.root_path);
    std::uint64_t signature= athena_outline_signature (document);
    bool has_previous= command.argument[1] != 0;
    if (has_previous && command.argument[0] == signature) break;
    array<heading_word_count_entry> entries=
      athena_heading_word_count_entries (document, impl_->state.root_path);
    athena_blob_id payload=
      athena_pack_outline_snapshot (entries, signature);
    if (!editor->ui_endpoint->publish (
          actor_command_kind::ui_outline_snapshot, payload, signature))
      (void) actor_blob_registry::instance ().discard (payload);
    break;
  }
  case actor_command_kind::request_thumbnail_manifest: {
#ifdef QTTEXMACS
    auto* typeset= dynamic_cast<edit_typeset_rep*> (editor);
    if (!typeset || !editor->ui_endpoint) break;
    auto manifest= make_thumbnail_manifest (editor, typeset);
    const auto source_generation= source_epoch ();
    if (manifest.flags & THUMBNAIL_MANIFEST_SLIDES &&
        source_generation != thumbnail_source_epoch_) {
      thumbnail_slide_signatures_= thumbnail_slide_signatures (editor);
      thumbnail_content_signature_=
        thumbnail_slides_content_signature (thumbnail_slide_signatures_);
      thumbnail_source_epoch_= source_generation;
    }
    if (manifest.flags & THUMBNAIL_MANIFEST_SLIDES)
      for (std::size_t i=0; i<manifest.pages.size () &&
                            i<thumbnail_slide_signatures_.size (); ++i)
        manifest.pages[i].content_signature= thumbnail_slide_signatures_[i];
    const std::uint64_t content_signature=
      (manifest.flags & THUMBNAIL_MANIFEST_SLIDES) ?
      thumbnail_content_signature_ : 0;
    const std::uint64_t style_signature= thumbnail_style_signature (editor);
    // Paper may re-typeset for auxiliary/viewport work without any source
    // change. Those passes must not cancel a dense page's pending thumbnail.
    const std::uint64_t revision= (manifest.flags & THUMBNAIL_MANIFEST_SLIDES) ?
      typeset->thumbnail_layout_revision () : source_generation;
    const std::uint64_t signature= thumbnail_signature (
      style_signature, manifest, revision, content_signature);
    if (command.argument[1] && command.argument[0] == signature) break;
    const std::size_t bytes= sizeof (thumbnail_manifest_header) +
      manifest.pages.size () * sizeof (thumbnail_page_bounds);
    auto reservation= actor_blob_registry::instance ().allocate (bytes);
    thumbnail_manifest_header header {signature, style_signature,
      content_signature,
      static_cast<std::uint32_t> (manifest.pages.size ()),
      manifest.active, manifest.flags, 0};
    std::memcpy (reservation.data (), &header, sizeof (header));
    if (!manifest.pages.empty ())
      std::memcpy (reservation.data () + sizeof (header), manifest.pages.data (),
                   manifest.pages.size () * sizeof (thumbnail_page_bounds));
    const auto payload= reservation.publish ();
    if (!editor->ui_endpoint->publish (
          actor_command_kind::ui_thumbnail_manifest, payload, signature))
      (void) actor_blob_registry::instance ().discard (payload);
#endif
    break;
  }
  case actor_command_kind::request_thumbnail_raster: {
#ifdef QTTEXMACS
    auto* typeset= dynamic_cast<edit_typeset_rep*> (editor);
    if (!typeset) break;
    auto manifest= make_thumbnail_manifest (editor, typeset);
    const int index= static_cast<int> (command.argument[0]);
    if (index < 0 || index >= static_cast<int> (manifest.pages.size ())) break;
    const auto source_generation= source_epoch ();
    if (manifest.flags & THUMBNAIL_MANIFEST_SLIDES &&
        source_generation != thumbnail_source_epoch_) {
      thumbnail_slide_signatures_= thumbnail_slide_signatures (editor);
      thumbnail_content_signature_=
        thumbnail_slides_content_signature (thumbnail_slide_signatures_);
      thumbnail_source_epoch_= source_generation;
    }
    if (manifest.flags & THUMBNAIL_MANIFEST_SLIDES)
      for (std::size_t i=0; i<manifest.pages.size () &&
                            i<thumbnail_slide_signatures_.size (); ++i)
        manifest.pages[i].content_signature= thumbnail_slide_signatures_[i];
    const std::uint64_t revision= (manifest.flags & THUMBNAIL_MANIFEST_SLIDES) ?
      typeset->thumbnail_layout_revision () : source_generation;
    const auto signature= thumbnail_signature (
      thumbnail_style_signature (editor), manifest, revision,
      (manifest.flags & THUMBNAIL_MANIFEST_SLIDES) ?
        thumbnail_content_signature_ : 0);
    if (signature != command.argument[1]) break;
    auto target= (manifest.flags & THUMBNAIL_MANIFEST_SLIDES) ?
      (index == static_cast<int> (manifest.active) ?
        typeset->thumbnail_page_box (0) :
        typeset->thumbnail_hidden_slide_box (index)) :
      typeset->thumbnail_page_box (index);
    box page= target.page;
    if (is_nil (page)) break;
    const double pageWidth= double (page->x2-page->x1);
    const double pageHeight= double (page->y2-page->y1);
    if (pageWidth <= 0 || pageHeight <= 0) break;
    constexpr int maxWidth= 190, maxHeight= 270;
    const double scale= std::min (double (maxWidth)/pageWidth,
                                  double (maxHeight)/pageHeight);
    const int width= std::max (1, int (std::ceil (pageWidth * scale)));
    const int height= std::max (1, int (std::ceil (pageHeight * scale)));
    const tree background= effective_document_background (
      typeset->get_init_value ("bg-color"));
    brush backgroundBrush (background);
    int red, green, blue, alpha;
    get_rgb_color (backgroundBrush->get_color (), red, green, blue, alpha);
    const std::uint32_t argb=
      (std::uint32_t (alpha) << 24) | (std::uint32_t (red) << 16) |
      (std::uint32_t (green) << 8) | std::uint32_t (blue);
    render_damage damage {0, 0, width, height};
    auto recording= qtm_begin_render_recording (
      command.argument[2], width, height, 1.0, argb,
      signature, std::uint64_t (index) + 1, damage, 0, 0, true);
    if (!recording) break;
    QPainter painter;
    qt_renderer_rep renderer (&painter, 1.0, width, height, true);
    renderer.begin (recording->device ());
    renderer.set_zoom_factor (scale * std_shrinkf * PIXEL, false);
    renderer.set_background (background);
    (void) typeset->paint_thumbnail_page (&renderer, target);
    renderer.end ();
    (void) recording->finish ();
#endif
    break;
  }
  case actor_command_kind::activate_thumbnail_page: {
    auto* typeset= dynamic_cast<edit_typeset_rep*> (editor);
    auto* interface= dynamic_cast<edit_interface_rep*> (editor);
    if (!typeset || !interface) break;
    const int index= static_cast<int> (command.argument[0]);
    const auto manifest= make_thumbnail_manifest (editor, typeset);
    if (manifest.flags & THUMBNAIL_MANIFEST_SLIDES) {
      if (index >= 0 && index < static_cast<int> (manifest.pages.size ()))
        presentation_switch_to (index);
      break;
    }
    const auto& pages= manifest.pages;
    if (index < 0 || index >= static_cast<int> (pages.size ())) break;
    const auto& page= pages[static_cast<std::size_t> (index)];
    interface->scroll_to ((page.x1+page.x2)/2, (page.y1+page.y2)/2);
    break;
  }
  case actor_command_kind::edit_screen: {
#ifdef QTTEXMACS
    // The source tree stays on BufferActor. The UI only receives serialized
    // clipboard data and the confirmation prompt through a GUI continuation.
    owned_actor_blob payload=
      actor_blob_registry::instance ().take (command.payload0);
    if (!editor) break;
    const auto operation= static_cast<screen_edit_operation> (command.argument[0]);
    if (operation > screen_edit_operation::paste_after) break;
    int index= static_cast<int> (command.argument[1]);
    if (index < 0) index= presentation_active_screen ();
    tree content= presentation_screen_copy (index);
    if (content == tree (UNINIT)) break;
    if (operation == screen_edit_operation::copy) {
      if (!editor->ui_endpoint) break;
      const std::string xml= athena::document::write_xml_v3 (
        content, athena::document::xml_kind::fragment);
      (void) editor->ui_endpoint->publish_continuation ([xml] {
        thumbnails_screen_set_clipboard (xml);
      });
      break;
    }
    if (editor->editor_command_state_snapshot ().read_only ()) break;
    if (operation == screen_edit_operation::erase &&
        !presentation_screen_empty (index)) {
      const auto stamp= static_cast<std::uint64_t> (
        static_cast<std::uint32_t> (hash (content))) + 1;
      if (command.argument[2] != stamp) {
        if (command.argument[2] == 0 && editor->ui_endpoint)
          (void) editor->ui_endpoint->publish_continuation (
            [actorId=id_, targetView=command.view_id, index, stamp] {
              thumbnails_screen_confirm_delete (
                actorId, targetView, index, stamp);
            });
        break;
      }
    }
    tree paste_content (UNINIT);
    if (operation == screen_edit_operation::paste_before ||
        operation == screen_edit_operation::paste_after) {
      if (!payload) break;
      paste_content= athena::document::read_xml_v3 (
        std::string_view (reinterpret_cast<const char*> (payload.data ()),
                          payload.size ()),
        athena::document::xml_kind::fragment);
    }
    if (presentation_edit_screen (operation, index, paste_content)) {
      editor->publish_editor_command_state ();
      editor->publish_focus_toolbar_state ();
    }
#endif
    break;
  }
  case actor_command_kind::activate_outline_entry: {
    owned_actor_blob payload=
      actor_blob_registry::instance ().take (command.payload0);
    std::size_t count= static_cast<std::size_t> (command.argument[0]);
    if (editor == nullptr || !payload ||
        count > payload.size () / sizeof (std::int32_t) ||
        count * sizeof (std::int32_t) != payload.size ())
      break;
    path target;
    const std::int32_t* items=
      reinterpret_cast<const std::int32_t*> (payload.data ());
    for (std::size_t i= count; i != 0; --i)
      target= path (static_cast<int> (items[i - 1]), target);
    editor->focus_on_this_editor ();
    editor->go_to_start (target);
    break;
  }
  case actor_command_kind::set_master_buffer: {
    string master= actor_text_registry::instance ().take (command.payload0);
    impl_->state.master= url (std::move (master));
    for (auto& entry: impl_->views)
      entry.second.instance->notify_change (THE_ENVIRONMENT);
    break;
  }
  case actor_command_kind::notify_environment:
    for (auto& entry: impl_->views)
      entry.second.instance->notify_change (THE_ENVIRONMENT);
    break;
  case actor_command_kind::contracts_ready:
    if (editor != nullptr) {
      editor->prepare_cursor_environment ();
      editor->notify_change (THE_CURSOR | THE_FOCUS);
    }
    break;
  case actor_command_kind::resume_hybrid_activation:
    if (editor != nullptr)
      editor->resume_pending_hybrid_activation (command.argument[0]);
    break;
  case actor_command_kind::query_modified:
  case actor_command_kind::query_autosaved: {
    bool autosave= command.kind == actor_command_kind::query_autosaved;
    bool modified= !impl_->state.read_only && (autosave ? impl_->state.source_autosave_modified :
                                                        impl_->state.source_modified);
    if (!impl_->state.read_only)
      for (auto& entry: impl_->views)
        if (entry.second.instance->need_save (!autosave)) {
          modified= true;
          break;
        }
    command.argument[0]= modified ? 1 : 0;
    break;
  }
  case actor_command_kind::mark_modified:
    for (auto& entry: impl_->views) entry.second.instance->require_save ();
    break;
  case actor_command_kind::mark_saved:
    // Commit the timestamp before a save continuation may start another save.
    // Never make the actor wait for its queued UI mirror to catch up.
    impl_->state.last_save= last_modified (impl_->state.name);
    impl_->state.source_modified= impl_->state.source_autosave_modified= false;
    for (auto& entry: impl_->views) entry.second.instance->notify_save ();
    command.argument[0]= static_cast<std::uint64_t> (impl_->state.last_save);
    break;
  case actor_command_kind::mark_autosaved:
    impl_->state.source_autosave_modified= false;
    for (auto& entry: impl_->views)
      entry.second.instance->notify_save (false);
    break;
  case actor_command_kind::set_realtime_save_paused: {
    impl_->state.realtime_save_paused= command.argument[0] != 0;
    editor_rep* target= editor;
    if (target == nullptr || target->ui_endpoint == nullptr) {
      target= nullptr;
      for (auto& entry: impl_->views)
        if (entry.second.instance->ui_endpoint != nullptr) {
          target= entry.second.instance.operator -> ();
          break;
        }
    }
    if (target != nullptr)
      (void) target->publish_ui (
        actor_command_kind::ui_realtime_save_state,
        impl_->state.realtime_save_paused ? 1 : 0, 0, 1);
    break;
  }
  case actor_command_kind::save_buffer:
  case actor_command_kind::realtime_save_buffer: {
    const bool realtime=
      command.kind == actor_command_kind::realtime_save_buffer;
    command.argument[0]= 1;
    string vault_text;
    if (command.payload0 != ATHENA_NO_BLOB)
      vault_text= actor_text_registry::instance ().take (command.payload0);
    editor_rep* save_editor= current_editor (command.view_id);
    if (!save_editor && !impl_->views.empty ())
      save_editor= impl_->views.begin ()->second.instance.operator -> ();
    auto publish_realtime_state= [&] (bool completed, bool success) {
      if (!realtime) return;
      editor_rep* target= save_editor;
      if (target == nullptr || target->ui_endpoint == nullptr) {
        target= nullptr;
        for (auto& entry: impl_->views)
          if (entry.second.instance->ui_endpoint != nullptr) {
            target= entry.second.instance.operator -> ();
            break;
          }
      }
      if (target != nullptr)
        (void) target->publish_ui (
          actor_command_kind::ui_realtime_save_state,
          impl_->state.realtime_save_paused ? 1 : 0,
          completed ? 1 : 0, success ? 1 : 0);
    };
    if (realtime) {
      if (athena_current_document_save_mode () !=
          athena_document_save_mode::realtime) {
        command.argument[0]= 0;
        publish_realtime_state (true, true);
        break;
      }
      bool modified= impl_->state.source_modified;
      for (auto& entry: impl_->views)
        if (entry.second.instance->need_save (true)) {
          modified= true;
          break;
        }
      if (impl_->state.realtime_save_paused || !modified) {
        command.argument[0]= 0;
        publish_realtime_state (true, true);
        break;
      }
    }
    try {
      if (impl_->state.storage_version != athena::document::xml_storage_version::v1 &&
          !impl_->state.node_identities)
        throw std::runtime_error ("Node-model document has no active source identity index");
      if (save_editor && impl_->state.node_identities &&
          !impl_->state.artifacts.ready (impl_->state)) {
        tree body= save_editor->the_buffer ();
        save_editor->start_editing ();
        try { impl_->state.artifacts.update (impl_->state, body, {}); }
        catch (...) { save_editor->cancel_editing (); impl_->state.artifacts.reset (); throw; }
        save_editor->end_editing ();
      }
      if (impl_->state.node_identities && impl_->state.node_identities->pending ()) {
        if (!save_editor || !save_editor->finish_node_identities ())
          throw std::runtime_error ("Source identities must be finalized before saving");
      }
      if (save_editor != nullptr) save_editor->get_data (impl_->state.data);
      refresh_interop_document_source (
        impl_->state.source_envelope,
        subtree (impl_->state.document, impl_->state.root_path),
        impl_->state.data);
      tree document= impl_->state.source_envelope;
      document= remove_doc_attr (document, "view");
      if (!is_headless ()) {
        tree body= subtree (impl_->state.document, impl_->state.root_path);
        tree links= native_get_link_locations (impl_->state.name, body);
        document= remove_doc_attr (document, "links");
        if (N(links) != 0) document << compound ("links", links);
      }
      string native= as_string (concretize (impl_->state.name), URL_SYSTEM);
      if (N(native) == 0)
        throw std::runtime_error ("Could not resolve local document path");
      std::filesystem::path path (
        std::string (native.data (), (std::size_t) N(native)));
      athena::document::document_save_result saved;
      std::optional<std::string> saved_predecessor;
      if (impl_->state.storage) {
        saved_predecessor= impl_->state.storage->source_sha256 ();
        saved= impl_->state.storage->save (document);
      }
      else {
        if (impl_->state.storage_capture_failed)
          throw std::runtime_error (
            "Document storage revision was not captured when the file was opened");
        if (std::filesystem::exists (path)) {
          std::optional<std::filesystem::path> vault;
          if (N(vault_text) != 0)
            vault= std::filesystem::path (
              std::string (vault_text.data (), (std::size_t) N(vault_text)));
          auto storage= athena::document::document_file::capture (path, vault);
          if (storage.version () != impl_->state.storage_version)
            throw std::runtime_error (
              "Save target uses a different ATHENA XML persistence version");
          impl_->state.storage= std::move (storage);
          saved_predecessor= impl_->state.storage->source_sha256 ();
          saved= impl_->state.storage->save (document);
        }
        else
          impl_->state.storage= athena::document::document_file::create (
            path, document, saved, impl_->state.storage_version);
      }
      if (saved.durability == athena::document::upgrade_durability::durable)
      {
#ifdef QTTEXMACS
        if (impl_->state.storage_version == athena::document::xml_storage_version::v3) {
          try {
            qtm_hodarium_saved (path,
              athena::node::id (subtree (impl_->state.document, impl_->state.root_path)),
              saved_predecessor, saved.committed_bytes);
          }
          catch (const std::exception& e) {
            std_warning << "Hodarium saved revision capture failed: " << string (e.what ()) << LF;
          }
        }
#endif
        athena::artifact::saved (path, saved.xml_sha256);
        command.argument[0]= 0;
        if (realtime) {
          impl_->state.last_save= last_modified (impl_->state.name);
          impl_->state.source_modified=
            impl_->state.source_autosave_modified= false;
          for (auto& entry: impl_->views)
            entry.second.instance->notify_save ();
          if (save_editor != nullptr) {
            (void) save_editor->publish_ui (
              actor_command_kind::ui_mark_buffer_saved,
              static_cast<std::uint64_t> (impl_->state.last_save));
            // Realtime saves bypass the manual save post-processing.
            (void) save_editor->publish_ui_text (
              actor_command_kind::ui_vault_backup_dispatch_realtime,
              copy (native));
          }
        }
        else if (N(vault_text) != 0 && !saved.xml_sha256.empty ()) {
          const std::uint64_t save_sequence=
            continuous_rag_save_sequence.fetch_add (
              1, std::memory_order_relaxed);
          athena::rag::rag_note_saved_generation (path, save_sequence);
          editor_rep* target= save_editor;
          if (target == nullptr || target->ui_endpoint == nullptr) {
            target= nullptr;
            for (auto& entry: impl_->views)
              if (entry.second.instance->ui_endpoint != nullptr) {
                target= entry.second.instance.operator -> ();
                break;
              }
          }
          if (target != nullptr && target->ui_endpoint != nullptr) {
            string metadata= copy (vault_text);
            metadata << '\0';
            metadata << string (saved.xml_sha256.data (),
                                static_cast<int> (saved.xml_sha256.size ()));
            (void) target->ui_endpoint->publish_text_pair (
              actor_command_kind::ui_continuous_rag_saved, copy (native),
              std::move (metadata), save_sequence);
          }
        }
      }
      else
        std_warning << "Document was replaced but directory sync failed for "
                    << impl_->state.name << LF;
    }
    catch (const std::exception& error) {
      std_warning << "Could not save XML document " << impl_->state.name << ": "
                   << string (error.what ()) << LF;
    }
    if (realtime && command.argument[0] != 0)
      impl_->state.realtime_save_paused= true;
    publish_realtime_state (true, command.argument[0] == 0);
    break;
  }
  case actor_command_kind::export_buffer: {
    // A thrown conversion/export must not leave the default success value in
    // a synchronous reply. execute() reports exceptions but still replies.
    command.argument[0]= 1;
    string destination=
      actor_text_registry::instance ().take (command.payload0);
    auto request= decode_conversion_export_request (
      actor_text_registry::instance ().take (command.payload1));
    string format= request.format;
    url dest (std::move (destination));
    editor_rep* export_editor= editor;
    if (export_editor == nullptr && command.view_id == ATHENA_NO_VIEW &&
        !impl_->views.empty ())
      export_editor=
        impl_->views.begin ()->second.instance.operator -> ();
    bool failed= export_editor == nullptr;
    if (!failed && format == "postscript")
      throw std::runtime_error ("PostScript export is not supported; use PDF");
    if (!failed && format == "pdf") {
      int old_stamp= last_modified (dest, false);
      export_editor->print_to_file (dest);
      int new_stamp= last_modified (dest, false);
      failed= new_stamp <= old_stamp;
    }
    else if (!failed && format == "texmacs" &&
             impl_->state.storage_version != athena::document::xml_storage_version::v1) {
      if (!impl_->state.node_identities ||
          (impl_->state.node_identities->pending () &&
           !export_editor->finish_node_identities ())) {
        failed= true;
      }
      else {
        export_editor->get_data (impl_->state.data);
        tree body= subtree (impl_->state.document, impl_->state.root_path);
        refresh_interop_document_source (
          impl_->state.source_envelope, body, impl_->state.data);
        tree document= remove_doc_attr (impl_->state.source_envelope, "view");
        tree links= native_get_link_locations (impl_->state.name, body);
        document= remove_doc_attr (document, "links");
        if (N(links) != 0) document << compound ("links", links);
        document= athena::document::strip_legacy_document_version (document);
        const std::string xml=
          impl_->state.storage_version == athena::document::xml_storage_version::v3 ?
            athena::document::write_xml_v3 (document) :
            athena::document::write_xml_v2 (document);
        string serialized (xml.data (), static_cast<int> (xml.size ()));
        failed= ends (as_string (dest), "~") ?
          save_autosave_string (dest, serialized) :
          save_string (dest, serialized);
      }
    }
    else if (!failed) {
      tree body= subtree (
        impl_->state.document, impl_->state.root_path);
      if (format == "verbatim") body= export_editor->exec_verbatim (body);
      if (format == "html") body= export_editor->exec_html (body);
      export_editor->get_data (impl_->state.data);
      tree document= attach_data (
        body, impl_->state.data, !export_editor->get_save_aux ());
      if (format == "latex")
        document= change_doc_attr (
          document, "view", as_string (current_view_url (command.view_id)));
      tree links= native_get_link_locations (impl_->state.name, body);
      if (N (links) != 0) document << compound ("links", links);
      // A transient website/export buffer can have a different master source.
      // Resolve HTML images and included files against that source, not the
      // temporary .ath file holding the export snapshot.
      url origin= format == "html" && !is_none (impl_->state.master)
        ? impl_->state.master : impl_->state.name;
      failed= export_tree_with_options (document, dest, format, origin,
                                        request.options, export_editor);
    }
    command.argument[0]= failed ? 1 : 0;
    break;
  }
  case actor_command_kind::latex_expand_buffer: {
    tree document= actor_tree_registry::instance ().take (command.payload0);
    editor_rep* expand_editor= editor;
    if (expand_editor == nullptr && !impl_->views.empty ())
      expand_editor=
        impl_->views.begin ()->second.instance.operator -> ();
    if (expand_editor != nullptr) {
      tree body= expand_editor->exec_latex (extract (document, "body"));
      document= change_doc_attr (document, "body", body);
    }
    command.payload0= actor_tree_registry::instance ().store (
      std::move (document));
    break;
  }
  default:
    athena_spdlog_error ("unimplemented buffer actor command " +
      std::to_string (static_cast<unsigned int> (command.kind)));
    break;
  }

  if (editor != nullptr &&
      (command.kind == actor_command_kind::key_press ||
       command.kind == actor_command_kind::text_input ||
       command.kind == actor_command_kind::mouse ||
       command.kind == actor_command_kind::native_ink_stroke ||
       command.kind == actor_command_kind::native_drawing_recognition ||
       command.kind == actor_command_kind::native_drawing_transform ||
       command.kind == actor_command_kind::native_drawing_insert_space ||
       command.kind == actor_command_kind::native_drawing_trim ||
       command.kind == actor_command_kind::replace_document ||
       command.kind == actor_command_kind::replace_body ||
       command.kind == actor_command_kind::run_native_continuation ||
       command.kind == actor_command_kind::resolve_native_callback))
    editor->mark_native_ink_interaction_dirty ();

  if (editor != nullptr && editor->ui_endpoint != nullptr &&
      (command.kind == actor_command_kind::initialize_view ||
       command.kind == actor_command_kind::apply_changes ||
       command.kind == actor_command_kind::typeset_document ||
       command.kind == actor_command_kind::progressive_typeset ||
       command.kind == actor_command_kind::render_view ||
       command.kind == actor_command_kind::key_press ||
       command.kind == actor_command_kind::text_input ||
       command.kind == actor_command_kind::mouse ||
       command.kind == actor_command_kind::replace_document ||
       command.kind == actor_command_kind::replace_body ||
       command.kind == actor_command_kind::activate_outline_entry) &&
      editor->cursor_context_at (editor->the_path ()))
    editor->ui_endpoint->set_wheel_capture (
      editor->inside_active_graphics ());

  if (editor != nullptr && editor->ui_endpoint != nullptr &&
      (command.kind == actor_command_kind::initialize_view ||
       command.kind == actor_command_kind::render_view))
    editor->refresh_native_ink_interaction ();
}

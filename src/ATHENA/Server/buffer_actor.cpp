/******************************************************************************
* MODULE     : buffer_actor.cpp
* DESCRIPTION: Per-buffer execution owner and ID-only command mailbox
* COPYRIGHT  : (C) 2026  Nuaptan F. Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "buffer_actor.hpp"

#include "actor_ui_bridge.hpp"
#include "buffer_state.hpp"
#include "convert.hpp"
#include "Data/new_buffer.hpp"
#include "Data/new_view.hpp"
#include "editor.hpp"
#include "file.hpp"
#include "guile_tm.hpp"
#include "glue.hpp"
#include "object.hpp"
#include "outline_snapshot.hpp"
#include "tm_buffer.hpp"
#include "tm_window.hpp"

#ifdef QTTEXMACS
#include "QTMRenderService.hpp"
#include "qt_renderer.hpp"
#include <QPainter>
#endif

#include <cstdio>
#include <cmath>
#include <cstring>
#include <exception>
#include <unordered_map>
#include <utility>

namespace {

std::mutex actor_registry_lock;
std::unordered_map<athena_actor_id, buffer_actor*> actor_registry;
athena_actor_id next_actor_id= 1;

struct actor_wait_request {
  actor_command_transport* transport;
  actor_command_transport::readable_command* command;
  bool received;
};

struct actor_dispatch_request {
  buffer_actor* actor;
  actor_command_record* command;
  std::exception_ptr failure;
};

tmscm
dispatch_actor_command (void* raw) {
  actor_dispatch_request* request=
    static_cast<actor_dispatch_request*> (raw);
  try { request->actor->dispatch (*request->command); }
  catch (...) { request->failure= std::current_exception (); }
  return TMSCM_UNSPECIFIED;
}

void
report_unhandled_actor_exception (std::exception_ptr failure) noexcept {
  try {
    if (failure != nullptr) std::rethrow_exception (failure);
  }
  catch (const std::exception& error) {
    std::fprintf (stderr, "ATHENA] buffer actor command failed: %s\n",
                  error.what ());
  }
  catch (...) {
    std::fprintf (stderr, "ATHENA] buffer actor command failed\n");
  }
}

athena_actor_id
register_actor (buffer_actor* actor) {
  std::lock_guard<std::mutex> guard (actor_registry_lock);
  athena_actor_id id= next_actor_id++;
  if (id == ATHENA_NO_ACTOR)
    FAILED ("buffer actor id space exhausted");
  actor_registry.emplace (id, actor);
  return id;
}

void
unregister_actor (athena_actor_id id) noexcept {
  std::lock_guard<std::mutex> guard (actor_registry_lock);
  actor_registry.erase (id);
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

  implementation (buffer_actor* actor, string name, string master,
                  string title, bool read_only):
    state (actor, std::move (name), std::move (master), std::move (title),
           read_only), views () {}
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

  tree& body= subtree (impl_->state.document, impl_->state.root_path);
  string title= as_string (
    call ("tmfs-title", as_string (impl_->state.name), object (body)));
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
  initial_read_only_ (owner->buf->read_only), commands_ (128),
  owner_thread_ (), next_command_id_ (1), next_response_id_ (1),
  completed_command_id_ (0), completed_commands_ (0), accepting_ (true),
  started_ (false), impl_ () {}

buffer_actor::~buffer_actor () {
  shutdown ();
  unregister_actor (id_);
}

athena_actor_id
buffer_actor::id () const noexcept {
  return id_;
}

actor_command_ticket
buffer_actor::submit (
  actor_command_kind kind, athena_view_id view_id,
  athena_blob_id payload0, athena_blob_id payload1,
  SchemeCapabilitySet capabilities, std::uint64_t argument0,
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
  SchemeCapabilitySet capabilities, std::uint64_t argument0,
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
  SchemeCapabilitySet capabilities, athena_response_id response_id,
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
  SchemeCapabilitySet capabilities, std::uint64_t argument0,
  std::uint64_t argument1, std::uint64_t argument2,
  std::uint64_t argument3, std::uint64_t argument4,
  std::uint64_t argument5, std::uint64_t argument6,
  std::uint64_t argument7) {
  buffer_actor* actor= lookup (actor_id);
  return actor == nullptr ? actor_command_ticket {} : actor->submit (
    kind, view_id, payload0, payload1, capabilities, argument0, argument1,
    argument2, argument3, argument4, argument5, argument6, argument7);
}

bool
buffer_actor::try_submit_coalesced_to (
  athena_actor_id actor_id, actor_command_kind kind, athena_view_id view_id,
  std::uint64_t argument0, std::uint64_t argument1,
  std::uint64_t argument2, std::uint64_t argument3) {
  buffer_actor* actor= lookup (actor_id);
  actor_ui_endpoint* endpoint= find_actor_ui_endpoint (view_id);
  if (actor == nullptr || endpoint == nullptr) return false;
  if (!endpoint->begin_coalesced_command (kind)) return true;
  actor_command_ticket ticket= actor->try_submit (
    kind, view_id, ATHENA_NO_BLOB, ATHENA_NO_BLOB,
    SCHEME_CAPABILITY_BUFFER, argument0, argument1, argument2, argument3);
  if (!ticket) endpoint->finish_coalesced_command (kind);
  return static_cast<bool> (ticket);
}

bool
buffer_actor::invoke_on (
  athena_actor_id actor_id, actor_command_kind kind, athena_view_id view_id,
  athena_blob_id payload0, athena_blob_id payload1,
  actor_command_record* result, SchemeCapabilitySet capabilities,
  std::uint64_t argument0, std::uint64_t argument1,
  std::uint64_t argument2, std::uint64_t argument3,
  std::uint64_t argument4, std::uint64_t argument5,
  std::uint64_t argument6, std::uint64_t argument7) {
  buffer_actor* actor= lookup (actor_id);
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
buffer_actor::invoke (
  actor_command_kind kind, athena_view_id view_id,
  athena_blob_id payload0, athena_blob_id payload1,
  actor_command_record* result, SchemeCapabilitySet capabilities,
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
  if (!accepting_ || !scheme_runtime_is_initialized ()) return false;
  if (!worker_.joinable ()) {
    athena_actor_id actor_id= id_;
    worker_= std::thread ([actor_id] { thread_entry (actor_id); });
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

url
buffer_actor::current_view_url (athena_view_id view_id) const {
  ASSERT (is_owner_thread (), "view URL accessed outside its actor");
  auto found= impl_->views.find (view_id);
  if (found != impl_->views.end ())
    return make_abstract_view_url (
      impl_->state.name, found->second.legacy_number);
  return url_none ();
}

buffer_actor*
buffer_actor::lookup (athena_actor_id id) noexcept {
  std::lock_guard<std::mutex> guard (actor_registry_lock);
  auto found= actor_registry.find (id);
  return found == actor_registry.end () ? nullptr : found->second;
}

void
buffer_actor::thread_entry (athena_actor_id id) {
  scm_with_guile (guile_entry,
                  reinterpret_cast<void*> (static_cast<std::uintptr_t> (id)));
}

void*
buffer_actor::guile_entry (void* raw_id) {
  athena_actor_id id= static_cast<athena_actor_id> (
    reinterpret_cast<std::uintptr_t> (raw_id));
  buffer_actor* actor= lookup (id);
  if (actor != nullptr) actor->run_in_guile ();
  return nullptr;
}

void*
buffer_actor::wait_without_guile (void* raw_request) {
  actor_wait_request* request= static_cast<actor_wait_request*> (raw_request);
  request->received= request->transport->wait_command (*request->command);
  return nullptr;
}

void
buffer_actor::run_in_guile () {
  string initial_name= actor_text_registry::instance ().take (initial_name_);
  string initial_master= actor_text_registry::instance ().take (initial_master_);
  string initial_title= actor_text_registry::instance ().take (initial_title_);
  initial_name_= ATHENA_NO_BLOB;
  initial_master_= ATHENA_NO_BLOB;
  initial_title_= ATHENA_NO_BLOB;
  impl_= std::make_unique<implementation> (
    this, std::move (initial_name), std::move (initial_master),
    std::move (initial_title), initial_read_only_);
  {
    std::lock_guard<std::mutex> guard (state_lock_);
    owner_thread_= std::this_thread::get_id ();
    started_= true;
  }
  started_condition_.notify_all ();

  while (true) {
    actor_command_transport::readable_command command;
    actor_wait_request request= {&commands_, &command, false};
    scm_without_guile (wait_without_guile, &request);
    if (!request.received) break;

    execute (*command.record);
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
    completed_condition_.notify_all ();
    scheme_runtime_safe_point ();
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
buffer_actor::execute (actor_command_record& command) {
  editor_rep* editor= current_editor (command.view_id);
  drd_info* drd= editor == nullptr ? nullptr : &editor->drd;
  SchemeExecutionContext context (
    this, editor, drd, impl_ == nullptr ? nullptr : &impl_->state.document,
    id_, command.view_id,
    command.command_id, command.capabilities);
  actor_dispatch_request request= {this, &command, nullptr};
  try {
    (void) scheme_with_execution_context (
      context, dispatch_actor_command, &request);
  }
  catch (...) { request.failure= std::current_exception (); }
  if (editor != nullptr && editor->ui_endpoint != nullptr &&
      (command.kind == actor_command_kind::apply_changes ||
       command.kind == actor_command_kind::animate ||
       command.kind == actor_command_kind::progressive_typeset ||
       command.kind == actor_command_kind::render_view ||
       command.kind == actor_command_kind::request_outline))
    editor->ui_endpoint->finish_coalesced_command (command.kind);
  report_unhandled_actor_exception (request.failure);
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
    impl_->views.emplace (
      command.view_id,
      implementation::actor_view {
        static_cast<int> (command.argument[0]), std::move (created), 0, 1, 0});
    break;
  }
  case actor_command_kind::destroy_view:
    if (editor != nullptr) {
      editor->buf= nullptr;
      impl_->views.erase (command.view_id);
    }
    break;
  case actor_command_kind::initialize_view:
    if (editor != nullptr) {
      initialize_current_view_scheme ();
      publish_tmfs_title (editor);
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
  case actor_command_kind::animate:
    if (editor != nullptr) editor->animate ();
    break;
  case actor_command_kind::progressive_typeset:
    if (editor != nullptr) editor->schedule_progressive_typeset ();
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
  case actor_command_kind::keyboard_focus:
    if (editor != nullptr)
      editor->handle_keyboard_focus (
        command.argument[0] != 0, static_cast<time_t> (command.argument[1]));
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
  case actor_command_kind::set_zoom:
    if (editor != nullptr)
      editor->handle_set_zoom_factor (argument_double (command.argument[0]));
    break;
  case actor_command_kind::change_zoom:
    if (editor != nullptr)
      call ("change-zoom-factor",
            object (argument_double (command.argument[0])));
    break;
  case actor_command_kind::zoom_by:
    if (editor != nullptr)
      call (command.argument[0] != 0 ? "zoom-in" : "zoom-out",
            object (argument_double (command.argument[1])));
    break;
  case actor_command_kind::viewport_changed:
    if (editor != nullptr)
      editor->handle_notify_resize (
        static_cast<SI> (command.argument[0]),
        static_cast<SI> (command.argument[1]));
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
          found->second.buffer_generation, frame_generation, damage);
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
  case actor_command_kind::run_scheme_handle: {
    athena_scheme_handle_id handle= command.argument[0];
    bool allow_repeat= command.argument[1] != 0;
    bool repeat= false;
    std::int64_t delay= 0;
    std::exception_ptr failure;
    try {
      tmscm procedure= scheme_command_handle_value (handle);
      if (scm_is_eq (procedure, SCM_UNDEFINED))
        FAILED ("delayed Scheme command handle is no longer live");
      tmscm result= call_scheme (procedure);
      if (allow_repeat && tmscm_is_int (result)) {
        repeat= true;
        delay= static_cast<std::int64_t> (tmscm_to_int (result));
      }
    }
    catch (...) { failure= std::current_exception (); }

    bool returned= editor != nullptr && editor->publish_ui (
      actor_command_kind::ui_scheme_completed, handle,
      repeat ? 1 : 0, static_cast<std::uint64_t> (delay));
    if (!returned) scheme_command_handle_release (handle);
    if (failure != nullptr) std::rethrow_exception (failure);
    break;
  }
  case actor_command_kind::invoke_scheme_handle: {
    athena_scheme_handle_id handle= command.argument[0];
    athena_scheme_handle_id arguments= command.argument[1];
    try {
      tmscm procedure= scheme_command_handle_value (handle);
      if (scm_is_eq (procedure, SCM_UNDEFINED))
        FAILED ("Scheme command handle is no longer live");
      if (arguments == ATHENA_NO_SCHEME_HANDLE)
        (void) call_scheme (procedure);
      else {
        tmscm value= scheme_command_handle_value (arguments);
        if (scm_is_eq (value, SCM_UNDEFINED))
          FAILED ("Scheme argument handle is no longer live");
        object args= tmscm_to_object (value);
        array<object> values= as_array_object (args);
        array<tmscm> arguments_scm (N (values));
        for (int i= 0; i < N (values); ++i)
          arguments_scm[i]= object_to_tmscm (values[i]);
        (void) call_scheme (procedure, arguments_scm);
      }
    }
    catch (...) {
      scheme_command_handle_release (handle);
      scheme_command_handle_release (arguments);
      throw;
    }
    scheme_command_handle_release (handle);
    scheme_command_handle_release (arguments);
    break;
  }
  case actor_command_kind::invoke_scheme_handle_tree: {
    athena_scheme_handle_id handle= command.argument[0];
    if (editor == nullptr) {
      (void) actor_tree_registry::instance ().discard (command.payload0);
      scheme_command_handle_release (handle);
      break;
    }
    tree value= actor_tree_registry::instance ().take (command.payload0);
    try {
      tmscm procedure= scheme_command_handle_value (handle);
      if (scm_is_eq (procedure, SCM_UNDEFINED))
        FAILED ("Scheme command handle is no longer live");
      (void) call_scheme (procedure, tree_to_tmscm (value));
    }
    catch (...) {
      scheme_command_handle_release (handle);
      throw;
    }
    scheme_command_handle_release (handle);
    break;
  }
  case actor_command_kind::evaluate_widget_handle: {
    athena_scheme_handle_id handle= command.argument[0];
    try {
      tmscm procedure= scheme_command_handle_value (handle);
      if (scm_is_eq (procedure, SCM_UNDEFINED))
        FAILED ("Scheme widget promise handle is no longer live");
      tmscm result= call_scheme (procedure);
      if (!tmscm_is_widget (result)) FAILED ("widget expected");
      widget value= tmscm_to_widget (result);
      command.argument[0]= actor_ui_store_widget (std::move (value));
    }
    catch (...) {
      scheme_command_handle_release (handle);
      throw;
    }
    scheme_command_handle_release (handle);
    break;
  }
  case actor_command_kind::run_native_continuation: {
    std::function<void()> continuation=
      actor_continuation_registry::instance ().take (command.argument[0]);
    if (continuation) continuation ();
    break;
  }
  case actor_command_kind::suspend_view:
    if (editor != nullptr) editor->suspend ();
    break;
  case actor_command_kind::resume_view:
    if (editor != nullptr) editor->resume ();
    break;
  case actor_command_kind::rename_buffer: {
    string name= actor_text_registry::instance ().take (command.payload0);
    if (N (name) != 0) {
      impl_->state.name= url (std::move (name));
      impl_->state.master= impl_->state.name;
      for (auto& entry: impl_->views)
        entry.second.instance->notify_change (THE_ENVIRONMENT);
      publish_tmfs_title (editor);
    }
    break;
  }
  case actor_command_kind::replace_document: {
    tree document= actor_tree_registry::instance ().take (command.payload0);
    tree body= detach_data (document, impl_->state.data);
    set_document (
      impl_->state.document, impl_->state.root_path, std::move (body));
    for (auto& entry: impl_->views) {
      entry.second.instance->set_data (impl_->state.data);
      entry.second.instance->init_update ();
    }
    publish_tmfs_title (editor);
    break;
  }
  case actor_command_kind::replace_body: {
    tree body= actor_tree_registry::instance ().take (command.payload0);
    assign (subtree (impl_->state.document, impl_->state.root_path),
            std::move (body));
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
    break;
  case actor_command_kind::set_buffer_title:
    impl_->state.title=
      actor_text_registry::instance ().take (command.payload0);
    break;
  case actor_command_kind::snapshot_document: {
    bool no_aux= false;
    if (editor != nullptr) {
      editor->get_data (impl_->state.data);
      no_aux= !editor->get_save_aux ();
    }
    tree body= subtree (impl_->state.document, impl_->state.root_path);
    tree snapshot= copy (attach_data (body, impl_->state.data, no_aux));
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
  case actor_command_kind::query_modified:
  case actor_command_kind::query_autosaved: {
    bool modified= false;
    bool autosave= command.kind == actor_command_kind::query_autosaved;
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
    for (auto& entry: impl_->views) entry.second.instance->notify_save ();
    break;
  case actor_command_kind::mark_autosaved:
    for (auto& entry: impl_->views)
      entry.second.instance->notify_save (false);
    break;
  case actor_command_kind::attach_notifier:
    if (!impl_->state.notifier_attached) {
      string id= as_string (impl_->state.name, URL_UNIX);
      tree& body= subtree (
        impl_->state.document, impl_->state.root_path);
      call ("buffer-initialize", id, body, impl_->state.name);
      impl_->state.links= link_repository (true);
      impl_->state.links->insert_locus (id, body, "buffer-notify");
      impl_->state.notifier_attached= true;
    }
    break;
  case actor_command_kind::export_buffer: {
    string destination=
      actor_text_registry::instance ().take (command.payload0);
    string format= actor_text_registry::instance ().take (command.payload1);
    url dest (std::move (destination));
    editor_rep* export_editor= editor;
    if (export_editor == nullptr && !impl_->views.empty ())
      export_editor=
        impl_->views.begin ()->second.instance.operator -> ();
    bool failed= export_editor == nullptr;
    if (!failed && (format == "postscript" || format == "pdf")) {
      int old_stamp= last_modified (dest, false);
      export_editor->print_to_file (dest);
      int new_stamp= last_modified (dest, false);
      failed= new_stamp <= old_stamp;
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
      tree links= as_tree (call (
        "get-link-locations", object (impl_->state.name), object (body)));
      if (N (links) != 0) document << compound ("links", links);
      failed= export_tree (document, dest, format);
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
    std::fprintf (stderr, "ATHENA] unimplemented buffer actor command %u\n",
                  static_cast<unsigned int> (command.kind));
    break;
  }

  if (editor != nullptr && editor->ui_endpoint != nullptr &&
      (command.kind == actor_command_kind::initialize_view ||
       command.kind == actor_command_kind::key_press ||
       command.kind == actor_command_kind::text_input ||
       command.kind == actor_command_kind::mouse ||
       command.kind == actor_command_kind::replace_document ||
       command.kind == actor_command_kind::replace_body ||
       command.kind == actor_command_kind::activate_outline_entry))
    editor->ui_endpoint->set_wheel_capture (
      editor->inside_active_graphics ());
}

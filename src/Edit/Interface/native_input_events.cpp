/******************************************************************************
* MODULE     : native_input_events.cpp
* DESCRIPTION: Native keyboard dispatch and actor-owned delayed input
* COPYRIGHT  : (C) 2026 The ATHENA developers
******************************************************************************/

#include "native_input_events.hpp"
#include "native_keyboard_registry.hpp"
#include "tooltip_commands.hpp"
#include "generic_keyboard_commands.hpp"
#include "reference_markup.hpp"
#include "edit_interface.hpp"
#include "buffer_actor.hpp"
#include "language.hpp"
#include "native_node_groups.hpp"
#include "native_interfaces.hpp"
#include "QTMNativeDialogs.hpp"

#include <QCoreApplication>
#include <QTimer>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <functional>

namespace {

std::atomic<bool> remote_enabled {false};
std::mutex remote_mutex;
std::unordered_map<std::string, std::string> remote_bindings;

std::string bytes (string value) {
  return std::string (value.data (), N(value));
}

void queue_input_idle (athena_actor_id actor, athena_view_id view,
                       int idle, std::function<void()> action) {
  QTimer::singleShot (idle, QCoreApplication::instance (),
    [actor, view, idle, action] {
      const auto continuation= actor_continuation_registry::instance ().store (
        [actor, view, idle, action] {
          if (current_scheme_execution_context ()->editor == nullptr) return;
          if (get_current_editor ()->idle_time () < idle)
            queue_input_idle (actor, view, idle, action);
          else action ();
        });
      if (!buffer_actor::submit_to (actor, actor_command_kind::run_native_continuation,
            view, ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER, continuation))
        actor_continuation_registry::instance ().discard (continuation);
    });
}

void after_input_idle (int idle, std::function<void()> action) {
  const auto* context= current_scheme_execution_context ();
  ASSERT (context && context->actor_id != ATHENA_NO_ACTOR,
          "delayed input requires a BufferActor");
  queue_input_idle (context->actor_id, context->view_id, idle, std::move (action));
}

bool inactive_reference () {
  return reference_inactive_at_cursor ();
}

bool present (object value) { return !(is_bool (value) && !as_bool (value)); }

void update_reference_preview () {
  auto& ed= dynamic_cast<edit_interface_rep&> (*get_current_editor ().operator-> ());
  ed.apply_changes ();
  object id1 (false), tip1 (false), id2 (false), tip2 (false);
  if (inactive_reference ()) {
    tree cursor=get_current_editor ()->the_subtree (path_up (get_current_editor ()->the_path ()));
    id1=object (cursor->label);
    tip1= reference_preview (as_string (id1));
    if (!present (tip1)) {
      id2= reference_infer_label (cursor);
      if (present (id2)) tip2= reference_preview (as_string (id2));
    }
  }
  object tip= present (tip1) ? tip1 : tip2;
  if (present (tip)) {
    tooltip_show (as_string (present (tip1) ? id1 : id2),
      get_current_editor ()->the_subtree (path_up (get_current_editor ()->the_path ())),
      stree_to_tree (tip), "auto", "auto", "keyboard", 0.6);
    if (!present (tip1))
      get_current_editor ()->set_message (
        tree (CONCAT, compound ("verbatim", as_string (id1)), " <rightarrow> ",
              compound ("verbatim", as_string (id2))), "");
  }
  else {
    qtm_tooltip_close ();
    get_current_editor ()->set_message ("", "");
  }
}

} // namespace

void native_deferred_tree_set (tree source, tree value) {
  const auto* context= current_scheme_execution_context ();
  ASSERT (context && context->editor && context->actor_id != ATHENA_NO_ACTOR,
          "deferred tree mutation requires its BufferActor");
  // Keep live tree references on their owner; only the continuation id is queued.
  const auto id= context->actor->retain_native_callback (
    [source= std::move (source), value= std::move (value)] {
      if (tree_active (source)) tree_set_diff (source, value);
      else tree_assign (source, value);
    });
  // A rendering callback must not wait for space in its own mailbox.
  if (!context->actor->try_submit (actor_command_kind::resolve_native_callback,
        context->view_id, ATHENA_NO_BLOB, ATHENA_NO_BLOB,
        SCHEME_CAPABILITY_BUFFER, id, 1))
    context->actor->resolve_native_callback (id, false);
}

bool native_remote_control_enabled () { return remote_enabled.load (); }
void native_remote_control_toggle () {
  bool old= remote_enabled.load ();
  while (!remote_enabled.compare_exchange_weak (old, !old)) {}
}
void native_remote_control_bind (string key, string command) {
  std::lock_guard<std::mutex> lock (remote_mutex);
  remote_bindings[bytes (key)]= bytes (command);
}

static void dispatch_keyboard_press (string key) {
  editor ed= get_current_editor ();
  if (native_remote_control_enabled ()) {
    {
      std::lock_guard<std::mutex> lock (remote_mutex);
      const auto mapped= remote_bindings.find (bytes (key));
      if (mapped != remote_bindings.end ())
        key= string (mapped->second.data (), mapped->second.size ());
    }
    ed->key_press (key);
    return;
  }
  const int mode= ed->get_input_mode ();
  const string prefix= mode == INPUT_SEARCH ? "search " :
                       mode == INPUT_SPELL ? "spell " :
                       mode == INPUT_COMPLETE ? "complete " : "";
  if (prefix != "") {
    auto binding= native_keyboard_find (prefix * key);
    if (binding && binding->action) {
      binding->action ();
      return;
    }
    if ((mode == INPUT_SEARCH && ed->search_keypress (key)) ||
        (mode == INPUT_SPELL && ed->spell_keypress (key)) ||
        (mode == INPUT_COMPLETE && ed->complete_keypress (key))) return;
  }
  ed->key_press (key);
}

void native_keyboard_press (string key, double time) {
  (void) time;
  qtm_tooltip_keyboard_event ();
  const bool before= inactive_reference ();
  dispatch_keyboard_press (key);
  const bool after= inactive_reference ();
  if ((before || after) && get_current_editor ()->defined_at_init ("preview-ref-package")) {
    auto& ed= dynamic_cast<edit_interface_rep&> (*get_current_editor ().operator-> ());
    ed.delay_reference_preview ();
  }
}

void edit_interface_rep::delay_reference_preview () {
  const auto generation= ++reference_preview_generation;
  after_input_idle (100, [generation] {
    auto& ed= dynamic_cast<edit_interface_rep&> (*get_current_editor ().operator-> ());
    if (generation == ed.reference_preview_generation) update_reference_preview ();
  });
}

void native_mouse_event (string kind, int x, int y, int modifiers,
                         double time, array<double> data) {
  qtm_tooltip_mouse_event (kind, x, y);
  get_current_editor ()->mouse_any (kind, x, y, modifiers, (time_t) time, data);
}

void native_delayed_mouse_event (string kind, int x, int y, int modifiers,
                                 double time, array<double> data) {
  std::vector<double> values;
  for (int i=0; i<N(data); ++i) values.push_back (data[i]);
  after_input_idle (1, [key=bytes (kind), x, y, modifiers, time, values] {
    array<double> data;
    for (double value: values) data << value;
    native_mouse_event (string (key.data (), key.size ()), x, y, modifiers, time, data);
  });
}

void native_keyboard_insert (tree value) {
  editor ed= get_current_editor ();
  const path position= ed->the_path ();
  const tree cursor= ed->the_subtree (path_up (position));
  const int offset= last_item (position);
  if (is_atomic (cursor) && offset > 0 && cursor->label[offset-1] == ' ' &&
      ed->get_env_string (MODE) == "math") {
    const string type= is_atomic (value) || is_compound (value, "named-symbol", 1) ?
      math_symbol_type (value) : string ("symbol");
    if (type == "postfix" || type == "infix" || type == "separator" ||
        type == "prefix-infix" || type == "middle-bracket" || type == "closing-bracket")
      ed->remove_text (false);
  }
  ed->insert_tree (value);
}

void edit_interface_rep::delay_pre_edit (string key, time_t time) {
  queue_pre_edit (bytes (key), time, ++pre_edit_generation, 100);
}

void edit_interface_rep::queue_pre_edit (std::string key, time_t time,
                                        std::uint64_t generation, int delay) {
  const auto* context= current_scheme_execution_context ();
  ASSERT (context && context->actor_id != ATHENA_NO_ACTOR,
          "delayed pre-edit requires a BufferActor");
  const auto actor= context->actor_id;
  const auto view= context->view_id;
  QTimer::singleShot (delay, QCoreApplication::instance (),
    [actor, view, key= std::move (key), time, generation] {
      const auto continuation= actor_continuation_registry::instance ().store (
        [key, time, generation] {
          if (current_scheme_execution_context ()->editor == nullptr) return;
          auto& ed= dynamic_cast<edit_interface_rep&> (*get_current_editor ().operator-> ());
          if (generation != ed.pre_edit_generation) return;
          const int remaining= 100 - ed.idle_time ();
          if (remaining > 0) ed.queue_pre_edit (key, time, generation, remaining);
          else {
            ed.apply_changes ();
            ed.start_editing ();
            try {
              native_keyboard_press (string (key.data (), key.size ()), (double) time);
              ed.end_editing ();
            }
            catch (...) {
              ed.cancel_editing ();
              throw;
            }
          }
        });
      if (!buffer_actor::submit_to (actor, actor_command_kind::run_native_continuation,
            view, ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER, continuation))
        actor_continuation_registry::instance ().discard (continuation);
    });
}

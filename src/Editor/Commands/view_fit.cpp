// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "Editor/Commands/view_fit.hpp"
#include "boot.hpp"
#include "Editor/Commands/view_zoom.hpp"
#include "Editor/Commands/document_commands.hpp"
#include "Editor/editor.hpp"
#include "new_view.hpp"
#include "new_buffer.hpp"
#include "App/Window/tm_window.hpp"
#include "App/Runtime/buffer_actor.hpp"
#include "App/Runtime/execution_context.hpp"

namespace {
bool applicable (editor_rep* ed) {
  string mode= document_get_init_page_rendering (ed);
  return get_preference ("persistent fit width") == "on" &&
         (mode == "paper" || mode == "slideshow");
}
void schedule (editor_rep* ed) {
  auto& state= ed->persistent_fit;
  state.restore_step= 2;
  state.step= applicable (ed)? 0: 4;
  state.started= texmacs_time ();
}
void fit (editor_rep* ed) {
  auto& state= ed->persistent_fit;
  state.cursor= copy (ed->the_path ());
  state.dx= ed->get_cursor_x ()-ed->get_scroll_x ();
  state.dy= ed->get_cursor_y ()-ed->get_scroll_y ();
  state.user_scroll= ed->ui_endpoint->user_scroll_generation ();
  view_fit_width ();
  state.fitted= texmacs_time ();
  state.restore_step= 0;
}
}

void view_schedule_fit_width () {
  const auto* context= current_execution_context ();
  if (context) {
    if (context->editor) schedule (context->editor);
    return;
  }
  tm_view view= concrete_view (get_current_view_safe ());
  if (!view) return;
  auto continuation= actor_continuation_registry::instance ().store ([] {
    const auto* owner= current_execution_context ();
    if (owner && owner->editor) schedule (owner->editor);
  });
  if (!view->buf->actor->submit (actor_command_kind::run_native_continuation,
      view->runtime_id, ATHENA_NO_BLOB, ATHENA_NO_BLOB,
      EXECUTION_CAPABILITY_BUFFER, continuation))
    actor_continuation_registry::instance ().discard (continuation);
}

void view_toggle_fit_width () {
  set_preference ("persistent fit width",
    get_preference ("persistent fit width") == "on"? "off": "on");
  editor ed= get_current_editor ();
  schedule (ed.operator-> ());
  if (applicable (ed.operator-> ()) && ed->ui_endpoint &&
      ed->ui_viewport ().attached) fit (ed.operator-> ());
}

void view_tick_fit_width (editor_rep* ed) {
  auto& state= ed->persistent_fit;
  if (state.step == 4 && state.restore_step == 2) return;
  if (!applicable (ed)) { state.step= 4; state.restore_step= 2; return; }
  // Decoration refreshes may be pending on every frame; only layout changes
  // make the page metrics and cursor coordinates unsuitable for fitting.
  if (ed->has_changed (THE_TREE | THE_ENVIRONMENT | THE_EXTENTS) ||
      !ed->ui_endpoint || !ed->ui_viewport ().attached) return;
  time_t now= texmacs_time ();
  if (state.restore_step < 2) {
    if (state.cursor != ed->the_path () ||
        state.user_scroll != ed->ui_endpoint->user_scroll_generation ())
      state.restore_step= 2;
    else if (now-state.fitted >= (state.restore_step == 0? 1: 25)) {
      ++state.restore_step;
      ed->scroll_to (ed->get_cursor_x ()-state.dx, ed->get_cursor_y ()-state.dy);
    }
  }
  static constexpr int delays[]= {25, 100, 250, 600};
  if (state.step < 4 && now-state.started >= delays[state.step]) {
    // Coalesce elapsed passes after a busy editor instead of replaying stale fits.
    do { ++state.step; } while (state.step < 4 && now-state.started >= delays[state.step]);
    fit (ed);
  }
}

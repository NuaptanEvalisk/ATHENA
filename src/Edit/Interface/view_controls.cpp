// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "view_controls.hpp"
#include "editor.hpp"
#include "server.hpp"
#include "new_window.hpp"
#include "new_view.hpp"
#include "tm_window.hpp"
#include "new_buffer.hpp"
#include "buffer_actor.hpp"
#include "scheme_execution_context.hpp"
#include "native_interfaces.hpp"
#include "native_input_events.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

void view_notify_header (string, string value) {
  get_server ()->show_header (value == "on");
}
void view_notify_footer (string, string value) {
  get_server ()->show_footer (value == "on");
}
void view_notify_icon_bar (string key, string value) {
  int which= key == "main icon bar"? 0: key == "mode dependent icons"? 1:
             key == "focus dependent icons"? 2: -1;
  if (which >= 0) get_server ()->show_icon_bar (which, value == "on");
}
void view_notify_toolbar (string, string) {
  const auto* context= current_scheme_execution_context ();
  if (context) {
    if (context->editor) context->editor->notify_change (THE_MENUS);
    return;
  }
  tm_view view= concrete_view (get_current_view_safe ());
  if (!view) return;
  auto continuation= actor_continuation_registry::instance ().store ([] {
    const auto* owner= current_scheme_execution_context ();
    if (owner && owner->editor) owner->editor->notify_change (THE_MENUS);
  });
  if (!view->buf->actor->submit (actor_command_kind::run_native_continuation,
      view->runtime_id, ATHENA_NO_BLOB, ATHENA_NO_BLOB,
      SCHEME_CAPABILITY_BUFFER, continuation))
    actor_continuation_registry::instance ().discard (continuation);
}
void view_notify_zoom (string, string value) {
  object number= string_to_object (value);
  if (!is_double (number)) throw std::runtime_error ("Invalid zoom preference");
  double zoom= as_double (number);
  if (!std::isfinite (zoom)) throw std::runtime_error ("Zoom preference must be finite");
  zoom= std::clamp (zoom, 0.04, 25.0);
  get_server ()->set_default_zoom_factor (zoom);
  get_server ()->set_window_zoom_factor (zoom);
}
void view_notify_remote (string key, string value) { native_remote_control_bind (value, key); }

void view_toggle_header () {
  bool visible= !get_server ()->visible_header ();
  if (get_nr_windows () == 1 && os_macos ()) set_preference ("header", visible? "on": "off");
  else get_server ()->show_header (visible);
}
void view_toggle_footer () {
  bool visible= !get_server ()->visible_footer ();
  if (get_nr_windows () == 1) set_preference ("status bar", visible? "on": "off");
  else get_server ()->show_footer (visible);
}
void view_toggle_icon_bar (int which) {
  const char* keys[]= {"main icon bar", "mode dependent icons", "focus dependent icons"};
  if (which < 0 || which >= 3) throw std::runtime_error ("Unknown icon bar");
  bool visible= !get_server ()->visible_icon_bar (which);
  if (get_nr_windows () == 1) set_preference (keys[which], visible? "on": "off");
  else get_server ()->show_icon_bar (which, visible);
  if (os_macos () && which == 0 && get_preference ("use unified toolbar") == "on")
    athena_notify_now (object ("Restart ATHENA to avoid potential visual artefacts"));
}
void view_toggle_snap () {
  set_preference ("snap to pages", get_preference ("snap to pages") == "on"? "off": "on");
}
void view_toggle_typewriter () {
  set_preference ("typewriter mode", get_preference ("typewriter mode") == "on"? "off": "on");
}

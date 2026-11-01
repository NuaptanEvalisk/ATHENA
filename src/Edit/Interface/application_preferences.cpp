// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "application_preferences.hpp"
#include "view_controls.hpp"
#include "native_interfaces.hpp"
#include "editor.hpp"
#include "server.hpp"
#include "tm_window.hpp"
#include "Data/Convert/Tex/latex_picture_fallback.hpp"
#include <atomic>

namespace {
std::atomic<bool> initialized {false};
void refresh_enunciations () { windows_refresh ("enunciations"); }
}
bool application_preferences_ready () { return initialized.load (); }
void application_preferences_initialize () {
  array<object> callbacks;
  for (const char* name: {"notify-enunciation-rendering", "notify-latex-command",
                         "notify-look-and-feel", "notify-restart",
                         "notify-security", "notify-tool"})
    callbacks << eval (symbol_object (name));
  call ("register-preference-callback-procedures", as_list_object (callbacks));
  initialized.store (true);
}
void application_notify_look (string, string) {
  get_server ()->set_message (
    "Restart in order to let the new look and feel take effect", "configure look and feel");
}
void application_notify_security (string, string value) {
  if (value == "accept no scripts") get_server ()->set_script_status (0);
  else if (value == "prompt on scripts") get_server ()->set_script_status (1);
  else if (value == "accept all scripts") get_server ()->set_script_status (2);
}
void application_notify_latex (string, string value) { set_latex_command (value); }
void application_notify_tool (string, string) { view_notify_change (THE_CURSOR | THE_MENUS); }
void application_notify_enunciations (string, string) { athena_dispatch_ui (refresh_enunciations); }
string application_exercises_smaller () {
  return get_preference ("render solution in smaller font") == "on"? "true": "false";
}
string application_number_solutions () {
  return get_preference ("number solutions") == "on"? "true": "false";
}

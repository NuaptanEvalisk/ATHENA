// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "application_preferences.hpp"
#include "analyze.hpp"
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
                         "notify-security", "notify-tool", "notify-printing-command",
                         "notify-paper-type", "notify-printer-dpi"})
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
void application_notify_printing_command (string, string value) {
  get_server ()->set_printing_command (value);
}
void application_notify_paper_type (string, string value) {
  get_server ()->set_printer_page_type (locase_first (value));
}
void application_notify_printer_dpi (string, string value) {
  get_server ()->set_printer_dpi (value);
}
void application_notify_enunciations (string, string) { athena_dispatch_ui (refresh_enunciations); }
void application_notify_cursor_color (string, string value) { gui_set_cursor_color (value); }
void application_notify_selection_color (string, string value) { gui_set_selection_color (value); }
void application_notify_focus_color (string, string value) { gui_set_focus_color (value); }
void application_notify_focus_border_width (string, string value) { gui_set_focus_border_width (value); }
void application_notify_document_colors (string, string) { windows_refresh (""); }
void application_notify_labels (string, string) { windows_refresh ("labels"); }
void application_notify_explorer_track (string, string value) {
  if (value == "on") athena_dispatch_ui (vault_track_current_buffer_if_enabled);
}
string application_exercises_smaller () {
  return get_preference ("render solution in smaller font") == "on"? "true": "false";
}
string application_number_solutions () {
  return get_preference ("number solutions") == "on"? "true": "false";
}

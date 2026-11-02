// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "application_preferences.hpp"
#include "analyze.hpp"
#include "view_controls.hpp"
#include "native_interfaces.hpp"
#include "editor.hpp"
#include "server.hpp"
#include "tm_window.hpp"
#include "file.hpp"
#include "boot.hpp"
#include "new_view.hpp"
#include "new_window.hpp"
#include "new_buffer.hpp"
#include "QTMPreferencesDialog.hpp"
#include "qt_utilities.hpp"
#include <QStringList>
#include "Data/Convert/Tex/latex_picture_fallback.hpp"
#include <atomic>
#include <vector>

namespace {
std::atomic<bool> initialized {false};
void refresh_enunciations () { windows_refresh ("enunciations"); }
}
bool application_preferences_ready () { return initialized.load (); }
void application_preferences_initialize () {
  notify_preferences_booted ();
  application_preferences_notify_all ();
  initialized.store (true);
}
void application_preference_notify (string key) {
  const string callback= get_user_preference_callback (key);
  const string value= get_user_preference (key, "default");
  struct handler { const char* name; void (*invoke) (string, string); };
  static const handler handlers[]= {
    {"notify-autosave", document_notify_autosave},
    {"notify-look-and-feel", application_notify_look},
    {"notify-security", application_notify_security},
    {"notify-latex-command", application_notify_latex},
    {"notify-tool", application_notify_tool},
    {"notify-printing-command", application_notify_printing_command},
    {"notify-paper-type", application_notify_paper_type},
    {"notify-printer-dpi", application_notify_printer_dpi},
    {"notify-enunciation-rendering", application_notify_enunciations},
    {"notify-enunciation-color", application_notify_enunciations},
    {"notify-cursor-color", application_notify_cursor_color},
    {"notify-selection-color", application_notify_selection_color},
    {"notify-focus-color", application_notify_focus_color},
    {"notify-focus-border-width", application_notify_focus_border_width},
    {"notify-link-color", application_notify_document_colors},
    {"notify-document-background-color", application_notify_document_colors},
    {"notify-labels-mode", application_notify_labels},
    {"notify-vault-explorer-track", application_notify_explorer_track},
    {"notify-vault-preferences-mode", vault_notify_preferences_mode},
    {"notify-header", view_notify_header},
    {"notify-icon-bar", view_notify_icon_bar},
    {"notify-toolbar-presentation", view_notify_toolbar},
    {"notify-status-bar", view_notify_footer},
    {"notify-zoom-factor", view_notify_zoom},
    {"notify-remote-control", view_notify_remote}
  };
  for (const auto& handler: handlers)
    if (callback == handler.name) { handler.invoke (key, value); return; }
  if (callback == "notify-restart") athena_notify_restart ();
  else if (callback == "notify-viewport-follow") notify_viewport_follow ();
  else if (callback == "notify-fold-table-of-contents") windows_refresh ("");
}
void application_preferences_notify_all () {
  const array<string> keys= get_user_preference_callback_names ();
  for (int i=0; i<N(keys); ++i) application_preference_notify (keys[i]);
}
void application_preference_set (string key, string value) {
  if (get_user_preference (key, "default") != value) set_preference (key, value);
}
void application_preference_reset (string key) {
  if (!has_user_preference (key)) return;
  reset_user_preference (key);
  save_user_preferences ();
}
void application_preference_toggle (string key) {
  const string value= get_user_preference (key, "default");
  if (value == "on" || value == "off") set_preference (key, value == "on" ? "off" : "on");
}
bool application_look_and_feel_matches (const array<string>& profiles) {
  for (int i=0; i<N(profiles); ++i)
    if (application_look_and_feel_matches (profiles[i])) return true;
  return false;
}
bool application_look_and_feel_matches (string profile) {
  if (starts (profile, "no-")) return !application_look_and_feel_matches (profile(3,N(profile)));
  string actual= get_user_preference ("look and feel", "default");
  if (actual == "default") actual= default_look_and_feel ();
  return profile == actual || (profile == "std" && actual != "emacs");
}
string application_fold_toc_in_reflow () {
  return get_preference ("fold table of contents in reflow") == "on" ? "true" : "false";
}
namespace {
void fold_toc (tree t, bool folded) {
  const path ip= obtain_ip (t);
  if (ip_attached (ip)) get_current_editor ()->toc_fold_set_at (reverse (ip), folded);
}
}
void application_toc_fold (tree t) { fold_toc (t, true); }
void application_toc_unfold (tree t) { fold_toc (t, false); }

namespace {
tree preference_table (const std::vector<QStringList>& entries, bool redact) {
  tree rows (TABLE);
  rows << tree (ROW, tree (CELL, compound ("strong", "Preference")),
    tree (CELL, compound ("strong", "Effective value")), tree (CELL, compound ("strong", "Source")));
  for (const auto& entry: entries) {
    string key= from_qstring (entry[4]);
    string value= get_user_preference (key, "default");
    if (redact && user_preference_is_sensitive (key) && value != "") value= "[REDACTED]";
    if (value == "") value= "(empty)";
    rows << tree (ROW, tree (CELL, from_qstring (entry[3])),
      tree (CELL, compound ("verbatim", value)),
      tree (CELL, has_user_preference (key) ? "Customized" : "Default"));
  }
  tree format (TFORMAT);
  format << tree (TWITH, "table-width", "1par") << tree (TWITH, "table-hmode", "exact");
  format << tree (CWITH, "1", "1", "1", "-1", "cell-background", "#e8e8e8");
  for (const auto& setting: {std::make_pair ("cell-hyphen", "t"),
       {"cell-lsep", "0.5em"}, {"cell-rsep", "0.5em"},
       {"cell-tsep", "0.25em"}, {"cell-bsep", "0.25em"}})
    format << tree (CWITH, "1", "-1", "1", "-1", setting.first, setting.second);
  for (int i=1; i<=3; ++i)
    format << tree (CWITH, "1", "-1", as_string (i), as_string (i), "cell-width", i == 1 ? "0.36par" : i == 2 ? "0.44par" : "0.20par");
  format << rows;
  return compound ("tabular", format);
}
void preference_sections (tree& doc, const std::vector<QStringList>& entries, int level, bool redact) {
  QStringList groups;
  for (const auto& entry: entries)
    if (!groups.contains (entry[level])) groups << entry[level];
  for (const auto& group: groups) {
    std::vector<QStringList> selected;
    for (const auto& entry: entries) if (entry[level] == group) selected.push_back (entry);
    if (level != 2 || group != selected.front ()[1])
      doc << compound (level == 0 ? "section" : level == 1 ? "subsection" : "subsubsection", from_qstring (group));
    if (level == 2) doc << preference_table (selected, redact);
    else preference_sections (doc, selected, level+1, redact);
  }
}
}
void application_view_preferences () {
  if (qt_defer_to_main_thread (application_view_preferences)) return;
  const int choice= qtm_preferences_export_privacy_dialog ();
  if (choice == 0) return;
  const bool redact= choice == 1;
  const QStringList metadata= qtm_preferences_export_metadata ();
  std::vector<QStringList> entries;
  for (int i=0; i<metadata.size (); i+=5) entries.push_back (metadata.mid (i, 5));
  tree mode= redact ? tree ("Sensitive values are redacted.") :
    tree (WITH, "color", "red", "Sensitive values are included. Do not share this document.");
  tree doc (DOCUMENT);
  doc << compound ("doc-data", compound ("doc-title", tree (CONCAT, "All ", compound ("ATHENA"), " Preferences")));
  doc << tree (CONCAT, compound ("strong", "Export mode: "), mode);
  doc << tree (CONCAT, "This document follows the category, tab, section, and setting order of the Preferences window. Values marked Default are inherited from ", compound ("ATHENA"), "; Customized values are explicitly stored in the active preferences scope.");
  preference_sections (doc, entries, 0, redact);
  const url target= create_buffer ();
  set_title_buffer (target, redact ? "All preferences (redacted)" : "All preferences");
  set_buffer_body (target, doc);
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

// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#ifndef ATHENA_APPLICATION_PREFERENCES_HPP
#define ATHENA_APPLICATION_PREFERENCES_HPP
#include "string.hpp"
#include "scheme.hpp"
void application_preferences_initialize ();
bool application_preferences_ready ();
void application_preference_notify (string key);
void application_preferences_notify_all ();
object application_preference_get (string key);
void application_preference_set (string key, object value);
void application_preference_reset (string key);
void application_preference_toggle (string key);
bool application_look_and_feel_matches (object profiles);
string application_fold_toc_in_reflow ();
void application_toc_fold (tree t);
void application_toc_unfold (tree t);
void application_view_preferences ();
void application_notify_look (string key, string value);
void application_notify_security (string key, string value);
void application_notify_latex (string key, string value);
void application_notify_tool (string key, string value);
void application_notify_printing_command (string key, string value);
void application_notify_paper_type (string key, string value);
void application_notify_printer_dpi (string key, string value);
void application_notify_enunciations (string key, string value);
void application_notify_cursor_color (string key, string value);
void application_notify_selection_color (string key, string value);
void application_notify_focus_color (string key, string value);
void application_notify_focus_border_width (string key, string value);
void application_notify_document_colors (string key, string value);
void application_notify_labels (string key, string value);
void application_notify_explorer_track (string key, string value);
string application_exercises_smaller ();
string application_number_solutions ();
#endif

/******************************************************************************
* MODULE     : QTMNativeDialogs.hpp
* DESCRIPTION: Native Qt replacements for legacy Scheme dialogs
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#ifndef QTMNATIVEDIALOGS_HPP
#define QTMNATIVEDIALOGS_HPP

#include "array.hpp"
#include "string.hpp"

string qtm_linked_file_choice_dialog (string name, array<string> items);
array<string> qtm_unsaved_buffers_dialog (array<string> buffers, bool restart);
void qtm_text_report_dialog (string title, string message);
array<string> qtm_latex_formula_dialog ();
array<string> qtm_background_selector_dialog (string mode,
                                               array<string> initial);
array<string> qtm_shortcut_editor_dialog (string initial_shortcut,
                                          string initial_command,
                                          array<string> entries);

#endif // QTMNATIVEDIALOGS_HPP

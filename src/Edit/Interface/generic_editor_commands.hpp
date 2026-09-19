/******************************************************************************
* MODULE     : generic_editor_commands.hpp
* DESCRIPTION: Native generic editor commands
* COPYRIGHT  : (C) 2001 Joris van der Hoeven, 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
*******************************************************************************/

#ifndef ATHENA_GENERIC_EDITOR_COMMANDS_HPP
#define ATHENA_GENERIC_EDITOR_COMMANDS_HPP

#include "scheme.hpp"

void generic_go_to_line (int line, object optional_from);
void generic_go_to_column (int column, object optional_from);
object generic_select_word (string word, tree t, int column);
object generic_search_parameters (object label);
void generic_label_insert (tree t);
void generic_recenter_window ();
void generic_make_label ();
void generic_make_specific (string format);
void generic_make_include (url target);
void generic_make_experimental_build_warning ();
void generic_make_note_ref ();
void generic_make_note_inline ();
void generic_make_note_wide ();
void generic_make_note_footnote ();
void generic_make_marginal_note ();
bool generic_test_marginal_note_hpos (string position);
void generic_set_marginal_note_hpos (string position);
bool generic_test_marginal_note_valign (string alignment);
void generic_set_marginal_note_valign (string alignment);
void generic_make_insertion (string type);
void generic_insertion_positioning (string position, bool allowed);
bool generic_test_insertion_positioning (string position);
bool generic_not_test_insertion_positioning (string position);
void generic_toggle_insertion_positioning (string position);
void generic_toggle_insertion_positioning_not (string position);

#endif

/******************************************************************************
* MODULE     : generic_keyboard_commands.hpp
* DESCRIPTION: Native generic keyboard editing fallbacks
* COPYRIGHT  : (C) 2001 Joris van der Hoeven, 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
*******************************************************************************/

#ifndef ATHENA_GENERIC_KEYBOARD_COMMANDS_HPP
#define ATHENA_GENERIC_KEYBOARD_COMMANDS_HPP

#include "scheme.hpp"
#include "tree.hpp"

void generic_keyboard_load ();
void generic_keyboard_load_domain (string domain);
object generic_keyboard_run (int group, int binding);
bool generic_keyboard_condition (int group);
void generic_escape_symbol_insert (string action);
void generic_open_escape_symbol_picker ();

void generic_kbd_horizontal (tree t, bool forwards);
void generic_kbd_vertical (tree t, bool downwards);
void generic_kbd_extremal (tree t, bool forwards);
void generic_kbd_incremental (tree t, bool downwards);
void generic_kbd_left_raw ();
void generic_kbd_right_raw ();
void generic_kbd_up_raw ();
void generic_kbd_down_raw ();
void generic_kbd_start_line_raw ();
void generic_kbd_end_line_raw ();
void generic_kbd_page_up_raw ();
void generic_kbd_page_down_raw ();
void generic_kbd_plain_move (object move);
void generic_kbd_left ();
void generic_kbd_right ();
void generic_kbd_up ();
void generic_kbd_down ();
void generic_kbd_start_line ();
void generic_kbd_end_line ();
void generic_kbd_page_up ();
void generic_kbd_page_down ();
void generic_kbd_select (object move);
void generic_kbd_select_if_active (object move);
void generic_insert_return ();
void generic_kbd_space ();
void generic_kbd_shift_space ();
void generic_kbd_return ();
void generic_kbd_shift_return ();
void generic_kbd_control_return ();
void generic_kbd_shift_control_return ();
void generic_kbd_alternate_return ();
void generic_kbd_shift_alternate_return ();
void generic_kbd_backspace ();
void generic_kbd_delete ();
void generic_kbd_tab ();
void generic_kbd_shift_tab ();
void generic_kbd_alternate_tab ();
void generic_kbd_shift_alternate_tab ();
void generic_kbd_copy ();
void generic_kbd_cut ();
void generic_kbd_paste ();
void generic_kbd_cancel ();
void generic_kbd_space_bar (tree t, bool shift);
void generic_kbd_enter (tree t, bool shift);
void generic_kbd_control_enter (tree t, bool shift);
void generic_kbd_alternate_enter (tree t, bool shift);
void generic_kbd_remove (tree t, bool forwards);
void generic_kbd_variant (tree t, bool forwards);
void generic_kbd_alternate_variant (tree t, bool forwards);
void generic_hybrid_kbd_space ();
void generic_hybrid_kbd_formula_open (string bracket);
void generic_hybrid_kbd_curly_left ();
void generic_hybrid_kbd_curly_right ();
void generic_hybrid_kbd_backslash ();
void generic_hybrid_kbd_sub ();
void generic_hybrid_kbd_sup ();
object generic_escape_symbol_dispatch (string action);
object generic_key_press_command (string key);
string generic_handwriting_symbol_input_description (string command);
void generic_handwriting_symbol_insert (string command);

#endif

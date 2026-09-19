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

#include "tree.hpp"

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

#endif

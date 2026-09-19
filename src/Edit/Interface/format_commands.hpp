/******************************************************************************
* MODULE     : format_commands.hpp
* DESCRIPTION: Native formatting wrappers and actor-owned editing commands
* COPYRIGHT  : (C) 2001 Joris van der Hoeven, 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef ATHENA_FORMAT_COMMANDS_HPP
#define ATHENA_FORMAT_COMMANDS_HPP

#include "scheme.hpp"

object format_with_ref (tree t, tree variable);
tree format_with_set (tree t, tree variable, tree value);
void format_with_simplify (tree t);
void format_with_merge (tree t);
bool format_test_env (string variable, string value);
void format_tree_with_set (tree t, object properties);
object format_tree_with_get (object t, tree variable);
void format_tree_with_reset (object t, tree variable);
bool format_with_like_check_insert (tree t);
void format_make_with_like (tree t);
void format_toggle_with_like (tree t, object back);
void format_toggle_bold ();
void format_toggle_italic ();
void format_toggle_small_caps ();
void format_toggle_underlined ();
bool format_focus_has_preferences (tree t);
object format_standard_parameters (string tag);
object format_parameter_choices (string variable);
object format_customizable_parameters (tree t);
object format_customizable_parameters_memo (tree t);
bool format_customizable_context (tree t);
bool format_pen_effect_context (tree t);
object format_get_effect_pen (object t);
void format_set_effect_pen (object t, string pen);
bool format_test_effect_pen (object t, string pen);
void format_make_multi_with (object properties);
void format_make_line_with (string variable, tree value);
void format_make_multi_line_with (object properties);
void format_make_page_break ();
void format_make_new_page ();
void format_make_new_dpage ();

#endif

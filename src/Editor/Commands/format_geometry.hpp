/******************************************************************************
* MODULE     : format_geometry.hpp
* DESCRIPTION: Native geometry commands, length arithmetic and gesture entrypoints
* COPYRIGHT  : (C) 2010 Joris van der Hoeven, 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef ATHENA_FORMAT_GEOMETRY_HPP
#define ATHENA_FORMAT_GEOMETRY_HPP

#include "tree.hpp"

bool geometry_rich_length (tree value);
string geometry_rich_length_string (tree value);
scheme_tree geometry_parse_rich_length (string value);
void geometry_length_increase (tree value, double amount);
void geometry_length_scale (tree value, double factor, double step_multiplier);
tree geometry_length_rightmost (tree value);
bool geometry_lengths_consistent (tree first, tree second);
void geometry_replace_empty (tree parent, int child, tree replacement);
void geometry_length_increase_step (tree value, int direction);
string geometry_zero_unit ();
void geometry_circulate_unit (int direction);

bool geometry_space_context (tree t);
bool geometry_var_space_context (tree t);
bool geometry_hspace_context (tree t);
bool geometry_vspace_context (tree t);
bool geometry_vadjust_context (tree t);
bool geometry_move_context (tree t);
bool geometry_resize_context (tree t);
void geometry_speed (tree t, bool increase);
void geometry_variant (tree t, bool forward);
void geometry_default (tree t);
void geometry_horizontal (tree t, bool forward);
void geometry_vertical (tree t, bool down);
void geometry_extremal (tree t, bool forward);
void geometry_incremental (tree t, bool down);
void geometry_scale (tree t, double factor);
void geometry_rotate (tree t, double angle);
void geometry_slower ();
void geometry_faster ();
void geometry_circulate (bool forward);
void geometry_reset ();
void geometry_left ();
void geometry_right ();
void geometry_up ();
void geometry_down ();
void geometry_start ();
void geometry_end ();
void geometry_top ();
void geometry_bottom ();
void geometry_pinch_clear ();
void geometry_pinch_start ();
void geometry_pinch_end ();
void geometry_pinch_scale (double factor);
void geometry_pinch_rotate (double angle);
void geometry_make_move (tree horizontal, tree vertical);
void geometry_make_shift (tree horizontal, tree vertical);
void geometry_make_resize (tree left, tree bottom, tree right, tree top);
void geometry_make_extend (tree left, tree bottom, tree right, tree top);
void geometry_make_clipped (tree left, tree bottom, tree right, tree top);
void geometry_make_reduce_by (tree amount);

#endif

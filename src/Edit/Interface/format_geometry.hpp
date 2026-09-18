/******************************************************************************
* MODULE     : format_geometry.hpp
* DESCRIPTION: Native length arithmetic for geometry editing commands
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

#endif

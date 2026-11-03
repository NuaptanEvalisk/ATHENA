
/******************************************************************************
* MODULE     : tree_cursor.hpp
* DESCRIPTION: abstract cursor handling
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef TREE_CURSOR_H
#define TREE_CURSOR_H
#include "path.hpp"
class cursor_navigation;

bool is_inside (tree t, path p);
path closest_inside (tree t, path p);
void show_hidden_upwards (tree t, path p);

#endif // defined TREE_CURSOR_H

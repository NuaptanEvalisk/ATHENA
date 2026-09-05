/******************************************************************************
* MODULE     : table_resize.hpp
* DESCRIPTION: Mouse resizing policy for source tables
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* See the file LICENSE in the root directory.
******************************************************************************/

#ifndef ATHENA_TABLE_RESIZE_HPP
#define ATHENA_TABLE_RESIZE_HPP

#include "tree.hpp"
#include "path.hpp"

inline bool
table_mouse_resize_allowed (tree document, path format) {
  if (!has_subtree (document, format)) return false;
  tree t= subtree (document, format);
  if (!is_func (t, TABLE) && !is_func (t, TFORMAT)) return false;
  while (!is_nil (format)) {
    format= path_up (format);
    t= subtree (document, format);
    if (is_compound (t, "eqnarray") || is_compound (t, "eqnarray*"))
      return false;
    // Content wrappers (notably DOCUMENT) do not own the layout table.
    // A surrounding cell/table does: its nested tables remain independent.
    if (is_func (t, CELL) || is_func (t, TABLE)) return true;
  }
  return true;
}

#endif

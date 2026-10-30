
/******************************************************************************
* MODULE     : tree_traverse.hpp
* DESCRIPTION: abstract cursor movement and tree traversal
* COPYRIGHT  : (C) 2005  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef TREE_TRAVERSE_H
#define TREE_TRAVERSE_H
#include "tree_cursor.hpp"
#include "Data/NodeContracts/editing_query.hpp"

bool exists_accessible_inside (tree t,
  const athena::contracts::source_contract_preparation&, path source);

bool inside_same (tree t, path p, path q, tree_label which);
bool inside_same_or_more (tree t, path p, path q, tree_label which);

array<tree> search_sections (tree t);
path previous_section (tree t, path p);

#endif // defined TREE_TRAVERSE_H

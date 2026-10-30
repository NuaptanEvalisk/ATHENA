
/******************************************************************************
* MODULE     : tree_modify.cpp
* DESCRIPTION: high level tree modification subroutines
* COPYRIGHT  : (C) 2010  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "tree_modify.hpp"
#include "Data/NodeContracts/editing_query.hpp"
#include "path.hpp"
#include "node_metadata.hpp"

/******************************************************************************
* Source-owned correction of trees
******************************************************************************/

void
correct_concat_node (tree& t, int done) {
  //cout << "Correct " << t << ", " << done << "\n";
  int i, n= N(t);
  if (n == 0) {
    tree empty ("");
    athena::node::copy_metadata (t, empty);
    assign (t, empty);
    return;
  }
  for (i=done; i<n; i++) {
    if (t[i] == "" && !athena::node::get (t[i])) {
      remove (t, i, 1);
      correct_concat_node (t, i);
      return;
    }
    if ((i<n-1) && is_atomic (t[i]) && is_atomic (t[i+1]) &&
        !athena::node::get (t[i]) && !athena::node::get (t[i+1])) {
      join (t, i);
      correct_concat_node (t, i);
      return;
    }
    if (is_concat (t[i]) && !athena::node::get (t[i]) &&
        !athena::node::get (t)) {
      insert_node (t, 0, CONCAT);
      split (t, 0, i);
      split (t, 1, 1);
      remove_node (t[1], 0);
      if (t[0] == tree (CONCAT)) remove (t, 0, 1);
      else join (t, 0);
      if (t[1] == tree (CONCAT)) remove (t, 1, 1);
      else join (t, 0);
      remove_node (t, 0);
      correct_concat_node (t, max (i-1, 0));
      return;
    }    
  }
}

void
correct_node (tree& t, const athena::contracts::editing_query& query) {
  // NOTE: this routine should only modify t and its descendants,
  // but not any ancestors
  if (is_compound (t)) {
    const auto& contract= query.describe (L(t));
    // Optional inference cannot authorize deleting source. Identified content
    // is retained for the owner validator, just as in import arity correction.
    if (contract.layout_origin != athena::contracts::property_authority::inferred &&
        !contract.layout.accepts (N(t)) && !athena::node::contains_metadata (t))
      assign (t, "");
    if (is_concat (t))
      correct_concat_node (t, 0);
  }
}

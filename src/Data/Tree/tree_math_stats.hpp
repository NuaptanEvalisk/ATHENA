
/******************************************************************************
* MODULE     : tree_math_stats.hpp
* DESCRIPTION: compile statistics for math formulas in documents
* COPYRIGHT  : (C) 2022  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef TREE_MATH_STATS_H
#define TREE_MATH_STATS_H
#include "Data/NodeContracts/editing_query.hpp"
#include "hashmap.hpp"

class math_statistics {
  hashmap<tree,int> occurrences {0};
  hashmap<tree,int> roles {0};
  bool ready= false;
  void compile_tokens (array<tree>, tree parent);
  bool compile_source (tree, tree parent, string mode,
    const athena::contracts::source_contract_preparation&, path);
public:
  // Missing source entries leave no partial result visible to input heuristics.
  bool compile (tree, string mode,
    const athena::contracts::source_contract_preparation&, path source= path ());
  int number_occurrences (tree) const;
  int number_in_role (tree) const;
};

#endif // defined TREE_MATH_STATS_H

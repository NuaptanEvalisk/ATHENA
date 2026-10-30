
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
#include <vector>

enum class math_statistics_progress { pending, ready, unavailable };

class math_statistics {
  hashmap<tree,int> occurrences {0};
  hashmap<tree,int> roles {0};
  bool ready= false;
  struct source_frame {
    tree value, parent;
    string mode;
    path source;
    int child= -1;
    std::shared_ptr<const athena::contracts::contract_scope> context;
  };
  std::vector<source_frame> pending;
  math_statistics_progress progress= math_statistics_progress::unavailable;
  void compile_tokens (array<tree>, tree parent);
  void compile_node (tree, tree parent);
public:
  // The owner keeps source stable between slices, or cancels before resuming.
  void begin (tree, string mode, path source= path ());
  math_statistics_progress advance (
    const athena::contracts::source_contract_preparation&, unsigned budget= 128);
  math_statistics_progress status () const { return progress; }
  // Missing source entries leave no partial result visible to input heuristics.
  bool compile (tree, string mode,
    const athena::contracts::source_contract_preparation&, path source= path ());
  int number_occurrences (tree) const;
  int number_in_role (tree) const;
};

#endif // defined TREE_MATH_STATS_H

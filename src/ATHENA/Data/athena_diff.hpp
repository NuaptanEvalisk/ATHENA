/******************************************************************************
* MODULE     : athena_diff.hpp
* DESCRIPTION: Structural comparison of ATHENA document trees
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#ifndef ATHENA_DIFF_HPP
#define ATHENA_DIFF_HPP

#include "tree.hpp"
#include "tree_select.hpp"

#include <cstddef>

struct AthenaTreeDiff {
  range_set left;
  range_set right;
  size_t hunks= 0;
};

AthenaTreeDiff athena_diff_trees (tree left, tree right);

#endif // ATHENA_DIFF_HPP

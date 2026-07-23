/******************************************************************************
* MODULE     : person_names.hpp
* DESCRIPTION: semantic person-name recognition for ATHENA documents
* COPYRIGHT  : (C) 2026  Nuaptan
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef ATHENA_PERSON_NAMES_HPP
#define ATHENA_PERSON_NAMES_HPP

#include "path.hpp"
#include "string.hpp"
#include "tree.hpp"

#include <vector>

struct athena_person_occurrence {
  string name;
  path   where;
};

tree athena_normalize_person_names (tree body, int& wrapped);
tree athena_normalize_person_names (
  tree body, int& wrapped, const std::vector<string>& trusted_names);
tree athena_normalize_person_names_for_scheme (tree body);

std::vector<athena_person_occurrence>
athena_collect_person_occurrences (tree body);

std::vector<string> athena_collect_person_names (tree body);

bool athena_tree_contains_person_text (tree body, string name);

#endif // ATHENA_PERSON_NAMES_HPP

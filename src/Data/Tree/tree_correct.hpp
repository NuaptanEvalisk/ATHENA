
/******************************************************************************
* MODULE     : tree_correct.hpp
* DESCRIPTION: source tree normalization and import correction
* COPYRIGHT  : (C) 2005  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef TREE_CORRECT_H
#define TREE_CORRECT_H
#include "tree.hpp"
#include "Data/NodeContracts/editing_query.hpp"
#include "Data/NodeContracts/source_environment.hpp"

namespace athena::contracts { class contract_scope; }

tree correct_import_arity (const athena::contracts::contract_scope& scope, tree t);
tree correct_concat_block (tree t);
tree with_correct (tree t);
tree superfluous_with_correct (tree t);
tree with_correct (tree, const athena::contracts::source_contract_preparation&);
tree superfluous_with_correct (tree,
  const athena::contracts::source_environment_preparation&);
using correction_context_factory= std::function<
  athena::contracts::source_environment_preparation (tree)>;
tree superfluous_invisible_correct (tree t);
tree superfluous_invisible_correct (tree, const correction_context_factory&);
tree missing_invisible_correct (tree t, int force= -1);
tree missing_invisible_correct (tree, int force, const correction_context_factory&);
tree upgrade_brackets (tree t, string mode= "");
tree upgrade_brackets (tree, const correction_context_factory&, string mode= "");
tree upgrade_big (tree t);
tree upgrade_big (tree, const correction_context_factory&);
tree downgrade_brackets (tree t, bool del_miss= false, bool big_dot= true);
tree downgrade_big (tree t);
tree move_brackets (tree t);

tree latex_correct (tree t);
tree automatic_correct (tree t, string version);
tree manual_correct (tree t);
tree manual_correct (tree, const correction_context_factory&);

#endif // defined TREE_CORRECT_H

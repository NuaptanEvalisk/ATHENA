
/******************************************************************************
* MODULE     : drd_info.hpp
* DESCRIPTION: data relation descriptions
* COPYRIGHT  : (C) 2003  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef DRD_INFO_H
#define DRD_INFO_H
#include "tree.hpp"
#include "hashmap.hpp"
#include "path.hpp"
#include "tag_info.hpp"
#include "Data/NodeContracts/contract_scope.hpp"
#include <memory>

namespace athena::contracts {
class macro_analysis;
class evaluation_input_snapshot;
}

class drd_info;
class drd_info_rep: concrete_struct {
public:
  string name;
  athena::contracts::contract_scope contracts;
  hashmap<string,tree> env;
  std::unique_ptr<athena::contracts::macro_analysis> analysis;

public:
  drd_info_rep (string name);
  drd_info_rep (string name, drd_info base);
  ~drd_info_rep ();
  bool contains (string l);
  const athena::contracts::node_contract& describe (tree_label label);

  /* Properties of the tag itself */
  void set_type (tree_label tag, int tp,
                 property_authority source= property_authority::inferred);
  int  get_type (tree_label tag);
  int  get_type (tree t);

  void set_arity (tree_label tag, int arity, int extra, int am, int cm,
                  property_authority source= property_authority::inferred);
  int  get_arity_mode (tree_label tag);
  int  get_arity_base (tree_label tag);
  int  get_arity_extra (tree_label tag);
  int  get_child_mode (tree_label tag);
  int  get_nr_indices (tree_label tag);
  int  get_old_arity (tree_label l);
  int  get_minimal_arity (tree_label l);
  int  get_maximal_arity (tree_label l);
  bool correct_arity (tree_label l, int i);
  bool insert_point (tree_label l, int i, int n);
  bool is_dynamic (tree t, bool hack= true);

  void set_border (tree_label tag, int mode,
                   property_authority source= property_authority::inferred);
  int  get_border (tree_label tag);
  bool is_child_enforcing (tree t);
  bool is_parent_enforcing (tree t);
  bool var_without_border (tree_label tag);

  void set_block (tree_label tag, int is_block);
  int  get_block (tree_label tag);

  void set_with_like (tree_label tag, bool is_with_like,
                      property_authority source= property_authority::inferred);
  bool get_with_like (tree_label tag);
  bool is_with_like (tree t);

  void set_var_type (tree_label tag, int vt,
                     property_authority source= property_authority::inferred);
  int  get_var_type (tree_label tag);

  void set_attribute (tree_label tag, string which, tree val);
  tree get_attribute (tree_label tag, string which);
  void set_name (tree_label tag, string val);
  void set_long_name (tree_label tag, string val);
  void set_syntax (tree_label tag, tree val);
  string get_name (tree_label tag);
  string get_long_name (tree_label tag);
  tree   get_syntax (tree_label tag);
  tree   get_syntax (tree t, path p= path (-1));

  /* Properties of the children of the tag */
  void set_type (tree_label tag, int nr, int tp,
                 property_authority source= property_authority::inferred);
  int  get_type (tree_label tag, int nr);
  int  get_type_child (tree t, int child);

  void set_accessible (tree_label tag, int nr, int access_mode,
                       property_authority source= property_authority::inferred);
  int  get_accessible (tree_label tag, int nr);
  bool all_accessible (tree_label tag);
  bool none_accessible (tree_label tag);
  bool is_accessible_child (tree t, int child);
  bool is_accessible_path (tree t, path p);

  void set_writability (tree_label tag, int nr, int writability,
                        property_authority source= property_authority::inferred);
  int  get_writability (tree_label tag, int nr);
  int  get_writability_child (tree t, int child);

  void set_block (tree_label tag, int nr, int require_block);
  int  get_block (tree_label tag, int nr);

  void set_env (tree_label tag, int nr, tree env,
                property_authority source= property_authority::inferred);
  tree get_env (tree_label tag, int nr);
  tree get_env_child (tree t, int child, tree env);
  tree get_env_child (tree t, int child, string var, tree val);
  tree get_env_descendant (tree t, path p, tree env);
  tree get_env_descendant (tree t, path p, string var, tree val);

  void set_child_name (tree_label tag, int nr, string val);
  void set_child_long_name (tree_label tag, int nr, string val);
  string get_child_name (tree_label tag, int nr);
  string get_child_long_name (tree_label tag, int nr);
  string get_child_name (tree t, int child);
  string get_child_long_name (tree t, int child);

  /* Declaration analysis; queries never invoke this boundary. */
  void set_environment (hashmap<string,tree> env);
  void analyze_environment (const athena::contracts::evaluation_input_snapshot& inputs,
                            hashmap<string,tree> env);
  void declaration_changed (tree_label tag);

  friend class drd_info;
  friend tm_ostream& operator << (tm_ostream& out, drd_info drd);
};

class drd_info {
  CONCRETE(drd_info);
  drd_info (string name);
  drd_info (string name, drd_info base);
  operator tree ();
};
CONCRETE_CODE(drd_info);

// Apply one declarative <drd-props|...> node without evaluating a macro.
// Detached importers and the live environment share this implementation.
void apply_drd_properties (drd_info drd, tree properties);

tree drd_env_write (tree env, string var, tree val);
tree drd_env_merge (tree env, tree t);
tree drd_env_read (tree env, string var, tree val= tree (UNINIT));

#endif // defined DRD_INFO_H


/******************************************************************************
* MODULE     : drd_info.cpp
* DESCRIPTION: data relation descriptions
* COPYRIGHT  : (C) 2003  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "vars.hpp"
#include "drd_info.hpp"
#include "drd_std.hpp"
#include "drd_mode.hpp"
#include "iterator.hpp"
#include "analyze.hpp"
#include "Data/NodeContracts/node_group_scope.hpp"
#include "Data/NodeContracts/macro_analysis.hpp"
#include "Data/NodeContracts/contract_declaration.hpp"
#include "Data/NodeContracts/native_fields.hpp"

/******************************************************************************
* Constructors and basic operations
******************************************************************************/

drd_info_rep::drd_info_rep (string name2):
  name (name2), env (UNINIT) {}
drd_info_rep::drd_info_rep (string name2, drd_info base):
  name (name2), contracts (base->contracts.snapshot ()), env (UNINIT) {}
drd_info_rep::~drd_info_rep ()= default;
drd_info::drd_info (string name):
  rep (tm_new<drd_info_rep> (name)) {}
drd_info::drd_info (string name, drd_info base):
  rep (tm_new<drd_info_rep> (name, base)) {}

bool
drd_info_rep::contains (string l) {
  if (!existing_tree_label (l)) {
    athena::contracts::observe_contract_absence (contracts, l);
    return false;
  }
  const auto label= as_tree_label (l);
  athena::contracts::observe_contract_query (contracts, label);
  return contracts.contains (label);
}

const athena::contracts::node_contract&
drd_info_rep::describe (tree_label label) {
  athena::contracts::observe_contract_query (contracts, label);
  return contracts.resolve (label);
}

tm_ostream&
operator << (tm_ostream& out, drd_info drd) {
  return out << "drd [" << drd->name << "]";
}

/******************************************************************************
* Tag types
******************************************************************************/

void
drd_info_rep::set_type (tree_label l, int tp, property_authority source) {
  contracts.edit (l, [=] (auto& value) {
    if (!athena::contracts::accepts (value.type_origin, source)) return;
    value.type= tp;
    value.type_origin= source;
  });
}

int
drd_info_rep::get_type (tree_label l) {
  return describe (l).type;
}

int
drd_info_rep::get_type (tree t) {
  return describe (L(t)).type;
}

/******************************************************************************
* Arity related methods
******************************************************************************/

void
drd_info_rep::set_arity (tree_label l, int arity, int extra, int am, int cm,
                         property_authority source) {
  contracts.edit (l, [=] (auto& value) {
    if (!athena::contracts::accepts (value.layout_origin, source)) return;
    value.layout_origin= source;
    value.layout= {am == ARITY_VAR_REPEAT ? extra : arity,
      am == ARITY_VAR_REPEAT ? arity : extra,
      static_cast<athena::contracts::arity_kind> (am),
      static_cast<athena::contracts::field_mapping> (cm)};
    const auto count= value.layout.fields ();
    if (int(value.fields.size ()) != count)
      value.fields.assign (count, athena::contracts::field_contract {});
  });
}

int
drd_info_rep::get_arity_mode (tree_label l) {
  return static_cast<int> (describe (l).layout.arity);
}

int
drd_info_rep::get_child_mode (tree_label l) {
  return static_cast<int> (describe (l).layout.mapping);
}

int
drd_info_rep::get_arity_base (tree_label l) {
  return describe (l).layout.base;
}

int
drd_info_rep::get_arity_extra (tree_label l) {
  return describe (l).layout.extra;
}

int
drd_info_rep::get_nr_indices (tree_label l) {
  return int(describe (l).fields.size ());
}

int
drd_info_rep::get_old_arity (tree_label l) {
  const auto& ti= describe (l);
  if (ti.layout.arity != athena::contracts::arity_kind::fixed) return -1;
  else return ((int) ti.layout.base) + ((int) ti.layout.extra);
}

int
drd_info_rep::get_minimal_arity (tree_label l) {
  return describe (l).layout.minimum ();
}

int
drd_info_rep::get_maximal_arity (tree_label l) {
  const auto& layout= describe (l).layout;
  switch (static_cast<int> (layout.arity)) {
  case ARITY_NORMAL:
  case ARITY_OPTIONS:
    return ((int) layout.base) + ((int) layout.extra);
  case ARITY_REPEAT:
  case ARITY_VAR_REPEAT:
    return 0x7fffffff;
  }
  return 0; // NOT REACHED
}

bool
drd_info_rep::correct_arity (tree_label l, int i) {
  return describe (l).layout.accepts (i);
}

bool
drd_info_rep::insert_point (tree_label l, int i, int n) {
  const auto& layout= describe (l).layout;
  switch (static_cast<int> (layout.arity)) {
  case ARITY_NORMAL:
    return false;
  case ARITY_OPTIONS:
    return (i >= ((int) layout.base)) && (i <= n) &&
           (n < ((int) layout.base) + ((int) layout.extra));
  case ARITY_REPEAT:
    return (i >= 0) &&
           ((i < ((int) layout.base)) ||
            ((i - layout.base) % layout.extra) == 0);
  case ARITY_VAR_REPEAT:
    return (i >= 0) &&
           ((i > (n - ((int) layout.base))) ||
            (i % layout.extra == 0));
  }
  return false; // NOT REACHED
}

bool
drd_info_rep::is_dynamic (tree t, bool hack) {
  if (hack && L(t) >= START_EXTENSIONS) return true; // FIXME: temporary fix
  if (is_atomic (t)) return false;
  if (is_func (t, DOCUMENT) || is_func (t, PARA) || is_func (t, CONCAT) ||
      is_func (t, TABLE) || is_func (t, ROW)) return false;
  return describe (L(t)).layout.arity != athena::contracts::arity_kind::fixed;
}

/******************************************************************************
* Border accessability related methods
******************************************************************************/

void
drd_info_rep::set_border (tree_label l, int mode, property_authority source) {
  contracts.edit (l, [=] (auto& value) {
    if (!athena::contracts::accepts (value.border_origin, source)) return;
    value.border= mode;
    value.border_origin= source;
  });
}

int
drd_info_rep::get_border (tree_label l) {
  return describe (l).border;
}

bool
drd_info_rep::is_child_enforcing (tree t) {
  return ((describe (L(t)).border & BORDER_INNER) != 0) &&
         (N(t) != 0);
}

bool
drd_info_rep::is_parent_enforcing (tree t) {
  return ((describe (L(t)).border & BORDER_OUTER) != 0) &&
         (N(t) != 0);
}

bool
drd_info_rep::var_without_border (tree_label l) {
  return ((describe (l).border & BORDER_INNER) != 0) &&
         (!std_contains (as_string (l)));
}

/******************************************************************************
* With-like structures correspond to macros which just modify
* the current environment, such as the 'strong' tag
******************************************************************************/

void
drd_info_rep::set_with_like (tree_label l, bool is_with_like,
                            property_authority source) {
  contracts.edit (l, [=] (auto& value) {
    if (!athena::contracts::accepts (value.with_origin, source)) return;
    value.with_like= is_with_like;
    value.with_origin= source;
  });
}

bool
drd_info_rep::get_with_like (tree_label l) {
  return describe (l).with_like;
}

bool
drd_info_rep::is_with_like (tree t) {
  return describe (L(t)).with_like && N(t) > 0;
}

/******************************************************************************
* The var_type determines whether the tag is a regular macro,
* or rather an environment variable (which could be a macro).
******************************************************************************/

void
drd_info_rep::set_var_type (tree_label l, int vt, property_authority source) {
  contracts.edit (l, [=] (auto& value) {
    if (!athena::contracts::accepts (value.variable_type_origin, source)) return;
    value.variable_type= vt;
    value.variable_type_origin= source;
  });
}

int
drd_info_rep::get_var_type (tree_label l) {
  return describe (l).variable_type;
}

/******************************************************************************
* Other attributes
******************************************************************************/

void
drd_info_rep::set_attribute (tree_label l, string which, tree val) {
  contracts.edit (l, [&] (auto& value) {
    value.attributes.insert_or_assign (std::string (which.data (), N(which)),
      athena::node::frozen_tree (val));
  });
}

tree
drd_info_rep::get_attribute (tree_label l, string which) {
  tree val= describe (l).attribute (std::string_view (which.data (), N(which)));
  if ((which == "name") && (val == ""))
    return as_string (l);
  return val;
}

void
drd_info_rep::set_name (tree_label l, string val) {
  set_attribute (l, "name", val);
}

void
drd_info_rep::set_long_name (tree_label l, string val) {
  set_attribute (l, "long-name", val);
}

void
drd_info_rep::set_syntax (tree_label l, tree val) {
  set_attribute (l, "syntax", val);
}

string
drd_info_rep::get_name (tree_label l) {
  return as_string (get_attribute (l, "name"));
}

string
drd_info_rep::get_long_name (tree_label l) {
  string r= as_string (get_attribute (l, "long-name"));
  if (r != "") return r;
  return as_string (get_attribute (l, "name"));
}

static tree
syntax_for_name (drd_info_rep& scope, const string& name) {
  if (existing_tree_label (name)) {
    tree syntax= scope.get_attribute (as_tree_label (name), "syntax");
    if (syntax != "") return syntax;
  }
  else athena::contracts::observe_contract_absence (scope.contracts, name);
  const bool present= scope.env->contains (name);
  tree value= present ? scope.env[name] : tree (UNINIT);
  athena::contracts::observe_contract_variable (scope.contracts, name, present,
                                               present ? &value : nullptr);
  return value;
}

tree
drd_info_rep::get_syntax (tree_label l) {
  return syntax_for_name (*this, as_string (l));
}

static tree
replace (tree t, hashmap<tree,tree> h) {
  if (h->contains (t)) return h[t];
  else if (is_atomic (t)) return t;
  else {
    int i, n= N(t);
    tree r (t, n);
    for (i=0; i<n; i++)
      r[i]= replace (t[i], h);
    return r;
  }
}

tree
drd_info_rep::get_syntax (tree t, path p) {
  if (is_func (t, VALUE, 1) && is_atomic (t[0])) {
    return syntax_for_name (*this, t[0]->label);
  }
  else if (is_func (t, OR_VALUE)) {
    for (int i=0; i<N(t); i++)
      if (is_atomic (t[i])) {
        tree r= syntax_for_name (*this, t[i]->label);
        if (r != UNINIT) return t;
      }
    return UNINIT;
  }
  else if (L(t) < START_EXTENSIONS || is_atomic (t))
    return UNINIT;
  else {
    tree fun= get_syntax (L(t));
    if (fun == UNINIT) return UNINIT;
    else if (N(t) == 0 && !is_func (fun, MACRO)) return fun;
    else if (!is_func (fun, MACRO)) return UNINIT;
    else {
      int i, n= N(fun)-1;
      hashmap<tree,tree> tab (UNINIT);
      for (i=0; i<n; i++) {
        tree var= tree (ARG, fun[i]);
        tree val= "";
        if (i < N(t)) {
          if (p == path (-1)) val= t[i];
          else val= tree (QUASI, t[i], (tree) (p * i));
        }
        tab (var)= val;
      }
      return replace (fun[n], tab);
    }
  }
}

/******************************************************************************
* Children's mode
******************************************************************************/

static const athena::contracts::node_contract&
descriptor_for (drd_info_rep& scope, const tree& value) {
  if (is_func (value, EXTERN) && N(value)>0 && is_atomic (value[0])) {
    const string name= "extern:" * value[0]->label;
    if (scope.contains (name)) return scope.describe (as_tree_label (name));
  }
  return scope.describe (L(value));
}

void
drd_info_rep::set_type (tree_label l, int nr, int tp, property_authority source) {
  contracts.edit (l, [&] (auto& value) {
    if (nr < 0 || nr >= int(value.fields.size ())) return;
    auto& field= value.fields[nr];
    if (!athena::contracts::accepts (field.type_origin, source)) return;
    field.type= tp;
    field.type_origin= source;
  });
}

int
drd_info_rep::get_type (tree_label l, int nr) {
  if (nr >= int(describe (l).fields.size ())) return TYPE_ADHOC;
  return describe (l).fields[nr].type;
}

int
drd_info_rep::get_type_child (tree t, int i) {
  const auto& ti= descriptor_for (*this, t);
  int index= ti.layout.index (i, N(t));
  if ((index<0) || (index>=int(ti.fields.size ()))) return TYPE_INVALID;
  int r= ti.fields[index].type;
  if (r != TYPE_BINDING) return r;
  if ((i & 1) == 0) return TYPE_VARIABLE;
  return TYPE_REGULAR;
}

/******************************************************************************
* Children's accessability related methods
******************************************************************************/

void
drd_info_rep::set_accessible (tree_label l, int nr, int is_accessible,
                             property_authority source) {
  contracts.edit (l, [&] (auto& value) {
    if (nr < 0 || nr >= int(value.fields.size ())) return;
    auto& field= value.fields[nr];
    if (!athena::contracts::accepts (field.accessible_origin, source)) return;
    field.accessible= is_accessible;
    field.accessible_origin= source;
  });
}

int
drd_info_rep::get_accessible (tree_label l, int nr) {
  if (nr >= int(describe (l).fields.size ())) return ACCESSIBLE_NEVER;
  return describe (l).fields[nr].accessible;
}

bool
drd_info_rep::all_accessible (tree_label l) {
  int i, n= int(describe (l).fields.size ());
  for (i=0; i<n; i++)
    if (describe (l).fields[i].accessible != ACCESSIBLE_ALWAYS)
      return false;
  return n>0;
}

bool
drd_info_rep::none_accessible (tree_label l) {
  int i, n= int(describe (l).fields.size ());
  for (i=0; i<n; i++)
    if (describe (l).fields[i].accessible != ACCESSIBLE_NEVER)
      return false;
  return true;
}

bool
drd_info_rep::is_accessible_child (tree t, int i) {
  //cout << "l= " << as_string (L(t)) << "\n";
  const auto& ti= descriptor_for (*this, t);
  int index= ti.layout.index (i, N(t));
  if ((index<0) || (index>=int(ti.fields.size ()))) {
    if (get_access_mode () == DRD_ACCESS_SOURCE)
      return !is_atomic (t) && i >= 0 && i < N(t);
    else return false;
  }
  switch (get_access_mode ()) {
  case DRD_ACCESS_NORMAL:
    return ti.fields[index].accessible == ACCESSIBLE_ALWAYS;
  case DRD_ACCESS_HIDDEN:
    return ti.fields[index].accessible == ACCESSIBLE_ALWAYS ||
           ti.fields[index].accessible == ACCESSIBLE_HIDDEN;
  case DRD_ACCESS_SOURCE:
    return true;
  }
  return true; // NOT REACHED
}

bool
drd_info_rep::is_accessible_path (tree t, path p) {
  if (is_nil (p)) return true;
  return
    is_accessible_child (t, p->item) &&
    (p->item < N(t)) &&
    is_accessible_path (t[p->item], p->next);
}

/******************************************************************************
* Children's writability
******************************************************************************/

void
drd_info_rep::set_writability (tree_label l, int nr, int writability,
                              property_authority source) {
  contracts.edit (l, [&] (auto& value) {
    if (nr < 0 || nr >= int(value.fields.size ())) return;
    auto& field= value.fields[nr];
    if (!athena::contracts::accepts (field.writability_origin, source)) return;
    field.writability= writability;
    field.writability_origin= source;
  });
}

int
drd_info_rep::get_writability (tree_label l, int nr) {
  if (nr >= int(describe (l).fields.size ())) return WRITABILITY_NORMAL;
  return describe (l).fields[nr].writability;
}

int
drd_info_rep::get_writability_child (tree t, int i) {
  const auto& ti= descriptor_for (*this, t);
  int index= ti.layout.index (i, N(t));
  if ((index<0) || (index>=int(ti.fields.size ()))) return WRITABILITY_DISABLE;
  return ti.fields[index].writability;
}

/******************************************************************************
* Child names, based on set/get attribute for parent
******************************************************************************/

void
drd_info_rep::set_child_name (tree_label l, int nr, string val) {
  set_attribute (l, "name-" * as_string (nr), val);
}

void
drd_info_rep::set_child_long_name (tree_label l, int nr, string val) {
  set_attribute (l, "long-name-" * as_string (nr), val);
}

string
drd_info_rep::get_child_name (tree_label l, int nr) {
  return as_string (get_attribute (l, "name-" * as_string (nr)));
}

string
drd_info_rep::get_child_long_name (tree_label l, int nr) {
  return as_string (get_attribute (l, "long-name-" * as_string (nr)));
}

string
drd_info_rep::get_child_name (tree t, int i) {
  if (const auto* native= athena::contracts::native_fields (L(t))) {
    const auto* field= native->child (i, N(t));
    return field ? string (field->name.data (), field->name.size ()) : string ("");
  }
  const auto& ti= descriptor_for (*this, t);
  int index= ti.layout.index (i, N(t));
  if ((index<0) || (index>=int(ti.fields.size ()))) return "";
  return as_string (ti.attribute ("name-" + std::to_string (index)));
}

string
drd_info_rep::get_child_long_name (tree t, int i) {
  if (const auto* native= athena::contracts::native_fields (L(t))) {
    const auto* field= native->child (i, N(t));
    return field ? string (field->name.data (), field->name.size ()) : string ("");
  }
  const auto& ti= descriptor_for (*this, t);
  int index= ti.layout.index (i, N(t));
  if ((index<0) || (index>=int(ti.fields.size ()))) return "";
  string r= as_string (ti.attribute ("long-name-" + std::to_string (index)));
  if (r != "") return r;
  return as_string (ti.attribute ("name-" + std::to_string (index)));
}

/******************************************************************************
* Environment determination
******************************************************************************/

tree
drd_env_write (tree env, string var, tree val) {
  for (int i=0; i<=N(env); i+=2)
    if (i == N(env))
      return env * tree (ATTR, var, val);
    else if (var <= env[i]->label) {
      if (var == env[i]->label)
        return env (0, i) * tree (ATTR, var, val) * env (i+2, N(env));
      return env (0, i) * tree (ATTR, var, val) * env (i, N(env));
    }
  return env;
}

tree
drd_env_merge (tree env, tree t) {
  int i, n= N(t);
  for (i=0; (i+1)<n; i+=2)
    if (is_atomic (t[i]))
      env= drd_env_write (env, t[i]->label, t[i+1]);
  return env;
}

tree
drd_env_read (tree env, string var, tree val) {
  int i, n= N(env);
  for (i=0; i<n; i+=2)
    if (env[i] == var)
      return env[i+1];
  return val;
}

void
drd_info_rep::set_env (tree_label l, int nr, tree env, property_authority source) {
  contracts.edit (l, [&] (auto& value) {
    if (nr < 0 || nr >= int(value.fields.size ())) return;
    auto& field= value.fields[nr];
    if (!athena::contracts::accepts (field.environment_origin, source)) return;
    field.environment= athena::node::frozen_tree (env);
    field.environment_origin= source;
  });
}

tree
drd_info_rep::get_env (tree_label l, int nr) {
  if (nr >= int(describe (l).fields.size ())) return tree (ATTR);
  return tree (describe (l).fields[nr].environment);
}

tree
drd_info_rep::get_env_child (tree t, int i, tree env) {
  if (L(t) == WITH && i == N(t)-1)
    return drd_env_merge (env, t (0, N(t)-1));
  else {
    if (const auto* native= athena::contracts::native_fields (L(t))) {
      const auto* field= native->child (i, N(t));
      if (!field) return "";
      tree local (ATTR);
      for (const auto& [key, value]: field->environment)
        local << string (key.data (), key.size ()) <<
                 string (value.data (), value.size ());
      return drd_env_merge (env, local);
    }
    /* makes cursor movement (is_accessible_cursor) slow for large preambles
    if (L(t) == DOCUMENT && N(t) > 0 &&
        (is_compound (t[0], "hide-preamble", 1) ||
         is_compound (t[0], "show-preamble", 1)))
      {
        tree u= t[0][0];
        if (!is_func (u, DOCUMENT)) u= tree (DOCUMENT, u);
        tree cenv (ATTR);
        for (int i=0; i<N(u); i++)
          if (is_func (u[i], ASSIGN, 2))
            cenv << copy (u[i][0]) << copy (u[i][1]);
        env= drd_env_merge (env, cenv);
      }
    */

    const auto& ti= describe (L(t));
    int index= ti.layout.index (i, N(t));
    if ((index<0) || (index>=int(ti.fields.size ()))) return "";
    tree cenv (ti.fields[index].environment);
    for (int i=1; i<N(cenv); i+=2)
      if (is_func (cenv[i], ARG, 1) && is_int (cenv[i][0])) {
        cenv= copy (cenv);
        int j= as_int (cenv[i][0]);
        if (j>=0 && j<N(t)) cenv[i]= copy (t[j]);
      }
    return drd_env_merge (env, cenv);
  }
}

tree
drd_info_rep::get_env_child (tree t, int i, string var, tree val) {
  tree env= get_env_child (t, i, tree (ATTR));
  return drd_env_read (env, var, val);
}

tree
drd_info_rep::get_env_descendant (tree t, path p, tree env) {
  if (is_nil (p) || env == "") return env;
  int  i= p->item;
  path q= p->next;
  if (is_compound (t) && i >= 0 && i < N(t))
    return get_env_descendant (t[i], q, get_env_child (t, i, env));
  return "";
}

tree
drd_info_rep::get_env_descendant (tree t, path p, string var, tree val) {
  if (is_nil (p)) return val;
  int  i= p->item;
  path q= p->next;
  if (is_compound (t) && i >= 0 && i < N(t))
    return get_env_descendant (t[i], q, var, get_env_child (t, i, var, val));
  return val;
}

/******************************************************************************
* Owner-local declaration analysis
******************************************************************************/

void
drd_info_rep::set_environment (hashmap<string,tree> env2) {
  env= env2;
}

void
drd_info_rep::analyze_environment (
    const athena::contracts::evaluation_input_snapshot& inputs,
    hashmap<string,tree> values) {
  set_environment (values);
  if (!analysis) analysis= std::make_unique<athena::contracts::macro_analysis> (*this);
  analysis->synchronize (inputs, values);
}

void
drd_info_rep::declaration_changed (tree_label label) {
  if (analysis) analysis->invalidate (as_string (label));
}

void
apply_drd_properties (drd_info destination, tree t) {
  const auto declaration= athena::contracts::decode_contract_declaration (t);
  if (athena::contracts::apply_contract_declaration (destination->contracts, declaration))
    destination->declaration_changed (declaration.target);
}

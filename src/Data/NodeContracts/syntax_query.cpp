// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "syntax_query.hpp"
#include "contract_declaration.hpp"
#include "analyze.hpp"

namespace athena::contracts {

tree syntax_query::definition (string name) const {
  if (existing_tree_label (name)) {
    const tree syntax= contracts.describe (as_tree_label (name)).attribute ("syntax");
    if (syntax != "") {
      if (read_definition) read_definition (name, syntax);
      return syntax;
    }
  }
  else observe_contract_absence (contracts.scope, name);
  auto variables= values;
  const bool present= variables->contains (name);
  tree value= present ? variables[name] : tree (UNINIT);
  observe_contract_variable (contracts.scope, name, present, present ? &value : nullptr);
  if (read_definition) read_definition (name, value);
  return value;
}

static tree substitute (tree t, hashmap<tree,tree> arguments) {
  if (arguments->contains (t)) return arguments[t];
  if (is_atomic (t)) return t;
  tree result (t, N(t));
  for (int i=0; i<N(t); ++i) result[i]= substitute (t[i], arguments);
  return result;
}

tree syntax_query::expand (tree t, path p) const {
  if (is_func (t, VALUE, 1) && is_atomic (t[0])) return definition (t[0]->label);
  if (is_func (t, OR_VALUE)) {
    for (int i=0; i<N(t); ++i)
      if (is_atomic (t[i]) && definition (t[i]->label) != UNINIT) return t;
    return UNINIT;
  }
  if (L(t) < START_EXTENSIONS || is_atomic (t)) return UNINIT;
  tree fun= definition (as_string (L(t)));
  if (fun == UNINIT) return UNINIT;
  if (!is_func (fun, MACRO)) return N(t) == 0 ? fun : tree (UNINIT);
  hashmap<tree,tree> arguments (UNINIT);
  for (int i=0; i<N(fun)-1; ++i) {
    tree value= "";
    if (i < N(t)) value= p == path (-1) ? t[i] : tree (QUASI, t[i], (tree) (p*i));
    arguments (tree (ARG, fun[i]))= value;
  }
  return substitute (fun[N(fun)-1], arguments);
}

} // namespace athena::contracts

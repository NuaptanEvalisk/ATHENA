// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "editing_query.hpp"
#include "contract_declaration.hpp"
#include "native_fields.hpp"
#include "analyze.hpp"
#include "vars.hpp"

namespace athena::contracts {

const node_contract& editing_query::describe (tree_label label) const {
  observe_contract_query (scope, label);
  return scope.resolve (label);
}

bool editing_query::contains (string name) const {
  if (!existing_tree_label (name)) {
    observe_contract_absence (scope, name);
    return false;
  }
  const auto label= as_tree_label (name);
  observe_contract_query (scope, label);
  return scope.contains (label);
}

string editing_query::name (tree_label label, bool long_name) const {
  const auto& contract= describe (label);
  if (long_name) {
    const string value= as_string (contract.attribute ("long-name"));
    if (value != "") return value;
  }
  const string value= as_string (contract.attribute ("name"));
  return value == "" ? as_string (label) : value;
}

bool editing_query::variable_arity (const tree& t) const {
  if (is_atomic (t) || is_func (t, DOCUMENT) || is_func (t, PARA) ||
      is_func (t, CONCAT) || is_func (t, TABLE) || is_func (t, ROW)) return false;
  return describe (L(t)).layout.arity != arity_kind::fixed;
}

bool editing_query::all_fields_editable (tree_label label) const {
  if (const auto* native= native_fields (label)) {
    for (const auto& field: native->children) if (!field.editable) return false;
    return !native->children.empty ();
  }
  const auto& fields= describe (label).fields;
  for (const auto& field: fields) if (field.accessible != ACCESSIBLE_ALWAYS) return false;
  return !fields.empty ();
}

bool editing_query::no_fields_editable (tree_label label) const {
  if (const auto* native= native_fields (label)) {
    for (const auto& field: native->children) if (field.editable) return false;
    return true;
  }
  for (const auto& field: describe (label).fields)
    if (field.accessible != ACCESSIBLE_NEVER) return false;
  return true;
}

const node_contract& editing_query::describe (const tree& t) const {
  if (is_func (t, EXTERN) && N(t) > 0 && is_atomic (t[0])) {
    const string name= "extern:" * t[0]->label;
    if (!existing_tree_label (name)) observe_contract_absence (scope, name);
    else {
      const auto label= as_tree_label (name);
      observe_contract_query (scope, label);
      if (scope.contains (label)) return scope.resolve (label);
    }
  }
  return describe (L(t));
}

bool editing_query::child_enforcing (const tree& t) const {
  if (const auto* native= native_fields (L(t)))
    return native->inner_border && N(t) != 0;
  return (describe (L(t)).border & BORDER_INNER) != 0 && N(t) != 0;
}

bool editing_query::parent_enforcing (const tree& t) const {
  if (native_fields (L(t))) return false;
  return (describe (L(t)).border & BORDER_OUTER) != 0 && N(t) != 0;
}

bool editing_query::accessible (const tree& t, int child) const {
  if (is_atomic (t) || child < 0 || child >= N(t)) return false;
  if (access == access_policy::source) return true;
  if (const auto* native= native_fields (L(t))) {
    const auto* field= native->child (child, N(t));
    return field && field->editable;
  }
  const auto& contract= describe (t);
  const int index= contract.layout.index (child, N(t));
  if (index < 0 || index >= int (contract.fields.size ())) return false;
  const int value= contract.fields[index].accessible;
  return value == ACCESSIBLE_ALWAYS ||
    (access == access_policy::hidden && value == ACCESSIBLE_HIDDEN);
}

bool editing_query::accessible (const tree& t, path p) const {
  if (is_nil (p)) return true;
  return accessible (t, p->item) && accessible (t[p->item], p->next);
}

bool editing_query::mapped_accessible (const tree& t, int child) const {
  if (is_atomic (t) || child < 0 || child >= N(t)) return false;
  if (access == access_policy::source || native_fields (L(t)))
    return accessible (t, child);
  const auto& contract= describe (t);
  const int index= contract.layout.index (child, N(t));
  if (index < 0 || index >= int (contract.fields.size ()))
    return contract.layout_origin == property_authority::inferred;
  // An actual display proves this one field is present even when analysis is
  // pending, conditional or ambiguous. Authored visibility remains authoritative.
  return contract.fields[index].accessible_origin == property_authority::inferred ||
    accessible (t, child);
}

int editing_query::child_type (const tree& t, int child) const {
  if (const auto* native= native_fields (L(t))) {
    const auto* field= native->child (child, N(t));
    if (!field) return TYPE_INVALID;
    return field->value == field_value::atom ? TYPE_STRING : TYPE_REGULAR;
  }
  const auto& contract= describe (t);
  const int index= contract.layout.index (child, N(t));
  if (index < 0 || index >= int (contract.fields.size ())) return TYPE_INVALID;
  const int type= contract.fields[index].type;
  return type != TYPE_BINDING ? type :
    (child & 1) == 0 ? TYPE_VARIABLE : TYPE_REGULAR;
}

string editing_query::child_name (const tree& t, int child, bool long_name) const {
  if (const auto* native= native_fields (L(t))) {
    const auto* field= native->child (child, N(t));
    return field ? string (field->name.data (), field->name.size ()) : string ("");
  }
  const auto& contract= describe (t);
  const int index= contract.layout.index (child, N(t));
  if (index < 0 || index >= int (contract.fields.size ())) return "";
  if (long_name) {
    const string result= as_string (contract.attribute ("long-name-" + std::to_string (index)));
    if (result != "") return result;
  }
  return as_string (contract.attribute ("name-" + std::to_string (index)));
}

int editing_query::writability (const tree& t, int child) const {
  if (const auto* native= native_fields (L(t)))
    return native->child (child, N(t)) ? WRITABILITY_NORMAL : WRITABILITY_DISABLE;
  const auto& contract= describe (t);
  const int index= contract.layout.index (child, N(t));
  if (index < 0 || index >= int (contract.fields.size ())) return WRITABILITY_DISABLE;
  return contract.fields[index].writability;
}

tree field_environment_write (tree env, string name, tree value) {
  for (int i= 0; i <= N(env); i+= 2) {
    if (i == N(env)) return env * tree (ATTR, name, value);
    if (name <= env[i]->label) {
      if (name == env[i]->label)
        return env (0, i) * tree (ATTR, name, value) * env (i+2, N(env));
      return env (0, i) * tree (ATTR, name, value) * env (i, N(env));
    }
  }
  return env;
}

tree field_environment_merge (tree env, tree overlay) {
  for (int i= 0; i+1 < N(overlay); i+= 2)
    if (is_atomic (overlay[i]))
      env= field_environment_write (env, overlay[i]->label, overlay[i+1]);
  return env;
}

tree field_environment_read (tree env, string name, tree fallback) {
  for (int i= 0; i < N(env); i+= 2)
    if (env[i] == name) return env[i+1];
  return fallback;
}

tree editing_query::environment (const tree& t, int child, tree inherited) const {
  if (L(t) == WITH && child == N(t)-1)
    return field_environment_merge (inherited, t (0, N(t)-1));
  if (const auto* native= native_fields (L(t))) {
    const auto* field= native->child (child, N(t));
    if (!field) return "";
    tree local (ATTR);
    for (const auto& [key, value]: field->environment)
      local << string (key.data (), key.size ()) << string (value.data (), value.size ());
    return field_environment_merge (inherited, local);
  }
  const auto& contract= describe (L(t));
  const int index= contract.layout.index (child, N(t));
  if (index < 0 || index >= int (contract.fields.size ())) return "";
  tree local (contract.fields[index].environment);
  for (int i= 1; i < N(local); i+= 2)
    if (is_func (local[i], ARG, 1) && is_int (local[i][0])) {
      const int source= as_int (local[i][0]);
      if (source >= 0 && source < N(t)) {
        local= copy (local);
        local[i]= copy (t[source]);
      }
    }
  return field_environment_merge (inherited, local);
}

tree editing_query::value (const tree& t, int child, string name, tree fallback) const {
  return field_environment_read (environment (t, child, tree (ATTR)), name, fallback);
}

tree editing_query::environment (const tree& t, path p, tree inherited) const {
  if (is_nil (p) || inherited == "") return inherited;
  const int child= p->item;
  if (is_atomic (t) || child < 0 || child >= N(t)) return "";
  return environment (t[child], p->next, environment (t, child, inherited));
}

tree editing_query::value (const tree& t, path p, string name, tree fallback) const {
  if (is_nil (p)) return fallback;
  const int child= p->item;
  if (is_atomic (t) || child < 0 || child >= N(t)) return fallback;
  return value (t[child], p->next, name, value (t, child, name, fallback));
}

editing_query editing_query::inside (const tree& t, int child, bool mapped) const {
  bool source= value (t, child, MODE, "") == "src";
  editing_query next= *this;
  int writable= writability (t, child);
  if (mapped && !native_fields (L(t))) {
    const auto& contract= describe (t);
    const int index= contract.layout.index (child, N(t));
    if (index < 0 || index >= int (contract.fields.size ())) {
      if (L(t) != WITH) source= false;
      if (contract.layout_origin == property_authority::inferred)
        writable= WRITABILITY_NORMAL;
    }
    else {
      if (L(t) != WITH && contract.fields[index].environment_origin == property_authority::inferred)
        source= false;
      if (contract.fields[index].writability_origin == property_authority::inferred)
        writable= WRITABILITY_NORMAL;
    }
  }
  if (source) return with_access (access_policy::source);
  if (writable == WRITABILITY_DISABLE) next.input_only= true;
  else if (writable == WRITABILITY_ENABLE) next.input_only= false;
  return next;
}

} // namespace athena::contracts

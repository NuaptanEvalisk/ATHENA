// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once

#include "contract_scope.hpp"
#include "path.hpp"

namespace athena::contracts {

enum class access_policy { normal, hidden, source };

// A read over one owner-prepared context. Recursive navigation copies policy,
// never changes the evaluator or another query's accessibility state.
struct editing_query {
  const contract_scope& scope;
  access_policy access= access_policy::normal;
  bool input_only= false;

  const node_contract& describe (tree_label) const;
  const node_contract& describe (const tree&) const;
  bool contains (string name) const;
  string name (tree_label, bool long_name= false) const;
  bool variable_arity (const tree&) const;
  bool all_fields_editable (tree_label) const;
  bool no_fields_editable (tree_label) const;
  bool child_enforcing (const tree&) const;
  bool parent_enforcing (const tree&) const;
  bool accessible (const tree&, int child) const;
  // Only for one source path admitted by a live display occurrence. This is
  // not a schema-wide permission or an inference result.
  bool mapped_accessible (const tree&, int child) const;
  bool accessible (const tree&, path) const;
  int child_type (const tree&, int child) const;
  string child_name (const tree&, int child, bool long_name= false) const;
  int writability (const tree&, int child) const;
  tree environment (const tree&, int child, tree inherited) const;
  tree value (const tree&, int child, string name, tree fallback) const;
  tree environment (const tree&, path descendant, tree inherited) const;
  tree value (const tree&, path descendant, string name, tree fallback) const;
  editing_query with_access (access_policy policy) const {
    return {scope, policy, input_only};
  }
  editing_query inside (const tree&, int child, bool mapped= false) const;
};

tree field_environment_write (tree, string name, tree value);
tree field_environment_merge (tree, tree overlay);
tree field_environment_read (tree, string name, tree fallback= tree (UNINIT));

} // namespace athena::contracts

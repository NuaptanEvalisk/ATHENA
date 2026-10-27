// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once

#include "env.hpp"
#include <optional>

class native_math_value_scope {
  edit_env environment;
  string name;
  tree previous;
public:
  native_math_value_scope (edit_env env, string key, tree value):
    environment (env), name (key), previous (env->local_begin (key, value)) {}
  ~native_math_value_scope () { environment->local_end (name, previous); }
  const tree& outer_value () const { return previous; }
  native_math_value_scope (const native_math_value_scope&)= delete;
  native_math_value_scope& operator= (const native_math_value_scope&)= delete;
};

// Expected counter failures are evaluator error trees, as in style expansion.
tree advance_native_equation (edit_env);
void validate_native_math_grid_numbering (
  edit_env, const tree& grid, std::optional<bool> block_numbering);

// Layout and semantic counter execution enter exactly the same row context.
// Number/tag rendering belongs to the caller, before it evaluates cell content.
class native_math_row_scope {
  native_math_value_scope row, label, tags;
public:
  tree number;
  tree binding_keys;
  bool show_number;
  native_math_row_scope (edit_env, const tree& source, int index,
                         bool row_numbering, std::optional<bool> block_numbering);
};

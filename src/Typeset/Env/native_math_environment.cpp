// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "native_math_environment.hpp"
#include "Table/table.hpp"
#include "math_tree_v3.hpp"
#include "node_metadata.hpp"
#include <stdexcept>

namespace {
void require_equation_counter (edit_env env) {
  if (!env->provides ("next-equation") || !env->provides ("the-equation"))
    throw std::invalid_argument ("Native equation numbering requires equation counter macros");
}

bool automatic_row_number (const tree& row, bool rows_numbered,
                            std::optional<bool> block_numbering) {
  using namespace athena::math_v3;
  const auto policy= get_row_numbering (row);
  return rows_numbered && !get_row_tag (row) &&
    (policy == row_numbering::numbered ||
     (policy == row_numbering::inherit && block_numbering.value_or (false)));
}
}

tree advance_native_equation (edit_env env) {
  if (!env->provides ("next-equation") || !env->provides ("the-equation"))
    return tree (_ERROR, "Native equation numbering requires equation counter macros");
  if (is_func (env->exec (compound ("next-equation")), _ERROR))
    return tree (_ERROR, "Native equation counter failed");
  const tree number= env->exec (compound ("the-equation"));
  if (is_func (number, _ERROR) || number == "")
    return tree (_ERROR, "Native equation counter produced no number");
  return number;
}

void validate_native_math_grid_numbering (
    edit_env env, const tree& grid, std::optional<bool> block_numbering) {
  using namespace athena::math_v3;
  validate_native_math_grid_layout (grid);
  const bool rows_numbered= native_math_grid_uses_row_numbering (grid);
  // Validate all rows before the first one can advance a counter.
  for (int i=0; i<N(grid); ++i) {
    const auto tag= get_row_tag (grid[i]);
    if (!block_numbering.has_value () &&
        (tag || get_row_numbering (grid[i]) != row_numbering::inherit))
      throw std::invalid_argument ("Row overrides require a direct MathBlock grid");
    if (tag) validate_native_math_row_tag (*tag);
    if (automatic_row_number (grid[i], rows_numbered, block_numbering))
      require_equation_counter (env);
  }
}

native_math_row_scope::native_math_row_scope (
    edit_env env, const tree& source, int index,
    bool row_numbering, std::optional<bool> block_numbering):
  row (env, CELL_ROW_NR, as_string (index)),
  label (env, THE_LABEL, ""), tags (env, "the-tags", tree (TUPLE)),
  number (""), binding_keys (TUPLE), show_number (false) {
  const auto tag= athena::math_v3::get_row_tag (source);
  const bool automatic= automatic_row_number (source, row_numbering, block_numbering);
  number= tag ? *tag : automatic ? advance_native_equation (env) : tree ("");
  if (automatic && is_func (number, _ERROR))
    throw std::invalid_argument (as_charp (number[0]->label));
  show_number= tag.has_value () || automatic;
  // VALUE evaluates its stored expression. Rich row tags must remain quoted
  // until LABEL consumes them; a whole-block label is already an expression.
  env->local_begin (THE_LABEL, !row_numbering && block_numbering.has_value () ?
    label.outer_value () : tree (QUOTE, number));
  const auto id= athena::node::id (source);
  if (row_numbering && !id.empty ()) {
    const tree binding= env->exec (tree (SET_BINDING,
      tree ("athena-node-" * string (id.data (), int (id.size ()))),
      tree (QUOTE, number)));
    if (!is_func (binding, HIDDEN_BINDING))
      throw std::invalid_argument ("Native row UUID binding failed");
    binding_keys= binding[0];
  }
}

/******************************************************************************
* MODULE     : compound_counters.cpp
* DESCRIPTION: Style-defined counter discovery and isolated state projection
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "compound_counters.hpp"
#include "analyze.hpp"
#include "iterator.hpp"
#include "Table/table.hpp"
#include "math_tree_v3.hpp"
#include "node_metadata.hpp"
#include "native_math_environment.hpp"
#include "local_environment.hpp"
#include <map>
#include <optional>
#include <stdexcept>
#include <string>

namespace athena::avd {
namespace {
template<typename Visit>
bool visit_grid (edit_env environment, const tree& grid,
                 std::optional<bool> block_numbering, Visit visit) {
  validate_native_math_grid_numbering (environment, grid, block_numbering);
  const bool row_numbers= native_math_grid_uses_row_numbering (grid);
  native_math_context_scope mode (*environment.operator-> (), native_math_context::math);
  native_math_value_scope format (environment, CELL_FORMAT, tree (TFORMAT));
  for (int i= 0; i < N(grid); ++i) {
    native_math_row_scope row (environment, grid[i], i, row_numbers, block_numbering);
    if (row.show_number) {
      const tree number= row.number;
      native_math_value_scope tag_mode (environment, MODE,
        native_math_row_tag_is_math (number) ? "math" : "text");
      native_math_value_scope shape (environment, FONT_SHAPE, "right");
      native_math_value_scope display (environment, MATH_DISPLAY, "false");
      (void) environment->exec (number);
    }
    if (const auto reached= visit (i)) {
      row.finish (*reached);
      format.finish (*reached);
      mode.finish (*reached);
      return *reached;
    }
  }
  return false;
}

template<typename Visit>
bool visit_cells (edit_env environment, const tree& row, Visit visit) {
  for (int i= 0; i < N(row); ++i) {
    native_math_value_scope column (environment, CELL_COL_NR, as_string (i));
    if (const auto reached= visit (i)) {
      column.finish (*reached);
      return *reached;
    }
  }
  return false;
}

tree execute_row (edit_env environment, const tree& row) {
  tree result (row, N(row));
  visit_cells (environment, row, [&] (int i) -> std::optional<bool> {
    result[i]= environment->exec (row[i]);
    return std::nullopt;
  });
  node::copy_metadata (row, result);
  return result;
}

tree execute_grid (edit_env environment, const tree& grid,
                   std::optional<bool> block_numbering) {
  tree result (grid, N(grid));
  visit_grid (environment, grid, block_numbering, [&] (int i) -> std::optional<bool> {
    result[i]= execute_row (environment, grid[i]);
    return std::nullopt;
  });
  node::copy_metadata (grid, result);
  return result;
}

template<typename Visit>
bool visit_block (edit_env environment, const tree& source, Visit visit) {
  if (!is_func (source, MATH_BLOCK, 1) || !math_v3::valid_native_math (source))
    throw std::invalid_argument ("Invalid native MathBlock during semantic evaluation");
  const bool numbered= std::get<bool> (
    node::get (source)->properties.at ("math:block-numbered").data);
  const bool grid= is_func (source[0], MATH_GRID);
  if (grid) validate_native_math_grid_layout (source[0]);
  const bool row_numbers= grid && native_math_grid_uses_row_numbering (source[0]);
  if (numbered && !row_numbers) {
    const tree number= advance_native_equation (environment);
    if (is_func (number, _ERROR))
      throw std::invalid_argument (as_charp (number[0]->label));
    if (is_func (environment->read ("the-tags"), TUPLE) &&
        is_func (environment->exec (tree (SET_BINDING, number)), _ERROR))
      throw std::invalid_argument ("Native block binding failed");
  }
  native_math_context_scope context (*environment.operator-> (), native_math_context::display);
  const bool reached= visit (row_numbers && numbered);
  context.finish (reached);
  return reached;
}

bool prepare_grid (edit_env environment, const tree& grid, path p,
                   std::optional<bool> block_numbering) {
  return visit_grid (environment, grid, block_numbering, [&] (int i) -> std::optional<bool> {
    if (i != p->item) { (void) execute_row (environment, grid[i]); return std::nullopt; }
    const path target= p->next;
    if (is_atom (target)) {
      if (target->item != 0) (void) execute_row (environment, grid[i]);
      return true;
    }
    return visit_cells (environment, grid[i], [&] (int j) -> std::optional<bool> {
      if (j != target->item) { (void) environment->exec (grid[i][j]); return std::nullopt; }
      return environment->exec_until (grid[i][j], target->next);
    });
  });
}

bool prepare_grid (edit_env environment, const tree& grid, path p, string argument,
                   int level, std::optional<bool> block_numbering) {
  return visit_grid (environment, grid, block_numbering, [&] (int i) -> std::optional<bool> {
    const bool reached= visit_cells (environment, grid[i], [&] (int j) -> std::optional<bool> {
      if (environment->exec_until (grid[i][j], p, argument, level)) return true;
      return std::nullopt;
    });
    return reached ? std::optional<bool> (true) : std::nullopt;
  });
}
} // namespace

tree exec_native_counter_node (edit_env environment, const tree& source) {
  if (is_func (source, MATH_GRID))
    return execute_grid (environment, source, std::nullopt);
  tree result (source, 1);
  visit_block (environment, source, [&] (bool numbering) {
    // Nested grids never inherit the enclosing block's numbering policy.
    result[0]= is_func (source[0], MATH_GRID) ? execute_grid (environment, source[0], numbering) :
                                             environment->exec (source[0]);
    return false;
  });
  node::copy_metadata (source, result);
  return result;
}

bool exec_native_counter_until (edit_env environment, const tree& source, path p) {
  if (is_func (source, MATH_GRID)) return prepare_grid (environment, source, p, std::nullopt);
  if (p->item != 0) return false;
  return visit_block (environment, source, [&] (bool numbering) {
    if (is_func (source[0], MATH_GRID) && !is_atom (p->next))
      return prepare_grid (environment, source[0], p->next, numbering);
    if (is_func (source[0], MATH_GRID) && p->next->item != 0) {
      (void) execute_grid (environment, source[0], numbering);
      return true;
    }
    return environment->exec_until (source[0], p->next);
  });
}

bool exec_native_counter_until (edit_env environment, const tree& source, path p,
                                string argument, int level) {
  if (is_func (source, MATH_GRID))
    return prepare_grid (environment, source, p, argument, level, std::nullopt);
  return visit_block (environment, source, [&] (bool numbering) {
    return is_func (source[0], MATH_GRID) ?
      prepare_grid (environment, source[0], p, argument, level, numbering) :
      environment->exec_until (source[0], p, argument, level);
  });
}

tree evaluate_counter_state (edit_env environment, const tree& source,
                             const tree& incoming,
                             document::xml_format_version version) {
  validate_counter_state (incoming);
  if (version != document::xml_format_version::v1 &&
      version != document::xml_format_version::v2 &&
      version != document::xml_format_version::v3)
    throw std::invalid_argument ("Unknown AVD source XML version");
  const bool previous= environment->compound_counter_execution;
  environment->compound_counter_execution= version == document::xml_format_version::v3;
  try {
    apply_counter_state (environment, counter_variables (environment), incoming);
    (void) environment->exec (source);
    tree result= capture_counter_state (environment, incoming);
    environment->compound_counter_execution= previous;
    return result;
  }
  catch (...) {
    environment->compound_counter_execution= previous;
    throw;
  }
}

hashset<string> counter_variables (edit_env environment) {
  hashmap<string,tree> variables (UNINIT);
  environment->read_env (variables);
  hashset<string> result;
  auto keys= iterate (variables);
  while (keys->busy ()) {
    const string key= keys->next ();
    if (!starts (key, "counter-") || !is_func (variables[key], MACRO, 1)) continue;
    const tree variable= environment->exec (tree (COMPOUND, key));
    if (!is_atomic (variable) || variable == "" || !environment->provides (variable->label))
      throw std::runtime_error ("Cannot resolve a style-defined AVD counter");
    result->insert (variable->label);
  }
  return result;
}

void validate_counter_state (const tree& state) {
  if (L (state) != COLLECTION)
    throw std::invalid_argument ("AVD counter state must be a collection");
  hashset<string> names;
  for (int i= 0; i < N (state); ++i) {
    const tree item= state[i];
    if (!is_func (item, ASSOCIATE, 2) || !is_atomic (item[0]) || item[0] == "")
      throw std::invalid_argument ("Invalid AVD counter state entry");
    if (names->contains (item[0]->label))
      throw std::invalid_argument ("Duplicate AVD counter state entry");
    names->insert (item[0]->label);
  }
}

void apply_counter_state (edit_env environment, hashset<string> variables,
                          const tree& state) {
  for (int i= 0; i < N (state); ++i)
    if (variables->contains (state[i][0]->label))
      environment->write (state[i][0]->label, copy (state[i][1]));
}

tree capture_counter_state (edit_env environment, const tree& inherited) {
  validate_counter_state (inherited);
  // Counters absent from this member's style pass through to later members.
  // Only registered counter values are added; macros, fonts and page settings
  // are never transferred to a different source document.
  std::map<std::string, tree> values;
  for (int i= 0; i < N (inherited); ++i) {
    const string key= inherited[i][0]->label;
    values.emplace (std::string (key.c_str (), N (key)), copy (inherited[i][1]));
  }
  auto keys= iterate (counter_variables (environment));
  while (keys->busy ()) {
    const string key= keys->next ();
    values.insert_or_assign (std::string (key.c_str (), N (key)), copy (environment->read (key)));
  }
  tree state (COLLECTION);
  for (const auto& [name, value]: values)
    state << tree (ASSOCIATE, string (name.data (), int (name.size ())), value);
  return state;
}
} // namespace athena::avd

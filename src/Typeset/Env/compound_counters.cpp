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
#include <map>
#include <optional>
#include <stdexcept>
#include <string>

namespace athena::avd {
namespace {
class local_value {
  edit_env environment_;
  string key_;
  tree previous_;
public:
  local_value (edit_env environment, string key, tree value):
    environment_ (environment), key_ (key),
    previous_ (environment->local_begin (key, value)) {}
  ~local_value () { environment_->local_end (key_, previous_); }
};

void require_equation_counter (edit_env environment) {
  if (!environment->provides ("next-equation") ||
      !environment->provides ("the-equation"))
    throw std::invalid_argument ("Native AVD numbering requires equation counter macros");
}

tree advance_equation (edit_env environment) {
  require_equation_counter (environment);
  if (is_func (environment->exec (compound ("next-equation")), _ERROR))
    throw std::invalid_argument ("Native AVD equation counter failed");
  tree number= environment->exec (compound ("the-equation"));
  if (is_func (number, _ERROR) || number == "")
    throw std::invalid_argument ("Native AVD equation counter produced no number");
  return number;
}

tree execute_grid (edit_env environment, const tree& grid,
                   std::optional<bool> block_numbering) {
  using namespace math_v3;
  validate_native_math_grid_layout (grid);
  const bool row_numbers= native_math_grid_uses_row_numbering (grid);
  // Validate the entire grid before advancing any row's counter.
  for (int i= 0; i < N(grid); ++i) {
    const auto tag= get_row_tag (grid[i]);
    const auto policy= get_row_numbering (grid[i]);
    if (!block_numbering && (tag || policy != row_numbering::inherit))
      throw std::invalid_argument ("Row overrides require a direct MathBlock grid");
    if (tag) validate_native_math_row_tag (*tag);
    if (row_numbers && !tag && (policy == row_numbering::numbered ||
        (policy == row_numbering::inherit && block_numbering.value_or (false))))
      require_equation_counter (environment);
  }
  local_value mode (environment, MODE, "math");
  local_value format (environment, CELL_FORMAT, tree (TFORMAT));
  tree result (grid, N(grid));
  for (int i= 0; i < N(grid); ++i) {
    const tree outer_label= environment->read (THE_LABEL);
    local_value row (environment, CELL_ROW_NR, as_string (i));
    local_value label (environment, THE_LABEL, "");
    local_value tags (environment, "the-tags", tree (TUPLE));
    const auto tag= get_row_tag (grid[i]);
    const auto policy= get_row_numbering (grid[i]);
    const bool automatic= row_numbers && !tag &&
      (policy == row_numbering::numbered ||
       (policy == row_numbering::inherit && block_numbering.value_or (false)));
    tree number= tag ? *tag : automatic ? advance_equation (environment) : tree ("");
    const bool whole_block= !row_numbers && block_numbering.has_value ();
    environment->local_begin (THE_LABEL,
      whole_block ? outer_label : tree (QUOTE, number));
    const auto id= node::id (grid[i]);
    if (row_numbers && !id.empty ()) {
      const tree bound= environment->exec (tree (SET_BINDING,
        tree ("athena-node-" * string (id.data (), int (id.size ()))),
        tree (QUOTE, number)));
      if (!is_func (bound, HIDDEN_BINDING))
        throw std::invalid_argument ("Native AVD row UUID binding failed");
    }
    if (tag || automatic) {
      local_value tag_mode (environment, MODE,
        native_math_row_tag_is_math (number) ? "math" : "text");
      local_value shape (environment, FONT_SHAPE, "right");
      local_value display (environment, MATH_DISPLAY, "false");
      (void) environment->exec (number);
    }
    // No block context is passed through exec: nested grids never inherit it.
    result[i]= environment->exec (grid[i]);
  }
  return result;
}
} // namespace

tree exec_native_counter_node (edit_env environment, const tree& source) {
  if (is_func (source, MATH_GRID))
    return execute_grid (environment, source, std::nullopt);
  if (!is_func (source, MATH_BLOCK, 1) || !math_v3::valid_native_math (source))
    throw std::invalid_argument ("Invalid native MathBlock during AVD counter evaluation");
  const bool numbered= std::get<bool> (
    node::get (source)->properties.at ("math:block-numbered").data);
  const bool grid= is_func (source[0], MATH_GRID);
  if (grid) validate_native_math_grid_layout (source[0]);
  const bool row_numbers= grid && native_math_grid_uses_row_numbering (source[0]);
  if (numbered && !row_numbers) {
    const tree number= advance_equation (environment);
    if (is_func (environment->read ("the-tags"), TUPLE) &&
        is_func (environment->exec (tree (SET_BINDING, number)), _ERROR))
      throw std::invalid_argument ("Native AVD block binding failed");
  }
  local_value mode (environment, MODE, "math");
  local_value display (environment, MATH_DISPLAY, "true");
  tree result (source, 1);
  result[0]= grid ? execute_grid (environment, source[0], row_numbers && numbered) :
                    environment->exec (source[0]);
  return result;
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

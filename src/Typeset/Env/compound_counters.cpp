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
#include <map>
#include <stdexcept>
#include <string>

namespace athena::avd {
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

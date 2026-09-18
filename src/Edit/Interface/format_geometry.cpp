/******************************************************************************
* MODULE     : format_geometry.cpp
* DESCRIPTION: Observer-aware geometry lengths and synchronized editing steps
* COPYRIGHT  : (C) 2010 Joris van der Hoeven, 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "format_geometry.hpp"
#include "analyze.hpp"
#include "convert.hpp"
#include "editor.hpp"
#include "new_view.hpp"
#include "scheme.hpp"
#include "native_interfaces.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <mutex>
#include <string>

namespace {

std::mutex step_mutex;
std::map<std::string, double> steps;
constexpr std::array<double, 16> step_sizes {
  0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 50,
  100, 200, 500};
constexpr std::array<const char*, 6> units {"spc", "cm", "in", "em", "ex", "pt"};

bool arithmetic_length (tree t) {
  return is_compound (t) && (L (t) == PLUS || L (t) == MINUS ||
    L (t) == MINIMUM || L (t) == MAXIMUM);
}

bool length_parts (tree t, double& value, string& unit) {
  if (!is_atomic (t)) return false;
  string text= t->label;
  if (ends (text, "%")) {
    string number= text (0, N (text) - 1);
    if (!is_double (number)) return false;
    value= as_double (number);
    unit= "%";
  }
  else {
    parse_length (text, value, unit);
    if (unit == "" || unit == "error") return false;
  }
  return std::isfinite (value);
}

double length_step (string unit) {
  const std::string key (unit.data (), N (unit));
  {
    std::lock_guard<std::mutex> lock (step_mutex);
    auto found= steps.find (key);
    if (found != steps.end ()) return found->second;
  }
  // Preference callbacks must never run under the shared numeric cache lock.
  string preference= get_preference (unit * " increase", "0.1");
  double value= is_double (preference) ? as_double (preference) : 0.1;
  if (!std::isfinite (value) || value <= 0) value= 0.1;
  std::lock_guard<std::mutex> lock (step_mutex);
  return steps.emplace (key, value).first->second;
}

void change_step (string unit, int direction) {
  (void) length_step (unit);
  double next;
  {
    std::lock_guard<std::mutex> lock (step_mutex);
    double& current= steps.at (std::string (unit.data (), N (unit)));
    auto found= std::find (step_sizes.begin (), step_sizes.end (), current);
    auto index= found - step_sizes.begin ();
    next= found == step_sizes.end () ? 0.1 :
      step_sizes[std::clamp<long long> (index + static_cast<long long> (direction),
                                      0, step_sizes.size () - 1)];
    current= next;
  }
  if (get_preference (unit * " increase", "") != "")
    set_preference (unit * " increase", as_string (next));
  get_current_editor ()->set_message (
    tree (CONCAT, "Current step-size: ", as_string (next), unit), "Change step-size");
}

} // namespace

bool geometry_rich_length (tree t) {
  if (is_atomic (t)) return true;
  if (L (t) != PLUS && L (t) != MINUS) return false;
  if (L (t) == MINUS && (N (t) == 0 || !is_atomic (t[N (t) - 1]))) return false;
  for (int i= 0; i < N (t); ++i)
    if (!geometry_rich_length (t[i])) return false;
  return true;
}

string geometry_rich_length_string (tree t) {
  if (is_atomic (t)) return t->label;
  if (L (t) == MINUS && N (t) == 1) {
    string value= "-" * geometry_rich_length_string (t[0]);
    return starts (value, "--") ? value (2, N (value)) : value;
  }
  if (L (t) != PLUS && L (t) != MINUS) return "";
  string result;
  for (int i= 0; i < N (t); ++i) {
    if (i > 0) result << "+";
    tree child= t[i];
    if (L (t) == MINUS && i == N (t) - 1) child= tree (MINUS, child);
    result << geometry_rich_length_string (child);
  }
  return replace (result, "+-", "-");
}

scheme_tree geometry_parse_rich_length (string value) {
  string split= replace (value, "-", "+-");
  tree parts (PLUS);
  int start= starts (split, "+") ? 1 : 0;
  for (int i= start; i <= N (split); ++i)
    if (i == N (split) || split[i] == '+') {
      parts << split (start, i);
      start= i + 1;
    }
  return tree_to_scheme_tree (N (parts) <= 1 ? tree (value) : parts);
}

tree geometry_length_rightmost (tree value) {
  while (arithmetic_length (value) && N (value) > 0)
    value= value[N (value) - 1];
  return value;
}

bool geometry_lengths_consistent (tree first, tree second) {
  double a, b;
  string ua, ub;
  return length_parts (geometry_length_rightmost (first), a, ua) &&
    length_parts (geometry_length_rightmost (second), b, ub) && ua == ub;
}

void geometry_length_increase (tree value, double amount) {
  if (arithmetic_length (value)) {
    if (N (value) > 0)
      geometry_length_increase (value[N (value) - 1], L (value) == MINUS ? -amount : amount);
    return;
  }
  double number;
  string unit;
  if (!length_parts (value, number, unit) || !std::isfinite (amount)) return;
  double next= number + amount * length_step (unit);
  if (std::isfinite (next)) tree_assign (value, as_string (next) * unit);
}

void geometry_length_scale (tree value, double factor, double step_multiplier) {
  if (!std::isfinite (factor) || !std::isfinite (step_multiplier) || step_multiplier <= 0) return;
  if (arithmetic_length (value)) {
    for (int i= 0; i < N (value); ++i)
      geometry_length_scale (value[i], factor, step_multiplier);
    return;
  }
  double number;
  string unit;
  if (!length_parts (value, number, unit)) return;
  double step= length_step (unit) * step_multiplier;
  if (!std::isfinite (step) || step <= 0) return;
  double next= std::nearbyint (factor * number / step) * step;
  if (std::isfinite (next) && ((number > 0 && next > 0) || (number < 0 && next < 0)))
    tree_assign (value, as_string (next) * unit);
}

void geometry_replace_empty (tree parent, int child, tree replacement) {
  if (is_compound (parent) && child >= 0 && child < N (parent) && is_empty (parent[child]))
    tree_assign (parent[child], replacement);
}

void geometry_length_increase_step (tree value, int direction) {
  double number;
  string unit;
  if (length_parts (geometry_length_rightmost (value), number, unit))
    change_step (unit, direction);
}

string geometry_zero_unit () {
  return "0" * get_preference ("default unit", "spc");
}

void geometry_circulate_unit (int direction) {
  string current= get_preference ("default unit", "spc");
  string next= "spc";
  for (int i= 0; i < static_cast<int> (units.size ()); ++i)
    if (current == units[i]) {
      long long index= (i + static_cast<long long> (direction)) %
                      static_cast<long long> (units.size ());
      if (index < 0) index += static_cast<long long> (units.size ());
      next= units[index];
      break;
    }
  set_preference ("default unit", next);
  get_current_editor ()->set_message (tree (CONCAT, "Default unit: ", next), "Change default unit");
}

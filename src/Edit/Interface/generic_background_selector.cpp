/******************************************************************************
* MODULE     : generic_background_selector.cpp
* DESCRIPTION: Native background selector value conversion and command dispatch
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "generic_editor_commands.hpp"
#include "native_interfaces.hpp"

namespace {

const char* gradient_source= "athena-gradient-vertical";
const char* neutral_source= "$ATHENA_PATH/misc/patterns/neutral-pattern.png";

string effect_value (tree effect, string kind) {
  while (is_compound (effect) && N (effect) > 0) {
    if (is_compound (effect, kind))
      return N (effect) >= 2 && is_atomic (effect[1]) ? effect[1]->label : string ("");
    effect= effect[0];
  }
  return "";
}

object old_value (object options) {
  array<object> values= as_array_object (options);
  return N (values) == 0 ? object (false) : values[0];
}

string pattern_delta_unix (url target) {
  url base= get_master_buffer (get_current_buffer_safe ());
  if (is_rooted (target) && !is_none (base)) target= delta (base, target);
  return as_unix_string (target);
}

string native_pattern_source (string source) {
  string pattern_dir= as_unix_string (url ("$ATHENA_PATH/misc/patterns"));
  if (starts (source, pattern_dir) ||
      starts (source, "$ATHENA_PATH/misc/patterns") ||
      starts (source, "$ATHENA_PATTERN_PATH"))
    return source;

  url source_url= url_unix (source);
  if (!is_rooted (source_url)) {
    url pattern_url= url ("$ATHENA_PATTERN_PATH") * source_url;
    if (exists (pattern_url)) return as_unix_string (pattern_url);
  }

  string relative_source= pattern_delta_unix (source_url);
  if (starts (relative_source, "../")) return source;
  return relative_source;
}

object native_pattern_value (object value) {
  tree pattern= content_to_tree (value);
  if (!is_compound (pattern, "pattern") || N (pattern) == 0 ||
      !is_atomic (pattern[0]))
    return value;
  pattern[0]= native_pattern_source (pattern[0]->label);
  return tree_to_stree (pattern);
}

void open_selector (string mode, object callback, object old, string width) {
  // The existing native dialog dispatches Qt work to the GUI thread. Only
  // value strings cross that boundary; the callback runs back on its caller.
  array<string> values= athena_native_background_selector (
    mode, generic_background_initial (mode, old, width));
  object result= generic_background_result (mode, values);
  if (is_bool (result) && !as_bool (result)) return;
  call (callback, native_pattern_value (result));
}

} // namespace

array<string> generic_background_initial (string mode, object old, string width) {
  tree pattern= compound ("pattern", neutral_source,
                          mode == "picture" ? string ("100%") : width,
                          mode == "picture" ? "100%" : "100@");
  if (mode == "gradient")
    pattern= compound ("pattern", gradient_source, "100%", "100%",
                       compound ("eff-gradient", "0", "black", "white"));
  tree candidate;
  if (is_tree (old)) candidate= as_tree (old);
  else if (is_list (old) && !is_null (old)) {
    array<object> terms= as_array_object (old);
    if (N (terms) >= 4 && terms[0] == symbol_object ("pattern"))
      candidate= content_to_tree (old);
  }
  if (is_compound (candidate, "pattern") && N (candidate) >= 3) pattern= candidate;
  array<string> values;
  for (int i= 0; i < 3; ++i) {
    ASSERT (is_atomic (pattern[i]), "background selector expects string dimensions and source");
    values << pattern[i]->label;
  }
  tree effect= N (pattern) >= 4 ? pattern[3] : tree ("");
  if (mode == "gradient") {
    if (!is_compound (effect, "eff-gradient") || N (effect) < 3)
      effect= compound ("eff-gradient", "0", "black", "white");
    for (int i= 0; i < 3; ++i) {
      ASSERT (is_atomic (effect[i]), "background selector expects string gradient values");
      values << effect[i]->label;
    }
  }
  else values << effect_value (effect, "eff-recolor") << effect_value (effect, "eff-skin");
  return values;
}

object generic_background_result (string mode, array<string> values) {
  if (N (values) < 3 || (mode == "gradient" && N (values) < 6)) return object (false);
  tree pattern= compound ("pattern", mode == "gradient" ? string (gradient_source) : values[0],
                          values[1], values[2]);
  if (mode == "gradient")
    pattern << compound ("eff-gradient", values[3], values[4], values[5]);
  else {
    tree effect ("0");
    if (N (values) >= 4 && values[3] != "") effect= compound ("eff-recolor", effect, values[3]);
    if (N (values) >= 5 && values[4] != "") effect= compound ("eff-skin", effect, values[4]);
    if (is_compound (effect)) pattern << effect;
  }
  return tree_to_stree (pattern);
}

void generic_open_pattern_selector (object callback, string width) {
  open_selector ("pattern", callback, object (false), width);
}

void generic_open_gradient_selector (object callback, object options) {
  open_selector ("gradient", callback, old_value (options), "100%");
}

void generic_open_background_picture_selector (object callback, object options) {
  open_selector ("picture", callback, old_value (options), "100%");
}

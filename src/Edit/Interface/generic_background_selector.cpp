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
#include "editor.hpp"
#include <optional>

namespace {

const char* gradient_source= "athena-gradient-vertical";
const char* neutral_source= "$ATHENA_PATH/misc/patterns/neutral-pattern.png";

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

array<string> background_initial (string mode) {
  array<string> values;
  values << string (mode == "gradient" ? gradient_source : neutral_source)
         << string (mode == "pattern" ? "1cm" : "100%")
         << string (mode == "pattern" ? "100@" : "100%");
  if (mode == "gradient") values << string ("0") << string ("black") << string ("white");
  else values << string ("") << string ("");
  return values;
}

std::optional<tree> background_result (string mode, array<string> values) {
  if (N (values) < 3 || (mode == "gradient" && N (values) < 6)) return {};
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
  return pattern;
}

} // namespace

void generic_document_background_select (string mode) {
  if (mode != "pattern" && mode != "gradient" && mode != "picture") return;
  editor ed= get_current_editor ();
  if (is_nil (ed)) return;
  array<string> values= athena_native_background_selector (
    mode, background_initial (mode));
  const auto result= background_result (mode, values);
  if (!result) return;
  tree pattern= *result;
  pattern[0]= native_pattern_source (pattern[0]->label);
  ed->init_env ("bg-color", pattern);
}

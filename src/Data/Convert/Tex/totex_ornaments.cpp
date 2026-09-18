/******************************************************************************
* MODULE     : totex_ornaments.cpp
* DESCRIPTION: Native ornament environment support for LaTeX export
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "totex_internal.hpp"

#include <vector>

namespace {

using namespace latex_export_internal;

enum OrnamentValueKind {
  ORNAMENT_LENGTH,
  ORNAMENT_COLOR,
  ORNAMENT_SHAPE
};

struct OrnamentRule {
  const char* env_key;
  const char* option1;
  const char* option2;
  OrnamentValueKind kind;
};

const OrnamentRule ornament_rules[]= {
  {"padding-above", "skipabove", nullptr, ORNAMENT_LENGTH},
  {"padding-below", "skipbelow", nullptr, ORNAMENT_LENGTH},
  {"overlined-sep", "innertopmargin", nullptr, ORNAMENT_LENGTH},
  {"underlined-sep", "innerbottommargin", nullptr, ORNAMENT_LENGTH},
  {"leftlined-sep", "innerleftmargin", nullptr, ORNAMENT_LENGTH},
  {"rightlined-sep", "innerrightmargin", nullptr, ORNAMENT_LENGTH},
  {"framed-hsep", "innerleftmargin", "innerrightmargin", ORNAMENT_LENGTH},
  {"framed-vsep", "innertopmargin", "innerbottommargin", ORNAMENT_LENGTH},
  {"ornament-vpadding", "innertopmargin", "innerbottommargin", ORNAMENT_LENGTH},
  {"ornament-hpadding", "innerleftmargin", "innerrightmargin", ORNAMENT_LENGTH},
  {"ornament-color", "backgroundcolor", nullptr, ORNAMENT_COLOR},
  {"ornament-shape", "roundcorner", nullptr, ORNAMENT_SHAPE}
};

const OrnamentRule*
find_rule (string key) {
  for (const auto& rule: ornament_rules)
    if (key == rule.env_key) return &rule;
  return nullptr;
}

string
ornament_value (const OrnamentRule& rule, object value) {
  if (rule.kind == ORNAMENT_LENGTH) return latex_tmtex_decode_length (value);
  if (!is_string (value)) return "";
  string text= as_string (value);
  if (rule.kind == ORNAMENT_SHAPE)
    return text == "rounded" ? "1.7ex" : "0pt";
  scheme_tree color= latex_export_decode_color (text, false);
  return string_atom (color) ? atom_text (color) : "";
}

void
append_option (string& output, string key, string value) {
  if (value == "") return;
  if (output != "") output << ",";
  output << key << "=" << value;
}

string
ornament_options () {
  object keys_object= latex_export_env_keys ();
  if (!is_list (keys_object)) return "";
  array<object> keys= as_array_object (keys_object);
  string output;
  for (int i=0; i<N(keys); ++i) {
    if (!is_string (keys[i])) continue;
    string key= as_string (keys[i]);
    const OrnamentRule* rule= find_rule (key);
    if (rule == nullptr) continue;
    object value= latex_export_env_get (key);
    if (is_bool (value) && !as_bool (value)) continue;
    string decoded= ornament_value (*rule, value);
    append_option (output, rule->option1, decoded);
    if (rule->option2 != nullptr) append_option (output, rule->option2, decoded);
  }
  return output;
}

scheme_tree
ornament_output (string key, scheme_tree args) {
  if (!stree_list (args) || N(args) == 0) return stree_string ("");
  scheme_tree begin= stree_apply ("!begin");
  begin << stree_string ("tm" * key);
  string options= ornament_options ();
  if (options != "") {
    scheme_tree option= stree_apply ("!option");
    option << stree_string (options);
    begin << option;
  }
  scheme_tree output (TUPLE);
  output << begin << tmtex_convert (args[0]);
  return output;
}

} // namespace

scheme_tree
latex_export_ornament (string key, scheme_tree args) {
  return ornament_output (key, args);
}

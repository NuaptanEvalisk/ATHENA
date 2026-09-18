/******************************************************************************
* MODULE     : totex_internal.hpp
* DESCRIPTION: Shared internal helpers for native LaTeX export modules
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#pragma once

#include <charconv>
#include <system_error>

#include "convert.hpp"
#include "scheme.hpp"
#include "Scheme/Scheme/glue.hpp"
#include "Scheme/Scheme/object.hpp"

namespace latex_export_internal {

struct LatexRegistryInfo {
  bool present= false;
  int arity= -1;
  bool option= false;
  bool needs= false;
  scheme_tree body= tree ("#f");
  scheme_tree preamble= tree ("#f");
};

LatexRegistryInfo registry_command_info (string name);
LatexRegistryInfo registry_environment_info (string name);
array<string> registry_command_needs (string name);
bool registry_symbol_known (string name);
bool registry_operator_known (string name);
bool registry_latex_name_known (string name);
array<string> registry_tag_names ();
array<string> registry_symbol_names ();
scheme_tree filter_known_style_macros (scheme_tree t);

url save_source_url ();
url save_target_url ();

// Publisher-specific export policy is entirely native and kept behind this
// seam so the generic driver/core do not depend on individual class families.
bool publisher_transform_style (scheme_tree style, scheme_tree& result);
void publisher_initialize (string source_style, scheme_tree body);
string publisher_source_style ();
bool publisher_prepare (scheme_tree body, scheme_tree document,
                         scheme_tree& result);
bool publisher_postprocess (scheme_tree value, scheme_tree& result);
bool publisher_postprocess_body (scheme_tree value, scheme_tree& result);
bool publisher_hook (string name, const array<scheme_tree>& args,
                      scheme_tree& result);
bool publisher_dispatch (string key, scheme_tree args, scheme_tree& result);

inline bool
stree_list (scheme_tree t) {
  return is_tuple (t);
}

inline bool
string_atom (scheme_tree t) {
  return is_atomic (t) && is_quoted (t->label);
}

inline string
atom_text (scheme_tree t) {
  if (!is_atomic (t)) return "";
  return is_quoted (t->label) ? scm_unquote (t->label) : t->label;
}

inline bool
empty_string_stree (scheme_tree t) {
  return string_atom (t) && atom_text (t) == "";
}

inline bool
contains_text (string s, string needle) {
  return search_forwards (needle, 0, s) >= 0;
}

inline bool
one_of (string s, const char* const* values, int count) {
  for (int i=0; i<count; ++i)
    if (s == values[i]) return true;
  return false;
}

inline string
scheme_inexact_string (double value) {
  char buf[128];
  auto result= std::to_chars (buf, buf + sizeof (buf), value,
                              std::chars_format::general);
  string out= result.ec == std::errc ()
                ? string (buf, (int) (result.ptr - buf))
                : as_string (value);
  if (!contains_text (out, ".") && !contains_text (out, "e") &&
      !contains_text (out, "E"))
    out << ".0";
  return out;
}

inline string
html_xcolor (string s) {
  if (starts (s, "#")) return html_xcolor (s (1, N(s)));
  if (N(s) == 3)
    return upcase_all (string (s[0]) * string (s[0]) *
                       string (s[1]) * string (s[1]) *
                       string (s[2]) * string (s[2]));
  if (N(s) == 4) return html_xcolor (s (0, 3));
  if (N(s) == 8) return upcase_all (s (0, 6));
  return upcase_all (s);
}

inline bool
head_is (scheme_tree t, string head) {
  return stree_list (t) && N(t) > 0 && is_atomic (t[0]) &&
         t[0]->label == head;
}

inline bool
func_is (scheme_tree t, string head, int arity= -1) {
  return head_is (t, head) && (arity < 0 || N(t) == arity + 1);
}

inline scheme_tree
stree_string (string value) {
  return tree (scm_quote (value));
}

inline scheme_tree
stree_apply (string head) {
  scheme_tree r (TUPLE);
  r << tree (head);
  return r;
}

inline object
stree_object (scheme_tree t) {
  return tmscm_to_object (scheme_tree_to_tmscm (t));
}

inline scheme_tree
tmtex_convert (scheme_tree t) {
  return latex_export_convert_node (t);
}

inline bool
math_mode () {
  object mode= latex_export_env_get ("mode");
  return is_string (mode) && as_string (mode) == "math";
}

} // namespace latex_export_internal

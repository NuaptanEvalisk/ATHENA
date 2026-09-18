/******************************************************************************
* MODULE     : totex_misc.cpp
* DESCRIPTION: Miscellaneous native handlers for LaTeX export
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "totex_internal.hpp"
#include "Data/Tree/tree_correct.hpp"
#include "System/Language/dictionary.hpp"

namespace {

using namespace latex_export_internal;

scheme_tree
source_apply (string head, scheme_tree args) {
  scheme_tree out= stree_apply (head);
  if (stree_list (args))
    for (int i=0; i<N(args); ++i) out << args[i];
  return out;
}

scheme_tree
around_output (string key, scheme_tree args) {
  scheme_tree source= source_apply (key, args);
  tree lowered= downgrade_brackets (scheme_tree_to_tree (source), false, false);
  scheme_tree st= tree_to_scheme_tree (lowered);
  scheme_tree concat_args (TUPLE);
  if (stree_list (st)) {
    for (int i=1; i<N(st); ++i) concat_args << st[i];
  }
  else concat_args << st;
  return latex_export_core_dispatch ("concat", concat_args);
}

scheme_tree
preserve_object_output (scheme_tree source) {
  scheme_tree records (TUPLE);
  records << stree_string (latex_export_athena_data_object (source));
  return latex_export_athena_data_inline (records);
}

scheme_tree
specific_output (scheme_tree args) {
  scheme_tree source= source_apply ("specific", args);
  if (!stree_list (args) || N(args) < 2 || !string_atom (args[0]))
    return preserve_object_output (source);

  string kind= atom_text (args[0]);
  if (kind == "latex") return stree_string (latex_export_tt (args[1]));
  if (kind == "image") return latex_export_render_image (args[1]);
  if (kind == "printer") return tmtex_convert (args[1]);
  if (kind == "odd" || kind == "even") {
    scheme_tree out= stree_apply ("ifthispageodd");
    if (kind == "odd") out << tmtex_convert (args[1]) << stree_string ("");
    else out << stree_string ("") << tmtex_convert (args[1]);
    return out;
  }
  return preserve_object_output (source);
}

scheme_tree
translate_output (scheme_tree args) {
  if (!stree_list (args) || N(args) < 3 ||
      !string_atom (args[1]) || !string_atom (args[2]))
    return stree_string ("");
  string rendered= translate (scheme_tree_to_tree (args[0]),
                              atom_text (args[1]), atom_text (args[2]));
  return tmtex_convert (stree_string (rendered));
}

scheme_tree
localize_output (scheme_tree args) {
  if (!stree_list (args) || N(args) == 0) return stree_string ("");
  string language= "english";
  object languages= latex_export_languages ();
  if (is_list (languages)) {
    array<object> values= as_array_object (languages);
    if (N(values) > 0 && is_string (values[N(values)-1]))
      language= as_string (values[N(values)-1]);
  }
  string rendered= translate (scheme_tree_to_tree (args[0]), "english", language);
  return tmtex_convert (stree_string (rendered));
}

} // namespace

scheme_tree
latex_export_misc_handler (string key, scheme_tree args) {
  if (key == "around" || key == "around*" || key == "big-around")
    return around_output (key, args);
  if (key == "specific") return specific_output (args);
  if (key == "translate") return translate_output (args);
  if (key == "localize") return localize_output (args);
  return scheme_tree ();
}

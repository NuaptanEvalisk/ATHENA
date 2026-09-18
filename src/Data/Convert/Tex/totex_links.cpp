/******************************************************************************
* MODULE     : totex_links.cpp
* DESCRIPTION: Native link and ATHENA navigation support for LaTeX export
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "totex_internal.hpp"

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
preserve_source (scheme_tree source) {
  scheme_tree records (TUPLE);
  records << stree_string (latex_export_athena_data_object (source));
  return latex_export_athena_data_inline (records);
}

scheme_tree
escape_hyperref_url (scheme_tree value) {
  if (string_atom (value)) {
    string out= atom_text (value);
    out= replace (out, "\\", "\\\\");
    out= replace (out, "#", "\\#");
    out= replace (out, "_", "\\_");
    return stree_string (out);
  }
  if (!stree_list (value)) return value;
  scheme_tree out (TUPLE);
  for (int i=0; i<N(value); ++i) out << escape_hyperref_url (value[i]);
  return out;
}

scheme_tree
hyperref_url (scheme_tree value) {
  return stree_string (latex_export_tt (escape_hyperref_url (value)));
}

scheme_tree
hlink_output (scheme_tree args) {
  if (!stree_list (args) || N(args) != 2) return tree ("#f");
  scheme_tree source= source_apply ("hlink", args);
  scheme_tree display= tmtex_convert (args[0]);
  scheme_tree destination= args[1];

  if (string_atom (destination)) {
    string target= atom_text (destination);
    if (starts (target, "tmfs://wikilink/")) {
      scheme_tree underline_args (TUPLE);
      underline_args << args[0];
      scheme_tree fallback= tmtex_convert (source_apply ("underline", underline_args));
      return latex_export_athena_data_wrap (source, fallback);
    }
    if (starts (target, "#")) {
      scheme_tree option= stree_apply ("!option");
      option << stree_string (target (1, N(target)));
      scheme_tree out= stree_apply ("hyperref");
      out << option << display;
      return out;
    }
  }

  scheme_tree out= stree_apply ("href");
  out << hyperref_url (destination) << display;
  return out;
}

bool
empty_cardlink_body (scheme_tree body) {
  return string_atom (body) && atom_text (body) == "";
}

scheme_tree
cardlink_default_body (scheme_tree destination) {
  return as_scheme_tree (
    call ("latex-native-cardlink-default-body", stree_object (destination)));
}

scheme_tree
cardlink_output (scheme_tree args) {
  if (!stree_list (args) || N(args) != 2) return tree ("#f");
  scheme_tree source= source_apply ("cardlink", args);
  scheme_tree display= empty_cardlink_body (args[0])
                        ? cardlink_default_body (args[1]) : args[0];
  scheme_tree link_args (TUPLE);
  link_args << display << args[1];
  return latex_export_athena_data_wrap (
    source, tmtex_convert (source_apply ("hlink", link_args)));
}

scheme_tree
transclude_output (scheme_tree args) {
  if (!stree_list (args) || N(args) != 4) return tree ("#f");
  scheme_tree source= source_apply ("transclude", args);
  scheme_tree resolved= as_scheme_tree (
    call ("latex-native-transclude-fallback", stree_object (args)));
  return latex_export_athena_data_wrap (source, tmtex_convert (resolved));
}

scheme_tree
material_visible (scheme_tree rendered) {
  if (func_is (rendered, "hlink", 2) && string_atom (rendered[2]) &&
      starts (atom_text (rendered[2]), "tmfs://material/"))
    return tmtex_convert (rendered[1]);
  return tmtex_convert (rendered);
}

scheme_tree
material_citation_output (scheme_tree args) {
  scheme_tree source= source_apply ("material-citation", args);
  if (!stree_list (args) || N(args) != 2) return preserve_source (source);
  return latex_export_athena_data_wrap (source, material_visible (args[1]));
}

scheme_tree
referenced_materials_output (scheme_tree args) {
  scheme_tree source= source_apply ("referenced-materials", args);
  if (!stree_list (args) || N(args) != 3) return preserve_source (source);
  return latex_export_athena_data_wrap (source, tmtex_convert (args[2]));
}

} // namespace

scheme_tree
latex_export_link_dispatch (string key, scheme_tree args) {
  if (key == "hlink") return hlink_output (args);
  if (key == "cardlink") return cardlink_output (args);
  if (key == "transclude") return transclude_output (args);
  if (key == "material-citation") return material_citation_output (args);
  if (key == "referenced-materials") return referenced_materials_output (args);
  return tree ("#f");
}

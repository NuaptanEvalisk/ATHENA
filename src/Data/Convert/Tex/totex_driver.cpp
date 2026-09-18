/******************************************************************************
* MODULE     : totex_driver.cpp
* DESCRIPTION: Native top-level orchestration for LaTeX export
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "totex_internal.hpp"
#include "sys_utils.hpp"

namespace {

using namespace latex_export_internal;

string
option_string (object options, string key) {
  tmscm cur= object_to_tmscm (options);
  while (tmscm_is_pair (cur)) {
    tmscm entry= tmscm_car (cur);
    if (tmscm_is_pair (entry)) {
      object k= tmscm_to_object (tmscm_car (entry));
      if (is_string (k) && as_string (k) == key) {
        tmscm rest= tmscm_cdr (entry);
        object direct= tmscm_to_object (rest);
        if (is_string (direct)) return as_string (direct);
        if (tmscm_is_pair (rest)) {
          object first= tmscm_to_object (tmscm_car (rest));
          if (is_string (first)) return as_string (first);
        }
        return "";
      }
    }
    cur= tmscm_cdr (cur);
  }
  return "";
}

void
initialize_native (object options) {
  latex_export_context_reset ();
  latex_export_set_mathjax (false);
  latex_export_set_portable (
    option_string (options, "texmacs->latex:portable") == "on");

  url target= save_target_url ();
  if (suffix (target) == "tex") {
    url root= unglue (target, 4);
    string suf= suffix (root);
    if (suf != "") root= unglue (root, N(suf) + 1);
    latex_export_set_image_root (root, as_unix_string (tail (root)));
  }
  else latex_export_set_image_root (url_unix ("image"), "image");

  latex_export_set_replace_style (
    option_string (options, "texmacs->latex:replace-style") == "on");
  latex_export_set_use_macros (
    option_string (options, "texmacs->latex:use-macros") == "on");

  if (option_string (options, "texmacs->latex:mathjax") == "on") {
    latex_export_env_set ("mode", object ("math"));
    latex_export_set_mathjax (true);
  }

  string charset= option_string (options, "texmacs->latex:encoding");
  if (latex_export_cjk_document () || charset == "" || charset == "ascii")
    charset= "utf-8";
  if (charset == "utf-8") latex_export_set_encoding (false, false, true);
  else if (charset == "cork") latex_export_set_encoding (true, false, false);
  else if (charset == "ascii") latex_export_set_encoding (false, true, false);
}

bool
false_value (scheme_tree t) {
  return is_atomic (t) && !is_quoted (t->label) && t->label == "#f";
}

bool
tmfile_entry (scheme_tree t) {
  return stree_list (t) && N(t) > 1 && is_atomic (t[0]);
}

scheme_tree
tmfile_extract_native (scheme_tree doc, string key) {
  if (!func_is (doc, "document")) return tree ("#f");
  for (int i=1; i<N(doc); ++i) {
    if (!tmfile_entry (doc[i])) return tree ("#f");
    if (doc[i][0]->label == key) return doc[i][1];
  }
  return tree ("#f");
}

bool
tmfile_native (scheme_tree doc) {
  return !false_value (tmfile_extract_native (doc, "TeXmacs")) &&
         !false_value (tmfile_extract_native (doc, "body"));
}

scheme_tree
style_list_native (scheme_tree style) {
  scheme_tree out (TUPLE);
  if (false_value (style)) {
    out << stree_string ("article");
    return out;
  }
  if (string_atom (style)) {
    out << style;
    return out;
  }
  if (func_is (style, "tuple")) {
    for (int i=1; i<N(style); ++i) out << style[i];
    if (N(out) == 0) out << stree_string ("article");
    return out;
  }
  out << stree_string ("article");
  return out;
}

bool
language_style_name (string s) {
  static const char* names[]= {
    "british", "bulgarian", "chinese", "croatian", "czech", "danish",
    "dutch", "english", "esperanto", "finnish", "french", "german",
    "greek", "hungarian", "italian", "japanese", "korean", "polish",
    "portuguese", "romanian", "russian", "slovak", "slovene", "spanish",
    "swedish", "taiwanese", "ukrainian"
  };
  for (unsigned i=0; i<sizeof (names) / sizeof (names[0]); ++i)
    if (s == names[i]) return true;
  return false;
}

string
initial_language (scheme_tree init) {
  if (!func_is (init, "collection")) return "";
  for (int i=1; i<N(init); ++i)
    if (func_is (init[i], "associate", 2) && string_atom (init[i][1]) &&
        atom_text (init[i][1]) == "language" && string_atom (init[i][2]))
      return atom_text (init[i][2]);
  return "";
}

string
tmfile_language_native (scheme_tree styles, scheme_tree init) {
  string explicit_language= initial_language (init);
  if (explicit_language != "") return explicit_language;
  if (stree_list (styles))
    for (int i=0; i<N(styles); ++i)
      if (string_atom (styles[i]) && language_style_name (atom_text (styles[i])))
        return atom_text (styles[i]);
  return "english";
}

scheme_tree
make_file_input (scheme_tree body, scheme_tree styles, string language,
                 scheme_tree init, scheme_tree attachments,
                 scheme_tree style_meta) {
  scheme_tree doc= stree_apply ("!file");
  doc << body << styles << stree_string (language) << init << attachments
      << stree_string (as_string (get_texmacs_path ())) << style_meta << init;
  return doc;
}

scheme_tree
publisher_prepare (scheme_tree body, scheme_tree doc) {
  scheme_tree native;
  if (latex_export_internal::publisher_prepare (body, doc, native)) return native;
  return doc;
}

scheme_tree
publisher_postprocess (scheme_tree result) {
  scheme_tree native;
  if (latex_export_internal::publisher_postprocess (result, native))
    return native;
  return result;
}

string
transformed_style_class (scheme_tree transformed) {
  if (string_atom (transformed)) return atom_text (transformed);
  if (stree_list (transformed) && N(transformed) > 0 &&
      string_atom (transformed[N(transformed)-1]))
    return atom_text (transformed[N(transformed)-1]);
  return "article";
}

scheme_tree
convert_tmfile_native (scheme_tree value, object options) {
  scheme_tree body= latex_export_discard_experimental_warning (
    tmfile_extract_native (value, "body"));
  scheme_tree style_meta= tmfile_extract_native (value, "style");
  scheme_tree styles= style_list_native (style_meta);
  scheme_tree init= tmfile_extract_native (value, "initial");
  scheme_tree attachments= tmfile_extract_native (value, "attachments");
  string language= tmfile_language_native (styles, init);

  string source_style= (N(styles) > 0 && string_atom (styles[0]))
    ? atom_text (styles[0]) : string ("article");
  latex_export_set_latex_texmacs_style (source_style);
  latex_export_internal::publisher_initialize (source_style, body);

  scheme_tree transformed= N(styles) > 0
    ? latex_export_transform_style (styles[0]) : tree ("#f");
  string main_style= transformed_style_class (transformed);

  latex_export_set_latex_style (main_style);
  latex_export_set_latex_packages (null_object ());
  latex_export_set_latex_extra_packages (null_object ());
  latex_export_recompute_dependencies ();

  latex_export_document_reset (language);

  scheme_tree doc= make_file_input (body, styles, language, init, attachments,
                                    style_meta);
  doc= publisher_prepare (body, doc);
  scheme_tree result= latex_export_convert_tree (doc, options);
  result= publisher_postprocess (result);
  latex_export_set_latex_texmacs_style ("generic");
  return result;
}

} // namespace

scheme_tree
latex_export_convert_top (scheme_tree value, object options,
                          url source, url target) {
  latex_export_set_save_urls (source, target);
  latex_export_enter ();
  scheme_tree result= tmfile_native (value)
    ? convert_tmfile_native (value, options)
    : latex_export_convert_tree (value, options);
  latex_export_leave ();
  return result;
}

void
latex_export_initialize (object options) {
  initialize_native (options);
}


/******************************************************************************
* MODULE     : convert.hpp
* DESCRIPTION: various conversion routines
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef CONVERT_H
#define CONVERT_H
#include "analyze.hpp"
#include "hashmap.hpp"
typedef tree scheme_tree;
class url;
class object;

/*** Miscellaneous ***/
bool   is_snippet (tree doc);
// Transitional old-format writer boundary; removed when XML saves are enabled.
tree   legacy_serialization_document (tree doc);
void   set_file_focus (url u);
url    get_file_focus ();

/*** Generic ***/
string suffix_to_format (string suffix);
string format_to_suffix (string format);
bool   format_exists (string format);
string get_format (string s, string suffix);
tree   generic_to_tree (string s, string format);
string tree_to_generic (tree doc, string format);
array<string> compute_keys (string s, string fm);
array<string> compute_keys (tree t, string fm);
array<string> compute_keys (url u);
scheme_tree compute_index (string s, string fm);
scheme_tree compute_index (tree t, string fm);
scheme_tree compute_index (url u);

/*** Texmacs ***/
tree   texmacs_to_tree (string s);
tree   texmacs_document_to_tree (string s);
string tree_to_texmacs (tree t);
tree   extract (tree doc, string attr);
tree   extract_document (tree doc);
tree   change_doc_attr (tree doc, string attr, tree val);
tree   remove_doc_attr (tree doc, string attr);
tree   substitute (tree t, tree which, tree by);
tree   nonumber_to_eqnumber (tree t);
tree   eqnumber_to_nonumber (tree t);
string search_metadata (tree doc, string kind);

/*** Scheme ***/
string scheme_tree_to_string (scheme_tree t);
string scheme_tree_to_block (scheme_tree t);
scheme_tree tree_to_scheme_tree (tree t);
string tree_to_scheme (tree t);
string tree_to_scheme_document (tree t);
scheme_tree string_to_scheme_tree (string s);
scheme_tree block_to_scheme_tree  (string s);
tree   scheme_tree_to_tree (scheme_tree t);
tree   scheme_tree_to_tree (scheme_tree t, string version);
tree   scheme_to_tree (string s);
tree   scheme_document_to_tree (string s);

/*** Verbatim ***/
string tree_to_verbatim (tree t, bool wrap= false, string enc= "default");
tree   verbatim_to_tree (string s, bool wrap= false, string enc= "default");
tree   verbatim_document_to_tree (string s, bool w= false, string e= "default");

/*** Latex ***/
tree   parse_latex (string s, bool change= false, bool as_pic= false);
tree   parse_latex_document (string s, bool change= false, bool as_pic= false);
tree   latex_to_tree (tree t, bool not_document= false);
tree   latex_document_to_tree (string s, bool as_pic= false, bool lite_mode= false);
tree   latex_class_document_to_tree (string s);
tree   latex_native_document_to_texmacs (string s, bool as_pic);
string latex_verbarg_to_string (tree t);
string get_latex_style (tree t);
string string_arg (tree t, bool u= false);
array<tree> tokenize_concat (tree t, array<tree> a, bool keep= false);
bool   is_verbatim (tree t);
int    latex_search_forwards (string s, int pos, string in);
int    latex_search_forwards (string s, string in);
tree   tracked_latex_to_texmacs (string s, bool as_pic);
string conservative_texmacs_to_latex (tree doc, object opts);
string tracked_texmacs_to_latex (tree doc, object opts);
string serialize_latex (scheme_tree t);
bool   latex_stree_multiline (scheme_tree t);
string latex_native_paper_opts (string cmd);
string latex_native_paper_type (string cmd);
string latex_native_type (string cmd);
int    latex_native_arity (string cmd);
array<string> latex_native_tags ();
void   latex_compat_set_style (string style);
void   latex_compat_set_packages (object packages);
void   latex_compat_set_extra (object packages);
void   latex_compat_add_extra (string package);
void   latex_compat_set_virtual_packages (object packages);
bool   latex_compat_has_style (string style);
bool   latex_compat_has_package (string package);
bool   latex_compat_has_texmacs_style (string style);
bool   latex_compat_has_texmacs_package (string package);
bool   latex_compat_depends (string package);
scheme_tree latex_compat_tmtex_env_patch (tree t, object legacy_options);
void   latex_export_context_reset ();
void   latex_export_document_reset (string language);
bool   latex_export_cjk_document ();
void   latex_export_set_encoding (bool catcodes, bool ascii, bool unicode);
bool   latex_export_use_catcodes ();
bool   latex_export_use_ascii ();
bool   latex_export_use_unicode ();
void   latex_export_set_use_macros (bool value);
bool   latex_export_use_macros ();
object latex_export_collect_user_defs (scheme_tree t);
void   latex_export_set_latex_language (string language);
string latex_export_latex_language ();
void   latex_export_set_latex_style (string style);
string latex_export_latex_style ();
void   latex_export_set_latex_packages (object packages);
object latex_export_latex_packages ();
void   latex_export_set_latex_extra_packages (object packages);
object latex_export_latex_extra_packages ();
bool   latex_export_add_latex_extra_package (string package);
void   latex_export_set_latex_virtual_packages (object packages);
object latex_export_latex_virtual_packages ();
void   latex_export_set_latex_all_packages (object packages);
object latex_export_latex_all_packages ();
void   latex_export_set_latex_texmacs_style (string style);
string latex_export_latex_texmacs_style ();
void   latex_export_set_latex_texmacs_packages (object packages);
object latex_export_latex_texmacs_packages ();
void   latex_export_set_latex_dependencies (object dependencies);
object latex_export_latex_dependencies ();
void   latex_export_enter ();
void   latex_export_leave ();
void   latex_export_reset_placeholders ();
scheme_tree latex_export_placeholder (scheme_tree st);
scheme_tree latex_export_placeholder_ref (string marker);
bool   latex_export_experimental_warning_name (scheme_tree x);
bool   latex_export_experimental_warning (scheme_tree t);
scheme_tree latex_export_discard_experimental_warning (scheme_tree t);
string latex_export_athena_data_record (string cmd, scheme_tree values);
scheme_tree latex_export_athena_data_inline (scheme_tree records);
string latex_export_athena_data_object (scheme_tree st);
bool   latex_export_latex_empty (scheme_tree t);
scheme_tree latex_export_athena_data_wrap_records (scheme_tree st,
                                                   scheme_tree records,
                                                   scheme_tree fallback);
scheme_tree latex_export_athena_data_wrap (scheme_tree st,
                                           scheme_tree fallback);
object latex_export_languages ();
void   latex_export_add_language (string language);
object latex_export_colors ();
void   latex_export_add_color (string color);
object latex_export_colormaps ();
void   latex_export_add_colormap (string colormap);
bool   latex_export_mathjax ();
void   latex_export_set_mathjax (bool value);
bool   latex_export_replace_style ();
void   latex_export_set_replace_style (bool value);
void   latex_export_dynamic_set (string name, string kind);
object latex_export_dynamic_get (string name);
void   latex_export_set_portable (bool value);
bool   latex_export_portable ();
bool   latex_export_portable_image_used (string name);
object latex_export_portable_image_copy_get (string source);
void   latex_export_portable_image_record (string source, string copied);
void   latex_export_set_image_root (url root, string root_string);
void   latex_export_set_save_urls (url source, url target);
url    latex_export_image_root_url ();
string latex_export_image_root_string ();
object latex_export_env_list (string var);
object latex_export_env_get (string var);
object latex_export_env_get_previous (string var);
void   latex_export_env_set (string var, object value);
void   latex_export_env_reset (string var);
void   latex_export_env_assign (string var, object value);
object latex_export_env_keys ();
int    latex_export_next_serial ();
int    latex_export_ref_count_get ();
void   latex_export_ref_count_set (int value);
int    latex_export_next_auto_produce ();
int    latex_export_next_auto_consume ();
int    latex_export_next_athena_data_serial ();
bool   latex_export_first_appendix ();
scheme_tree latex_export_filter_preamble (scheme_tree t);
scheme_tree latex_export_filter_body (scheme_tree t);
scheme_tree latex_export_filter_duplicates (scheme_tree t);
scheme_tree latex_export_clean_body (scheme_tree t);
scheme_tree latex_export_filter_style_macros (scheme_tree t,
                                              scheme_tree protected_names);
scheme_tree latex_export_apply_init (scheme_tree body, scheme_tree init);
scheme_tree latex_tmtex_math_concat_spaces (scheme_tree t);
scheme_tree latex_tmtex_rewrite_no_break (scheme_tree t);
scheme_tree latex_tmtex_pre_scripts (scheme_tree t);
scheme_tree latex_tmtex_pre_brackets_recurse (scheme_tree t);
string latex_tmtex_large_decode (scheme_tree t);
string latex_tmtex_large_decode_text (scheme_tree t);
string latex_tmtex_big_decode (scheme_tree t);
scheme_tree latex_tmtex_decode_long_arrow (scheme_tree t);
scheme_tree latex_export_core_dispatch (string key, scheme_tree args);
scheme_tree latex_export_convert_node (scheme_tree value);
scheme_tree latex_export_generic_function (string key, scheme_tree args);
scheme_tree latex_export_string (string value);
scheme_tree latex_export_verb_string (string value);
string latex_export_tt (scheme_tree value);
scheme_tree latex_export_transform_style (scheme_tree style);
scheme_tree latex_export_filter_styles (scheme_tree styles);
scheme_tree latex_export_convert_charset (scheme_tree value);
scheme_tree latex_export_convert_tree (scheme_tree value, object options);
scheme_tree latex_export_convert_top (scheme_tree value, object options,
                                      url source, url target);
void latex_export_initialize (object options);
scheme_tree latex_export_mathjax_pre (scheme_tree value);
scheme_tree latex_export_mathjax (scheme_tree value);
scheme_tree latex_export_preamble_page_type (object entries);
string latex_export_html_xcolor (string color);
string latex_export_colors_defs (object colors);
object latex_export_packages_dependencies (object packages);
object latex_export_packages_simplify (object packages);
void latex_export_recompute_dependencies ();
string latex_export_use_package_command (scheme_tree document, object colors,
                                         object colormaps);
string latex_export_as_use_package (object packages);
string latex_export_catcode_defs (scheme_tree document);
scheme_tree latex_export_expand_macros (scheme_tree value);
scheme_tree latex_export_macro_defs (scheme_tree document);
string latex_export_serialize_preamble (scheme_tree value);
array<string> latex_export_preamble_data (scheme_tree text, scheme_tree style,
                                          scheme_tree language,
                                          scheme_tree init,
                                          scheme_tree colors,
                                          scheme_tree colormaps);
void latex_export_init_mode_stats (scheme_tree value);
scheme_tree latex_export_preprocess_preamble_list (scheme_tree values);
string latex_export_var_name (string name);
string latex_tmtex_decode_length (object value);
bool latex_tmtex_px_length (object value);
scheme_tree latex_export_float_sub (bool wide, string position, scheme_tree body);
scheme_tree latex_export_table_apply (string key, scheme_tree args,
                                      scheme_tree table);
scheme_tree latex_export_image (scheme_tree args);
scheme_tree latex_export_render_image (scheme_tree source);
scheme_tree latex_export_commutative_diagram (scheme_tree args);
scheme_tree latex_export_ornament (string key, scheme_tree args);
scheme_tree latex_export_link_dispatch (string key, scheme_tree args);
scheme_tree latex_export_misc_handler (string key, scheme_tree args);
scheme_tree latex_export_env_patch (scheme_tree st, bool expand_user_macros);
bool latex_export_known_tmtex_name (string name);
scheme_tree latex_export_metadata_make_inline (scheme_tree t);
scheme_tree latex_export_metadata_inline (scheme_tree t);
scheme_tree latex_export_metadata_field (string key, scheme_tree t);
scheme_tree latex_export_metadata_select (string name, scheme_tree values);
scheme_tree latex_export_metadata_transform (string name, scheme_tree values);
scheme_tree latex_export_metadata_remove_line_feeds (scheme_tree t);
scheme_tree latex_export_metadata_replace_documents (scheme_tree t);
bool latex_export_metadata_contains_tags (scheme_tree t, scheme_tree tags);
bool latex_export_metadata_contains_stree (scheme_tree t, scheme_tree needle);
scheme_tree latex_export_metadata_make_references (scheme_tree values,
                                                   string tag, bool author,
                                                   bool global_counter);
scheme_tree latex_export_metadata_default (string key, scheme_tree args);
scheme_tree latex_export_with (scheme_tree args);
scheme_tree latex_export_decode_color (string color, bool force_html);
scheme_tree latex_tex_concat (scheme_tree t);
scheme_tree latex_tex_concat_strings (scheme_tree t);
scheme_tree latex_tmtex_concat_sep (scheme_tree t);
scheme_tree latex_tmtex_concat_Sep (scheme_tree t);
tree   conservative_latex_to_texmacs (string s, bool as_pic);
int    number_latex_errors (url log);
int    number_latex_pages (url log);
tree   postprocess_metadata (tree t);

/*** Xml / Html / Mathml ***/
string old_tm_to_xml_cdata (string s);
object tm_to_xml_cdata (string s);
string old_xml_cdata_to_tm (string s);
string tm_to_xml_name (string s);
string xml_name_to_tm (string s);
string xml_unspace (string s, bool first, bool last);

tree   parse_xml (string s);
tree   parse_plain_html (string s);
tree   parse_html (string s);
tree   clean_html (tree t);
tree   upgrade_mathml (tree t);
string serialize_xml (scheme_tree t);
tree   mathml_to_tree (scheme_tree t);
tree   retrieve_mathjax (int id);

tree   find_first_element_by_name (tree t, string name);
string get_attr_from_element (tree t, string name, string default_value);
int    parse_xml_length (string length);

/*** Post corrections ***/
bool   seems_buggy_html_paste (string s);
string correct_buggy_html_paste (string s);
bool   seems_buggy_paste (string s);
string correct_buggy_paste (string s);
tree   default_with_simplify (tree t);

/*** Obsidian ***/
extern bool aofm_insert_build_warning;
std::string aofm_normalize_chatgpt_markdown (const std::string& source);
tree aofm_markdown_to_tree (string source);
tree aofm_chatgpt_to_tree (string source);
bool aofm_convert_tree (string file_path, tree& document,
                        bool materialize_anchor_literals=true);
bool aofm_import_vault (string source_dir, string destination_dir,
                        bool ignore_nonempty= false, int parallelism= 0,
                        string model_vault= "");
void aofm_debug_dump (const std::string& file_path);

/*** Tree compression ***/
tree compress_tree (tree t);
tree decompress_tree (tree t);
string compress_html (tree t, int mode= 0);
tree decompress_html (string s, int mode= 0);

#endif // defined CONVERT_H

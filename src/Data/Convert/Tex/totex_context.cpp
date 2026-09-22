/******************************************************************************
* MODULE     : totex_context.cpp
* DESCRIPTION: Native mutable state for LaTeX export
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "convert.hpp"
#include "totex_internal.hpp"
#include "converter.hpp"
#include "analyze.hpp"
#include "colors.hpp"
#include "base64.hpp"
#include "hashmap.hpp"
#include "iterator.hpp"
#include "scheme.hpp"
#include "tree_traverse.hpp"
#include "file.hpp"
#include "new_view.hpp"
#include "Scheme/Scheme/glue.hpp"
#include "Scheme/Scheme/object.hpp"

namespace {

using latex_export_internal::empty_string_stree;
using latex_export_internal::one_of;

struct LatexExportContext {
  hashmap<string,array<object>> env;
  hashmap<string,string> dynamic;
  hashmap<string,int> command_text_uses;
  hashmap<string,int> command_math_uses;
  array<object> languages;
  array<object> colors;
  array<object> colormaps;
  hashmap<string,string> portable_image_copies;
  hashmap<string,bool> portable_image_used;
  hashmap<string,scheme_tree> placeholders;
  url image_root_url;
  string image_root_string;
  url save_source_url;
  url save_target_url;
  string latex_language;
  string latex_style;
  array<object> latex_packages;
  array<object> latex_extra_packages;
  array<object> latex_virtual_packages;
  array<object> latex_all_packages;
  string latex_texmacs_style;
  array<object> latex_texmacs_packages;
  array<object> latex_dependencies;
  int serial;
  int ref_count;
  int auto_produce;
  int auto_consume;
  int athena_data_serial;
  int placeholder_serial;
  int export_depth;
  bool appendices;
  bool mathjax;
  bool replace_style;
  bool portable;
  bool cjk_document;
  bool use_catcodes;
  bool use_unicode;
  bool use_ascii;
  bool use_macros;

  LatexExportContext ():
    env (array<object> ()), dynamic (""), command_text_uses (0),
    command_math_uses (0), portable_image_copies (""),
    portable_image_used (false), placeholders (tree ("#f")),
    latex_language ("english"), latex_style ("generic"),
    latex_texmacs_style ("generic"), serial (0),
    ref_count (1), auto_produce (0), auto_consume (0), athena_data_serial (0),
    placeholder_serial (0), export_depth (0), appendices (false),
    mathjax (false), replace_style (true), portable (false),
    cjk_document (false), use_catcodes (false), use_unicode (false),
    use_ascii (false), use_macros (false) {
    latex_dependencies << object ("generic");
  }

  void reset () {
    env= hashmap<string,array<object>> (array<object> ());
    dynamic= hashmap<string,string> ("");
    command_text_uses= hashmap<string,int> (0);
    command_math_uses= hashmap<string,int> (0);
    portable_image_copies= hashmap<string,string> ("");
    portable_image_used= hashmap<string,bool> (false);
    serial= 0;
    ref_count= 1;
    auto_produce= 0;
    auto_consume= 0;
    athena_data_serial= 0;
    appendices= false;
    mathjax= false;
    portable= false;
  }
};

// Export runs on the owning Scheme/BufferActor thread.  The native top-level
// driver resets this thread-local context for each conversion; thread-local
// storage also prevents simultaneous BufferActors from sharing exporter state.
thread_local LatexExportContext latex_export_context;

array<object>
env_values (string var) {
  return latex_export_context.env[var];
}

object
optional_object (const array<object>& values, int index) {
  if (index < 0 || index >= N(values)) return object (false);
  return values[index];
}

void
prepend_unique_string (array<object>& values, string value) {
  for (int i=0; i<N(values); ++i)
    if (is_string (values[i]) && as_string (values[i]) == value) return;
  array<object> next;
  next << object (value);
  for (int i=0; i<N(values); ++i) next << values[i];
  values= next;
}

bool
contains_string (const array<object>& values, string value) {
  for (int i=0; i<N(values); ++i)
    if (is_string (values[i]) && as_string (values[i]) == value) return true;
  return false;
}

bool
stree_list (scheme_tree t) {
  return is_tuple (t);
}

bool
string_atom (scheme_tree t) {
  return is_atomic (t) && is_quoted (t->label);
}

string
atom_text (scheme_tree t) {
  if (!is_atomic (t)) return "";
  return is_quoted (t->label) ? scm_unquote (t->label) : t->label;
}

bool
head_is (scheme_tree t, string head) {
  return stree_list (t) && N(t) > 0 && is_atomic (t[0]) &&
         t[0]->label == head;
}

bool
func_is (scheme_tree t, string head, int arity= -1) {
  return head_is (t, head) && (arity < 0 || N(t) == arity + 1);
}

scheme_tree
stree_list_new () {
  return scheme_tree (TUPLE);
}

scheme_tree
stree_string (string value) {
  return tree (scm_quote (value));
}

scheme_tree
stree_apply (string head) {
  scheme_tree r (TUPLE);
  r << tree (head);
  return r;
}

object
stree_object (scheme_tree t) {
  return tmscm_to_object (scheme_tree_to_tmscm (t));
}

scheme_tree
tmtex_convert (scheme_tree t) {
  return latex_export_convert_node (t);
}

scheme_tree
tmtex_convert_list (scheme_tree l) {
  scheme_tree r (TUPLE);
  if (!stree_list (l)) return r;
  for (int i=0; i<N(l); ++i) r << tmtex_convert (l[i]);
  return r;
}

scheme_tree
source_apply (string head, scheme_tree args) {
  scheme_tree r= stree_apply (head);
  if (stree_list (args))
    for (int i=0; i<N(args); ++i) r << args[i];
  return r;
}

string tmtex_var_name_native (string var);
scheme_tree with_one_native (string var, string val, scheme_tree arg);
scheme_tree decode_color_native (string color, bool force_html);

bool
contains_text (string s, string needle) {
  return search_forwards (needle, 0, s) >= 0;
}

bool
experimental_warning_name (scheme_tree x) {
  return is_atomic (x) && atom_text (x) == "experimental-build-warning";
}

bool
tree_contains_string (scheme_tree t, string what) {
  if (string_atom (t)) return contains_text (atom_text (t), what);
  if (!stree_list (t)) return false;
  for (int i=0; i<N(t); ++i)
    if (tree_contains_string (t[i], what)) return true;
  return false;
}

bool
experimental_warning (scheme_tree t) {
  return head_is (t, "experimental-build-warning") ||
         (head_is (t, "compound") && N(t) > 1 && experimental_warning_name (t[1])) ||
         (head_is (t, "error") &&
          tree_contains_string (t, "compound experimental-build-warning")) ||
         (head_is (t, "note") &&
          tree_contains_string (t, "This document was typeset") &&
          tree_contains_string (t, "currently experimental") &&
          tree_contains_string (t, "GitHub Issues"));
}

string
athena_data_record (string cmd, const array<string>& vals) {
  string r= "ATHENA-DATA cmd=\"" * cmd * "\" val=(";
  for (int i=0; i<N(vals); ++i) {
    if (i) r << ", ";
    r << "\"" << vals[i] << "\"";
  }
  return r * ")";
}

string
athena_data_object (scheme_tree st) {
  if (experimental_warning (st)) st= stree_apply ("experimental-build-warning");
  string payload= replace (
    encode_base64 (tree_to_texmacs (scheme_tree_to_tree (st))), "\n", "");
  array<string> vals;
  vals << as_string (N(payload)) << payload;
  return athena_data_record ("object", vals);
}

scheme_tree
athena_data_inline (scheme_tree records) {
  scheme_tree r= stree_apply ("!athena-data-inline");
  r << records;
  return r;
}

bool
latex_empty (scheme_tree x) {
  if (empty_string_stree (x)) return true;
  if (head_is (x, "!concat") || head_is (x, "!document")) {
    for (int i=1; i<N(x); ++i)
      if (!latex_empty (x[i])) return false;
    return true;
  }
  return false;
}

scheme_tree
trim_latex_empties (scheme_tree body) {
  if (!stree_list (body)) return body;
  int first= 0;
  while (first < N(body) && latex_empty (body[first])) ++first;
  int last= N(body);
  while (last > first && latex_empty (body[last-1])) --last;
  scheme_tree r (TUPLE);
  for (int i=first; i<last; ++i) r << body[i];
  return r;
}

scheme_tree
clean_athena_fallback (scheme_tree fallback) {
  if (!head_is (fallback, "!document") && !head_is (fallback, "!concat"))
    return fallback;
  scheme_tree body (TUPLE);
  for (int i=1; i<N(fallback); ++i) body << fallback[i];
  body= trim_latex_empties (body);
  if (N(body) == 0) return stree_string ("");
  scheme_tree r= stree_apply (fallback[0]->label);
  for (int i=0; i<N(body); ++i) r << body[i];
  return r;
}

scheme_tree
placeholder_object (scheme_tree st) {
  string marker= "ATHENAEXPORTOBJECT" *
                 as_string (latex_export_context.placeholder_serial++) * "X";
  scheme_tree records (TUPLE);
  records << stree_string (athena_data_object (st));
  latex_export_context.placeholders (marker)= athena_data_inline (records);
  return stree_string (marker);
}

bool
drop_marker (scheme_tree x) {
  return func_is (x, "!athena-drop", 0);
}

scheme_tree
discard_experimental_warning (scheme_tree t) {
  if (experimental_warning (t)) {
    scheme_tree st= head_is (t, "experimental-build-warning") ?
                    t : stree_apply ("experimental-build-warning");
    return placeholder_object (st);
  }
  if (head_is (t, "!athena-data-inline") || head_is (t, "athena-preserved-object"))
    return t;
  if (!stree_list (t)) return t;
  scheme_tree r (TUPLE);
  for (int i=0; i<N(t); ++i) {
    scheme_tree child= discard_experimental_warning (t[i]);
    if (!drop_marker (child)) r << child;
  }
  return r;
}

scheme_tree
athena_data_wrap_with_records (scheme_tree st, scheme_tree records,
                               scheme_tree fallback) {
  string id= as_string (++latex_export_context.athena_data_serial);
  fallback= clean_athena_fallback (fallback);

  scheme_tree begin_records (TUPLE);
  begin_records << stree_string (athena_data_object (st));
  if (stree_list (records))
    for (int i=0; i<N(records); ++i) begin_records << records[i];
  array<string> begin_id; begin_id << id;
  begin_records << stree_string (athena_data_record ("skip_begin", begin_id));

  scheme_tree end_records (TUPLE);
  array<string> end_id; end_id << id;
  end_records << stree_string (athena_data_record ("skip_end", end_id));

  scheme_tree r= stree_apply ("!concat");
  r << athena_data_inline (begin_records) << fallback << athena_data_inline (end_records);
  return r;
}

bool
export_math_mode () {
  array<object> values= env_values ("mode");
  return N(values) > 0 && is_string (values[0]) && as_string (values[0]) == "math";
}

scheme_tree
preserve_object (scheme_tree st) {
  scheme_tree records (TUPLE);
  records << stree_string (athena_data_object (st));
  return athena_data_inline (records);
}

scheme_tree
preserve_lossy (scheme_tree st, scheme_tree fallback) {
  return latex_empty (fallback) ? preserve_object (st)
                                : athena_data_wrap_with_records (
                                    st, scheme_tree (TUPLE), fallback);
}

scheme_tree
converted_child (scheme_tree args, int index) {
  if (!stree_list (args) || index < 0 || index >= N(args))
    return stree_string ("");
  return tmtex_convert (args[index]);
}

scheme_tree
tex_apply_fixed (string command) {
  scheme_tree application= stree_apply (command);
  if (export_math_mode ()) return application;
  scheme_tree group= stree_apply ("!group");
  group << application;
  return group;
}

string
decode_length_string (string s) {
  if (ends (s, "fn"))  return replace (s, "fn", "em");
  if (ends (s, "tab")) return replace (s, "tab", "em");
  if (ends (s, "spc")) return replace (s, "spc", "em");
  if (ends (s, "sep")) return replace (s, "sep", "ex");
  if (ends (s, "par")) return replace (s, "par", "\\columnwidth");
  if (ends (s, "pag")) return replace (s, "pag", "\\textheight");

  string parsed= s;
  while (starts (parsed, "--")) parsed= parsed (2, N(parsed));
  int i=0;
  while (i < N(parsed) &&
         (is_digit (parsed[i]) || parsed[i] == '-' || parsed[i] == '.')) ++i;
  if (i > 0 && parsed (i, N(parsed)) == "px" && is_double (parsed (0, i)))
    return as_string (0.75 * as_double (parsed (0, i))) * "pt";
  return s;
}

bool
px_length_string (string s) {
  while (starts (s, "--")) s= s (2, N(s));
  int i=0;
  while (i < N(s) && (is_digit (s[i]) || s[i] == '-' || s[i] == '.')) ++i;
  return i > 0 && s (i, N(s)) == "px" && is_double (s (0, i));
}

scheme_tree
tex_apply_one (string command, scheme_tree arg) {
  scheme_tree application= stree_apply (command);
  application << arg;
  if (export_math_mode ()) return application;
  scheme_tree group= stree_apply ("!group");
  group << application;
  return group;
}

scheme_tree
hspace_command (scheme_tree args) {
  if (!stree_list (args) || N(args) == 0) return stree_string ("");
  scheme_tree raw= N(args) == 1 ? args[0] : args[1];
  string s= string_atom (raw) ? atom_text (raw) : "";
  if (s == "0.5fn" || s == "0.5em") return stree_apply ("enspace");
  if (s == "1fn" || s == "1em") return stree_apply ("quad");
  if (s == "2fn" || s == "2em") return stree_apply ("qquad");
  if (s == "0.2spc") return stree_apply (",");
  if (!export_math_mode ()) {
    if (s == "0.4spc" || s == "0.6spc" || s == "0.16667em")
      return stree_apply (",");
    return tex_apply_one ("hspace", stree_string (decode_length_string (s)));
  }
  if (s == "0.4spc") return stree_apply (":");
  if (s == "0.6spc") return stree_apply (";");
  if (s == "-0.6spc" || s == "-0.4spc" || s == "-0.2spc") {
    int count= s == "-0.6spc" ? 3 : (s == "-0.4spc" ? 2 : 1);
    scheme_tree out= stree_apply ("!concat");
    for (int i=0; i<count; ++i) out << stree_apply ("!");
    return out;
  }
  return tex_apply_one ("hspace", stree_string (decode_length_string (s)));
}

scheme_tree
vspace_command (scheme_tree args) {
  if (!stree_list (args) || N(args) == 0) return stree_string ("");
  scheme_tree raw= N(args) == 1 ? args[0] : args[1];
  string s= string_atom (raw) ? atom_text (raw) : "";
  if (s == "0.5fn") return tex_apply_fixed ("smallskip");
  if (s == "1fn") return tex_apply_fixed ("medskip");
  if (s == "2fn") return tex_apply_fixed ("bigskip");
  return tex_apply_one ("vspace", stree_string (decode_length_string (s)));
}

bool
sectional_command (string command) {
  static const char* names[]= {
    "part", "chapter", "appendix", "section", "subsection", "subsubsection",
    "paragraph", "subparagraph", "part*", "chapter*", "appendix*", "section*",
    "subsection*", "subsubsection*", "paragraph*", "subparagraph*"
  };
  return one_of (command, names, sizeof (names) / sizeof (names[0]));
}

scheme_tree
tex_apply_converted (string command, scheme_tree converted_args) {
  scheme_tree application= stree_apply (command);
  if (stree_list (converted_args))
    for (int i=0; i<N(converted_args); ++i) application << converted_args[i];
  if (export_math_mode () || sectional_command (command)) return application;
  scheme_tree group= stree_apply ("!group");
  group << application;
  return group;
}

scheme_tree
tex_math_apply_one (string command, scheme_tree arg) {
  scheme_tree application= stree_apply (command);
  application << arg;
  if (export_math_mode ()) return application;
  scheme_tree ensure= stree_apply ("ensuremath");
  ensure << application;
  return ensure;
}


scheme_tree
escape_verbatim_tree_native (scheme_tree value, bool backslashes, bool braces) {
  if (string_atom (value)) {
    string s= atom_text (value);
    if (backslashes) s= replace (s, "\\", "\\textbackslash ");
    if (braces) {
      s= replace (s, "{", "\\{");
      s= replace (s, "}", "\\}");
    }
    return stree_string (s);
  }
  if (!stree_list (value)) return value;
  scheme_tree out (TUPLE);
  for (int i=0; i<N(value); ++i)
    out << escape_verbatim_tree_native (value[i], backslashes, braces);
  return out;
}

scheme_tree
verbatim_output_native (scheme_tree args, bool starred) {
  if (!stree_list (args) || N(args) == 0) return stree_string ("");
  scheme_tree value= args[0];
  if (head_is (value, "document")) {
    scheme_tree source= starred ? value
      : escape_verbatim_tree_native (value, true, true);
    string text= latex_export_tt (source);
    if (starred && starts (text, "#")) text= "\\" * text;
    scheme_tree out= stree_apply (starred ? "!verbatim*" : "!verbatim");
    out << stree_string (text);
    return out;
  }
  scheme_tree out= stree_apply ("tmverbatim");
  out << tmtex_convert (value);
  return out;
}

scheme_tree
code_inline_output_native (string key, scheme_tree args) {
  scheme_tree option= stree_apply ("!option");
  option << stree_string (key);
  scheme_tree out= stree_apply ("tmcodeinline");
  out << option << converted_child (args, 0);
  return out;
}

scheme_tree
code_block_output_native (string key, scheme_tree args) {
  scheme_tree escaped (TUPLE);
  if (stree_list (args))
    for (int i=0; i<N(args); ++i)
      escaped << escape_verbatim_tree_native (args[i], true, true);

  string language= key;
  int dash= search_forwards ("-", 0, language);
  if (dash >= 0) language= language (0, dash);

  scheme_tree begin= stree_apply ("!begin*");
  begin << stree_string ("tmcode");
  if (language != "verbatim" && language != "code") {
    scheme_tree option= stree_apply ("!option");
    option << stree_string (language);
    begin << option;
  }
  scheme_tree out (TUPLE);
  out << begin << verbatim_output_native (escaped, true);
  return out;
}

scheme_tree
mixed_source_output_native (scheme_tree args) {
  if (!stree_list (args) || N(args) < 2) return stree_string ("");
  scheme_tree value= args[1];
  if (func_is (value, "text", 1)) value= value[1];

  array<object> previous= env_values ("mode");
  array<object> scoped;
  scoped << object ("text");
  for (int i=0; i<N(previous); ++i) scoped << previous[i];
  latex_export_context.env ("mode")= scoped;
  string text= latex_export_tt (value);
  latex_export_context.env ("mode")= previous;

  if (search_forwards ("tikzpicture", 0, text) >= 0)
    latex_export_add_latex_extra_package ("tikz");
  scheme_tree verb= stree_apply ("!verbatim*");
  verb << stree_string (text);
  scheme_tree out= stree_apply ("!unindent");
  out << verb;
  return out;
}

scheme_tree
convert_concat (scheme_tree args) {
  if (!stree_list (args)) return stree_string ("");
  if (N(args) > 50) {
    int split= N(args) / 2;
    scheme_tree left= stree_apply ("concat");
    scheme_tree right= stree_apply ("concat");
    for (int i=0; i<split; ++i) left << args[i];
    for (int i=split; i<N(args); ++i) right << args[i];
    scheme_tree halves (TUPLE);
    halves << left << right;
    return convert_concat (halves);
  }
  if (export_math_mode ()) {
    scheme_tree normalized= latex_tmtex_pre_brackets_recurse (latex_tmtex_pre_scripts (args));
    return latex_tex_concat (latex_tmtex_math_concat_spaces (tmtex_convert_list (normalized)));
  }
  return latex_tex_concat (tmtex_convert_list (latex_tmtex_rewrite_no_break (args)));
}

scheme_tree
convert_surround (scheme_tree args) {
  if (!stree_list (args) || N(args) < 3) return stree_string ("");
  scheme_tree converted= tmtex_convert_list (args);
  scheme_tree x= converted[0];
  scheme_tree z= converted[1];
  scheme_tree y= converted[2];
  if (!head_is (y, "!document")) {
    scheme_tree parts (TUPLE);
    parts << x << y << z;
    return latex_tex_concat (parts);
  }

  scheme_tree body (TUPLE);
  scheme_tree first_parts (TUPLE);
  first_parts << x;
  if (N(y) > 1) first_parts << y[1];
  body << latex_tex_concat (first_parts);
  for (int i=2; i<N(y); ++i) body << y[i];
  if (N(body) > 0) {
    scheme_tree last_parts (TUPLE);
    last_parts << body[N(body)-1] << z;
    scheme_tree rebuilt (TUPLE);
    for (int i=0; i<N(body)-1; ++i) rebuilt << body[i];
    rebuilt << latex_tex_concat (last_parts);
    body= rebuilt;
  }
  scheme_tree out= stree_apply ("!document");
  for (int i=0; i<N(body); ++i) out << body[i];
  return out;
}

scheme_tree
replace_float_equations (scheme_tree t) {
  if (!stree_list (t) || N(t) == 0) return t;
  if (head_is (t, "equation") || head_is (t, "equation*")) {
    scheme_tree out= stree_apply ("math");
    if (N(t) == 2 && func_is (t[1], "document", 1)) out << t[1][1];
    else for (int i=1; i<N(t); ++i) out << t[i];
    return out;
  }
  scheme_tree out (TUPLE);
  for (int i=0; i<N(t); ++i) out << replace_float_equations (t[i]);
  return out;
}

scheme_tree
replace_float_documents (scheme_tree t) {
  if (!stree_list (t) || N(t) == 0) return t;
  if (head_is (t, "document")) {
    scheme_tree out= stree_apply ("para");
    for (int i=1; i<N(t); ++i) out << replace_float_documents (t[i]);
    return out;
  }
  scheme_tree out (TUPLE);
  for (int i=0; i<N(t); ++i) out << replace_float_documents (t[i]);
  return out;
}

scheme_tree
float_single_paragraph (scheme_tree t) {
  return replace_float_documents (replace_float_equations (t));
}

bool
float_figure (scheme_tree t) {
  return func_is (t, "small-figure", 2) || func_is (t, "big-figure", 2);
}

bool
float_table (scheme_tree t) {
  return func_is (t, "small-table", 2) || func_is (t, "big-table", 2);
}

string
float_size (scheme_tree t) {
  return head_is (t, "small-table") || head_is (t, "small-figure") ?
         "small" : "big";
}

scheme_tree
float_make (bool wide, string size, string type, string position,
            scheme_tree content, scheme_tree caption_source) {
  string pos= replace (position, "f", "");
  string type_star= wide ? type * "*" : type;
  scheme_tree body= tmtex_convert (content);
  scheme_tree caption= tmtex_convert (float_single_paragraph (caption_source));

  scheme_tree caption_cmd= stree_apply ("caption");
  caption_cmd << caption;
  scheme_tree paragraph= stree_apply ("!paragraph");
  if (size == "big" && type == "figure") paragraph << stree_apply ("centering");
  paragraph << body << caption_cmd;

  if (size == "big" && (type == "figure" || type == "table")) {
    scheme_tree begin= stree_apply ("!begin");
    begin << stree_string (pos == "" ? type : type_star);
    if (pos != "") {
      scheme_tree option= stree_apply ("!option");
      option << stree_string (pos);
      begin << option;
    }
    scheme_tree out (TUPLE);
    out << begin << paragraph;
    return out;
  }

  scheme_tree out= stree_apply ("tmfloat");
  out << stree_string (pos) << stree_string (size) << stree_string (type_star)
      << body << caption;
  return out;
}

scheme_tree
float_sub (bool wide, string position, scheme_tree body) {
  string pos= replace (position, "f", "");
  if (func_is (body, "document", 1)) return float_sub (wide, pos, body[1]);
  if (float_figure (body))
    return float_make (wide, float_size (body), "figure", pos, body[1], body[2]);
  if (float_table (body))
    return float_make (wide, float_size (body), "table", pos, body[1], body[2]);
  return float_make (wide, "big", "figure", pos, body, stree_string (""));
}

scheme_tree
with_convert_native (scheme_tree args) {
  if (!stree_list (args) || N(args) == 0) return stree_string ("");
  if (N(args) == 1) return tmtex_convert (args[0]);

  string var= string_atom (args[0]) ? atom_text (args[0]) : "";
  string val= string_atom (args[1]) ? atom_text (args[1]) : "";
  scheme_tree rest (TUPLE);
  for (int i=2; i<N(args); ++i) rest << args[i];

  array<object> previous= env_values (var);
  array<object> scoped;
  scoped << object (val);
  for (int i=0; i<N(previous); ++i) scoped << previous[i];
  latex_export_context.env (var)= scoped;

  scheme_tree inner= with_convert_native (rest);
  scheme_tree result= with_one_native (var, val, inner);
  latex_export_context.env (var)= previous;
  return result;
}

scheme_tree
convert_in_mode (scheme_tree value, string mode) {
  array<object> previous= env_values ("mode");
  array<object> scoped;
  scoped << object (mode);
  for (int i=0; i<N(previous); ++i) scoped << previous[i];
  latex_export_context.env ("mode")= scoped;
  scheme_tree result= tmtex_convert (value);
  latex_export_context.env ("mode")= previous;
  return result;
}

scheme_tree
post_process_math_text_native (string command, scheme_tree arg) {
  if (string_atom (arg) && is_alpha (atom_text (arg))) {
    scheme_tree out= stree_apply (command);
    out << arg;
    return out;
  }
  string text_command= command;
  if      (command == "mathrm" || command == "tmop") text_command= "textrm";
  else if (command == "mathbf") text_command= "textbf";
  else if (command == "mathsf") text_command= "textsf";
  else if (command == "mathit") text_command= "textit";
  else if (command == "mathsl") text_command= "textsl";
  else if (command == "mathtt") text_command= "texttt";
  scheme_tree out= stree_apply (text_command);
  out << arg;
  return out;
}

scheme_tree
textual_wrapper_output (string key, scheme_tree args) {
  scheme_tree arg= N(args) > 0 ? convert_in_mode (args[0], "text")
                               : stree_string ("");
  if (key == "text") {
    scheme_tree out= stree_apply ("text");
    out << arg;
    return out;
  }
  string command= "mathrm";
  if      (key == "math-ss") command= "mathsf";
  else if (key == "math-tt") command= "mathtt";
  else if (key == "math-bf") command= "mathbf";
  else if (key == "math-sl") command= "mathsl";
  else if (key == "math-it") command= "mathit";
  return post_process_math_text_native (command, arg);
}

scheme_tree
math_output_native (scheme_tree args) {
  if (!stree_list (args) || N(args) == 0) return stree_string ("");
  scheme_tree value= args[0];
  if (head_is (value, "equation") || head_is (value, "equation*") ||
      head_is (value, "eqnarray") || head_is (value, "eqnarray*"))
    return tmtex_convert (value);

  if (!head_is (value, "document")) {
    scheme_tree scoped (TUPLE);
    scoped << stree_string ("mode") << stree_string ("math") << value;
    return with_convert_native (scoped);
  }

  if (func_is (value, "document", 1)) {
    scheme_tree one (TUPLE);
    one << value[1];
    return math_output_native (one);
  }

  scheme_tree out= stree_apply ("!document");
  for (int i=1; i<N(value); ++i) {
    scheme_tree one (TUPLE);
    one << value[i];
    out << math_output_native (one);
  }
  return out;
}

bool
sectional_key (string key) {
  static const char* names[]= {
    "section", "subsection", "subsubsection", "paragraph", "subparagraph",
    "part", "chapter"
  };
  return one_of (key, names, sizeof (names) / sizeof (names[0]));
}

bool
find_first_label_native (scheme_tree value, scheme_tree& label) {
  if (head_is (value, "label")) {
    label= value;
    return true;
  }
  if (!stree_list (value)) return false;
  for (int i=0; i<N(value); ++i)
    if (find_first_label_native (value[i], label)) return true;
  return false;
}

scheme_tree
remove_labels_native (scheme_tree value) {
  if (head_is (value, "label")) return stree_string ("");
  if (!stree_list (value)) return value;
  scheme_tree out (TUPLE);
  for (int i=0; i<N(value); ++i) out << remove_labels_native (value[i]);
  return out;
}

scheme_tree
sectional_output_native (string key, scheme_tree args) {
  if (!stree_list (args) || N(args) == 0) return stree_string ("");
  scheme_tree label;
  bool has_label= find_first_label_native (args[0], label);
  scheme_tree title= has_label ? remove_labels_native (args[0]) : args[0];
  scheme_tree section= stree_apply (key);
  section << tmtex_convert (title);
  if (!has_label) return section;
  scheme_tree out= stree_apply ("!concat");
  out << section << tmtex_convert (label);
  return out;
}

bool
book_style_native () {
  return latex_export_context.latex_style == "book" ||
         latex_export_context.latex_style == "svmono";
}

scheme_tree
appendix_output_native (string key, scheme_tree args) {
  string command= book_style_native () ? "chapter" : "section";
  if (key == "appendix*") command << "*";
  scheme_tree heading= stree_apply (command);
  heading << converted_child (args, 0);
  if (!latex_export_first_appendix ()) return heading;
  scheme_tree out= stree_apply ("!concat");
  out << stree_apply ("appendix") << heading;
  return out;
}

bool
enunciation_key (string key) {
  static const char* names[]= {
    "theorem", "proposition", "lemma", "corollary", "proof", "axiom",
    "definition", "notation", "conjecture", "remark", "note", "example",
    "convention", "warning", "acknowledgments", "exercise", "problem",
    "question", "solution", "answer", "quote-env", "quotation", "verse",
    "theorem*", "proposition*", "lemma*", "corollary*", "axiom*",
    "definition*", "notation*", "conjecture*", "remark*", "note*",
    "example*", "convention*", "warning*", "acknowledgments*", "exercise*",
    "problem*", "question*", "solution*", "answer*"
  };
  return one_of (key, names, sizeof (names) / sizeof (names[0]));
}

void
collect_due_to_native (scheme_tree value, scheme_tree& out) {
  if (head_is (value, "dueto")) {
    out << value;
    return;
  }
  if (!stree_list (value)) return;
  for (int i=0; i<N(value); ++i) collect_due_to_native (value[i], out);
}

scheme_tree
filter_enunciation_body_native (scheme_tree value) {
  if (head_is (value, "dueto")) return scheme_tree (TUPLE);
  if (!stree_list (value)) return value;
  scheme_tree out (TUPLE);
  for (int i=0; i<N(value); ++i) {
    scheme_tree child= filter_enunciation_body_native (value[i]);
    if (stree_list (child) && N(child) == 0) continue;
    out << child;
  }
  return out;
}

scheme_tree
enunciation_output_native (string key, scheme_tree args) {
  if (!stree_list (args) || N(args) == 0) return stree_string ("");
  scheme_tree due (TUPLE);
  collect_due_to_native (args[0], due);
  scheme_tree begin= stree_apply ("!begin");
  begin << stree_string (key);
  for (int i=0; i<N(due); ++i) {
    if (N(due[i]) < 2) continue;
    scheme_tree option= stree_apply ("!option");
    option << tmtex_convert (due[i][1]);
    begin << option;
  }
  scheme_tree out (TUPLE);
  out << begin << tmtex_convert (filter_enunciation_body_native (args[0]));
  return out;
}

scheme_tree
simple_environment_output_native (string environment, scheme_tree args) {
  scheme_tree begin= stree_apply ("!begin");
  begin << stree_string (environment);
  scheme_tree out (TUPLE);
  out << begin << converted_child (args, 0);
  return out;
}

bool
list_environment_key (string key) {
  static const char* names[]= {
    "description", "description-compact", "description-aligned",
    "description-dash", "description-long", "description-paragraphs",
    "itemize", "itemize-minus", "itemize-dot", "itemize-arrow",
    "enumerate", "enumerate-numeric", "enumerate-roman", "enumerate-Roman",
    "enumerate-alpha", "enumerate-Alpha"
  };
  return one_of (key, names, sizeof (names) / sizeof (names[0]));
}

scheme_tree
list_environment_output_native (string key, scheme_tree args) {
  string environment= replace (key, "-", "");
  if (environment == "enumerateRoman") environment= "enumerateromancap";
  else if (environment == "enumerateAlpha") environment= "enumeratealphacap";
  return simple_environment_output_native (environment, args);
}

bool
simple_layout_environment (string key, string& environment) {
  if (key == "center" || key == "padded-center") environment= "center";
  else if (key == "padded-left-aligned") environment= "flushleft";
  else if (key == "padded-right-aligned") environment= "flushright";
  else if (key == "compact") environment= "tmcompact";
  else if (key == "compressed") environment= "tmcompressed";
  else if (key == "amplified") environment= "tmamplified";
  else if (key == "indent" || key == "algorithm-indent") environment= "tmindent";
  else if (key == "jump-in") environment= "tmjumpin";
  else return false;
  return true;
}

bool
size_wrapper_command (string key, string& command, bool& grouped) {
  grouped= true;
  if (key == "really-tiny" || key == "very-tiny" || key == "tiny") command= "tiny";
  else if (key == "really-small" || key == "very-small") command= "scriptsize";
  else if (key == "smaller") command= "footnotesize";
  else if (key == "small" || key == "flat-size") command= "small";
  else if (key == "normal-size") command= "normalsize";
  else if (key == "sharp-size" || key == "large") command= "large";
  else if (key == "larger") command= "Large";
  else if (key == "very-large" || key == "really-large") command= "LARGE";
  else if (key == "really-huge") { command= "Huge"; grouped= false; }
  else return false;
  return true;
}

scheme_tree
size_wrapper_output_native (string command, bool grouped, scheme_tree args) {
  scheme_tree converted (TUPLE);
  converted << converted_child (args, 0);
  if (grouped) return tex_apply_converted (command, converted);
  scheme_tree out= stree_apply (command);
  out << converted[0];
  return out;
}

bool
language_wrapper_key (string key) {
  static const char* names[]= {
    "british", "bulgarian", "chinese", "croatian", "czech", "danish", "dutch",
    "english", "esperanto", "finnish", "french", "german", "greek", "hungarian",
    "italian", "japanese", "korean", "polish", "portuguese", "romanian", "russian",
    "slovak", "slovene", "spanish", "swedish", "chineset", "ukrainian"
  };
  return one_of (key, names, sizeof (names) / sizeof (names[0]));
}

scheme_tree
language_wrapper_output_native (string key, scheme_tree args) {
  scheme_tree scoped (TUPLE);
  scoped << stree_string ("language") << stree_string (key);
  if (N(args) > 0) scoped << args[0];
  return with_convert_native (scoped);
}

scheme_tree
equation_output_native (string key, scheme_tree args) {
  scheme_tree body= N(args) > 0 ? convert_in_mode (args[0], "math") : stree_string ("");
  if (key == "equation") {
    scheme_tree begin= stree_apply ("!begin");
    begin << stree_string ("equation");
    scheme_tree out (TUPLE);
    out << begin << body;
    return out;
  }
  scheme_tree out= stree_apply ("!eqn");
  out << body;
  return out;
}

scheme_tree
frame_output_native (scheme_tree args) {
  string command= export_math_mode () ? "boxed" : "fbox";
  if (export_math_mode ()) latex_export_add_latex_extra_package ("amsmath");
  scheme_tree out= stree_apply (command);
  out << converted_child (args, 0);
  return out;
}

scheme_tree
colored_frame_output_native (scheme_tree args) {
  string color= N(args) > 0 && string_atom (args[0]) ? atom_text (args[0]) : "";
  scheme_tree out= stree_apply ("colorbox");
  out << decode_color_native (color, false) << converted_child (args, 1);
  return out;
}

scheme_tree
fcolorbox_output_native (scheme_tree args) {
  scheme_tree out= stree_apply ("fcolorbox");
  if (N(args) > 1) {
    for (int i=0; i<N(args)-1; ++i) {
      string color= string_atom (args[i]) ? atom_text (args[i]) : "";
      out << decode_color_native (color, false);
    }
    out << converted_child (args, N(args)-1);
  }
  return out;
}

scheme_tree
rotate_output_native (scheme_tree args) {
  scheme_tree option= stree_apply ("!option");
  option << stree_string ("origin=c");
  scheme_tree body= converted_child (args, 1);
  if (export_math_mode ()) {
    scheme_tree ensured= stree_apply ("ensuremath");
    ensured << body;
    body= ensured;
  }
  scheme_tree out= stree_apply ("rotatebox");
  out << option << converted_child (args, 0) << body;
  return out;
}

scheme_tree
href_output_native (scheme_tree args) {
  scheme_tree out= stree_apply ("url");
  string value= N(args) > 0 && string_atom (args[0]) ? atom_text (args[0]) : "";
  out << latex_export_verb_string (value);
  return out;
}

bool
modifier_key (string key) {
  static const char* names[]= {
    "strong", "em", "name", "samp", "abbr", "dfn", "kbd", "var",
    "acronym", "person"
  };
  return one_of (key, names, sizeof (names) / sizeof (names[0]));
}

scheme_tree
modifier_output_native (string key, scheme_tree args) {
  scheme_tree converted (TUPLE);
  converted << converted_child (args, 0);
  return tex_apply_converted ("tm" * key, converted);
}

scheme_tree
text_tt_output_native (scheme_tree args) {
  if (export_math_mode ()) return textual_wrapper_output ("math-tt", args);
  return modifier_output_native ("tt", args);
}

scheme_tree
render_key_output_native (scheme_tree args) {
  scheme_tree body= converted_child (args, 0);
  if (head_is (body, "!concat")) {
    scheme_tree append= stree_apply ("!append");
    for (int i=1; i<N(body); ++i) append << body[i];
    body= append;
  }
  scheme_tree out= stree_apply ("key");
  out << body;
  return out;
}

bool
builtin_theorem_environment (string name) {
  static const char* names[]= {
    "theorem", "proposition", "lemma", "corollary", "axiom", "definition",
    "notation", "conjecture", "remark", "note", "example", "convention",
    "warning", "acknowledgments", "answer", "question", "exercise", "problem",
    "solution", "theorem*", "proposition*", "lemma*", "corollary*", "axiom*",
    "definition*", "notation*", "conjecture*", "remark*", "note*", "example*",
    "convention*", "warning*", "acknowledgments*", "answer*", "question*",
    "exercise*", "problem*", "solution*"
  };
  return one_of (name, names, sizeof (names) / sizeof (names[0]));
}

scheme_tree
new_theorem_output_native (scheme_tree args) {
  if (!stree_list (args) || N(args) == 0 || !string_atom (args[0]))
    return stree_string ("");
  string original= atom_text (args[0]);
  string name= tmtex_var_name_native (original);
  latex_export_context.dynamic (original)= "environment";
  latex_export_context.dynamic (name)= "environment";
  if (builtin_theorem_environment (name)) return stree_string ("");

  scheme_tree title (TUPLE);
  for (int i=1; i<N(args); ++i) title << args[i];
  scheme_tree out= stree_apply ("newtheorem");
  out << stree_string (name) << title;
  return out;
}

bool
tm_wrapper_key (string key) {
  static const char* names[]= {
    "folded", "unfolded", "folded-plain", "unfolded-plain", "folded-std",
    "unfolded-std", "folded-explain", "unfolded-explain", "folded-env",
    "unfolded-env", "folded-documentation", "unfolded-documentation",
    "folded-grouped", "unfolded-grouped", "summarized", "detailed",
    "summarized-plain", "summarized-std", "summarized-env",
    "summarized-documentation", "summarized-grouped", "summarized-raw",
    "summarized-tiny", "detailed-plain", "detailed-std", "detailed-env",
    "detailed-documentation", "detailed-grouped", "detailed-raw", "detailed-tiny"
  };
  return one_of (key, names, sizeof (names) / sizeof (names[0]));
}

scheme_tree
tm_wrapper_output_native (string key, scheme_tree args) {
  string command= "tm" * replace (key, "-", "");
  scheme_tree out= stree_apply (command);
  scheme_tree converted= tmtex_convert_list (args);
  for (int i=0; i<N(converted); ++i) out << converted[i];
  return out;
}

scheme_tree
transform_style_native (scheme_tree style) {
  scheme_tree publisher;
  if (latex_export_internal::publisher_transform_style (style, publisher))
    return publisher;
  if (!string_atom (style))
    return latex_export_context.replace_style ? tree ("#f") : style;
  string name= atom_text (style);
  if (name == "generic" || name == "tmarticle" || name == "tmdoc")
    return stree_string ("article");
  if (name == "book" || name == "tmbook" || name == "tmmanual")
    return stree_string ("book");
  if (name == "letter") return stree_string ("letter");
  if (name == "beamer") return stree_string ("beamer");
  if (name == "seminar") return stree_string ("slides");
  if (!latex_export_context.replace_style) return style;
  return tree ("#f");
}

scheme_tree
filter_styles_native (scheme_tree styles) {
  scheme_tree out (TUPLE);
  if (!stree_list (styles)) return out;
  for (int i=0; i<N(styles); ++i) {
    scheme_tree next= transform_style_native (styles[i]);
    if (!(is_atomic (next) && next->label == "#f")) out << next;
  }
  return out;
}

scheme_tree
convert_charset_native (scheme_tree value) {
  if (string_atom (value)) {
    string s= utf8_to_cork (atom_text (value));
    s= replace (replace (s, "<less>", "<"), "<gtr>", ">");
    return stree_string (s);
  }
  if (!stree_list (value) || N(value) == 0) return value;
  scheme_tree out (TUPLE);
  out << value[0];
  for (int i=1; i<N(value); ++i) out << convert_charset_native (value[i]);
  return out;
}

void
compute_mode_stats_native (tree t, string mode) {
  if (!is_compound (t)) return;
  string tag= as_string (L(t));
  if (mode == "math")
    latex_export_context.command_math_uses (tag)=
      latex_export_context.command_math_uses[tag] + 1;
  else
    latex_export_context.command_text_uses (tag)=
      latex_export_context.command_text_uses[tag] + 1;
  for (int i=0; i<N(t); ++i) {
    tree nmode= get_env_child (t, i, "mode", tree (mode));
    string child_mode= is_atomic (nmode) ? nmode->label : mode;
    compute_mode_stats_native (t[i], child_mode);
  }
}

void
init_mode_stats_native (scheme_tree value) {
  latex_export_context.command_text_uses= hashmap<string,int> (0);
  latex_export_context.command_math_uses= hashmap<string,int> (0);
  compute_mode_stats_native (scheme_tree_to_tree (value), "text");
}

scheme_tree
mode_protect_native (scheme_tree value) {
  if (!stree_list (value) || N(value) == 0 || !is_atomic (value[0]))
    return value;
  string head= value[0]->label;
  if (starts (head, "tmtext")) {
    scheme_tree out= stree_apply ("text");
    out << value;
    return out;
  }
  if (starts (head, "tmmath") || starts (head, "math")) {
    scheme_tree out= stree_apply ("ensuremath");
    out << value;
    return out;
  }
  if (head == "!concat") {
    scheme_tree out= stree_apply ("!concat");
    for (int i=1; i<N(value); ++i) out << mode_protect_native (value[i]);
    return out;
  }
  return value;
}

scheme_tree
preprocess_preamble_native (scheme_tree value) {
  if (func_is (value, "para") || func_is (value, "concat")) {
    scheme_tree out= stree_apply ("!paragraph");
    for (int i=1; i<N(value); ++i) out << preprocess_preamble_native (value[i]);
    return out;
  }
  if (func_is (value, "mtm", 2)) {
    scheme_tree out= stree_apply ("mtm");
    out << value[1] << preprocess_preamble_native (value[2]);
    return out;
  }
  if (func_is (value, "assign", 2) && string_atom (value[1])) {
    string name= atom_text (value[1]);
    int text_uses= latex_export_context.command_text_uses[name];
    int math_uses= latex_export_context.command_math_uses[name];
    if (ends (name, "*") &&
        (starts (name, "itemize") || starts (name, "enumerate") ||
         starts (name, "description")))
      return stree_string ("");

    scheme_tree result= text_uses >= math_uses
      ? tmtex_convert (value)
      : convert_in_mode (value, "math");
    int opposite_uses= text_uses >= math_uses ? math_uses : text_uses;
    if (opposite_uses > 0 && func_is (result, "newcommand", 2)) {
      scheme_tree rebuilt= stree_apply ("newcommand");
      rebuilt << result[1] << mode_protect_native (result[2]);
      return rebuilt;
    }
    return result;
  }
  return tmtex_convert (value);
}

scheme_tree
preprocess_preamble_list_native (scheme_tree values) {
  scheme_tree out (TUPLE);
  if (!stree_list (values)) return out;
  for (int i=0; i<N(values); ++i) out << preprocess_preamble_native (values[i]);
  return out;
}

scheme_tree
context_object_list (array<object> values) {
  return as_scheme_tree (as_list_object (values));
}

bool
list_contains_stree (scheme_tree values, scheme_tree needle) {
  if (!stree_list (values)) return false;
  for (int i=0; i<N(values); ++i)
    if (values[i] == needle) return true;
  return false;
}

scheme_tree
file_output_native (scheme_tree args) {
  if (!stree_list (args) || N(args) < 4) return stree_string ("");
  scheme_tree doc= args[0];
  scheme_tree styles= args[1];
  scheme_tree init_raw= args[3];
  bool missing_init= (is_atomic (init_raw) &&
                      ((!is_quoted (init_raw->label) && init_raw->label == "#f") ||
                       (string_atom (init_raw) && atom_text (init_raw) == "#f")));
  scheme_tree init= init_raw;
  if (missing_init) init= stree_apply ("collection");

  scheme_tree style_meta;
  if (N(args) > 6) style_meta= args[6];
  else if (stree_list (styles) && N(styles) == 1) style_meta= styles[0];
  else {
    style_meta= stree_apply ("tuple");
    if (stree_list (styles))
      for (int i=0; i<N(styles); ++i) style_meta << styles[i];
  }
  scheme_tree init_meta= N(args) > 7 ? args[7] : init;

  scheme_tree filtered_style_doc= latex_export_internal::filter_known_style_macros (doc);
  scheme_tree doc_pre= latex_export_filter_preamble (filtered_style_doc);
  scheme_tree doc_preamble= latex_export_filter_duplicates (doc_pre);
  scheme_tree doc_body_pre= latex_export_filter_body (doc);
  scheme_tree doc_body= latex_export_apply_init (doc_body_pre, init);

  init_mode_stats_native (doc_body_pre);
  latex_export_context.latex_texmacs_style=
    stree_list (styles) && N(styles) > 0 && string_atom (styles[0])
      ? atom_text (styles[0]) : string ("none");
  latex_export_context.latex_texmacs_packages= array<object> ();
  if (stree_list (styles))
    for (int i=1; i<N(styles); ++i)
      latex_export_context.latex_texmacs_packages << stree_object (styles[i]);

  if (get_preference ("texmacs->latex:expand-user-macros", "off") == "on")
    doc_preamble= scheme_tree (TUPLE);

  if (!stree_list (styles) || N(styles) == 0) return tmtex_convert (doc);

  scheme_tree filtered_styles= filter_styles_native (styles);
  scheme_tree output_styles= filtered_styles;
  scheme_tree par_columns= stree_apply ("associate");
  par_columns << stree_string ("par-columns") << stree_string ("2");
  if (stree_list (filtered_styles) && N(filtered_styles) == 1 &&
      string_atom (filtered_styles[0]) && atom_text (filtered_styles[0]) == "article" &&
      list_contains_stree (init, par_columns)) {
    scheme_tree two_column (TUPLE);
    two_column << stree_string ("twocolumn") << stree_string ("article");
    output_styles= scheme_tree (TUPLE);
    output_styles << two_column;
  }

  array<object> previous_preamble= env_values (":preamble");
  array<object> scoped_preamble;
  scoped_preamble << object (true);
  for (int i=0; i<N(previous_preamble); ++i) scoped_preamble << previous_preamble[i];
  latex_export_context.env (":preamble")= scoped_preamble;
  scheme_tree preamble= preprocess_preamble_list_native (doc_preamble);
  latex_export_context.env (":preamble")= previous_preamble;

  scheme_tree body= tmtex_convert (doc_body);
  scheme_tree publisher_body;
  if (latex_export_internal::publisher_postprocess_body (body, publisher_body))
    body= publisher_body;
  body= latex_export_clean_body (body);

  scheme_tree needs (TUPLE);
  needs << context_object_list (latex_export_context.languages)
        << context_object_list (latex_export_context.colors)
        << context_object_list (latex_export_context.colormaps);

  scheme_tree metadata= stree_apply ("!athena-file-metadata");
  metadata << style_meta << init_meta;
  scheme_tree out= stree_apply ("!file");
  out << body << output_styles << needs << init << preamble << metadata;
  return out;
}

scheme_tree
convert_nonfile_native (scheme_tree value, object options) {
  scheme_tree filtered= discard_experimental_warning (value);
  tree normalized= eqnumber_to_nonumber (scheme_tree_to_tree (filtered));
  scheme_tree rewritten= tree_to_scheme_tree (normalized);
  rewritten= as_scheme_tree (call ("tmtm-match-brackets", stree_object (rewritten)));
  rewritten= as_scheme_tree (call ("tmpre-produce", stree_object (rewritten)));
  rewritten= discard_experimental_warning (rewritten);

  latex_export_initialize (options);
  scheme_tree result= tmtex_convert (rewritten);
  if (latex_export_context.mathjax)
    result= latex_export_mathjax_pre (result);
  if (!latex_export_context.use_macros)
    result= latex_export_expand_macros (result);
  if (latex_export_context.mathjax)
    result= latex_export_mathjax (result);
  return result;
}

scheme_tree
latex_id_native (scheme_tree value) {
  return tmtex_convert (string_atom (value) ? value : stree_string (""));
}

bool
athena_anchor_label (string s) {
  int n= N(s);
  if (n >= 3 && s[0] == 'H' && is_digit (s[1])) {
    for (int i=2; i<n; ++i) {
      if (s[i] == ' ') return true;
      if (!is_digit (s[i])) break;
    }
  }
  return n >= 3 && (ends (s, " {") || ends (s, " }"));
}

scheme_tree
simple_converted_apply (string head, scheme_tree args) {
  scheme_tree out= stree_apply (head);
  if (stree_list (args))
    for (int i=0; i<N(args); ++i) out << tmtex_convert (args[i]);
  return out;
}

scheme_tree
convert_mtm (scheme_tree args) {
  if (!stree_list (args) || N(args) == 0) return stree_string ("");
  if (N(args) == 1) return tmtex_convert (args[0]);
  scheme_tree label= args[0];
  if (func_is (label, "mtm", 1)) label= label[1];
  scheme_tree out= stree_apply ("!concat");
  scheme_tree begin= stree_apply ("!marker");
  begin << tree ("btm") << label;
  scheme_tree end= stree_apply ("!marker");
  end << tree ("etm") << label;
  out << begin << tmtex_convert (args[1]) << end;
  return out;
}

bool
core_noop_key (string key) {
  static const char* names[]= {
    "hidden", "vspace*", "repeat", "dlines", "dpages", "dbox",
    "with-limits", "yes-indent", "no-indent*", "yes-indent*", "page-break*",
    "no-page-break*", "no-break-here*", "no-break-end", "new-page*",
    "new-dpage*", "new-dpage",
    "twith", "cwith", "tmarker", "row", "cell", "subtable", "provides",
    "quote-value", "drd-props", "arg", "quote-arg", "experimental-build-warning",
    "xmacro", "get-label", "get-arity", "map-args", "eval-args", "mark", "eval",
    "quasi", "unquote*", "copy", "if", "if*", "case", "while", "for-each",
    "extern", "include", "use-package", "or", "xor", "and", "not", "plus",
    "minus", "times", "over", "div", "mod", "merge", "length", "range",
    "find-file", "is-tuple", "look-up", "equal", "unequal", "less", "lesseq",
    "greater", "greatereq", "cm-length", "mm-length", "in-length", "pt-length",
    "bp-length", "dd-length", "pc-length", "cc-length", "fs-length", "fbs-length",
    "em-length", "ln-length", "sep-length", "yfrac-length", "ex-length",
    "fn-length", "fns-length", "bls-length", "spc-length", "xspc-length",
    "par-length", "pag-length", "gm-length", "gh-length", "style-with",
    "style-with*", "style-only", "style-only*", "active", "active*", "inactive",
    "inactive*", "rewrite-inactive", "inline-tag", "open-tag", "middle-tag",
    "close-tag", "symbol", "latex", "hybrid", "tuple", "attr", "tmlen",
    "collection", "associate", "backup", "set-binding", "get-binding", "write",
    "tag", "meaning", "flag", "superpose", "gr-group", "gr-transform", "text-at",
    "cline", "arc", "carc", "spline", "spine*", "cspline", "fill", "box-info",
    "frame-direct", "frame-inverse", "format", "set", "reset", "expand", "expand*",
    "hide-expand", "display-baloon", "apply", "begin", "end", "func", "env",
    "phantom-float", "set-header", "set-footer", "set-this-page-header",
    "set-this-page-footer"
  };
  return one_of (key, names, sizeof (names) / sizeof (names[0]));
}

double
numeric_arg (scheme_tree args, int index) {
  if (!stree_list (args) || index < 0 || index >= N(args) ||
      !string_atom (args[index])) return 0.0;
  string s= atom_text (args[index]);
  return is_double (s) ? as_double (s) : 0.0;
}

scheme_tree
delimiter_output (string key, scheme_tree args) {
  if (!stree_list (args) || N(args) == 0) return tree ("#f");
  bool math= export_math_mode ();
  if (!math) return stree_string (latex_tmtex_large_decode_text (args[0]));
  string decoded= latex_tmtex_large_decode (args[0]);
  double n= N(args) == 2 ? numeric_arg (args, 1) : 0.0;
  string prefix;
  if (key == "left") {
    if (n == 1.0) prefix= "bigl";
    else if (n == 2.0) prefix= "Bigl";
    else if (n == 3.0) prefix= "biggl";
    else if (n == 4.0) prefix= "Biggl";
    else prefix= "left";
  }
  else if (key == "right") {
    if (n == 1.0) prefix= "bigr";
    else if (n == 2.0) prefix= "Bigr";
    else if (n == 3.0) prefix= "biggr";
    else if (n == 4.0) prefix= "Biggr";
    else prefix= "right";
  }
  else {
    if (n == 1.0) prefix= "bigm";
    else if (n == 2.0) prefix= "Bigm";
    else if (n == 3.0) prefix= "biggm";
    else if (n == 4.0) prefix= "Biggm";
    else if (key == "!middle") prefix= "middle";
    else if (decoded == ".") return stree_string ("");
    else return stree_string (decoded);
  }
  return stree_apply (prefix * decoded);
}

bool
contains_table (scheme_tree x) {
  if (!stree_list (x)) return false;
  if (N(x) >= 2 && head_is (x, "!table")) return true;
  for (int i=1; i<N(x); ++i)
    if (contains_table (x[i])) return true;
  return false;
}

scheme_tree
script_output (string which, scheme_tree script) {
  scheme_tree converted= tmtex_convert (script);
  if (contains_table (converted)) {
    scheme_tree wrapped= stree_apply ("tmscript");
    wrapped << converted;
    scheme_tree out= stree_apply (which);
    out << wrapped;
    return out;
  }
  scheme_tree out= stree_apply (which);
  out << converted;
  return out;
}

scheme_tree
right_script_output (string key, scheme_tree args) {
  if (!stree_list (args) || N(args) == 0) return tree ("#f");
  if (empty_string_stree (args[0])) return stree_string ("");
  bool sub= key == "rsub";
  if (export_math_mode ()) return script_output (sub ? "!sub" : "!sup", args[0]);
  scheme_tree out= stree_apply (sub ? "tmrsub" : "tmrsup");
  out << tmtex_convert (args[0]);
  return out;
}

scheme_tree
left_script_output (bool sub, scheme_tree args) {
  if (!stree_list (args) || N(args) == 0) return tree ("#f");
  if (empty_string_stree (args[0])) return stree_string ("");
  scheme_tree script= stree_apply (sub ? "rsub" : "rsup");
  script << args[0];
  if (!export_math_mode ()) return tmtex_convert (script);
  scheme_tree empty_group= stree_apply ("!group");
  scheme_tree concat= stree_apply ("concat");
  concat << empty_group << script;
  return tmtex_convert (concat);
}

scheme_tree
long_arrow_output (scheme_tree args) {
  if (!stree_list (args) || N(args) < 2) return tree ("#f");
  scheme_tree cmd= latex_tmtex_decode_long_arrow (args[0]);
  bool symbol= is_atomic (cmd) && !string_atom (cmd) && cmd->label != "#f";
  if (symbol && N(args) == 2) {
    scheme_tree out= stree_apply (cmd->label);
    out << tmtex_convert (args[1]);
    return out;
  }
  if (symbol && N(args) >= 3) {
    scheme_tree option= stree_apply ("!option");
    option << tmtex_convert (args[2]);
    scheme_tree out= stree_apply (cmd->label);
    out << option << tmtex_convert (args[1]);
    return out;
  }
  scheme_tree converted_cmd= tmtex_convert (cmd);
  if (N(args) == 2) {
    scheme_tree out= stree_apply ("overset");
    out << tmtex_convert (args[1]) << converted_cmd;
    return out;
  }
  if (empty_string_stree (args[1])) {
    scheme_tree out= stree_apply ("underset");
    out << tmtex_convert (args[2]) << converted_cmd;
    return out;
  }
  scheme_tree over= stree_apply ("overset");
  over << tmtex_convert (args[1]) << converted_cmd;
  scheme_tree out= stree_apply ("underset");
  out << tmtex_convert (args[2]) << over;
  return out;
}

bool
tmtex_token_string (string s) {
  if (N(s) == 1) return true;
  if (N(s) == 0 || s[0] != '<') return false;
  return search_forwards (">", 0, s) == N(s) - 1;
}

bool
wide_source (scheme_tree x, bool below) {
  string head= below ? "wide*" : "wide";
  if (func_is (x, head, 1)) return wide_source (x[1], below);
  if (!string_atom (x)) return true;
  return !tmtex_token_string (atom_text (x));
}

string
normalize_wide_accent (scheme_tree value) {
  if (!string_atom (value)) return "";
  string acc= atom_text (value);
  if (N(acc) >= 6 && acc (0, 6) == "<wide-") acc= "<" * acc (6, N(acc));
  return acc;
}

scheme_tree
one_arg_command (string command, scheme_tree arg) {
  scheme_tree out= stree_apply (command);
  out << arg;
  return out;
}

scheme_tree
brace_fill_output (scheme_tree source, bool below) {
  scheme_tree fill= stree_apply ("text");
  fill << stree_apply (below ? "downbracefill" : "upbracefill");
  scheme_tree out= stree_apply (below ? "underset" : "overset");
  out << tmtex_convert (fill) << tmtex_convert (source);
  return out;
}

scheme_tree
wide_accent_output (scheme_tree args, bool below) {
  if (!stree_list (args) || N(args) < 2) return tree ("#f");
  bool wide= wide_source (args[0], below);
  scheme_tree arg= tmtex_convert (args[0]);
  if (!string_atom (args[1])) return arg;
  string original= atom_text (args[1]);
  string acc= normalize_wide_accent (args[1]);

  if (below) {
    if (acc == "<hat>" || acc == "^")
      return one_arg_command (wide ? "uwidehat" : "uhat", arg);
    if (acc == "<tilde>" || acc == "~")
      return one_arg_command (wide ? "uwidetilde" : "utilde", arg);
    if (acc == "<bar>") return one_arg_command ("underline", arg);
    if (acc == "<vect>")
      return one_arg_command (wide ? "underrightarrow" : "uvec", arg);
    if (acc == "<breve>") return one_arg_command ("ubreve", arg);
    if (acc == "<invbreve>") return one_arg_command ("uinvbreve", arg);
    if (acc == "<check>") return one_arg_command ("ucheck", arg);
    if (acc == "<abovering>") return one_arg_command ("uring", arg);
    if (acc == "<acute>") return one_arg_command ("uacute", arg);
    if (acc == "<grave>") return one_arg_command ("ugrave", arg);
    if (acc == "<dot>") return one_arg_command ("underdot", arg);
    if (acc == "<ddot>") return one_arg_command ("uddot", arg);
    if (acc == "<dddot>") return one_arg_command ("udddot", arg);
    if (acc == "<ddddot>") return one_arg_command ("uddddot", arg);
    if (acc == "<rightarrow>" || acc == "<varrightarrow>")
      return one_arg_command ("underrightarrow", arg);
    if (acc == "<leftarrow>" || acc == "<varleftarrow>")
      return one_arg_command ("underleftarrow", arg);
    if (acc == "<leftrightarrow>" || acc == "<varleftrightarrow>")
      return one_arg_command ("underleftrightarrow", arg);
    if (acc == "<underbrace>" || acc == "<underbrace*>" ||
        acc == "<punderbrace>" || acc == "<punderbrace*>" ||
        acc == "<squnderbrace>" || acc == "<squnderbrace*>")
      return one_arg_command ("underbrace", arg);
    if (acc == "<overbrace>" || acc == "<overbrace*>" ||
        acc == "<poverbrace>" || acc == "<poverbrace*>" ||
        acc == "<sqoverbrace>" || acc == "<sqoverbrace*>")
      return brace_fill_output (args[0], true);
    cout << "ATHENA] non converted accent below: " << acc << "\n";
    return arg;
  }

  bool text= !export_math_mode ();
  if (acc == "<hat>" || acc == "^")
    return one_arg_command (text ? "^" : (wide ? "widehat" : "hat"), arg);
  if (acc == "<tilde>" || acc == "~")
    return one_arg_command (text ? "~" : (wide ? "widetilde" : "tilde"), arg);
  if (original == "<wide-bar>")
    return one_arg_command (text ? "=" : "overline", arg);
  if (acc == "<bar>")
    return one_arg_command (text ? "=" : (wide ? "overline" : "bar"), arg);
  if (acc == "<vect>") return one_arg_command (wide ? "overrightarrow" : "vec", arg);
  if (acc == "<breve>") return one_arg_command (text ? "u" : "breve", arg);
  if (acc == "<invbreve>") return one_arg_command ("invbreve", arg);
  if (acc == "<check>") return one_arg_command (text ? "v" : "check", arg);
  if (acc == "<abovering>") return one_arg_command (text ? "r" : "ring", arg);
  if (acc == "<acute>") return one_arg_command (text ? "'" : "acute", arg);
  if (acc == "<grave>") return one_arg_command (text ? "`" : "grave", arg);
  if (acc == "<dot>") return one_arg_command (text ? "." : "dot", arg);
  if (acc == "<ddot>") return one_arg_command (text ? "\"" : "ddot", arg);
  if (acc == "<dddot>") return one_arg_command ("dddot", arg);
  if (acc == "<ddddot>") return one_arg_command ("ddddot", arg);
  if (acc == "<rightarrow>" || acc == "<varrightarrow>")
    return one_arg_command ("overrightarrow", arg);
  if (acc == "<leftarrow>" || acc == "<varleftarrow>")
    return one_arg_command ("overleftarrow", arg);
  if (acc == "<leftrightarrow>" || acc == "<varleftrightarrow>")
    return one_arg_command ("overleftrightarrow", arg);
  if (acc == "<overbrace>" || acc == "<overbrace*>" ||
      acc == "<poverbrace>" || acc == "<poverbrace*>" ||
      acc == "<sqoverbrace>" || acc == "<sqoverbrace*>")
    return one_arg_command ("overbrace", arg);
  if (acc == "<underbrace>" || acc == "<underbrace*>" ||
      acc == "<punderbrace>" || acc == "<punderbrace*>" ||
      acc == "<squnderbrace>" || acc == "<squnderbrace*>")
    return brace_fill_output (args[0], false);
  cout << "ATHENA] non converted accent: " << acc << "\n";
  return arg;
}

bool
tmtex_protected_name (string name) {
  static const char* names[]= {
    "a", "b", "c", "d", "i", "j", "k", "l", "o", "r", "t", "u", "v",
    "H", "L", "O", "P", "S", "aa", "ae", "bf", "cr", "dh", "dj", "dp",
    "em", "fi", "ge", "gg", "ht", "if", "in", "it", "le", "lg", "ll",
    "lu", "lq", "mp", "mu", "ne", "ng", "ni", "nu", "oe", "or", "pi",
    "pm", "rm", "rq", "sb", "sc", "sf", "sl", "sp", "ss", "th", "to",
    "tt", "wd", "wp", "wr", "xi", "AA", "AE", "DH", "DJ", "Im", "NG",
    "OE", "Pi", "Pr", "Re", "SS", "TH", "Xi"
  };
  return one_of (name, names, sizeof (names) / sizeof (names[0]));
}

string
digit_word (char c) {
  switch (c) {
  case '0': return "zero";
  case '1': return "one";
  case '2': return "two";
  case '3': return "three";
  case '4': return "four";
  case '5': return "five";
  case '6': return "six";
  case '7': return "seven";
  case '8': return "eight";
  case '9': return "nine";
  default: return "";
  }
}

string
tmtex_var_name_native (string var) {
  if (tmtex_protected_name (var)) return "tm" * var;
  if (N(var) <= 1) return var;
  string r;
  for (int i=0; i<N(var); ++i) {
    char c= var[i];
    if (is_alpha (c)) r << c;
    else if (is_digit (c)) r << digit_word (c);
    else if (c == '*' && i + 1 == N(var)) r << c;
  }
  if (search_forwards ("*", 0, r) >= 0) {
    if (!latex_export_internal::registry_latex_name_known (r))
      r= replace (r, "*", "star");
  }
  return r;
}

int
macro_arg_index (scheme_tree value, scheme_tree args) {
  if (!stree_list (args)) return 0;
  for (int i=0; i<N(args); ++i)
    if (value == args[i]) return i + 1;
  return 0;
}

scheme_tree
rewrite_macro_args (scheme_tree x, scheme_tree args) {
  if (!stree_list (x)) return x;
  if ((head_is (x, "arg") || head_is (x, "value")) && N(x) > 1) {
    int n= macro_arg_index (x[1], args);
    if (n != 0) {
      scheme_tree out= stree_apply ("!arg");
      out << stree_string (as_string (n));
      return out;
    }
  }
  scheme_tree out (TUPLE);
  for (int i=0; i<N(x); ++i) out << rewrite_macro_args (x[i], args);
  return out;
}

void
assign_env_value (string var, scheme_tree value) {
  array<object> old= env_values (var);
  array<object> next;
  next << stree_object (value);
  for (int i=1; i<N(old); ++i) next << old[i];
  latex_export_context.env (var)= next;
}

string
assign_command_native (string var, string val) {
  if (var == "font-size") {
    double x= as_double (val) * 10.0;
    if (x < 1.0) return "";
    if (x < 5.5) return "tiny";
    if (x < 6.5) return "scriptsize";
    if (x < 7.5) return "footnotesize";
    if (x < 9.5) return "small";
    if (x < 11.5) return "normalsize";
    if (x < 13.5) return "large";
    if (x < 15.5) return "Large";
    if (x < 18.5) return "LARGE";
    if (x < 22.5) return "huge";
    if (x < 50.0) return "Huge";
    return "";
  }
  if (var == "font-family") {
    if (val == "rm") return "rmfamily";
    if (val == "ss") return "ssfamily";
    if (val == "tt") return "ttfamily";
  }
  if (var == "font-series") {
    if (val == "medium") return "mdseries";
    if (val == "bold") return "bfseries";
  }
  if (var == "font-shape") {
    if (val == "right") return "upshape";
    if (val == "slanted") return "slshape";
    if (val == "italic") return "itshape";
    if (val == "small-caps") return "scshape";
  }
  return "";
}

struct NativeWithCommand {
  bool found;
  bool environment;
  string name;
  string argument;

  NativeWithCommand ():
    found (false), environment (false), name (""), argument ("") {}
  NativeWithCommand (string command):
    found (true), environment (false), name (command), argument ("") {}
  NativeWithCommand (string env, string arg, bool):
    found (true), environment (true), name (env), argument (arg) {}
};

NativeWithCommand
text_with_command_native (string var, string val) {
  if (var == "font-family") {
    if (val == "rm") return NativeWithCommand ("tmtextrm");
    if (val == "ss") return NativeWithCommand ("tmtextsf");
    if (val == "tt") return NativeWithCommand ("tmtexttt");
  }
  if (var == "font-series") {
    if (val == "medium") return NativeWithCommand ("tmtextmd");
    if (val == "bold") return NativeWithCommand ("tmtextbf");
  }
  if (var == "font-shape") {
    if (val == "right") return NativeWithCommand ("tmtextup");
    if (val == "slanted") return NativeWithCommand ("tmtextsl");
    if (val == "italic") return NativeWithCommand ("tmtextit");
    if (val == "small-caps") return NativeWithCommand ("tmtextsc");
  }
  if (var == "par-columns" && (val == "2" || val == "3"))
    return NativeWithCommand ("multicols", val, true);
  if (var == "par-mode") {
    if (val == "center") return NativeWithCommand ("center", "", true);
    if (val == "left") return NativeWithCommand ("flushleft", "", true);
    if (val == "right") return NativeWithCommand ("flushright", "", true);
  }
  return NativeWithCommand ();
}

NativeWithCommand
math_with_command_native (string var, string val) {
  if (var == "font" || var == "math-font") {
    if (val == "cal") return NativeWithCommand ("mathcal");
    if (val == "cal*") return NativeWithCommand ("mathscr");
    if (val == "cal**") return NativeWithCommand ("EuScript");
    if (val == "Euler") return NativeWithCommand ("mathfrak");
    if (val == "Bbb") return NativeWithCommand ("mathbb");
    if (val == "Bbb*") return NativeWithCommand ("mathbbm");
    if (val == "Bbb**") return NativeWithCommand ("mathbbmss");
    if (val == "Bbb***") return NativeWithCommand ("mathbb");
    if (val == "Bbb****") return NativeWithCommand ("mathds");
  }
  if (var == "font-family") {
    if (val == "rm") return NativeWithCommand ("mathrm");
    if (val == "ss") return NativeWithCommand ("mathsf");
    if (val == "tt") return NativeWithCommand ("mathtt");
  }
  if (var == "font-series") {
    if (val == "medium") return NativeWithCommand ("tmmathmd");
    if (val == "bold") return NativeWithCommand ("tmmathbf");
  }
  if (var == "font-shape") {
    if (val == "right" || val == "small-caps") return NativeWithCommand ("mathrm");
    if (val == "slanted" || val == "italic") return NativeWithCommand ("mathit");
  }
  if (var == "math-font-family") {
    if (val == "mr" || val == "rm") return NativeWithCommand ("mathrm");
    if (val == "ms" || val == "ss") return NativeWithCommand ("mathsf");
    if (val == "mt" || val == "tt") return NativeWithCommand ("mathtt");
    if (val == "normal") return NativeWithCommand ("mathnormal");
    if (val == "bf") return NativeWithCommand ("mathbf");
    if (val == "it") return NativeWithCommand ("mathit");
  }
  if (var == "math-font-series" && val == "bold")
    return NativeWithCommand ("tmmathbf");
  return NativeWithCommand ();
}

NativeWithCommand
with_command_native (string var, string val) {
  if (export_math_mode ()) {
    NativeWithCommand math= math_with_command_native (var, val);
    if (math.found) return math;
  }
  return text_with_command_native (var, val);
}

string
length_number_prefix (string s) {
  while (starts (s, "--")) s= s (2, N(s));
  int i=0;
  while (i < N(s) && (is_digit (s[i]) || s[i] == '-' || s[i] == '.')) ++i;
  return i > 0 ? s (0, i) : string ("");
}

string
tex_length_native (string s) {
  if (s == "") return "0pt";
  string parsed= s;
  while (starts (parsed, "--")) parsed= parsed (2, N(parsed));
  int i=0;
  while (i < N(parsed) && (is_digit (parsed[i]) || parsed[i] == '-' || parsed[i] == '.')) ++i;
  string number= i > 0 ? parsed (0, i) : string ("");
  string unit= parsed (i, N(parsed));
  if (unit == "" && number != "" && is_double (number) && as_double (number) == 0.0)
    return "0pt";
  if (unit == "fn" && number != "" && is_double (number))
    return as_string (as_double (number)) * "em";
  return s;
}

bool
zero_tex_length (string s) {
  if (s == "") return true;
  string number= length_number_prefix (s);
  return number != "" && is_double (number) && as_double (number) == 0.0;
}

scheme_tree
parmod_output_native (string left, string right, string first,
                      scheme_tree arg, bool omit_zero) {
  left = tex_length_native (left);
  right= tex_length_native (right);
  first= tex_length_native (first);
  if (omit_zero && zero_tex_length (left) && zero_tex_length (right) &&
      zero_tex_length (first)) return arg;
  scheme_tree begin= stree_apply ("!begin");
  begin << stree_string ("tmparmod") << stree_string (left)
        << stree_string (right) << stree_string (first);
  scheme_tree out (TUPLE);
  out << begin << arg;
  return out;
}

scheme_tree
parsep_output_native (string amount, scheme_tree arg) {
  scheme_tree begin= stree_apply ("!begin");
  begin << stree_string ("tmparsep") << stree_string (tex_length_native (amount));
  scheme_tree out (TUPLE);
  out << begin << arg;
  return out;
}

scheme_tree
language_output_native (string language, scheme_tree arg) {
  if (language == "verbatim") {
    scheme_tree out= stree_apply ("tt");
    out << arg;
    return out;
  }
  latex_export_add_language (language);
  if (latex_stree_multiline (arg)) {
    scheme_tree begin= stree_apply ("!begin");
    begin << stree_string ("otherlanguage") << stree_string (language);
    scheme_tree out (TUPLE);
    out << begin << arg;
    return out;
  }
  scheme_tree out= stree_apply ("foreignlanguage");
  out << stree_string (language) << arg;
  return out;
}

scheme_tree
decode_color_native (string color, bool force_html) {
  string map= starts (color, "#") ? string ("HTML")
                                   : named_color_to_xcolormap (color);
  if (map == "none" && force_html)
    return decode_color_native (get_hex_color (color), true);
  if (map == "HTML" && force_html) {
    scheme_tree option= stree_apply ("!option");
    option << stree_string ("HTML");
    scheme_tree out (TUPLE);
    out << option << stree_string (latex_export_internal::html_xcolor (color));
    return out;
  }
  if (map == "texmacs") {
    latex_export_add_color (color);
    return stree_string (replace (color, " ", ""));
  }
  if (map == "x11names")
    return decode_color_native (get_hex_color (color), true);
  if (map != "xcolor" && map != "none") latex_export_add_colormap (map);
  return stree_string (replace (color, " ", ""));
}

scheme_tree
color_output_native (string color, scheme_tree arg) {
  scheme_tree decoded= decode_color_native (color, true);
  if (stree_list (decoded)) {
    scheme_tree command= stree_apply ("color");
    for (int i=0; i<N(decoded); ++i) command << decoded[i];
    scheme_tree append= stree_apply ("!append");
    append << command << arg;
    scheme_tree group= stree_apply ("!group");
    group << append;
    return group;
  }
  scheme_tree out= stree_apply ("tmcolor");
  out << decoded << arg;
  return out;
}

scheme_tree
wrap_with_command_native (NativeWithCommand command, scheme_tree arg) {
  if (!command.environment) {
    scheme_tree out= stree_apply (command.name);
    out << arg;
    return out;
  }
  scheme_tree begin= stree_apply ("!begin");
  begin << stree_string (command.name);
  if (command.argument != "") begin << stree_string (command.argument);
  scheme_tree out (TUPLE);
  out << begin << arg;
  return out;
}

scheme_tree
with_one_native (string var, string val, scheme_tree arg) {
  if (var == "mode") {
    array<object> values= env_values ("mode");
    string old= N(values) > 1 && is_string (values[1]) ? as_string (values[1]) : "";
    if (val == "text" && old != "text") {
      scheme_tree out= stree_apply ("text"); out << arg; return out;
    }
    if (val == "math" && old != "math") {
      scheme_tree out= stree_apply (N(env_values (":preamble")) > 0 ? "ensuremath" : "!math");
      out << arg; return out;
    }
    if (val == "prog" && old == "text") {
      scheme_tree out= stree_apply ("tt"); out << arg; return out;
    }
    if (val == "prog" && old == "math") {
      scheme_tree tt= stree_apply ("tt"); tt << arg;
      scheme_tree out= stree_apply ("text"); out << tt; return out;
    }
    return arg;
  }

  NativeWithCommand command= with_command_native (var, val);
  string assignment= assign_command_native (var, val);
  if (command.found && !command.environment && func_is (arg, command.name, 1))
    return arg;
  if (command.found && !command.environment &&
      (command.name == "mathrm" || command.name == "mathbf" ||
       command.name == "mathsf" || command.name == "mathit" ||
       command.name == "mathtt" || command.name == "mathsl"))
    return post_process_math_text_native (command.name, arg);
  if (!export_math_mode () && command.found && assignment != "" &&
      latex_stree_multiline (arg)) {
    scheme_tree parts (TUPLE);
    parts << stree_apply (assignment) << stree_string (" ") << arg;
    scheme_tree group= stree_apply ("!group");
    group << latex_tex_concat (parts);
    return group;
  }
  if (command.found) return wrap_with_command_native (command, arg);
  if (assignment != "") {
    scheme_tree parts (TUPLE);
    parts << stree_apply (assignment) << stree_string (" ") << arg;
    scheme_tree group= stree_apply ("!group");
    group << latex_tex_concat (parts);
    return group;
  }
  if (var == "par-left")  return parmod_output_native (val, "0pt", "0pt", arg, true);
  if (var == "par-right") return parmod_output_native ("0pt", val, "0pt", arg, true);
  if (var == "par-first") return parmod_output_native ("0pt", "0pt", val, arg, false);
  if (var == "par-par-sep") return parsep_output_native (val, arg);
  if (var == "language") return language_output_native (val, arg);
  if (var == "color") return color_output_native (val, arg);
  return arg;
}

scheme_tree
assign_output (scheme_tree args) {
  if (!stree_list (args) || N(args) < 2 || !string_atom (args[0]))
    return stree_string ("");

  string var= tmtex_var_name_native (atom_text (args[0]));
  if (var == "") return stree_string ("");

  string bsvar= "\\" * var;
  string def= latex_export_internal::registry_latex_name_known (var) ?
              "providecommand" : "newcommand";

  scheme_tree val= args[1];
  while (func_is (val, "quote", 1)) val= val[1];
  assign_env_value (var, val);

  if (string_atom (val)) {
    string command= assign_command_native (var, atom_text (val));
    if (command != "") return stree_apply (command);
    scheme_tree out= stree_apply (def);
    out << stree_string (bsvar) << tmtex_convert (val);
    return out;
  }

  if (head_is (val, "macro") || head_is (val, "func")) {
    scheme_tree out= stree_apply (def);
    out << stree_string (bsvar);
    if (N(val) <= 2) {
      if (N(val) > 1) out << tmtex_convert (val[N(val)-1]);
      return out;
    }

    int argc= N(val) - 2;
    scheme_tree option= stree_apply ("!option");
    option << stree_string (as_string (argc));
    out << option;

    scheme_tree parameters (TUPLE);
    for (int i=1; i<N(val)-1; ++i) parameters << val[i];
    scheme_tree rewritten= rewrite_macro_args (val[N(val)-1], parameters);
    out << tmtex_convert (rewritten);
    return out;
  }

  scheme_tree out= stree_apply (def);
  out << stree_string (bsvar) << tmtex_convert (val);
  return out;
}

string
number_renderer_native (scheme_tree value) {
  scheme_tree cur= value;
  while (stree_list (cur) && N(cur) > 0) cur= cur[0];
  string r= is_atomic (cur) ? atom_text (cur) : "";
  if (r == "alpha") return "alph";
  if (r == "Alpha") return "Alph";
  return r;
}

string
number_counter_native (scheme_tree value) {
  scheme_tree cur= value;
  if (head_is (cur, "value")) {
    scheme_tree tail (TUPLE);
    for (int i=1; i<N(cur); ++i) tail << cur[i];
    cur= tail;
  }
  while (stree_list (cur) && N(cur) == 1) cur= cur[0];
  if (!is_atomic (cur)) return "";
  string r= atom_text (cur);
  if (ends (r, "-nr")) r= r (0, N(r)-3);
  return r;
}

scheme_tree
number_output (scheme_tree args) {
  if (!stree_list (args) || N(args) == 0) return stree_string ("");
  scheme_tree renderer_args (TUPLE);
  for (int i=1; i<N(args); ++i) renderer_args << args[i];
  string renderer= number_renderer_native (renderer_args);
  string counter= number_counter_native (args[0]);
  scheme_tree out= stree_apply (renderer);
  out << tmtex_convert (stree_string (counter));
  return out;
}

scheme_tree
change_case_output (scheme_tree args) {
  if (!stree_list (args) || N(args) == 0) return stree_string ("");
  if (N(args) > 1 && string_atom (args[1])) {
    string mode= atom_text (args[1]);
    if (mode == "UPCASE")
      return tex_apply_one ("MakeUppercase", converted_child (args, 0));
    if (mode == "locase")
      return tex_apply_one ("MakeLowercase", converted_child (args, 0));
  }
  return converted_child (args, 0);
}

scheme_tree
line_note_output (scheme_tree args) {
  scheme_tree out= stree_apply ("tmlinenote");
  out << converted_child (args, 0);
  string first= N(args) > 1 && string_atom (args[1]) ? atom_text (args[1]) : "";
  string second= N(args) > 2 && string_atom (args[2]) ? atom_text (args[2]) : "";
  out << stree_string (decode_length_string (first));
  out << stree_string (decode_length_string (second));
  return out;
}

scheme_tree
listing_output (scheme_tree args) {
  scheme_tree begin= stree_apply ("!begin");
  begin << stree_string ("tmlisting");
  scheme_tree out (TUPLE);
  out << begin << converted_child (args, 0);
  return out;
}

scheme_tree
minipage_output (scheme_tree args) {
  if (!stree_list (args) || N(args) < 3) return stree_string ("");
  string position= string_atom (args[0]) ? atom_text (args[0]) : "";
  string size= string_atom (args[1]) ? atom_text (args[1]) : "";
  scheme_tree begin= stree_apply ("!begin");
  begin << stree_string ("minipage");
  if (position != "f") {
    scheme_tree option= stree_apply ("!option");
    option << stree_string (position);
    begin << option;
  }
  begin << stree_string (decode_length_string (size));
  scheme_tree out (TUPLE);
  out << begin << tmtex_convert (args[2]);
  return out;
}

scheme_tree
math_class_output (string command, scheme_tree args) {
  scheme_tree out= stree_apply (command);
  out << converted_child (args, 0);
  return out;
}

scheme_tree
item_output (scheme_tree args, bool with_option) {
  scheme_tree item= stree_apply ("item");
  if (with_option) {
    scheme_tree option= stree_apply ("!option");
    option << converted_child (args, 0);
    item << option;
  }
  scheme_tree parts (TUPLE);
  parts << item << stree_string (" ");
  return latex_tex_concat (parts);
}

scheme_tree
marginal_note_output (string key, scheme_tree args) {
  scheme_tree out= stree_apply ("marginpar");
  scheme_tree last= stree_string ("");
  if (stree_list (args) && N(args) > 0) last= tmtex_convert (args[N(args)-1]);
  if (key == "marginal-left-note" || key == "marginal-even-left-note") {
    scheme_tree option= stree_apply ("!option");
    option << last;
    out << option << stree_string ("");
  }
  else if (key == "marginal-right-note" || key == "marginal-even-right-note") {
    scheme_tree option= stree_apply ("!option");
    option << stree_string ("");
    out << option << last;
  }
  else out << last;
  return out;
}

scheme_tree
menu_output (scheme_tree args) {
  if (!stree_list (args) || N(args) == 0) return stree_string ("");
  scheme_tree parts (TUPLE);
  for (int i=0; i<N(args); ++i) {
    if (i > 0) {
      scheme_tree math= stree_apply ("!math");
      math << stree_apply ("rightarrow");
      parts << math;
    }
    scheme_tree one (TUPLE);
    one << args[i];
    parts << tmtex_convert (source_apply ("samp", one));
  }
  return latex_tex_concat (parts);
}

scheme_tree
proof_pair (scheme_tree title, scheme_tree body) {
  scheme_tree begin= stree_apply ("!begin");
  begin << stree_string ("proof*") << title;
  scheme_tree out (TUPLE);
  out << begin << body;
  return out;
}

scheme_tree
proof_wrapped_output (string key, scheme_tree args) {
  scheme_tree original= source_apply (key, args);
  if (key == "proof-alternative")
    return athena_data_wrap_with_records (
      original, scheme_tree (TUPLE),
      proof_pair (stree_string ("Proof (Alternative)"), converted_child (args, 0)));
  if (key == "proof-standard")
    return athena_data_wrap_with_records (
      original, scheme_tree (TUPLE),
      proof_pair (stree_string ("Proof (Standard)"), converted_child (args, 0)));
  if (key == "proof-of") {
    scheme_tree title= stree_apply ("!concat");
    title << stree_string ("Proof ") << converted_child (args, 0);
    return athena_data_wrap_with_records (
      original, scheme_tree (TUPLE),
      proof_pair (title, converted_child (args, 1)));
  }
  return athena_data_wrap_with_records (
    original, scheme_tree (TUPLE),
    proof_pair (converted_child (args, 0), converted_child (args, 1)));
}

scheme_tree
render_proof_output (scheme_tree args) {
  return proof_pair (converted_child (args, 0), converted_child (args, 1));
}

scheme_tree
glossary_label_output (bool consume) {
  int nr= consume ? ++latex_export_context.auto_consume
                  : ++latex_export_context.auto_produce;
  scheme_tree out= stree_apply ("label");
  out << stree_string ("autolab" * as_string (nr));
  return out;
}

scheme_tree
glossary_entry_output (scheme_tree args) {
  int nr= ++latex_export_context.auto_consume;
  scheme_tree page= stree_apply ("pageref");
  page << stree_string ("autolab" * as_string (nr));
  scheme_tree out= stree_apply ("glossaryentry");
  out << converted_child (args, 0) << converted_child (args, 1) << page;
  return out;
}

scheme_tree
glossary_line_output (scheme_tree t) {
  scheme_tree converted= tmtex_convert (t);
  if (head_is (converted, "glossaryentry")) return converted;
  scheme_tree out= stree_apply ("listpart");
  out << converted;
  return out;
}

scheme_tree
glossary_body_output (scheme_tree body) {
  if (!head_is (body, "document")) return tmtex_convert (body);
  scheme_tree out= stree_apply ("!document");
  for (int i=1; i<N(body); ++i) out << glossary_line_output (body[i]);
  return out;
}

scheme_tree
the_glossary_output (scheme_tree args) {
  string style= latex_export_latex_style ();
  bool book= style == "book" || style == "svmono";
  scheme_tree heading= stree_apply (book ? "chapter*" : "section*");
  heading << stree_string ("Glossary");

  scheme_tree begin= stree_apply ("!begin");
  begin << stree_string ("theglossary");
  if (stree_list (args) && N(args) > 0) begin << args[0];
  scheme_tree env (TUPLE);
  env << begin << (N(args) > 1 ? glossary_body_output (args[1]) : stree_string (""));

  scheme_tree out= stree_apply ("!document");
  out << heading << env;
  return out;
}

scheme_tree
render_line_number_output (scheme_tree args) {
  scheme_tree out= stree_apply ("tmlinenumber");
  out << converted_child (args, 0);
  scheme_tree len= converted_child (args, 1);
  string value= string_atom (len) ? atom_text (len) : "";
  out << stree_string (decode_length_string (value));
  return out;
}

scheme_tree
generic_function_output (string key, scheme_tree args) {
  if (N(key) > 0 && key[0] == '!') {
    scheme_tree out= stree_apply (key);
    scheme_tree converted= tmtex_convert_list (args);
    for (int i=0; i<N(converted); ++i) out << converted[i];
    return out;
  }
  string name= tmtex_var_name_native (key);
  if (name == "") return preserve_object (source_apply (key, args));
  return tex_apply_converted (name, tmtex_convert_list (args));
}

scheme_tree
compound_output (scheme_tree args) {
  if (!stree_list (args) || N(args) == 0)
    return preserve_object (stree_apply ("compound"));
  if (experimental_warning_name (args[0]))
    return preserve_object (stree_apply ("experimental-build-warning"));
  if (string_atom (args[0])) {
    string name= atom_text (args[0]);
    scheme_tree call_args (TUPLE);
    for (int i=1; i<N(args); ++i) call_args << args[i];
    return tmtex_convert (source_apply (name, call_args));
  }
  return preserve_object (source_apply ("compound", args));
}

scheme_tree
core_dispatch (string key, scheme_tree args) {
  scheme_tree original= source_apply (key, args);
  if (key == "!file") return file_output_native (args);
  if (key == "tformat" || key == "table") {
    scheme_tree source= stree_apply (key);
    for (int i=0; i<N(args); ++i) source << args[i];
    return latex_export_table_apply ("tabular", scheme_tree (TUPLE), source);
  }
  if (key == "eqnarray" || key == "eqnarray*" || key == "leqnarray*" ||
      key == "gather" || key == "multline" || key == "gather*" ||
      key == "multline*" || key == "align" || key == "flalign" ||
      key == "alignat" || key == "align*" || key == "flalign*" ||
      key == "alignat*") {
    if (N(args) == 0) return stree_string ("");
    array<object> previous= env_values ("mode");
    array<object> scoped;
    scoped << object ("math");
    for (int i=0; i<N(previous); ++i) scoped << previous[i];
    latex_export_context.env ("mode")= scoped;
    scheme_tree out= latex_export_table_apply (key, scheme_tree (TUPLE), args[0]);
    latex_export_context.env ("mode")= previous;
    return out;
  }
  if (key == "document" || key == "para") {
    scheme_tree out= stree_apply (key == "document" ? "!document" : "!paragraph");
    scheme_tree converted= tmtex_convert_list (args);
    for (int i=0; i<N(converted); ++i) out << converted[i];
    return out;
  }
  if (key == "date") return simple_converted_apply ("tmdate", args);
  if (key == "assign") return assign_output (args);
  if (key == "with") {
    if (N(args) == 3 && string_atom (args[0]) && string_atom (args[1]) &&
        atom_text (args[0]) == "par-columns" && atom_text (args[1]) == "1" &&
        stree_list (args[2]) && N(args[2]) > 0 && is_atomic (args[2][0])) {
      string child= args[2][0]->label;
      if (child == "small-figure" || child == "big-figure" ||
          child == "small-table" || child == "big-table")
        return float_sub (true, "h", args[2]);
    }
    if (N(args) > 0 && head_is (args[N(args)-1], "graphics"))
      return latex_export_render_image (original);
    return with_convert_native (args);
  }
  if (key == "tree" || key == "graphics")
    return latex_export_render_image (original);
  if (key == "image") return latex_export_image (args);
  if (key == "commutative-diagram") return latex_export_commutative_diagram (args);
  if (key == "framed" || key == "ornamented" || key == "padded" ||
      key == "underlined" || key == "overlined" || key == "bothlined" ||
      key == "leftlined" || key == "rightlined" || key == "verticallined")
    return latex_export_ornament (key, args);
  if (key == "hlink" || key == "cardlink" || key == "transclude" ||
      key == "material-citation" || key == "referenced-materials")
    return latex_export_link_dispatch (key, args);
  if (key == "around" || key == "around*" || key == "big-around" ||
      key == "specific" || key == "translate" || key == "localize")
    return latex_export_misc_handler (key, args);
  if (key == "compound" || key == "value") return compound_output (args);
  if (key == "number") return number_output (args);
  if (key == "change-case") return change_case_output (args);
  if (key == "text" || key == "math-up" || key == "math-ss" ||
      key == "math-tt" || key == "math-bf" || key == "math-sl" ||
      key == "math-it")
    return textual_wrapper_output (key, args);
  if (key == "math") return math_output_native (args);
  if (sectional_key (key)) return sectional_output_native (key, args);
  if (key == "appendix" || key == "appendix*")
    return appendix_output_native (key, args);
  if (enunciation_key (key)) return enunciation_output_native (key, args);
  if (key == "footnote" || key == "wide-footnote") {
    scheme_tree out= stree_apply ("footnote");
    out << converted_child (args, 0);
    return out;
  }
  if (key == "footnotemark*") {
    scheme_tree option= stree_apply ("!option");
    option << converted_child (args, 0);
    scheme_tree out= stree_apply ("footnotemark");
    out << option;
    return out;
  }
  if (list_environment_key (key)) return list_environment_output_native (key, args);
  string layout_environment;
  if (simple_layout_environment (key, layout_environment))
    return simple_environment_output_native (layout_environment, args);
  string size_command;
  bool size_grouped= true;
  if (size_wrapper_command (key, size_command, size_grouped))
    return size_wrapper_output_native (size_command, size_grouped, args);
  if (language_wrapper_key (key)) return language_wrapper_output_native (key, args);
  if (key == "equation" || key == "equation*")
    return equation_output_native (key, args);
  if (key == "frame") return frame_output_native (args);
  if (key == "colored-frame") return colored_frame_output_native (args);
  if (key == "fcolorbox") return fcolorbox_output_native (args);
  if (key == "rotate") return rotate_output_native (args);
  if (key == "hrule") return stree_apply ("hrulefill");
  if (key == "verbatim") return verbatim_output_native (args, false);
  if (key == "verbatim*") return verbatim_output_native (args, true);
  if (key == "code" || key == "cpp-code" || key == "scm-code" ||
      key == "shell-code" || key == "scilab-code" || key == "verbatim-code")
    return code_block_output_native (key, args);
  if (key == "cpp" || key == "scm" || key == "shell" || key == "scilab")
    return code_inline_output_native (key, args);
  if (key == "latex-picture-fallback" || key == "latex_preview" ||
      key == "picture-mixed" || key == "source-mixed")
    return mixed_source_output_native (args);
  if (key == "href" || key == "slink") return href_output_native (args);
  if (modifier_key (key)) return modifier_output_native (key, args);
  if (key == "tt") return text_tt_output_native (args);
  if (key == "render-key") return render_key_output_native (args);
  if (key == "new-theorem" || key == "new-remark" || key == "new-exercise")
    return new_theorem_output_native (args);
  if (key == "small-figure" || key == "big-figure" ||
      key == "small-table" || key == "big-table")
    return float_sub (false, "h", original);
  if (tm_wrapper_key (key)) return tm_wrapper_output_native (key, args);
  if (key == "hide-part") return preserve_object (original);
  if (key == "show-part") return preserve_lossy (original, converted_child (args, 1));
  if (key == "condensed") return preserve_lossy (original, converted_child (args, 0));
  if (key == "line-note") return line_note_output (args);
  if (key == "syntax") return converted_child (args, 0);
  if (key == "listing") return listing_output (args);
  if (key == "minipage") return minipage_output (args);
  if (key == "the-index") return stree_apply ("printindex");
  if (key == "table-of-contents") return tex_apply_fixed ("tableofcontents");
  if (key == "item") return item_output (args, false);
  if (key == "item*") return item_output (args, true);
  if (key == "menu") return menu_output (args);
  if (key == "proof-alternative" || key == "proof-standard" || key == "proof-of" ||
      key == "render-proof-alternative" || key == "render-proof-standard")
    return proof_wrapped_output (key, args);
  if (key == "render-proof") return render_proof_output (args);
  if (key == "nbsp") return stree_apply ("!nbsp");
  if (key == "nbhyph") return stree_apply ("!nbhyph");
  if (key == "frac*") {
    scheme_tree out= stree_apply ("sfrac");
    out << converted_child (args, 0) << converted_child (args, 1);
    return out;
  }
  if (key == "glossary" || key == "glossary-explain")
    return glossary_label_output (false);
  if (key == "glossary-2") return glossary_entry_output (args);
  if (key == "the-glossary") return the_glossary_output (args);
  if (key == "marginal-note" || key == "marginal-normal-note" ||
      key == "marginal-left-note" || key == "marginal-even-left-note" ||
      key == "marginal-right-note" || key == "marginal-even-right-note")
    return marginal_note_output (key, args);
  if (key == "math-separator") return math_class_output ("mathpunct", args);
  if (key == "math-quantifier" || key == "math-not" || key == "math-prefix" ||
      key == "math-postfix" || key == "math-ordinary" || key == "math-ignore")
    return math_class_output ("mathord", args);
  if (key == "math-imply" || key == "math-or" || key == "math-and" ||
      key == "math-union" || key == "math-intersection" || key == "math-exclude" ||
      key == "math-plus" || key == "math-minus" || key == "math-times" ||
      key == "math-over")
    return math_class_output ("mathbin", args);
  if (key == "math-relation") return math_class_output ("mathrel", args);
  if (key == "math-big") return math_class_output ("mathop", args);
  if (key == "math-open") return math_class_output ("mathopen", args);
  if (key == "math-close") return math_class_output ("mathclose", args);
  if (key == "render-line-number") return render_line_number_output (args);
  if (key == "!ilx") {
    scheme_tree out= stree_apply ("!invariant");
    if (N(args) > 0) out << args[0];
    return out;
  }
  if (key == "mtm") return convert_mtm (args);
  if (key == "surround") return convert_surround (args);
  if (key == "concat") return convert_concat (args);
  if (key == "float" || key == "wide-float") {
    if (N(args) < 3) return stree_string ("");
    string position= string_atom (args[1]) ? atom_text (args[1]) : "";
    return float_sub (key == "wide-float", position, args[2]);
  }
  if (key == "hspace") return hspace_command (args);
  if (key == "separating-space" || key == "application-space")
    return hspace_command (args);
  if (key == "vspace") return vspace_command (args);
  if (key == "space") {
    scheme_tree one (TUPLE);
    if (N(args) > 0) one << args[0];
    return hspace_command (one);
  }
  if (key == "rigid" || key == "hgroup") {
    scheme_tree out= stree_apply ("!group");
    scheme_tree converted= tmtex_convert_list (args);
    for (int i=0; i<N(converted); ++i) out << converted[i];
    return out;
  }
  if (key == "athena-preserved-object")
    return N(args) > 0 ? preserve_object (args[0]) : stree_string ("");
  if (key == "!athena-data-inline") {
    scheme_tree out= stree_apply ("!athena-data-inline");
    if (N(args) > 0) out << args[0];
    return out;
  }
  if (key == "vgroup" || key == "move" || key == "shift" ||
      key == "resize" || key == "clipped")
    return preserve_lossy (original, converted_child (args, 0));
  if (key == "shown")
    return preserve_lossy (original, converted_child (args, 0));
  if (key == "datoms")
    return preserve_lossy (original, converted_child (args, 1));
  if (key == "hidden-binding") {
    if (N(args) == 2 && string_atom (args[1]) && is_double (atom_text (args[1]))) {
      scheme_tree out= stree_apply ("custombinding");
      out << args[1];
      return out;
    }
    return preserve_object (original);
  }
  if (key == "label") {
    string id= N(args) > 0 && string_atom (args[0]) ? atom_text (args[0]) : "";
    if (athena_anchor_label (id)) return preserve_object (original);
    scheme_tree out= stree_apply ("label");
    out << latex_id_native (N(args) > 0 ? args[0] : stree_string (""));
    return out;
  }
  if (key == "reference" || key == "pageref" || key == "eqref") {
    scheme_tree out= stree_apply (key == "reference" ? "ref" : key);
    out << latex_id_native (N(args) > 0 ? args[0] : stree_string (""));
    return out;
  }
  if (key == "smart-ref") {
    string joined;
    for (int i=0; i<N(args); ++i) {
      if (i) joined << ",";
      joined << atom_text (latex_id_native (args[i]));
    }
    scheme_tree out= stree_apply ("Cref");
    out << stree_string (joined);
    return out;
  }
  if (key == "action") {
    scheme_tree out= stree_apply ("tmaction");
    if (N(args) > 0) out << tmtex_convert (args[0]);
    if (N(args) > 1) out << tmtex_convert (args[1]);
    return out;
  }
  if (key == "choose") {
    scheme_tree out= stree_apply ("binom");
    if (N(args) > 0) out << tmtex_convert (args[0]);
    if (N(args) > 1) out << tmtex_convert (args[1]);
    return out;
  }
  if (key == "!arg") return original;
  if (key == "unknown" || key == "uninit" || key == "error" ||
      key == "raw-data")
    return preserve_object (original);
  if (key == "hide-preamble" || key == "show-preamble" ||
      key == "doc-title-options" || key == "author-data" ||
      key == "eq-number") {
    scheme_tree out= stree_apply (key);
    scheme_tree converted= tmtex_convert_list (args);
    for (int i=0; i<N(converted); ++i) out << converted[i];
    return out;
  }
  if (core_noop_key (key)) return preserve_object (original);

  if (key == "line-break") return tex_apply_fixed ("linebreak");
  if (key == "page-break") return tex_apply_fixed ("pagebreak");
  if (key == "no-page-break" || key == "no-break-here" ||
      key == "no-break-start")
    return tex_apply_fixed ("nopagebreak");
  if (key == "new-page") return tex_apply_fixed ("newpage");
  if (key == "no-indent") return tex_apply_fixed ("noindent");
  if (key == "htab") {
    scheme_tree args1 (TUPLE);
    args1 << stree_apply ("fill");
    return tex_apply_converted ("hspace*", args1);
  }
  if (key == "next-line") return stree_apply ("!nextline");
  if (key == "new-line")
    return export_math_mode () ? stree_apply ("!nextline")
                               : tex_apply_fixed ("!newline");
  if (key == "emdash") return stree_string ("---");
  if (key == "no-break") {
    scheme_tree group= stree_apply ("!group");
    group << stree_apply ("nobreak");
    return group;
  }
  if (key == "left" || key == "mid" || key == "!middle" || key == "right")
    return delimiter_output (key, args);
  if (key == "big") {
    if (N(args) == 0) return tree ("#f");
    return stree_apply (latex_tmtex_big_decode (args[0]));
  }
  if (key == "long-arrow") return long_arrow_output (args);
  if (key == "below" || key == "above") {
    if (N(args) < 2) return tree ("#f");
    scheme_tree out= stree_apply (key == "below" ? "underset" : "overset");
    out << tmtex_convert (args[1]) << tmtex_convert (args[0]);
    return out;
  }
  if (key == "lsub") return left_script_output (true, args);
  if (key == "lsup" || key == "lprime") return left_script_output (false, args);
  if (key == "rsub") return right_script_output ("rsub", args);
  if (key == "rsup" || key == "rprime") return right_script_output ("rsup", args);
  if (key == "modulo") {
    if (N(args) == 0) return tree ("#f");
    return script_output ("mod", args[0]);
  }
  if (key == "frac" || key == "neg")
    return tex_apply_converted (key == "frac" ? "frac" : "not",
                                tmtex_convert_list (args));
  if (key == "sqrt") {
    if (N(args) == 0) return tree ("#f");
    if (N(args) == 1)
      return tex_apply_converted ("sqrt", tmtex_convert_list (args));
    scheme_tree option= stree_apply ("!option");
    option << tmtex_convert (args[1]);
    scheme_tree out= stree_apply ("sqrt");
    out << option << tmtex_convert (args[0]);
    return out;
  }
  if (key == "wide") return wide_accent_output (args, false);
  if (key == "wide*") return wide_accent_output (args, true);
  return tree ("#f");
}

bool
false_stree (scheme_tree value) {
  return is_atomic (value) && !is_quoted (value->label) &&
         value->label == "#f";
}

bool
style_dependent_key (string key) {
  static const char* names[]= {
    "doc-data", "abstract-data",
    "abstract", "abstract-acm", "abstract-arxiv", "abstract-msc",
    "abstract-pacs", "abstract-keywords",
    "doc-title", "doc-running-title", "doc-subtitle", "doc-note",
    "doc-misc", "doc-date", "doc-running-author", "doc-author",
    "author-name", "author-affiliation", "author-misc", "author-note",
    "author-email", "author-homepage",
    "doc-subtitle-ref", "doc-date-ref", "doc-note-ref", "doc-misc-ref",
    "author-affiliation-ref", "author-email-ref", "author-homepage-ref",
    "author-note-ref", "author-misc-ref",
    "doc-subtitle-label", "doc-date-label", "doc-note-label",
    "doc-misc-label", "author-affiliation-label", "author-email-label",
    "author-homepage-label", "author-note-label", "author-misc-label",
    "equation", "equation*", "elsevier-frontmatter", "conferenceinfo",
    "CopyrightYear", "slide", "tit", "crdata"
  };
  return one_of (key, names, sizeof (names) / sizeof (names[0]));
}

scheme_tree
style_dependent_default_native (string key, scheme_tree args) {
  scheme_tree source= stree_apply (key);
  for (int i=0; i<N(args); ++i) source << args[i];

  if (key == "doc-data" || key == "abstract-data") {
    scheme_tree wrapper (TUPLE); wrapper << args;
    return latex_export_metadata_default (key, wrapper);
  }
  if (key == "abstract" || key == "abstract-acm" ||
      key == "abstract-arxiv" || key == "abstract-msc" ||
      key == "abstract-pacs" || key == "abstract-keywords") {
    scheme_tree wrapper (TUPLE); wrapper << source;
    return latex_export_metadata_default (key, wrapper);
  }
  if (key == "doc-title" || key == "doc-running-title" ||
      key == "doc-subtitle" || key == "doc-note" || key == "doc-misc" ||
      key == "doc-date" || key == "doc-running-author" ||
      key == "author-name" || key == "author-affiliation" ||
      key == "author-misc" || key == "author-note" ||
      key == "author-email" || key == "author-homepage")
    return latex_export_metadata_field (key, source);
  if (key == "doc-author") {
    scheme_tree wrapper (TUPLE); wrapper << source;
    return latex_export_metadata_default (key, wrapper);
  }
  if (key == "equation" || key == "equation*")
    return core_dispatch (key, args);
  return generic_function_output (key, args);
}

scheme_tree
convert_node_native (scheme_tree value) {
  if (is_atomic (value)) {
    if (!string_atom (value)) return stree_string ("");
    string text= atom_text (value);
    scheme_tree placeholder= latex_export_context.placeholders[text];
    if (!false_stree (placeholder)) return placeholder;
    return latex_export_string (text);
  }
  if (!stree_list (value) || N(value) == 0 || !is_atomic (value[0]))
    return stree_string ("");

  string key= value[0]->label;
  scheme_tree args (TUPLE);
  for (int i=1; i<N(value); ++i) args << value[i];

  string dynamic= latex_export_context.dynamic[key];
  if (dynamic == "environment") {
    string environment= key == "quote-env" ? string ("quote") : key;
    scheme_tree begin= stree_apply ("!begin");
    begin << stree_string (environment);
    scheme_tree out (TUPLE);
    out << begin
        << (N(args) > 0 ? convert_node_native (args[0]) : stree_string (""));
    return out;
  }

  if (style_dependent_key (key)) {
    scheme_tree publisher;
    if (latex_export_internal::publisher_dispatch (key, args, publisher))
      return publisher;
    return style_dependent_default_native (key, args);
  }

  // Keyboard markup belongs to the documentation subsystem.  Keep that
  // rewrite as a narrow callback, then resume native conversion immediately.
  if (key == "key" || key == "key*") {
    scheme_tree source= N(args) > 0 ? args[0] : stree_string ("");
    scheme_tree rewritten= as_scheme_tree (
      call ("latex-native-key-rewrite", object (key), stree_object (source)));
    return convert_node_native (rewritten);
  }

  if (key == "quote" || key == "quasiquote" || key == "unquote")
    return preserve_object (value);

  scheme_tree converted= core_dispatch (key, args);
  if (!false_stree (converted)) return converted;

  // Legacy tmtex-tmstyle% also treated table-shaped custom tags as table
  // environments before falling back to generic LaTeX command construction.
  if (N(args) >= 1 && (head_is (args[0], "tformat") || head_is (args[0], "table"))) {
    scheme_tree table_args (TUPLE);
    for (int i=1; i<N(args); ++i) table_args << args[i];
    return latex_export_table_apply (key, table_args, args[0]);
  }

  // No external modules register generic tmtex-methods% entries.  Truly
  // unknown/custom tags therefore end at the generic LaTeX function path.
  return generic_function_output (key, args);
}

} // namespace

namespace latex_export_internal {

url
save_source_url () {
  return latex_export_context.save_source_url;
}

url
save_target_url () {
  return latex_export_context.save_target_url;
}

} // namespace latex_export_internal

void
latex_export_context_reset () {
  latex_export_context.reset ();
}

void
latex_export_document_reset (string language) {
  latex_export_context.languages= array<object> ();
  latex_export_context.colors= array<object> ();
  latex_export_context.colormaps= array<object> ();
  latex_export_context.languages << object (language);
  latex_export_context.cjk_document=
    language == "chinese" || language == "chineset" ||
    language == "japanese" || language == "korean";
}

bool
latex_export_cjk_document () {
  return latex_export_context.cjk_document;
}

void
latex_export_set_encoding (bool catcodes, bool ascii, bool unicode) {
  latex_export_context.use_catcodes= catcodes;
  latex_export_context.use_ascii= ascii;
  latex_export_context.use_unicode= unicode;
}

bool
latex_export_use_catcodes () {
  return latex_export_context.use_catcodes;
}

bool
latex_export_use_ascii () {
  return latex_export_context.use_ascii;
}

bool
latex_export_use_unicode () {
  return latex_export_context.use_unicode;
}

void
latex_export_set_use_macros (bool value) {
  latex_export_context.use_macros= value;
}

bool
latex_export_use_macros () {
  return latex_export_context.use_macros;
}

void
latex_export_set_latex_language (string language) {
  latex_export_context.latex_language= language;
}

string
latex_export_latex_language () {
  return latex_export_context.latex_language;
}

void
latex_export_set_latex_style (string style) {
  latex_export_context.latex_style= style;
}

string
latex_export_latex_style () {
  return latex_export_context.latex_style;
}

void
latex_export_set_latex_packages (object packages) {
  latex_export_context.latex_packages= as_array_object (packages);
}

object
latex_export_latex_packages () {
  return as_list_object (latex_export_context.latex_packages);
}

void
latex_export_set_latex_extra_packages (object packages) {
  latex_export_context.latex_extra_packages= as_array_object (packages);
}

object
latex_export_latex_extra_packages () {
  return as_list_object (latex_export_context.latex_extra_packages);
}

bool
latex_export_add_latex_extra_package (string package) {
  if (contains_string (latex_export_context.latex_extra_packages, package))
    return false;
  prepend_unique_string (latex_export_context.latex_extra_packages, package);
  return true;
}

void
latex_export_set_latex_virtual_packages (object packages) {
  latex_export_context.latex_virtual_packages= as_array_object (packages);
}

object
latex_export_latex_virtual_packages () {
  return as_list_object (latex_export_context.latex_virtual_packages);
}

void
latex_export_set_latex_all_packages (object packages) {
  latex_export_context.latex_all_packages= as_array_object (packages);
}

object
latex_export_latex_all_packages () {
  return as_list_object (latex_export_context.latex_all_packages);
}

void
latex_export_set_latex_texmacs_style (string style) {
  latex_export_context.latex_texmacs_style= style;
}

string
latex_export_latex_texmacs_style () {
  return latex_export_context.latex_texmacs_style;
}

void
latex_export_set_latex_texmacs_packages (object packages) {
  latex_export_context.latex_texmacs_packages= as_array_object (packages);
}

object
latex_export_latex_texmacs_packages () {
  return as_list_object (latex_export_context.latex_texmacs_packages);
}

void
latex_export_set_latex_dependencies (object dependencies) {
  latex_export_context.latex_dependencies= as_array_object (dependencies);
}

object
latex_export_latex_dependencies () {
  return as_list_object (latex_export_context.latex_dependencies);
}

void
latex_export_enter () {
  if (latex_export_context.export_depth == 0) latex_export_reset_placeholders ();
  ++latex_export_context.export_depth;
}

void
latex_export_leave () {
  if (latex_export_context.export_depth > 0) --latex_export_context.export_depth;
}

void
latex_export_reset_placeholders () {
  latex_export_context.placeholders= hashmap<string,scheme_tree> (tree ("#f"));
  latex_export_context.placeholder_serial= 0;
}

scheme_tree
latex_export_placeholder (scheme_tree st) {
  return placeholder_object (st);
}

scheme_tree
latex_export_placeholder_ref (string marker) {
  return latex_export_context.placeholders[marker];
}

bool
latex_export_experimental_warning_name (scheme_tree x) {
  return experimental_warning_name (x);
}

bool
latex_export_experimental_warning (scheme_tree t) {
  return experimental_warning (t);
}

scheme_tree
latex_export_discard_experimental_warning (scheme_tree t) {
  return discard_experimental_warning (t);
}

string
latex_export_athena_data_record (string cmd, scheme_tree values) {
  array<string> vals;
  if (stree_list (values))
    for (int i=0; i<N(values); ++i) vals << atom_text (values[i]);
  return athena_data_record (cmd, vals);
}

scheme_tree
latex_export_athena_data_inline (scheme_tree records) {
  return athena_data_inline (records);
}

string
latex_export_athena_data_object (scheme_tree st) {
  return athena_data_object (st);
}

bool
latex_export_latex_empty (scheme_tree t) {
  return latex_empty (t);
}

scheme_tree
latex_export_athena_data_wrap_records (scheme_tree st, scheme_tree records,
                                       scheme_tree fallback) {
  return athena_data_wrap_with_records (st, records, fallback);
}

scheme_tree
latex_export_athena_data_wrap (scheme_tree st, scheme_tree fallback) {
  return athena_data_wrap_with_records (st, scheme_tree (TUPLE), fallback);
}

object
latex_export_languages () {
  return as_list_object (latex_export_context.languages);
}

void
latex_export_add_language (string language) {
  prepend_unique_string (latex_export_context.languages, language);
}

object
latex_export_colors () {
  return as_list_object (latex_export_context.colors);
}

void
latex_export_add_color (string color) {
  prepend_unique_string (latex_export_context.colors, color);
}

object
latex_export_colormaps () {
  return as_list_object (latex_export_context.colormaps);
}

void
latex_export_add_colormap (string colormap) {
  prepend_unique_string (latex_export_context.colormaps, colormap);
}

bool
latex_export_mathjax () {
  return latex_export_context.mathjax;
}

void
latex_export_set_mathjax (bool value) {
  latex_export_context.mathjax= value;
}

bool
latex_export_replace_style () {
  return latex_export_context.replace_style;
}

void
latex_export_set_replace_style (bool value) {
  latex_export_context.replace_style= value;
}

void
latex_export_dynamic_set (string name, string kind) {
  latex_export_context.dynamic (name)= kind;
}

object
latex_export_dynamic_get (string name) {
  string kind= latex_export_context.dynamic[name];
  if (kind == "") return object (false);
  return symbol_object (kind);
}

void
latex_export_set_portable (bool value) {
  latex_export_context.portable= value;
}

bool
latex_export_portable () {
  return latex_export_context.portable;
}

bool
latex_export_portable_image_used (string name) {
  return latex_export_context.portable_image_used[name];
}

object
latex_export_portable_image_copy_get (string source) {
  string copied= latex_export_context.portable_image_copies[source];
  if (copied == "") return object (false);
  return object (copied);
}

void
latex_export_portable_image_record (string source, string copied) {
  latex_export_context.portable_image_copies (source)= copied;
  latex_export_context.portable_image_used (copied)= true;
}

void
latex_export_set_image_root (url root, string root_string) {
  latex_export_context.image_root_url= root;
  latex_export_context.image_root_string= root_string;
}

void
latex_export_set_save_urls (url source, url target) {
  latex_export_context.save_source_url= source;
  latex_export_context.save_target_url= target;
}

url
latex_export_image_root_url () {
  return latex_export_context.image_root_url;
}

string
latex_export_image_root_string () {
  return latex_export_context.image_root_string;
}

object
latex_export_env_list (string var) {
  return as_list_object (env_values (var));
}

object
latex_export_env_get (string var) {
  return optional_object (env_values (var), 0);
}

object
latex_export_env_get_previous (string var) {
  return optional_object (env_values (var), 1);
}

void
latex_export_env_set (string var, object value) {
  array<object> old= env_values (var);
  array<object> next;
  next << value;
  for (int i=0; i<N(old); ++i) next << old[i];
  latex_export_context.env (var)= next;
}

void
latex_export_env_reset (string var) {
  array<object> old= env_values (var);
  array<object> next;
  for (int i=1; i<N(old); ++i) next << old[i];
  latex_export_context.env (var)= next;
}

void
latex_export_env_assign (string var, object value) {
  latex_export_env_reset (var);
  latex_export_env_set (var, value);
}

object
latex_export_env_keys () {
  array<object> keys;
  iterator<string> it= iterate (latex_export_context.env);
  while (it->busy ()) {
    string key= it->next ();
    if (N(latex_export_context.env[key]) != 0) keys << object (key);
  }
  return as_list_object (keys);
}

int
latex_export_next_serial () {
  return ++latex_export_context.serial;
}

int
latex_export_ref_count_get () {
  return latex_export_context.ref_count;
}

void
latex_export_ref_count_set (int value) {
  latex_export_context.ref_count= value;
}

int
latex_export_next_auto_produce () {
  return ++latex_export_context.auto_produce;
}

int
latex_export_next_auto_consume () {
  return ++latex_export_context.auto_consume;
}

int
latex_export_next_athena_data_serial () {
  return ++latex_export_context.athena_data_serial;
}

bool
latex_export_first_appendix () {
  if (latex_export_context.appendices) return false;
  latex_export_context.appendices= true;
  return true;
}

scheme_tree
latex_export_core_dispatch (string key, scheme_tree args) {
  return core_dispatch (key, args);
}

scheme_tree
latex_export_convert_node (scheme_tree value) {
  return convert_node_native (value);
}

scheme_tree
latex_export_generic_function (string key, scheme_tree args) {
  return generic_function_output (key, args);
}

scheme_tree
latex_export_transform_style (scheme_tree style) {
  return transform_style_native (style);
}

scheme_tree
latex_export_filter_styles (scheme_tree styles) {
  return filter_styles_native (styles);
}

scheme_tree
latex_export_convert_charset (scheme_tree value) {
  return convert_charset_native (value);
}

scheme_tree
latex_export_convert_tree (scheme_tree value, object options) {
  return convert_nonfile_native (value, options);
}

void
latex_export_init_mode_stats (scheme_tree value) {
  init_mode_stats_native (value);
}

scheme_tree
latex_export_preprocess_preamble_list (scheme_tree values) {
  return preprocess_preamble_list_native (values);
}

string
latex_export_var_name (string name) {
  return tmtex_var_name_native (name);
}

string
latex_tmtex_decode_length (object value) {
  return decode_length_string (is_string (value) ? as_string (value) : "");
}

bool
latex_tmtex_px_length (object value) {
  return px_length_string (is_string (value) ? as_string (value) : "");
}

scheme_tree
latex_export_float_sub (bool wide, string position, scheme_tree body) {
  return float_sub (wide, position, body);
}

scheme_tree
latex_export_with (scheme_tree args) {
  return with_convert_native (args);
}

scheme_tree
latex_export_decode_color (string color, bool force_html) {
  return decode_color_native (color, force_html);
}

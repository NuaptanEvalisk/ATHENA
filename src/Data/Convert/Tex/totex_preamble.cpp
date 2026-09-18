/******************************************************************************
* MODULE     : totex_preamble.cpp
* DESCRIPTION: Native preamble and MathJax support for LaTeX export
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "totex_internal.hpp"
#include "colors.hpp"
#include "converter.hpp"
#include "dictionary.hpp"

namespace {

using namespace latex_export_internal;

scheme_tree mathjax_pre (scheme_tree x);

scheme_tree
mathjax_text (string head, scheme_tree arg) {
  scheme_tree x= mathjax_pre (arg);
  if (!stree_list (x) || N(x) == 0) {
    scheme_tree out= stree_apply (head);
    out << x;
    return out;
  }
  if (func_is (x, "tmtextsf", 1)) return mathjax_text ("textsf", x[1]);
  if (func_is (x, "tmtexttt", 1)) return mathjax_text ("texttt", x[1]);
  if (func_is (x, "tmtextit", 1)) return mathjax_text ("textit", x[1]);
  if (func_is (x, "tmtextbf", 1)) return mathjax_text ("textbf", x[1]);
  if (func_is (x, "tmtextrm", 1) || func_is (x, "tmtextup", 1))
    return mathjax_text (head, x[1]);
  scheme_tree out= stree_apply (head);
  out << x;
  return out;
}

scheme_tree
mathjax_pre (scheme_tree x) {
  if (!stree_list (x) || N(x) == 0) return x;
  if (func_is (x, "text", 1)) return mathjax_text ("text", x[1]);
  if (func_is (x, "dotminus", 0)) {
    scheme_tree out= stree_apply ("dot"); out << stree_string ("-"); return out;
  }
  if (func_is (x, "dotpm", 0)) {
    scheme_tree out= stree_apply ("dot"); out << stree_apply ("pm"); return out;
  }
  if (func_is (x, "dotmp", 0)) {
    scheme_tree out= stree_apply ("dot"); out << stree_apply ("mp"); return out;
  }
  if (func_is (x, "dotamalg", 0)) {
    scheme_tree out= stree_apply ("dot"); out << stree_apply ("amalg"); return out;
  }
  if (func_is (x, "dotplus", 0)) {
    scheme_tree out= stree_apply ("dot"); out << stree_string ("+"); return out;
  }
  if (func_is (x, "dottimes", 0)) {
    scheme_tree out= stree_apply ("dot"); out << stree_apply ("times"); return out;
  }
  if (func_is (x, "dotast", 0)) {
    scheme_tree out= stree_apply ("dot"); out << stree_apply ("ast"); return out;
  }
  if (head_is (x, "dag")) return stree_apply ("dagger");
  if (func_is (x, "color", 2) && func_is (x[1], "!option", 1))
    return stree_string ("");

  scheme_tree out (TUPLE);
  for (int i=0; i<N(x); ++i)
    out << (i == 0 ? x[i] : mathjax_pre (x[i]));
  return out;
}

scheme_tree
mathjax_cleanup (scheme_tree x) {
  if (!stree_list (x) || N(x) == 0) return x;
  if (func_is (x, "ensuremath", 1)) return mathjax_cleanup (x[1]);
  if (func_is (x, "hspace*", 1)) {
    scheme_tree out= stree_apply ("hspace"); out << mathjax_cleanup (x[1]); return out;
  }
  if (func_is (x, "mathbbm", 1)) {
    scheme_tree out= stree_apply ("mathbb"); out << mathjax_cleanup (x[1]); return out;
  }
  if (func_is (x, "fill", 0)) return stree_string ("3cm");
  if (head_is (x, "newcommand") || head_is (x, "custombinding") ||
      head_is (x, "nobreak") || head_is (x, "label"))
    return stree_string ("");

  scheme_tree out (TUPLE);
  for (int i=0; i<N(x); ++i)
    out << (i == 0 ? x[i] : mathjax_cleanup (x[i]));
  return out;
}

string
paper_option_key (string key) {
  if (key == "page-top") return "top";
  if (key == "page-bot") return "bottom";
  if (key == "page-odd" || key == "page-even") return "left";
  if (key == "page-right") return "right";
  if (key == "page-height") return "paperheight";
  if (key == "page-width") return "paperwidth";
  if (key == "page-type") return "page-type";
  if (key == "page-orientation") return "page-orientation";
  return "";
}

string
paper_type_option (string value) {
  static const char* names[]= {
    "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7", "a8", "a9",
    "b0", "b1", "b2", "b3", "b4", "b5", "b6", "b7", "b8", "b9",
    "legal", "letter", "executive", "archA", "archB", "archC", "archD",
    "archE", "10x14", "11x17", "C5", "Comm10", "DL", "halfletter",
    "halfexecutive", "ledger", "Monarch", "csheet", "dsheet", "esheet",
    "flsa", "flse", "folio", "lecture note", "note", "quarto", "statement",
    "tabloid"
  };
  static const char* options[]= {
    "a0paper", "a1paper", "a2paper", "a3paper", "a4paper", "a5paper",
    "a6paper", "papersize={74mm,105mm}", "papersize={52mm,74mm",
    "papersize={37mm,52mm}", "b0paper", "b1paper", "b2paper", "b3paper",
    "b4paper", "b5paper", "b6paper", "papersize={88mm,125mm}",
    "papersize={62mm,88mm}", "papersize={44mm,62mm}", "legalpaper",
    "letterpaper", "executivepaper", "papersize={9in,12in}",
    "papersize={12in,18in}", "papersize={18in,24in}",
    "papersize={24in,36in}", "papersize={36in,48in}",
    "papersize={10in,14in}", "papersize={11in,17in}",
    "papersize={162mm,229mm}", "papersize={297pt,684pt}",
    "papersize={110mm,220mm}", "papersize={140mm,216mm}",
    "papersize={133mm,184mm}", "papersize={432mm,279mm}",
    "papersize={98mm,190mm}", "papersize={432mm,559mm}",
    "papersize={559mm,864mm}", "papersize={864mm,1118mm}",
    "papersize={216mm,330mm}", "papersize={216mm,330mm}",
    "papersize={216mm,330mm}", "papersize={15.5cm,23.5cm}",
    "papersize={216mm,279mm}", "papersize={215mm,275mm}",
    "papersize={140mm,216mm}", "papersize={279mm,432mm}"
  };
  const int count= sizeof (names) / sizeof (names[0]);
  for (int i=0; i<count; ++i)
    if (value == names[i]) return options[i];
  return "";
}

scheme_tree
preamble_page_type (object entries) {
  array<object> items= as_array_object (entries);
  array<string> options;
  for (int i=0; i<N(items); ++i) {
    if (!is_list (items[i])) continue;
    array<object> pair= as_array_object (items[i]);
    if (N(pair) < 2 || !is_string (pair[0]) || !is_string (pair[1])) continue;
    string source_key= as_string (pair[0]);
    string value= as_string (pair[1]);
    string key= paper_option_key (source_key);
    if (key == "") continue;
    if (key == "page-type") {
      string option= paper_type_option (value);
      if (option != "") options << option;
    }
    else if (key == "page-orientation") options << value;
    else if (value != "auto")
      options << key * "=" * latex_tmtex_decode_length (pair[1]);
  }
  if (N(options) == 0) return stree_string ("");

  scheme_tree concat= stree_apply ("!concat");
  for (int i=0; i<N(options); ++i) {
    if (i) concat << stree_string (",");
    concat << stree_string (options[i]);
  }
  scheme_tree geometry= stree_apply ("geometry");
  geometry << concat;
  scheme_tree out= stree_apply ("!append");
  out << geometry << stree_string ("\n");
  return out;
}

string
colors_defs (object colors) {
  array<object> items= as_array_object (colors);
  string out;
  for (int i=0; i<N(items); ++i) {
    if (!is_string (items[i])) continue;
    string name= as_string (items[i]);
    out << "\\definecolor{" << replace (name, " ", "") << "}{HTML}{"
        << html_xcolor (get_hex_color (name)) << "}\n";
  }
  return out;
}

bool
contains_string (const array<string>& values, string value) {
  for (int i=0; i<N(values); ++i)
    if (values[i] == value) return true;
  return false;
}

void
append_unique (array<string>& values, string value) {
  if (!contains_string (values, value)) values << value;
}

array<string>
object_strings (object values) {
  array<string> out;
  if (!is_list (values)) return out;
  array<object> items= as_array_object (values);
  for (int i=0; i<N(items); ++i) {
    if (is_string (items[i])) out << as_string (items[i]);
    else if (is_symbol (items[i])) out << as_symbol (items[i]);
  }
  return out;
}

object
string_list_object (const array<string>& values) {
  array<object> out;
  for (int i=0; i<N(values); ++i) out << object (values[i]);
  return as_list_object (out);
}

array<string>
direct_dependencies (string package) {
  array<string> out;
  if (package == "amsart") out << string ("amstex");
  else if (package == "amstex") out << string ("amsmath") << string ("amsthm");
  return out;
}

array<string>
package_dependencies (array<string> pending) {
  array<string> out;
  while (N(pending) > 0) {
    string package= pending[0];
    array<string> rest;
    for (int i=1; i<N(pending); ++i) rest << pending[i];
    if (!contains_string (out, package)) {
      out << package;
      array<string> deps= direct_dependencies (package);
      array<string> next;
      for (int i=0; i<N(deps); ++i) next << deps[i];
      for (int i=0; i<N(rest); ++i) next << rest[i];
      pending= next;
    }
    else pending= rest;
  }
  return out;
}

int
package_priority (string package) {
  if (package == "geometry") return 10;
  if (package == "amsmath") return 20;
  if (package == "amssymb") return 30;
  if (package == "esint") return 35;
  if (package == "graphicx") return 40;
  if (package == "wasysym") return 50;
  if (package == "stmaryrd" || package == "textcomp") return 60;
  if (package == "enumerate") return 70;
  if (package == "epsfig") return 80;
  if (package == "mathrsfs") return 90;
  if (package == "bbm") return 100;
  if (package == "dsfont") return 110;
  if (package == "euscript") return 120;
  if (package == "multicol") return 130;
  if (package == "tikz-cd") return 135;
  if (package == "hyperref") return 140;
  if (package == "mathtools") return 150;
  if (package == "cleveref") return 160;
  return 999999;
}

array<string>
stable_priority_sort (array<string> values) {
  for (int i=1; i<N(values); ++i) {
    string value= values[i];
    int priority= package_priority (value);
    int j= i;
    while (j > 0 && package_priority (values[j-1]) > priority) {
      values[j]= values[j-1];
      --j;
    }
    values[j]= value;
  }
  return values;
}

array<string>
command_needs (string command) {
  return registry_command_needs (command);
}

void
record_command_needs (array<string>& used, string command) {
  array<string> packages= command_needs (command);
  for (int i=0; i<N(packages); ++i) append_unique (used, packages[i]);
}

void
scan_package_uses (scheme_tree value, array<string>& used) {
  if (!stree_list (value) || N(value) == 0) return;
  scheme_tree head= value[0];
  if (is_atomic (head) && !is_quoted (head->label)) {
    string command= head->label;
    if (starts (command, "left\\")) command= command (5, N(command));
    else if (starts (command, "right\\")) command= command (6, N(command));
    record_command_needs (used, command);
  }
  else if (stree_list (head) && N(value) >= 2 && func_is (head, "!begin") &&
           N(head) >= 2 && string_atom (head[1])) {
    string environment= atom_text (head[1]);
    record_command_needs (used, "begin-" * environment);
    if (environment == "enumerate" && N(head) >= 3 && head_is (head[2], "!option"))
      append_unique (used, "enumerate");
  }
  for (int i=1; i<N(value); ++i) scan_package_uses (value[i], used);
}

bool
provided_package (string package) {
  return latex_export_latex_texmacs_style () == "amsart" && package == "amsmath";
}

string
join_strings (const array<string>& values, string separator) {
  string out;
  for (int i=0; i<N(values); ++i) {
    if (i) out << separator;
    out << values[i];
  }
  return out;
}

string
as_use_package (array<string> packages, const array<string>& xcolor_options,
                bool has_xcolor_options) {
  packages= stable_priority_sort (packages);
  array<string> plain;
  array<string> individual;
  for (int i=0; i<N(packages); ++i) {
    if (provided_package (packages[i])) continue;
    if (packages[i] == "xcolor" && has_xcolor_options) individual << packages[i];
    else plain << packages[i];
  }

  string out;
  if (N(plain) > 0)
    out << "\\usepackage{" << join_strings (plain, ",") << "}\n";
  for (int i=0; i<N(individual); ++i) {
    string options= join_strings (xcolor_options, ",");
    out << "\\usepackage";
    if (options != "") out << "[" << options << "]";
    out << "{" << individual[i] << "}\n";
  }
  return out;
}

string
use_package_command (scheme_tree document, object colors, object colormaps) {
  array<string> used;
  scan_package_uses (document, used);

  array<string> xcolor_options= object_strings (colormaps);
  bool has_xcolor_options= N(xcolor_options) > 0;
  if (!has_xcolor_options && is_list (colors) && N(as_array_object (colors)) > 0) {
    xcolor_options << string ("");
    has_xcolor_options= true;
  }
  if (has_xcolor_options) append_unique (used, "xcolor");

  array<string> all= object_strings (latex_export_latex_all_packages ());
  array<string> first;
  for (int i=0; i<N(all); ++i)
    if (all[i] != "amsthm") first << all[i];

  array<string> inferred;
  for (int i=0; i<N(used); ++i)
    if (!contains_string (all, used[i])) inferred << used[i];
  return as_use_package (first, xcolor_options, has_xcolor_options) *
         as_use_package (inferred, xcolor_options, has_xcolor_options);
}

void
set_catcode (array<string>& keys, array<string>& values,
             string key, string value) {
  for (int i=0; i<N(keys); ++i)
    if (keys[i] == key) {
      values[i]= value;
      return;
    }
  keys << key;
  values << value;
}

void
collect_cork_catcodes (scheme_tree value, array<string>& keys,
                       array<string>& values) {
  if (string_atom (value)) {
    string text= atom_text (value);
    for (int i=0; i<N(text); ++i) {
      string key (text[i]);
      string utf8= convert (key, "Cork", "UTF-8");
      string latex= convert (utf8, "UTF-8", "LaTeX");
      if (latex != utf8 && key != "\n") set_catcode (keys, values, key, latex);
    }
    return;
  }
  if (!stree_list (value)) return;
  for (int i=0; i<N(value); ++i)
    collect_cork_catcodes (value[i], keys, values);
}

bool
raw_environment (scheme_tree value, string environment) {
  return stree_list (value) && N(value) > 0 && stree_list (value[0]) &&
         func_is (value[0], "!begin") && N(value[0]) > 1 &&
         string_atom (value[0][1]) && atom_text (value[0][1]) == environment;
}

bool
math_context (scheme_tree value) {
  if (head_is (value, "!math") || head_is (value, "!eqn")) return true;
  static const char* environments[]= {
    "equation", "gather", "multline", "split", "equation*", "gather*",
    "multline*", "align", "flalign", "alignat", "align*", "flalign*",
    "alignat*"
  };
  for (const char* environment: environments)
    if (raw_environment (value, environment)) return true;
  return false;
}

bool
verb_context (scheme_tree value) {
  return head_is (value, "!verb") || head_is (value, "!verbatim") ||
         head_is (value, "!verbatim*") || head_is (value, "tmverbatim");
}

void
collect_angle_catcodes (scheme_tree value, bool text_mode,
                        array<string>& keys, array<string>& values) {
  if (string_atom (value)) {
    if (!text_mode) return;
    string text= atom_text (value);
    for (int i=0; i<N(text); ++i) {
      if (text[i] == '<') set_catcode (keys, values, "<", "60");
      else if (text[i] == '>') set_catcode (keys, values, ">", "62");
    }
    return;
  }
  if (!stree_list (value)) return;
  bool child_text_mode= text_mode;
  if (head_is (value, "text")) child_text_mode= true;
  else if (math_context (value) || verb_context (value)) child_text_mode= false;
  for (int i=0; i<N(value); ++i)
    collect_angle_catcodes (value[i], child_text_mode, keys, values);
}

void
sort_catcodes (array<string>& keys, array<string>& values) {
  for (int i=1; i<N(keys); ++i) {
    string key= keys[i], value= values[i];
    int j= i;
    while (j > 0 && key < keys[j-1]) {
      keys[j]= keys[j-1]; values[j]= values[j-1]; --j;
    }
    keys[j]= key; values[j]= value;
  }
}

string
catcode_definition (string key, string image) {
  return "\\catcode`\\" * key * "=\\active \\def" * key * "{" * image * "}\n";
}

string
catcode_defs (scheme_tree document) {
  string out;
  if (latex_export_use_catcodes ()) {
    array<string> keys, values;
    collect_cork_catcodes (document, keys, values);
    sort_catcodes (keys, values);
    for (int i=0; i<N(keys); ++i)
      out << catcode_definition (keys[i], values[i]);
  }

  array<string> keys, values;
  collect_angle_catcodes (document, true, keys, values);
  sort_catcodes (keys, values);
  for (int i=0; i<N(keys); ++i) {
    string image= "\n\\fontencoding{T1}\\selectfont\\symbol{" * values[i] *
                  "}\\fontencoding{\\encodingdefault}";
    out << catcode_definition (keys[i], image);
  }
  return out;
}

bool
scheme_false (scheme_tree value) {
  return is_atomic (value) && value->label == "#f";
}

bool
scheme_number (scheme_tree value) {
  return is_atomic (value) && !is_quoted (value->label) && is_int (value->label);
}

bool
environment_begin (scheme_tree value) {
  return head_is (value, "!begin") || head_is (value, "!begin*");
}

string
tex_environment_name (string name) {
  return replace (name, "-", "");
}

scheme_tree
translated_label (string label) {
  scheme_tree out= stree_apply ("!translate");
  out << stree_string (label);
  return out;
}

scheme_tree
newtheorem_preamble (string name, string label) {
  scheme_tree theorem= stree_apply ("newtheorem");
  theorem << stree_string (name) << translated_label (label);
  scheme_tree out= stree_apply ("!append");
  out << theorem << stree_string ("\n");
  return out;
}

scheme_tree
ams_remark_preamble (string name, string label) {
  scheme_tree style= stree_apply ("theoremstyle");
  style << stree_string ("remark");
  scheme_tree recurse= stree_apply ("!recurse"); recurse << style;
  scheme_tree theorem= stree_apply ("newtheorem");
  theorem << stree_string (name) << translated_label (label);
  scheme_tree out= stree_apply ("!append");
  out << stree_string ("{") << recurse << theorem << stree_string ("}")
      << stree_string ("\n");
  return out;
}

scheme_tree
acm_definition_preamble (string name, string label) {
  scheme_tree theorem= stree_apply ("newtheorem");
  theorem << stree_string (name) << translated_label (label);
  scheme_tree out= stree_apply ("!append");
  out << stree_string ("\\theoremstyle{acmdefinition}\n") << theorem
      << stree_string ("\n\\theoremstyle{acmplain}")
      << stree_string ("\n");
  return out;
}

scheme_tree
elsevier_qed_preamble () {
  scheme_tree renew= stree_apply ("renewcommand");
  renew << stree_string ("\\qed") << stree_string ("");
  scheme_tree out= stree_apply ("!append");
  out << renew << stree_string ("\n");
  return out;
}

scheme_tree
ieee_rotated_symbol (string symbol) {
  scheme_tree math= stree_apply ("!math"); math << stree_apply (symbol);
  scheme_tree option= stree_apply ("!option");
  option << stree_string ("origin=c");
  scheme_tree rotate= stree_apply ("rotatebox");
  rotate << option << stree_string ("180") << math;
  scheme_tree reflect= stree_apply ("reflectbox"); reflect << rotate;
  scheme_tree mbox= stree_apply ("mbox"); mbox << reflect;
  scheme_tree mathop= stree_apply ("mathop"); mathop << mbox;
  scheme_tree group= stree_apply ("!group"); group << mathop;
  return group;
}

bool
acm_registry_style () {
  string style= publisher_source_style ();
  return style == "acmsmall" || style == "acmlarge" || style == "acmtog" ||
         style == "sigconf" || style == "sigchi" || style == "sigplan" ||
         style == "acmart";
}

bool
springer_registry_style () {
  string style= publisher_source_style ();
  return style == "svjour" || style == "svjour3" || style == "llncs" ||
         style == "svmono";
}

void
publisher_command_registry_overlay (string name, LatexRegistryInfo& info) {
  string style= publisher_source_style ();
  if (acm_registry_style ()) {
    if (name == "nequiv") info.body= tree ("#f");
    else if (name == "category") {
      info.arity= 3;
      info.body= stree_string ("");
    }
  }
  if (style == "elsarticle") {
    if (name == "comma") info.body= tree ("#f");
    else if (name == "qed") info.preamble= elsevier_qed_preamble ();
  }
  if (style == "ieeeconf" || style == "ieeetran") {
    if (name == "ieeehbar") {
      info.arity= 0;
      scheme_tree body= stree_apply ("not"); body << stree_string ("h");
      info.body= body;
    }
    else if (name == "ieeejmath") {
      info.arity= 0;
      info.body= stree_string ("j");
    }
    else if (name == "ieeeamalg") {
      info.arity= 0;
      info.body= ieee_rotated_symbol ("Pi");
    }
    else if (name == "ieeecoprod") {
      info.arity= 0;
      info.body= ieee_rotated_symbol ("prod");
    }
  }
}

void
publisher_environment_registry_overlay (string name, LatexRegistryInfo& info) {
  string style= publisher_source_style ();
  if (acm_registry_style ()) {
    if (name == "proof") info.body= tree ("#f");
    static const char* suppressed[]= {
      "theorem", "conjecture", "proposition", "lemma", "corollary",
      "definition", "example"
    };
    for (auto item: suppressed)
      if (name == item) info.preamble= tree ("#f");

    static const char* plain_names[]= {"axiom", "notation"};
    static const char* plain_labels[]= {"Axiom", "Notation"};
    for (int i=0; i<2; ++i)
      if (name == plain_names[i]) {
        info.arity= 0;
        info.preamble= newtheorem_preamble (plain_names[i], plain_labels[i]);
      }

    static const char* definition_names[]= {
      "remark", "note", "convention", "warning", "acknowledgments",
      "answer", "question", "exercise", "problem", "solution"
    };
    static const char* definition_labels[]= {
      "Remark", "Note", "Convention", "Warning", "Acknowledgments",
      "Answer", "Question", "Exercise", "Problem", "Solution"
    };
    for (int i=0; i<10; ++i)
      if (name == definition_names[i]) {
        info.arity= 0;
        info.preamble= acm_definition_preamble (definition_names[i],
                                                 definition_labels[i]);
      }
  }

  if (style == "amsart") {
    static const char* names[]= {
      "remark", "note", "example", "convention", "warning",
      "acknowledgments", "answer", "question", "exercise", "problem",
      "solution"
    };
    static const char* labels[]= {
      "Remark", "Note", "Example", "Convention", "Warning",
      "Acknowledgments", "Answer", "Question", "Exercise", "Problem",
      "Solution"
    };
    for (int i=0; i<11; ++i)
      if (name == names[i]) {
        info.arity= 0;
        info.preamble= ams_remark_preamble (names[i], labels[i]);
      }
  }

  if (springer_registry_style ()) {
    static const char* names[]= {
      "theorem", "proposition", "lemma", "corollary", "definition",
      "exercise", "problem", "solution", "remark", "note", "case",
      "conjecture", "example", "property", "question", "claim"
    };
    for (auto item: names)
      if (name == item) info.preamble= tree ("#f");
    if (name == "proof") info.body= tree ("#f");
  }
}

LatexRegistryInfo
command_info (scheme_tree head) {
  if (!is_atomic (head) || is_quoted (head->label)) return LatexRegistryInfo ();
  LatexRegistryInfo info= registry_command_info (head->label);
  publisher_command_registry_overlay (head->label, info);
  return info;
}

LatexRegistryInfo
environment_info (scheme_tree head) {
  if (!environment_begin (head) || N(head) < 2 || !string_atom (head[1]))
    return LatexRegistryInfo ();
  string name= atom_text (head[1]);
  LatexRegistryInfo info= registry_environment_info (name);
  publisher_environment_registry_overlay (name, info);
  return info;
}

scheme_tree expand_macros_native (scheme_tree value);

scheme_tree
substitute_macro (scheme_tree value, scheme_tree args) {
  if (scheme_number (value)) {
    int index= as_int (value->label);
    return index >= 0 && index < N(args) ? args[index] : value;
  }
  if (is_atomic (value) && value->label == "---")
    return N(args) > 0 ? args[0] : value;
  if (func_is (value, "!recurse", 1))
    return expand_macros_native (substitute_macro (value[1], args));
  if (func_is (value, "!translate", 1))
    return stree_string (translate (scheme_tree_to_tree (value[1]), "english",
                                    latex_export_latex_language ()));
  if (!stree_list (value)) return value;
  scheme_tree out (TUPLE);
  for (int i=0; i<N(value); ++i) out << substitute_macro (value[i], args);
  return out;
}

scheme_tree
expand_macros_native (scheme_tree value) {
  if (!stree_list (value) || N(value) == 0) return value;
  scheme_tree head= value[0];
  scheme_tree tail (TUPLE);
  for (int i=1; i<N(value); ++i) tail << expand_macros_native (value[i]);

  LatexRegistryInfo command= command_info (head);
  if (!scheme_false (command.body) && command.arity == N(tail)) {
    scheme_tree args (TUPLE); args << head;
    for (int i=0; i<N(tail); ++i) args << tail[i];
    return substitute_macro (command.body, args);
  }

  LatexRegistryInfo environment= environment_info (head);
  if (!scheme_false (environment.body) && N(tail) == 1 &&
      stree_list (head) && N(head) - 2 == environment.arity) {
    scheme_tree args (TUPLE);
    args << tail[0];
    for (int i=2; i<N(head); ++i) args << head[i];
    return substitute_macro (environment.body, args);
  }

  scheme_tree out (TUPLE); out << head;
  for (int i=0; i<N(tail); ++i) out << tail[i];
  return out;
}

scheme_tree
expand_definition (scheme_tree value, bool protect) {
  if (protect && scheme_number (value)) {
    scheme_tree out= stree_apply ("!group");
    out << stree_string ("#" * value->label);
    return out;
  }
  bool child_protect= protect;
  if (!protect && head_is (value, "!option")) child_protect= true;
  if (is_atomic (value) && value->label == "---") return stree_string ("#-#-#");
  if (scheme_number (value)) return stree_string ("#" * value->label);
  if (func_is (value, "!recurse", 1))
    return expand_definition (value[1], child_protect);
  if (func_is (value, "!translate", 1))
    return stree_string (translate (scheme_tree_to_tree (value[1]), "english",
                                    latex_export_latex_language ()));
  if (!stree_list (value)) return value;
  scheme_tree out (TUPLE);
  for (int i=0; i<N(value); ++i)
    out << expand_definition (value[i], child_protect);
  return out;
}

struct DefinitionEntry {
  string identity;
  string sort_name;
  scheme_tree key;
  int arity= 0;
  scheme_tree body;
};

struct PreambleEntry {
  string identity;
  string sort_name;
  scheme_tree body;
};

string
key_name (scheme_tree key) {
  if (string_atom (key)) return atom_text (key);
  if (is_atomic (key)) return key->label;
  if (environment_begin (key) && N(key) > 1 && string_atom (key[1]))
    return atom_text (key[1]);
  return "";
}

string
key_identity (scheme_tree key) {
  return (string_atom (key) ? string ("str:") : string ("sym:")) * key_name (key);
}

void
set_definition (array<DefinitionEntry>& entries, scheme_tree key, int arity,
                scheme_tree body) {
  string identity= key_identity (key);
  for (int i=0; i<N(entries); ++i)
    if (entries[i].identity == identity) {
      entries[i].arity= arity; entries[i].body= body; return;
    }
  DefinitionEntry entry;
  entry.identity= identity; entry.sort_name= key_name (key); entry.key= key;
  entry.arity= arity; entry.body= body; entries << entry;
}

void
set_preamble (array<PreambleEntry>& entries, scheme_tree key, scheme_tree body) {
  string identity= key_identity (key);
  for (int i=0; i<N(entries); ++i)
    if (entries[i].identity == identity) {
      entries[i].body= body; return;
    }
  PreambleEntry entry;
  entry.identity= identity; entry.sort_name= key_name (key); entry.body= body;
  entries << entry;
}

template<typename T>
void
sort_entries (array<T>& entries) {
  for (int i=1; i<N(entries); ++i) {
    T value= entries[i]; int j= i;
    while (j > 0 && value.sort_name < entries[j-1].sort_name) {
      entries[j]= entries[j-1]; --j;
    }
    entries[j]= value;
  }
}

int
filtered_arg_count (scheme_tree values, int start) {
  int count= 0;
  for (int i=start; i<N(values); ++i)
    if (!head_is (values[i], "!option")) ++count;
  return count;
}

void macro_defs_sub (scheme_tree value, array<DefinitionEntry>& macros,
                     array<DefinitionEntry>& environments,
                     array<PreambleEntry>& preambles);

void
macro_defs_sub (scheme_tree value, array<DefinitionEntry>& macros,
                array<DefinitionEntry>& environments,
                array<PreambleEntry>& preambles) {
  if (!stree_list (value) || N(value) == 0) return;
  bool command_definition= (head_is (value, "newcommand") ||
                            head_is (value, "renewcommand")) && N(value) > 2;
  int recurse_start= command_definition ? 2 : 1;
  for (int i=recurse_start; i<N(value); ++i)
    macro_defs_sub (value[i], macros, environments, preambles);

  scheme_tree head= value[0];
  LatexRegistryInfo command= command_info (head);
  if (!command.needs && !scheme_false (command.body) && command.arity >= 0) {
    int argc= command.option ? filtered_arg_count (value, 1) : N(value) - 1;
    if (argc == command.arity) {
      int arity= command.arity + (command.option ? 1 : 0);
      set_definition (macros, head, arity,
                      expand_definition (command.body, false));
      macro_defs_sub (command.body, macros, environments, preambles);
    }
  }

  LatexRegistryInfo environment= environment_info (head);
  if (!environment.needs && !scheme_false (environment.body) &&
      environment.arity >= 0 && stree_list (head) && N(head) >= 2) {
    int argc= environment.option ? filtered_arg_count (head, 0) : N(head);
    if (argc == environment.arity + 2) {
      int arity= environment.arity + (environment.option ? 1 : 0);
      set_definition (environments, head[1], arity,
                      expand_definition (environment.body, false));
      macro_defs_sub (environment.body, macros, environments, preambles);
    }
  }

  scheme_tree preamble= tree ("#f");
  scheme_tree preamble_key;
  if (!command.needs && !scheme_false (command.preamble)) {
    preamble= command.preamble; preamble_key= head;
  }
  else if (!environment.needs && !scheme_false (environment.preamble) &&
           stree_list (head) && N(head) >= 2) {
    preamble= environment.preamble; preamble_key= head[1];
  }
  if (!scheme_false (preamble)) {
    set_preamble (preambles, preamble_key, preamble);
    macro_defs_sub (preamble, macros, environments, preambles);
  }
}

scheme_tree
macro_definitions (scheme_tree document) {
  array<DefinitionEntry> macros, environments;
  array<PreambleEntry> preambles;
  macro_defs_sub (document, macros, environments, preambles);
  sort_entries (macros); sort_entries (environments); sort_entries (preambles);

  scheme_tree out= stree_apply ("!append");
  for (int i=0; i<N(macros); ++i) {
    scheme_tree item= stree_apply ("!newcommand");
    item << macros[i].key << tree (as_string (macros[i].arity)) << macros[i].body;
    out << item;
  }
  for (int i=0; i<N(environments); ++i) {
    scheme_tree item= stree_apply ("!newenvironment");
    item << environments[i].key << tree (as_string (environments[i].arity))
         << environments[i].body;
    out << item;
  }
  for (int i=0; i<N(preambles); ++i)
    out << expand_definition (preambles[i].body, false);
  return out;
}

string
serialize_macro_definition (scheme_tree value, bool environment) {
  if (!stree_list (value) || N(value) != 4) return serialize_latex (value);
  string name= key_name (value[1]);
  int arity= scheme_number (value[2]) ? as_int (value[2]->label) : 0;
  scheme_tree body= value[3];
  string option;
  if (stree_list (body) && N(body) > 1 && stree_list (body[0]) &&
      head_is (body[0], "!option") && N(body[0]) > 1) {
    option= "[" * serialize_latex (expand_definition (body[0][1], false)) * "]";
    body= body[1];
  }
  string rendered= serialize_latex (expand_definition (body, false));
  if (!environment) {
    rendered= replace (rendered, "\n\n", "*/!!/*");
    rendered= replace (rendered, "\n", " ");
    rendered= replace (rendered, "*/!!/*", "\n\n");
    string arity_text= arity == 0 ? string ("")
                                  : string ("[") * as_string (arity) * "]";
    return "\\newcommand{\\" * name * "}" * arity_text * option *
           "{" * rendered * "}\n";
  }

  rendered= replace (rendered, "%\n#-#-#", "#-#-#");
  rendered= replace (rendered, "%\n  #-#-#", "#-#-#");
  rendered= replace (rendered, "\n\n", "*/!!/*");
  rendered= replace (rendered, "\n  ", " ");
  rendered= replace (rendered, "\n", " ");
  rendered= replace (rendered, "   #-#-# ", "}{");
  rendered= replace (rendered, "#-#-# ", "}{");
  rendered= replace (rendered, "#-#-#", "}{");
  rendered= replace (rendered, "*/!!/*", "\n\n");
  string arity_text= arity == 0 ? string ("")
                                : string ("[") * as_string (arity) * "]";
  return "\\newenvironment{" * tex_environment_name (name) * "}" *
         arity_text * option * "{" * rendered * "}\n";
}

string
serialize_preamble_native (scheme_tree value) {
  if (string_atom (value)) return atom_text (value);
  if (head_is (value, "!append")) {
    string out;
    for (int i=1; i<N(value); ++i) out << serialize_preamble_native (value[i]);
    return out;
  }
  if (func_is (value, "!newcommand", 3))
    return serialize_macro_definition (value, false);
  if (func_is (value, "!newenvironment", 3))
    return serialize_macro_definition (value, true);
  return serialize_latex (value);
}

object
init_entries (scheme_tree init) {
  array<string> keys;
  array<scheme_tree> values;
  if (func_is (init, "collection")) {
    for (int i=1; i<N(init); ++i) {
      if (!func_is (init[i], "associate", 2) || !string_atom (init[i][1]))
        continue;
      string key= atom_text (init[i][1]);
      int pos= -1;
      for (int j=0; j<N(keys); ++j)
        if (keys[j] == key) { pos= j; break; }
      if (pos < 0) {
        keys << key;
        values << init[i][2];
      }
      else values[pos]= init[i][2];
    }
  }

  array<object> entries;
  for (int i=0; i<N(keys); ++i) {
    array<object> pair;
    pair << object (keys[i]) << stree_object (values[i]);
    entries << as_list_object (pair);
  }
  return as_list_object (entries);
}

string
style_name (scheme_tree style) {
  if (string_atom (style)) return atom_text (style);
  if (stree_list (style) && N(style) > 0 && string_atom (style[0]))
    return atom_text (style[0]);
  return "";
}

string
style_options (scheme_tree style) {
  if (!stree_list (style) || N(style) <= 1) return "";
  string out= "[";
  for (int i=1; i<N(style); ++i) {
    if (i > 1) out << ",";
    out << atom_text (style[i]);
  }
  out << "]";
  return out;
}

scheme_tree
preamble_text_bundle (scheme_tree page, scheme_tree macros,
                      string colors, scheme_tree text) {
  scheme_tree out= stree_apply ("!tuple");
  out << page << macros << stree_string (colors) << text;
  return out;
}

array<string>
preamble_data_native (scheme_tree text, scheme_tree style, scheme_tree,
                      scheme_tree init, scheme_tree colors,
                      scheme_tree colormaps) {
  string old_style= latex_export_latex_texmacs_style ();
  string scoped_style= style_name (style);
  if (scoped_style != "") latex_export_set_latex_texmacs_style (scoped_style);

  scheme_tree page= preamble_page_type (init_entries (init));
  scheme_tree macros= macro_definitions (text);

  array<object> color_objects;
  if (stree_list (colors))
    for (int i=0; i<N(colors); ++i)
      if (string_atom (colors[i])) color_objects << object (atom_text (colors[i]));
  object color_list= as_list_object (color_objects);
  string color_text= colors_defs (color_list);

  array<object> colormap_objects;
  if (stree_list (colormaps))
    for (int i=0; i<N(colormaps); ++i)
      if (string_atom (colormaps[i]))
        colormap_objects << object (atom_text (colormaps[i]));
  object colormap_list= as_list_object (colormap_objects);

  scheme_tree bundle= preamble_text_bundle (page, macros, color_text, text);
  string pre_page= serialize_preamble_native (page);
  string pre_macro= serialize_preamble_native (macros);
  string pre_catcode= catcode_defs (bundle);
  string pre_uses= use_package_command (bundle, color_list, colormap_list);
  string pre_extra;
  if (latex_export_latex_texmacs_style () == "ifacconf")
    pre_extra= "\\newcommand{\\labelitemiii}{\\labelitemi}\n"
               "\\newcommand{\\labelitemiv}{\\labelitemii}\n";

  string options;
  array<string> all= object_strings (latex_export_latex_all_packages ());
  if (contains_string (all, "amsthm") && string_atom (style) &&
      atom_text (style) == "amsart")
    options= "[amsthm]";
  else if (stree_list (style)) options= style_options (style);

  latex_export_set_latex_texmacs_style (old_style);

  array<string> result;
  result << options << (pre_uses * pre_extra) << pre_page
         << (pre_catcode * pre_macro * color_text);
  return result;
}

} // namespace

scheme_tree
latex_export_mathjax_pre (scheme_tree value) {
  return mathjax_pre (value);
}

scheme_tree
latex_export_mathjax (scheme_tree value) {
  return mathjax_cleanup (value);
}

scheme_tree
latex_export_preamble_page_type (object entries) {
  return preamble_page_type (entries);
}

string
latex_export_html_xcolor (string color) {
  return html_xcolor (color);
}

string
latex_export_colors_defs (object colors) {
  return colors_defs (colors);
}

object
latex_export_packages_dependencies (object packages) {
  return string_list_object (package_dependencies (object_strings (packages)));
}

object
latex_export_packages_simplify (object packages) {
  array<string> values= object_strings (packages);
  array<string> out;
  for (int i=0; i<N(values); ++i) {
    array<string> others;
    for (int j=0; j<N(values); ++j)
      if (j != i) others << values[j];
    if (!contains_string (package_dependencies (others), values[i])) out << values[i];
  }
  return string_list_object (out);
}

void
latex_export_recompute_dependencies () {
  array<string> all;
  array<string> packages= object_strings (latex_export_latex_packages ());
  array<string> extra= object_strings (latex_export_latex_extra_packages ());
  array<string> virtual_packages=
    object_strings (latex_export_latex_virtual_packages ());
  for (int i=0; i<N(packages); ++i) append_unique (all, packages[i]);
  for (int i=0; i<N(extra); ++i) append_unique (all, extra[i]);
  for (int i=0; i<N(virtual_packages); ++i) append_unique (all, virtual_packages[i]);
  latex_export_set_latex_all_packages (string_list_object (all));

  array<string> roots;
  roots << latex_export_latex_style ();
  for (int i=0; i<N(all); ++i) roots << all[i];
  latex_export_set_latex_dependencies (
    string_list_object (package_dependencies (roots)));
}

string
latex_export_use_package_command (scheme_tree document, object colors,
                                  object colormaps) {
  return use_package_command (document, colors, colormaps);
}

string
latex_export_as_use_package (object packages) {
  array<string> none;
  return as_use_package (object_strings (packages), none, false);
}

string
latex_export_catcode_defs (scheme_tree document) {
  return catcode_defs (document);
}

scheme_tree
latex_export_expand_macros (scheme_tree value) {
  return expand_macros_native (value);
}

scheme_tree
latex_export_macro_defs (scheme_tree document) {
  return macro_definitions (document);
}

string
latex_export_serialize_preamble (scheme_tree value) {
  return serialize_preamble_native (value);
}

array<string>
latex_export_preamble_data (scheme_tree text, scheme_tree style,
                            scheme_tree language, scheme_tree init,
                            scheme_tree colors, scheme_tree colormaps) {
  return preamble_data_native (text, style, language, init, colors, colormaps);
}

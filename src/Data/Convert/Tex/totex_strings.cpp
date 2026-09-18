/******************************************************************************
* MODULE     : totex_strings.cpp
* DESCRIPTION: Native LaTeX text, math token, and verbatim string conversion
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "totex_internal.hpp"
#include "converter.hpp"
#include "analyze.hpp"

namespace {

using namespace latex_export_internal;

bool
export_math_mode () {
  object mode= latex_export_env_get ("mode");
  return is_string (mode) && as_string (mode) == "math";
}

bool
one_of (string value, const char* const* values, int count) {
  for (int i=0; i<count; ++i) if (value == values[i]) return true;
  return false;
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
modified_token (string op, string s, int pos) {
  string suffix= s (pos, N(s));
  scheme_tree arg;
  if (N(suffix) == 1) arg= stree_string (suffix);
  else {
    scheme_tree no_args (TUPLE);
    arg= tex_apply_converted (suffix, no_args);
  }
  return tex_math_apply_one (op, arg);
}

bool
special_token_text (string name, string& text) {
  if (name == "less") { text= "<"; return true; }
  if (name == "gtr") { text= ">"; return true; }
  if (name == "minus") { text= "-"; return true; }
  if (name == "over") { text= ":"; return true; }
  return false;
}

scheme_tree
special_token_tree (string name) {
  if (name == "box") return stree_apply ("Box");
  if (name == "||") return stree_apply ("|");
  if (name == "precdot") return stree_apply ("tmprecdot");
  return tree ("#f");
}

string
text_symbol_command (string name) {
  if (name == "#20AC") return "euro";
  if (name == "cent") return "textcent";
  if (name == "circledR") return "textregistered";
  if (name == "copyright") return "textcopyright";
  if (name == "currency") return "textcurrency";
  if (name == "degree") return "textdegree";
  if (name == "mu") return "textmu";
  if (name == "onehalf") return "textonehalf";
  if (name == "onequarter") return "textonequarter";
  if (name == "onesuperior") return "textonesuperior";
  if (name == "paragraph") return "P";
  if (name == "threequarters") return "textthreequarters";
  if (name == "threesuperior") return "textthreesuperior";
  if (name == "trademark") return "texttrademark";
  if (name == "twosuperior") return "texttwosuperior";
  if (name == "yen") return "textyen";
  return "";
}

struct token_piece {
  bool plain;
  string text;
  scheme_tree node;
  token_piece (string s): plain (true), text (s), node (tree ("#f")) {}
  token_piece (scheme_tree t): plain (false), text (""), node (t) {}
};

token_piece
latex_token_piece (string name, bool group) {
  string special;
  if (special_token_text (name, special)) return token_piece (special);
  scheme_tree fixed= special_token_tree (name);
  if (!(is_atomic (fixed) && fixed->label == "#f")) return token_piece (fixed);

  if (starts (name, "up-")) return token_piece (modified_token ("mathrm", name, 3));
  if (starts (name, "bbb-") && N(name) >= 5 && is_digit (name[4]))
    return token_piece (modified_token ("mathbbm", name, 4));
  if (starts (name, "bbb-")) return token_piece (modified_token ("mathbb", name, 4));
  if (starts (name, "cal-")) return token_piece (modified_token ("mathcal", name, 4));
  if (starts (name, "frak-")) return token_piece (modified_token ("mathfrak", name, 5));
  if (starts (name, "b-cal-"))
    return token_piece (tex_math_apply_one ("tmmathbf", modified_token ("mathcal", name, 6)));
  if (starts (name, "b-up-")) return token_piece (modified_token ("mathbf", name, 5));
  if (starts (name, "b-")) return token_piece (modified_token ("tmmathbf", name, 2));

  if (!export_math_mode ()) {
    string command= text_symbol_command (name);
    if (command != "") {
      scheme_tree group_node= stree_apply ("!group");
      group_node << stree_apply (command);
      return token_piece (group_node);
    }
  }

  if (starts (name, "#")) {
    string value= convert ("<" * name * ">", "Cork", "UTF-8");
    if (latex_export_use_catcodes ())
      value= convert (value, "UTF-8", "LaTeX");
    scheme_tree wide= stree_apply ("!widechar");
    wide << stree_string (value);
    return token_piece (wide);
  }

  string symbol_name= replace (name, "-", "");
  scheme_tree command;
  if (symbol_name == "space") command= stree_apply ("tmxspace");
  else if (!latex_export_internal::registry_symbol_known (symbol_name)) {
    cout << "ATHENA] non converted symbol: " << name << "\n";
    command= stree_apply ("nonconverted");
    command << stree_string (symbol_name);
  }
  else command= stree_apply (symbol_name);

  if (group) {
    scheme_tree out= stree_apply ("!group");
    out << command;
    return token_piece (out);
  }
  scheme_tree out= stree_apply ("!symbol");
  out << command;
  return token_piece (out);
}

void
flush_text_run (scheme_tree& out, string& run) {
  if (run != "") out << stree_string (run);
  run= "";
}

bool
math_break_char (char c) {
  string breaks= "+ -:=,?;()[]{}<>/";
  return search_forwards (string (c), 0, breaks) >= 0;
}

void
append_math_plain (scheme_tree& out, string& run,
                   bool& start_alpha, bool& start_numeric,
                   char original, string rendered) {
  bool alpha= is_alpha (original);
  bool numeric= is_digit (original);
  if (original == ' ') {
    flush_text_run (out, run);
    start_alpha= start_numeric= false;
    return;
  }
  if (math_break_char (original)) {
    flush_text_run (out, run);
    if (rendered != "") out << stree_string (rendered);
    start_alpha= start_numeric= false;
    return;
  }
  if (run != "" && ((alpha && start_numeric) || (numeric && start_alpha))) {
    flush_text_run (out, run);
    start_alpha= start_numeric= false;
  }
  if (run == "") {
    start_alpha= alpha;
    start_numeric= numeric;
  }
  run << rendered;
}

bool
latex_native_operator_name (string op) {
  static const char* names[]= {
    "arccos", "arcsin", "arctan", "arg", "cos", "cosh", "cot", "coth",
    "csc", "deg", "det", "dim", "exp", "gcd", "inf", "ker", "lg",
    "lim", "liminf", "limsup", "ln", "log", "max", "min", "Pr", "sec",
    "sin", "sinh", "sup", "tan", "tanh"
  };
  return one_of (op, names, sizeof (names) / sizeof (names[0]));
}

scheme_tree
math_operator_piece (string op) {
  if (latex_export_internal::registry_operator_known (op)) {
    if (latex_native_operator_name (op)) {
      scheme_tree symbol= stree_apply ("!symbol");
      symbol << stree_apply (op);
      return symbol;
    }
    if (latex_export_add_latex_extra_package ("amsmath"))
      latex_export_recompute_dependencies ();
    scheme_tree out= stree_apply ("operatorname");
    out << stree_string (op);
    return out;
  }
  scheme_tree out= stree_apply ("tmop");
  out << stree_string (op);
  return out;
}

scheme_tree
latex_text_string_native (string s) {
  scheme_tree out (TUPLE);
  string run;
  for (int i=0; i<N(s); ) {
    char c= s[i];
    if (c == '<') {
      int j= i + 1;
      while (j < N(s) && s[j] != '>') ++j;
      string name= s (i+1, j);
      token_piece piece= latex_token_piece (name, true);
      if (piece.plain) run << piece.text;
      else { flush_text_run (out, run); out << piece.node; }
      i= j < N(s) ? j + 1 : N(s);
      continue;
    }
    if (c == ' ') {
      run << " ";
      ++i;
      while (i < N(s) && s[i] == ' ') {
        flush_text_run (out, run);
        out << stree_apply (" ");
        ++i;
      }
      continue;
    }
    if (search_forwards (string (c), 0, "#$%&_{}") >= 0) {
      flush_text_run (out, run);
      out << stree_apply (string (c));
      ++i;
      continue;
    }
    if (c == '~') { run << "\\~{}"; ++i; continue; }
    if (c == '^') { run << "\\^{}"; ++i; continue; }
    if (c == '\\') {
      flush_text_run (out, run); out << stree_apply ("textbackslash"); ++i; continue;
    }
    if (c == '`') { run << "`"; ++i; continue; }
    unsigned char uc= (unsigned char) c;
    if      (uc == 000) run << "\\`{}";
    else if (uc == 001) run << "\\'{}";
    else if (uc == 004) run << "\\\"{}";
    else if (uc == 005) run << "\\H{}";
    else if (uc == 006) run << "\\r{}";
    else if (uc == 007) run << "\\v{}";
    else if (uc == 010) run << "\\u{}";
    else if (uc == 011) run << "\\={}";
    else if (uc == 012) run << "\\.{}";
    else if (uc == 014) run << "\\k{}";
    else if (uc == 020) run << "``";
    else if (uc == 021) run << "''";
    else if (uc == 022) run << ",,";
    else if (uc == 025) run << "--";
    else if (uc == 026) run << "---";
    else if (uc == 027) run << "{}";
    else if (uc == 033) run << "ff";
    else if (uc == 034) { flush_text_run (out, run); out << stree_apply ("textbackslash"); }
    else if (uc == 035) run << "fl";
    else if (uc == 036) run << "ffi";
    else if (uc == 037) run << "ffl";
    else if (uc == 0174) { flush_text_run (out, run); out << stree_apply ("textbar"); }
    else if (latex_export_use_unicode () || latex_export_use_ascii ())
      run << convert (string (c), "Cork", "UTF-8");
    else run << string (c);
    ++i;
  }
  flush_text_run (out, run);
  return latex_tex_concat (out);
}

scheme_tree
latex_math_string_native (string s) {
  scheme_tree out (TUPLE);
  string run;
  bool start_alpha= false, start_numeric= false;
  for (int i=0; i<N(s); ) {
    char c= s[i];
    if (c == '<') {
      flush_text_run (out, run); start_alpha= start_numeric= false;
      int j= i + 1;
      while (j < N(s) && s[j] != '>') ++j;
      string name= s (i+1, j);
      token_piece piece= latex_token_piece (name, false);
      if (piece.plain)
        append_math_plain (out, run, start_alpha, start_numeric,
                           N(piece.text) == 1 ? piece.text[0] : c, piece.text);
      else out << piece.node;
      i= j < N(s) ? j + 1 : N(s);
      continue;
    }
    if (search_forwards (string (c), 0, "#$%&_{}") >= 0) {
      flush_text_run (out, run); start_alpha= start_numeric= false;
      out << stree_apply (string (c)); ++i; continue;
    }
    if (c == '~' || c == '^' || c == '*') { ++i; continue; }
    if (c == '\\') {
      flush_text_run (out, run); start_alpha= start_numeric= false;
      out << stree_apply ("backslash"); ++i; continue;
    }
    if (c == '\'' || c == '`') {
      flush_text_run (out, run); start_alpha= start_numeric= false;
      out << stree_apply (c == '\'' ? "prime" : "backprime"); ++i; continue;
    }
    if (is_alpha (c) && i+1 < N(s) && is_alpha (s[i+1])) {
      flush_text_run (out, run); start_alpha= start_numeric= false;
      int j= i + 2;
      while (j < N(s) && is_alpha (s[j])) ++j;
      out << math_operator_piece (s (i, j));
      i= j;
      continue;
    }
    string rendered= (latex_export_use_unicode () || latex_export_use_ascii ()) ?
                     convert (string (c), "Cork", "UTF-8") : string (c);
    append_math_plain (out, run, start_alpha, start_numeric, c, rendered);
    ++i;
  }
  flush_text_run (out, run);
  return latex_tex_concat (out);
}

scheme_tree
latex_string_native (string s) {
  if (N(s) > 1000) {
    array<string> chunks= tm_string_split (s);
    scheme_tree out= stree_apply ("!concat");
    for (int i=0; i<N(chunks); ++i) out << latex_string_native (chunks[i]);
    return out;
  }
  return export_math_mode () ? latex_math_string_native (s)
                             : latex_text_string_native (s);
}

string
convert_cork_chars_to_utf8 (string s) {
  string out;
  for (int i=0; i<N(s); ++i) out << convert (string (s[i]), "Cork", "UTF-8");
  return out;
}

scheme_tree
latex_verb_string_native (string s) {
  string plain;
  for (int i=0; i<N(s); ) {
    if (s[i] == '<') {
      int j= i + 1;
      while (j < N(s) && s[j] != '>') ++j;
      token_piece piece= latex_token_piece (s (i+1, j), true);
      if (piece.plain) plain << piece.text;
      i= j < N(s) ? j + 1 : N(s);
      continue;
    }
    plain << string (s[i]);
    ++i;
  }

  scheme_tree parts (TUPLE);
  if (!export_math_mode ()) parts << stree_string (plain);
  else {
    string run;
    bool start_alpha= false, start_numeric= false;
    for (int i=0; i<N(plain); ++i)
      append_math_plain (parts, run, start_alpha, start_numeric,
                         plain[i], string (plain[i]));
    flush_text_run (parts, run);
  }

  scheme_tree converted (TUPLE);
  for (int i=0; i<N(parts); ++i) {
    if (!string_atom (parts[i])) { converted << parts[i]; continue; }
    string value= atom_text (parts[i]);
    if (latex_export_use_unicode () || latex_export_use_ascii ())
      value= convert_cork_chars_to_utf8 (value);
    else {
      value= replace (value, "<less>", "<");
      value= replace (value, "<gtr>", ">");
    }
    converted << stree_string (value);
  }
  return latex_tex_concat (converted);
}

string
verbatim_tree_text (scheme_tree t) {
  if (string_atom (t)) return atom_text (t);
  if (head_is (t, "!concat")) {
    string out;
    for (int i=1; i<N(t); ++i) out << verbatim_tree_text (t[i]);
    return out;
  }
  return "";
}

string tt_native (scheme_tree x);

string
tt_document_native (scheme_tree x) {
  string out;
  int first= head_is (x, "document") || head_is (x, "para") ? 1 : 0;
  for (int i=first; i<N(x); ++i) {
    if (i > first) out << "\n";
    out << tt_native (x[i]);
  }
  return out;
}

string
tt_native (scheme_tree x) {
  if (string_atom (x)) return verbatim_tree_text (latex_verb_string_native (atom_text (x)));
  if (func_is (x, "next-line", 0)) return "\n";
  if (head_is (x, "document") || head_is (x, "para")) return tt_document_native (x);
  if (head_is (x, "concat")) {
    string out;
    for (int i=1; i<N(x); ++i) out << tt_native (x[i]);
    return out;
  }
  if (func_is (x, "mtm", 2)) return tt_native (x[N(x)-1]);
  if (func_is (x, "surround", 3))
    return tt_native (x[1]) * tt_native (x[3]) * tt_native (x[2]);
  if (func_is (x, "hgroup", 1) || func_is (x, "vgroup", 1)) return tt_native (x[1]);
  if (head_is (x, "with")) {
    cout << "ATHENA] lost <with> in verbatim content: " << scheme_tree_to_tree (x) << "\n";
    return N(x) > 1 ? tt_native (x[N(x)-1]) : string ("");
  }
  if (head_is (x, "math")) {
    cout << "ATHENA] lost <math> in verbatim content: " << scheme_tree_to_tree (x) << "\n";
    return N(x) > 1 ? tt_native (x[N(x)-1]) : string ("");
  }
  cout << "ATHENA] non converted verbatim content: " << scheme_tree_to_tree (x) << "\n";
  return "";
}


} // namespace

scheme_tree
latex_export_string (string value) {
  return latex_string_native (value);
}

scheme_tree
latex_export_verb_string (string value) {
  return latex_verb_string_native (value);
}

string
latex_export_tt (scheme_tree value) {
  return tt_native (value);
}

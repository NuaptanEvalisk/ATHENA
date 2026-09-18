/******************************************************************************
* MODULE     : totex_serializer.cpp
* DESCRIPTION: Native serialization of intermediate LaTeX trees
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "Tex/convert_tex.hpp"
#include "convert.hpp"
#include "converter.hpp"
#include "scheme.hpp"
#include "Scheme/Scheme/glue.hpp"
#include "Scheme/Scheme/object.hpp"
#include "base64.hpp"
#include "config.h"

namespace {

bool stree_list (scheme_tree t) { return is_tuple (t); }

string atom_text (scheme_tree t) {
  if (!is_atomic (t)) return "";
  return is_quoted (t->label) ? scm_unquote (t->label) : t->label;
}

bool string_atom (scheme_tree t) {
  return is_atomic (t) && is_quoted (t->label);
}

bool head_is (scheme_tree t, string head) {
  return stree_list (t) && N(t) > 0 && is_atomic (t[0]) &&
         t[0]->label == head;
}

bool func_is (scheme_tree t, string head, int arity= -1) {
  return head_is (t, head) && (arity < 0 || N(t) == arity + 1);
}

scheme_tree make_list (string head) {
  scheme_tree r (TUPLE);
  r << tree (head);
  return r;
}

scheme_tree quoted (string value) { return tree (scm_quote (value)); }

object stree_object (scheme_tree t) {
  return tmscm_to_object (scheme_tree_to_tmscm (t));
}

string strip_dashes (string s) {
  string r;
  for (int i=0; i<N(s); ++i) if (s[i] != '-') r << s[i];
  return r;
}

bool contains_text (string s, string needle) {
  return search_forwards (needle, 0, s) >= 0;
}

string repeat_spaces (int n) {
  string r;
  for (int i=0; i<n; ++i) r << " ";
  return r;
}

class LatexOutput {
  string accu;
  bool comment= false;
  int indentation= 0;
  int count= 0;
  bool start= true;
  bool pending_space= false;
  bool breakable= true;
  string tail;
  bool exact= false;
  int line_length= 79;

public:
  void raw (string s) {
    if (s != "") { start= false; breakable= true; }
    accu << s;
  }

  void line_return () {
    start= true;
    int indent= max (0, min (40, indentation));
    accu << "\n" << (exact ? string ("") : repeat_spaces (indent));
    if (comment) accu << "% ";
    count= indent;
  }

  void prepared (string s) {
    if (s == "" && !pending_space) return;
    if (pending_space) count++;
    count += N(s);
    if (pending_space) breakable= true;
    if (count < line_length || start || !breakable || exact) {
      if (pending_space && !start) raw (" ");
      raw (s);
    }
    else {
      line_return ();
      count += N(s);
      raw (s);
    }
    pending_space= false;
  }

  void text (string s) {
    s= tail * s;
    int begin= 0;
    for (int i=0; i<N(s); ++i) {
      if (s[i] != ' ') continue;
      prepared (s (begin, i));
      pending_space= true;
      begin= i + 1;
    }
    tail= s (begin, N(s));
  }

  void flush () {
    prepared (tail);
    tail= "";
  }

  void verbatim (string s) {
    flush ();
    raw (s);
    breakable= false;
  }

  void verb (string s) {
    if (tail != "") flush ();
    prepared (s);
  }

  void lf_verbatim (string s) {
    flush ();
    if (!start) raw ("\n");
    raw (s);
    breakable= false;
  }

  void remove_indentation () {
    if (tail != "") {
      while (ends (tail, " ")) tail= tail (0, N(tail)-1);
      return;
    }
    while (ends (accu, " ")) accu= accu (0, N(accu)-1);
  }

  void invariant (string s) {
    remove_indentation ();
    lf_verbatim (s);
  }

  void lf () {
    if (tail != "") flush ();
    line_return ();
  }

  void marker (string s) {
    if (tail != "") flush ();
    accu << s;
  }

  bool test_end (string s) const { return ends (tail, s); }

  void remove_tail (int n) {
    if (N(tail) >= n) tail= tail (0, N(tail)-n);
  }

  void set_comment (bool on) { comment= on; }
  int indent () const { return indentation; }
  void set_indent (int n) { indentation= n; }
  void add_indent (int n) { flush (); indentation += n; }

  string produce () {
    flush ();
    return accu;
  }
};

bool latex_multiline (scheme_tree x) {
  if (!stree_list (x) || N(x) == 0) return false;
  if (stree_list (x[0]) && N(x[0]) > 0 &&
      (head_is (x[0], "!begin") || head_is (x[0], "!begin*"))) return true;
  string h= is_atomic (x[0]) ? x[0]->label : string ("");
  if (h == "!begin" || h == "!nextline" || h == "!newline" ||
      h == "!linefeed" || h == "!eqn" || h == "!table") return true;
  if ((h == "!document" || h == "!paragraph") && N(x) > 2) return true;
  if (N(x) <= 1) return false;
  if (latex_multiline (x[1])) return true;
  scheme_tree rest= make_list ("!concat");
  for (int i=2; i<N(x); ++i) rest << x[i];
  return latex_multiline (rest);
}

bool single_symbol_list (scheme_tree t, string symbol) {
  return stree_list (t) && N(t) == 1 && is_atomic (t[0]) &&
         !is_quoted (t[0]->label) && t[0]->label == symbol;
}

bool latex_empty (scheme_tree x) {
  if (string_atom (x) && atom_text (x) == "") return true;
  if (func_is (x, "!concat")) {
    for (int i=1; i<N(x); ++i) if (!latex_empty (x[i])) return false;
    return true;
  }
  return func_is (x, "!document", 1) && latex_empty (x[1]);
}

bool double_math (scheme_tree x) {
  if ((func_is (x, "!document", 1) || func_is (x, "!concat", 1)) &&
      double_math (x[1])) return true;
  if (!stree_list (x) || N(x) != 2 || !stree_list (x[0]) || N(x[0]) != 2)
    return false;
  if (!head_is (x[0], "!begin")) return false;
  string env= atom_text (x[0][1]);
  return env == "eqnarray" || env == "eqnarray*" || env == "leqnarray*";
}

bool empty_line (scheme_tree x) {
  if (string_atom (x) && atom_text (x) == "") return true;
  if (func_is (x, "!marker")) return true;
  if (func_is (x, "!concat")) {
    for (int i=1; i<N(x); ++i) if (!empty_line (x[i])) return false;
    return true;
  }
  return false;
}

bool stree_contains (scheme_tree t, string needle) {
  if (string_atom (t)) {
    string compact;
    for (int i=0; i<N(needle); ++i) if (needle[i] != ' ') compact << needle[i];
    return contains_text (atom_text (t), compact);
  }
  if (!stree_list (t)) return false;
  for (int i=0; i<N(t); ++i) if (stree_contains (t[i], needle)) return true;
  return false;
}

bool attached_macro (scheme_tree t) {
  return func_is (t, "!concat", 4) && stree_list (t[1]) &&
         func_is (t[1], "!preamble", 1) && string_atom (t[1][1]) &&
         atom_text (t[1][1]) == "%%%%%%%%%% Start TeXmacs macros\n";
}

scheme_tree detach_macros (scheme_tree t) {
  if (attached_macro (t)) return t[4];
  if (!stree_list (t)) return t;
  scheme_tree r (TUPLE);
  for (int i=0; i<N(t); ++i) r << detach_macros (t[i]);
  return r;
}

bool uses_cyrillic (scheme_tree t, bool ascii) {
  if (!ascii) return false;
  if (func_is (t, "!widechar", 1)) {
    string s= convert (atom_text (t[1]), "UTF-8", "LaTeX");
    return starts (s, "{\\cyr") || starts (s, "{\\CYR");
  }
  if (!stree_list (t)) return false;
  for (int i=1; i<N(t); ++i) if (uses_cyrillic (t[i], ascii)) return true;
  return false;
}

bool uses_xlatin (scheme_tree t, bool ascii) {
  if (!ascii) return false;
  if (string_atom (t)) {
    string raw= atom_text (t);
    string s= convert (raw, "UTF-8", "LaTeX");
    return s != raw && contains_text (s, "{\\k ") &&
           (contains_text (s, "{\\k a}") || contains_text (s, "{\\k e}") ||
            contains_text (s, "{\\k A}") || contains_text (s, "{\\k E}"));
  }
  if (!stree_list (t)) return false;
  for (int i=1; i<N(t); ++i) if (uses_xlatin (t[i], ascii)) return true;
  return false;
}

class LatexSerializer {
  LatexOutput out;
  bool ascii= false;
  bool unicode= true;

  void output_tex (string s) {
    out.text (ascii ? convert (s, "UTF-8", "LaTeX") : s);
  }

  bool want_space (scheme_tree x1, scheme_tree x2) const {
    string a= string_atom (x1) ? atom_text (x1) : string ("");
    string b= string_atom (x2) ? atom_text (x2) : string ("");
    bool forbid=
      (a != "" && (ends (a, "(") || ends (a, "["))) ||
      single_symbol_list (x1, "{") || single_symbol_list (x1, "nobreak") ||
      (b != "" && (starts (b, ",") || starts (b, ")") || starts (b, "]"))) ||
      single_symbol_list (x2, "}") || single_symbol_list (x2, "nobreak") ||
      a == " " || b == " " || func_is (x2, "!nextline") || b == "'" ||
      func_is (x2, "!sub") || func_is (x2, "!sup") || func_is (x1, "&") ||
      func_is (x2, "&") || func_is (x1, "!nbsp") || func_is (x2, "!nbsp") ||
      func_is (x1, "!nbhyph") || func_is (x2, "!nbhyph") ||
      (a == "'" && !stree_list (x2));
    if (forbid) return false;
    if (a == "," || a == ";" || a == ":" || func_is (x1, "tmop") ||
        func_is (x2, "tmop") || func_is (x1, "!symbol") ||
        func_is (x2, "!symbol")) return true;
    if (stree_list (x1) && N(x1) == 1 && is_atomic (x1[0]) &&
        !is_quoted (x1[0]->label) && is_alpha (x1[0]->label) &&
        string_atom (x2) && atom_text (x2) != "") return true;
    return !stree_list (x1) && !stree_list (x2);
  }

  void args (scheme_tree x, int first) {
    for (int i=first; i<N(x); ++i) {
      if (func_is (x[i], "!option", 1)) {
        output_tex ("["); serialize (x[i][1]); output_tex ("]");
      }
      else {
        output_tex ("{"); serialize (x[i]); output_tex ("}");
      }
    }
  }

  void apply (scheme_tree x) {
    string command= atom_text (x[0]);
    if (!string_atom (x[0])) command= "\\" * command;
    output_tex (command);
    args (x, 1);
  }

  void concat (scheme_tree x) {
    bool have_prev= false;
    scheme_tree prev;
    for (int i=1; i<N(x); ++i) {
      if (func_is (x[i], "!marker")) { serialize (x[i]); continue; }
      if (have_prev && want_space (prev, x[i])) serialize (quoted (" "));
      serialize (x[i]);
      prev= x[i]; have_prev= true;
    }
  }

  void script (string where, scheme_tree x) {
    if (N(x) < 2) return;
    scheme_tree arg= x[1];
    if (where == "^" && single_symbol_list (arg, "prime")) { output_tex ("'"); return; }
    if (where == "^" && func_is (arg, "!concat") && N(arg) > 1) {
      bool primes= true;
      for (int i=1; i<N(arg); ++i) primes= primes && single_symbol_list (arg[i], "prime");
      if (primes) { for (int i=1; i<N(arg); ++i) output_tex ("'"); return; }
    }
    if (string_atom (arg) && N(atom_text (arg)) == 1 &&
        atom_text (arg) != "<" && atom_text (arg) != ">") {
      output_tex (where); output_tex (atom_text (arg)); return;
    }
    output_tex (where);
    scheme_tree temp= make_list ("args");
    for (int i=1; i<N(x); ++i) temp << x[i];
    args (temp, 1);
  }

  void environment (scheme_tree descriptor, scheme_tree inside, bool starred) {
    if (N(descriptor) < 2) return;
    string env= strip_dashes (atom_text (descriptor[1]));
    output_tex ("\\begin{" * env * "}");
    args (descriptor, 2);
    if (env == "tmparmod" || env == "tmparsep") output_tex ("%");
    if (starred) out.lf ();
    else { out.add_indent (2); out.lf (); }
    serialize (inside);
    if (!starred) out.add_indent (-2);
    out.lf ();
    output_tex ("\\end{" * env * "}");
  }

  string athena_record (string cmd, const array<string>& vals) const {
    string r= "ATHENA-DATA cmd=\"" * cmd * "\" val=(";
    for (int i=0; i<N(vals); ++i) {
      if (i) r << ", ";
      r << "\"" << vals[i] << "\"";
    }
    return r * ")";
  }

  void encoded_metadata (string key, scheme_tree st) {
    tree converted= scheme_tree_to_tree (st);
    string payload= replace (encode_base64 (tree_to_texmacs (converted)), "\n", "");
    array<string> vals; vals << key << as_string (N(payload)) << payload;
    out.verbatim ("% " * athena_record ("aux", vals) * "\n");
  }

  void header () {
    out.verbatim ("% IMPORTANT: This LaTeX file is exported by ATHENA. Please do NOT change ANY ATHENA-DATA record to avoid loss of ATHENA interoperability.\n");
    out.verbatim ("% ATHENA is free software. To learn more about ATHENA please visit https://athena.evalisk.org/\n");
    out.verbatim ("% ATHENA-DATA cmd=\"version\" val=(\"" * string (ATHENA_VERSION) * "\")\n");
  }

  scheme_tree concat_document (scheme_tree preamble, scheme_tree body) {
    scheme_tree r= make_list ("!concat");
    if (stree_list (preamble))
      for (int i=0; i<N(preamble); ++i) r << preamble[i];
    r << body;
    return r;
  }

  array<string> preamble_data (scheme_tree text, scheme_tree style,
                               scheme_tree lan, scheme_tree init,
                               scheme_tree colors, scheme_tree colormaps) {
    return latex_export_preamble_data (text, style, lan, init, colors, colormaps);
  }

  void file (scheme_tree x) {
    if (N(x) < 6) { apply (x); return; }
    scheme_tree body= x[1];
    bool has_preamble= stree_contains (body, "\\begin{document}");
    bool has_end= stree_contains (body, "\\end{document}");
    scheme_tree styles= x[2];
    scheme_tree style= (stree_list (styles) && N(styles) > 0) ? styles[0] : quoted ("article");
    string style_name= stree_list (style) && N(style) > 0 ? atom_text (style[N(style)-1]) : atom_text (style);
    scheme_tree needs= x[3];
    scheme_tree prelan= (stree_list (needs) && N(needs) > 0) ? needs[0] : quoted ("");
    scheme_tree colors= (stree_list (needs) && N(needs) > 1) ? needs[1] : scheme_tree (TUPLE);
    scheme_tree colormaps= (stree_list (needs) && N(needs) > 2) ? needs[2] : scheme_tree (TUPLE);
    scheme_tree lan= prelan;
    if (string_atom (prelan) && atom_text (prelan) == "") {
      lan= scheme_tree (TUPLE); lan << quoted ("english");
    }
    else if (!stree_list (lan)) {
      scheme_tree one (TUPLE); one << lan; lan= one;
    }
    scheme_tree init= x[4];
    scheme_tree doc_preamble= x[5];
    int source_begin= 6;
    scheme_tree metadata;
    bool have_metadata= false;
    if (source_begin < N(x) && func_is (x[source_begin], "!athena-file-metadata", 2)) {
      metadata= x[source_begin++]; have_metadata= true;
    }
    string post_begin, pre_end;

    if (!has_preamble) {
      body= detach_macros (body);
      scheme_tree misc= concat_document (doc_preamble, body);
      array<string> pre= preamble_data (misc, style, lan, init, colors, colormaps);
      header ();
      if (have_metadata) {
        encoded_metadata ("document_style", metadata[1]);
        encoded_metadata ("document_initial", metadata[2]);
      }
      out.verbatim ("\\documentclass" * pre[0] * "{" * style_name * "}\n");
      out.verbatim ("\\long\\def\\INLINE_COMMENT#1{}\n");
      string main_lang= N(lan) > 0 ? atom_text (lan[N(lan)-1]) : string ("english");
      if (main_lang == "korean") out.verbatim ("\\usepackage{hangul}\n");
      else if (main_lang == "chinese" || main_lang == "chineset" || main_lang == "japanese") {
        string opt= main_lang == "japanese" ? "{min}" :
                    main_lang == "chineset" ? "{bsmi}" : "{gbsn}";
        post_begin= "\\begin{CJK*}{UTF8}" * opt * "\n";
        pre_end= "\n\\end{CJK*}";
        out.verbatim ("\\usepackage{CJK}\n");
      }
      else {
        if (uses_cyrillic (doc_preamble, ascii) || uses_cyrillic (body, ascii))
          out.verbatim ("\\usepackage[T2A,T1]{fontenc}\n");
        else if (uses_xlatin (doc_preamble, ascii) || uses_xlatin (body, ascii))
          out.verbatim ("\\usepackage[T1]{fontenc}\n");
        string langs;
        for (int i=0; i<N(lan); ++i) {
          if (i) langs << ", ";
          langs << atom_text (lan[i]);
        }
        out.verbatim ("\\usepackage[" * langs * "]{babel}\n");
        if (unicode) out.verbatim ("\\usepackage[utf8]{inputenc}\n");
      }
      if ((starts (style_name, "acm") || starts (style_name, "sig")) &&
          contains_text (pre[1], "amssymb")) out.verbatim ("\\let\\Bbbk\\relax\n");
      out.verbatim (pre[1]);
      string packages= latex_export_use_package_command (
        body, null_object (), null_object ());
      if (contains_text (packages, "makeidx")) out.verbatim ("\\makeindex\n");
      out.verbatim (pre[2]);
      if (pre[3] != "") {
        out.lf ();
        out.verbatim ("%%%%%%%%%% Start TeXmacs macros\n" * pre[3] *
                      "%%%%%%%%%% End TeXmacs macros\n");
      }
      if (stree_list (doc_preamble) && N(doc_preamble) > 0) {
        out.lf ();
        for (int i=0; i<N(doc_preamble); ++i) { serialize (doc_preamble[i]); out.lf (); }
      }
      out.lf (); output_tex ("\\begin{document}"); out.lf ();
      output_tex (post_begin); out.lf ();
    }
    else header ();
    serialize (body);
    if (!has_end) {
      out.lf (); output_tex (pre_end); out.lf ();
      output_tex ("\\end{document}"); out.lf ();
    }
    if (source_begin < N(x)) serialize (x[source_begin]);
  }

public:
  LatexSerializer () {
    ascii= latex_export_use_ascii ();
    unicode= latex_export_use_unicode ();
  }

  void serialize (scheme_tree x) {
    if (string_atom (x)) { output_tex (atom_text (x)); return; }
    if (!stree_list (x) || N(x) == 0) return;
    if (stree_list (x[0]) && head_is (x[0], "!begin") && N(x)>1) {
      environment (x[0], x[1], false);
      return;
    }
    if (stree_list (x[0]) && head_is (x[0], "!begin*") && N(x)>1) {
      environment (x[0], x[1], true);
      return;
    }
    if (!is_atomic (x[0])) return;
    string h= x[0]->label;
    if (h == "!widechar") { if (N(x)>1) output_tex (atom_text (x[1])); }
    else if (h == "!file") file (x);
    else if (h == "!preamble") { if (N(x)>1) out.verbatim (atom_text (x[1])); }
    else if (h == "!athena-data") { if (N(x)>1) out.verbatim ("% " * atom_text (x[1]) * "\n"); }
    else if (h == "!athena-data-inline") {
      out.flush ();
      if (N(x)>1 && stree_list (x[1]))
        for (int i=0; i<N(x[1]); ++i)
          out.verbatim ("\\INLINE_COMMENT{" * atom_text (x[1][i]) * "}");
    }
    else if (h == "!athena-latex-raw") { if (N(x)>1) out.verbatim (atom_text (x[1])); }
    else if (h == "!comment") {
      out.set_comment (true); output_tex ("% "); if (N(x)>1) serialize (x[1]);
      out.set_comment (false); out.lf ();
    }
    else if (h == "!document") {
      for (int i=1; i<N(x); ++i) {
        serialize (x[i]); if (empty_line (x[i])) output_tex ("\\ ");
        if (i+1<N(x)) { out.lf (); out.lf (); }
      }
    }
    else if (h == "!paragraph") {
      for (int i=1; i<N(x); ++i) { serialize (x[i]); if (i+1<N(x)) out.lf (); }
    }
    else if (h == "!table") {
      for (int i=1; i<N(x); ++i) {
        if (func_is (x[i], "!row")) {
          if (N(x[i]) > 1 && string_atom (x[i][1]) && starts (atom_text (x[i][1]), "["))
            output_tex ("{}");
          else if (N(x[i]) > 1 && func_is (x[i][1], "!concat") && N(x[i][1]) > 1 &&
                   string_atom (x[i][1][1]) && starts (atom_text (x[i][1][1]), "["))
            output_tex ("{}");
          for (int j=1; j<N(x[i]); ++j) { serialize (x[i][j]); if (j+1<N(x[i])) output_tex (" & "); }
          if (i+1<N(x)) { output_tex ("\\\\"); out.lf (); }
        }
        else { serialize (x[i]); if (i+1<N(x)) out.lf (); }
      }
    }
    else if (h == "!concat") concat (x);
    else if (h == "!append") { for (int i=1; i<N(x); ++i) serialize (x[i]); }
    else if (h == "!symbol") { if (N(x)>1) serialize (x[1]); }
    else if (h == "!linefeed") out.lf ();
    else if (h == "!indent") {
      if (N(x)>1) {
        if (latex_multiline (x[1])) { out.add_indent (2); out.lf (); serialize (x[1]); out.add_indent (-2); out.lf (); }
        else serialize (x[1]);
      }
    }
    else if (h == "!unindent") {
      int old= out.indent (); out.set_indent (0); if (N(x)>1) serialize (x[1]); out.set_indent (old);
    }
    else if (h == "!newline") { out.lf (); out.lf (); }
    else if (h == "!nextline") { output_tex ("\\\\"); out.lf (); }
    else if (h == "!nbsp") output_tex ("~");
    else if (h == "!nbhyph") output_tex ("\\mbox{-}");
    else if (h == "!verb") {
      string v= N(x)>1 ? atom_text (x[1]) : string ("");
      const char* ds[]= {"|", "$", "@", "!", "9", "X"};
      string d= "ď";
      for (auto cand: ds) if (!contains_text (v, cand)) { d= cand; break; }
      out.verb ("\\verb" * d * v * d);
    }
    else if (h == "!verbatim") { if (N(x)>1) out.lf_verbatim ("\\begin{alltt}\n" * atom_text (x[1]) * "\n\\end{alltt}"); }
    else if (h == "!verbatim*") { if (N(x)>1) out.lf_verbatim (atom_text (x[1])); }
    else if (h == "!invariant") { if (N(x)>1) out.invariant (atom_text (x[1])); }
    else if (h == "!arg") { if (N(x)>1) output_tex ("#" * atom_text (x[1])); }
    else if (h == "!group") { output_tex ("{"); for (int i=1;i<N(x);++i) serialize (x[i]); output_tex ("}"); }
    else if (h == "!marker") {
      if (N(x)>2) out.marker ("{\\" * atom_text (x[1]) * "{" * atom_text (x[2]) * "}}");
    }
    else if (h == "!math") {
      if (N(x)>1 && !latex_empty (x[1])) {
        if (double_math (x[1])) serialize (x[1]);
        else if (func_is (x[1], "!begin", 1) && atom_text (x[1][1]) == "center") {
          scheme_tree d= make_list ("!begin"); d << quoted ("equation");
          scheme_tree w (TUPLE); w << d << x[1][1]; serialize (w);
        }
        else if (out.test_end ("$") && !out.test_end ("\\$")) {
          out.remove_tail (1); output_tex (" "); serialize (x[1]); output_tex ("$");
        }
        else { output_tex ("$"); serialize (x[1]); output_tex ("$"); }
      }
    }
    else if (h == "!eqn") { output_tex ("\\[ "); out.add_indent (3); if (N(x)>1) serialize (x[1]); out.add_indent (-3); output_tex (" \\]"); }
    else if (h == "!sub") script ("_", x);
    else if (h == "!sup") script ("^", x);
    else if (h == "!annotate") { if (N(x)>1) serialize (x[1]); }
    else if (h == "!ignore") return;
    else apply (x);
  }

  string result () { return out.produce (); }
};

} // namespace

string
serialize_latex (scheme_tree t) {
  LatexSerializer serializer;
  serializer.serialize (t);
  return serializer.result ();
}

bool
latex_stree_multiline (scheme_tree t) {
  return latex_multiline (t);
}

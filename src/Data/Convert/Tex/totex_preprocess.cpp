/******************************************************************************
* MODULE     : totex_preprocess.cpp
* DESCRIPTION: Deterministic tree preprocessing for native LaTeX export
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "totex_internal.hpp"
#include "hashmap.hpp"
#include "iterator.hpp"

namespace {

using namespace latex_export_internal;

bool
macro_definition (scheme_tree x) {
  return func_is (x, "assign", 2) && string_atom (x[1]) &&
         func_is (x[2], "macro");
}

scheme_tree
comment_preamble (scheme_tree t) {
  if (string_atom (t)) {
    scheme_tree r= stree_apply ("!comment");
    r << t;
    return r;
  }
  if (head_is (t, "para") || head_is (t, "concat") ||
      head_is (t, "document")) {
    scheme_tree r (TUPLE);
    for (int i=0; i<N(t); ++i) r << comment_preamble (t[i]);
    return r;
  }
  return t;
}

scheme_tree
filter_preamble (scheme_tree l) {
  scheme_tree out (TUPLE);
  if (!stree_list (l) || N(l) == 0) return out;
  if (macro_definition (l)) {
    out << l;
    return out;
  }
  if (func_is (l, "hide-preamble", 1) && stree_list (l[1]) && N(l[1]) > 0) {
    for (int i=1; i<N(l[1]); ++i) out << comment_preamble (l[1][i]);
    return out;
  }
  for (int i=1; i<N(l); ++i) {
    scheme_tree part= filter_preamble (l[i]);
    for (int j=0; j<N(part); ++j) out << part[j];
  }
  return out;
}

scheme_tree
filter_body (scheme_tree l) {
  if (!stree_list (l) || N(l) == 0) return l;
  if (head_is (l, "assign") || head_is (l, "hide-preamble")) {
    scheme_tree r= stree_apply ("athena-preserved-object");
    r << l;
    return r;
  }
  if (head_is (l, "concat") || head_is (l, "document")) {
    scheme_tree r= stree_apply (l[0]->label);
    for (int i=1; i<N(l); ++i) {
      scheme_tree child= filter_body (l[i]);
      if (!empty_string_stree (child)) r << child;
    }
    if (N(r) == 1) {
      if (l[0]->label == "concat") return stree_string ("");
      r << stree_string ("");
    }
    return r;
  }
  scheme_tree r (TUPLE);
  r << l[0];
  for (int i=1; i<N(l); ++i) r << filter_body (l[i]);
  return r;
}

scheme_tree
filter_duplicates_list (scheme_tree l, hashmap<string,bool>& seen) {
  scheme_tree out (TUPLE);
  if (!stree_list (l)) return out;
  array<scheme_tree> reversed;
  for (int i=N(l)-1; i>=0; --i) {
    scheme_tree item= l[i];
    if (func_is (item, "assign", 2)) {
      string var= atom_text (item[1]);
      if (seen[var]) continue;
      seen (var)= true;
      reversed << item;
    }
    else if (head_is (item, "concat") || head_is (item, "para") ||
             head_is (item, "document")) {
      scheme_tree children (TUPLE);
      for (int j=1; j<N(item); ++j) children << item[j];
      scheme_tree filtered= filter_duplicates_list (children, seen);
      scheme_tree rebuilt= stree_apply (item[0]->label);
      for (int j=0; j<N(filtered); ++j) rebuilt << filtered[j];
      reversed << rebuilt;
    }
    else reversed << item;
  }
  for (int i=N(reversed)-1; i>=0; --i) out << reversed[i];
  return out;
}

scheme_tree
clean_body (scheme_tree b) {
  if (!head_is (b, "!document") || N(b) <= 1) return b;
  scheme_tree first= b[1];
  if (!func_is (first, "!document", 1) || !empty_string_stree (first[1]))
    return b;
  scheme_tree r= stree_apply ("!document");
  for (int i=2; i<N(b); ++i) r << b[i];
  return r;
}

scheme_tree tex_concat_native (scheme_tree l);

scheme_tree
concat_list_flatten (scheme_tree l) {
  scheme_tree out (TUPLE);
  if (!stree_list (l)) return out;
  for (int i=0; i<N(l); ++i) {
    scheme_tree item= l[i];
    if (empty_string_stree (item)) continue;
    if (head_is (item, "!concat")) {
      for (int j=1; j<N(item); ++j) out << item[j];
    }
    else out << item;
  }
  return out;
}

scheme_tree
concat_similar (scheme_tree l) {
  if (!stree_list (l) || N(l) <= 1) return l;
  if (N(l) > 1000) {
    int split= N(l) / 2;
    scheme_tree left= stree_apply ("!concat");
    scheme_tree right= stree_apply ("!concat");
    for (int i=0; i<split; ++i) left << l[i];
    for (int i=split; i<N(l); ++i) right << l[i];
    scheme_tree pair (TUPLE);
    pair << left << right;
    return concat_similar (pair);
  }

  scheme_tree tail (TUPLE);
  for (int i=1; i<N(l); ++i) tail << l[i];
  scheme_tree rest= concat_similar (tail);
  scheme_tree first= l[0];
  if (N(rest) > 0 &&
      ((func_is (first, "!sub", 1) && func_is (rest[0], "!sub", 1)) ||
       (func_is (first, "!sup", 1) && func_is (rest[0], "!sup", 1)))) {
    scheme_tree args (TUPLE);
    args << first[1] << rest[0][1];
    scheme_tree merged= stree_apply (first[0]->label);
    merged << tex_concat_native (args);
    scheme_tree out (TUPLE);
    out << merged;
    for (int i=1; i<N(rest); ++i) out << rest[i];
    return out;
  }

  scheme_tree out (TUPLE);
  out << first;
  for (int i=0; i<N(rest); ++i) out << rest[i];
  return out;
}

scheme_tree
tex_concat_native (scheme_tree l) {
  scheme_tree normalized= concat_similar (concat_list_flatten (l));
  if (N(normalized) == 0) return stree_string ("");
  if (N(normalized) == 1) return normalized[0];
  scheme_tree out= stree_apply ("!concat");
  for (int i=0; i<N(normalized); ++i) out << normalized[i];
  return out;
}

scheme_tree
concat_strings (scheme_tree l) {
  scheme_tree out (TUPLE);
  if (!stree_list (l)) return out;
  for (int i=0; i<N(l); ++i) {
    scheme_tree item= l[i];
    if (N(out) > 0 && string_atom (out[N(out)-1]) && string_atom (item)) {
      string merged= atom_text (out[N(out)-1]) * atom_text (item);
      scheme_tree next (TUPLE);
      for (int j=0; j<N(out)-1; ++j) next << out[j];
      next << stree_string (merged);
      out= next;
    }
    else out << item;
  }
  return out;
}

scheme_tree
concat_with_separator (scheme_tree l, string separator_name) {
  scheme_tree result (TUPLE);
  if (!stree_list (l) || N(l) == 0) return result;
  scheme_tree concat= stree_apply ("!concat");
  scheme_tree separator= stree_apply ("!concat");
  scheme_tree sep_command= stree_apply (separator_name);
  separator << sep_command << stree_string (" ");
  for (int i=0; i<N(l); ++i) {
    if (i != 0) concat << separator;
    concat << l[i];
  }
  result << concat;
  return result;
}

bool
user_definition (scheme_tree t) {
  return (func_is (t, "new-theorem", 2) || func_is (t, "assign", 2)) &&
         string_atom (t[1]);
}

void
collect_user_defs (scheme_tree t, hashmap<string,bool>& defs) {
  if (!stree_list (t) || N(t) == 0) return;
  if (user_definition (t)) {
    defs (atom_text (t[1]))= true;
    return;
  }
  for (int i=1; i<N(t); ++i) collect_user_defs (t[i], defs);
}

bool
stree_name_list_contains (scheme_tree names, string name) {
  if (!stree_list (names)) return false;
  for (int i=0; i<N(names); ++i)
    if (string_atom (names[i]) && atom_text (names[i]) == name) return true;
  return false;
}

scheme_tree
filter_style_macros (scheme_tree t, scheme_tree protected_names) {
  if (!stree_list (t)) return t;
  scheme_tree r (TUPLE);
  for (int i=0; i<N(t); ++i) {
    scheme_tree child= t[i];
    if (macro_definition (child) &&
        stree_name_list_contains (protected_names, atom_text (child[1])))
      continue;
    r << filter_style_macros (child, protected_names);
  }
  return r;
}

scheme_tree
filter_style_macros_native (scheme_tree t) {
  if (!stree_list (t)) return t;
  scheme_tree r (TUPLE);
  for (int i=0; i<N(t); ++i) {
    scheme_tree child= t[i];
    if (macro_definition (child) && string_atom (child[1]) &&
        latex_export_known_tmtex_name (atom_text (child[1])))
      continue;
    r << filter_style_macros_native (child);
  }
  return r;
}

scheme_tree
apply_initial_environment (scheme_tree body, scheme_tree init) {
  if (!head_is (init, "collection")) return body;
  for (int i=1; i<N(init); ++i) {
    scheme_tree a= init[i];
    if (!func_is (a, "associate", 2) || !string_atom (a[1])) continue;
    if (atom_text (a[1]) != "language") continue;
    if (string_atom (a[2]) && atom_text (a[2]) == "verbatim") {
      scheme_tree r= stree_apply ("verbatim");
      r << body;
      return r;
    }
    return body;
  }
  return body;
}

bool
no_space_before (scheme_tree x) {
  if (head_is (x, "!sub") || head_is (x, "!sup")) return true;
  if (string_atom (x)) {
    string s= atom_text (x);
    if (N(s) == 0) return false;
    return s[0] == '\'' || s[0] == ',' || s[0] == ')' || s[0] == ']';
  }
  return head_is (x, "!concat") && N(x) > 1 && no_space_before (x[1]);
}

bool
no_space_after (scheme_tree x) {
  if (string_atom (x)) {
    string s= atom_text (x);
    if (N(s) == 0) return false;
    return s[0] == '(' || s[0] == '[';
  }
  return head_is (x, "!concat") && N(x) > 1 && no_space_after (x[N(x)-1]);
}

scheme_tree
math_concat_spaces (scheme_tree l) {
  if (!stree_list (l) || N(l) <= 1) return l;
  scheme_tree out (TUPLE);
  for (int i=0; i<N(l); ++i) {
    if (i > 0 && !no_space_after (l[i-1]) && !no_space_before (l[i]))
      out << stree_string (" ");
    out << l[i];
  }
  return out;
}

scheme_tree
rewrite_no_break (scheme_tree l) {
  if (!stree_list (l)) return l;
  scheme_tree out (TUPLE);
  for (int i=0; i<N(l); ++i) {
    if (i + 1 < N(l) && string_atom (l[i]) &&
        func_is (l[i+1], "no-break", 0)) {
      string s= atom_text (l[i]);
      if (N(s) > 0 && s[N(s)-1] == ' ') {
        s= s (0, N(s)-1);
        if (N(s) > 0) out << stree_string (s);
        out << stree_apply ("!nbsp");
        ++i;
        continue;
      }
    }
    out << l[i];
  }
  return out;
}

bool
check_double_script (scheme_tree l, bool sub, bool sup) {
  if (!stree_list (l) || N(l) == 0 || !stree_list (l[0]) || N(l[0]) == 0)
    return false;
  string h= is_atomic (l[0][0]) ? l[0][0]->label : "";
  scheme_tree tail (TUPLE);
  for (int i=1; i<N(l); ++i) tail << l[i];
  if (h == "rsub") return sub || check_double_script (tail, true, sup);
  if (h == "rsup" || h == "rprime")
    return sup || check_double_script (tail, sub, true);
  return false;
}

scheme_tree
pre_scripts (scheme_tree l) {
  if (!stree_list (l) || N(l) < 2) return l;

  if ((head_is (l[0], "above") || head_is (l[0], "below")) &&
      (head_is (l[1], "rsub") || head_is (l[1], "rsup") ||
       head_is (l[1], "rprime"))) {
    scheme_tree grouped= stree_apply ("!group");
    grouped << l[0];
    scheme_tree next (TUPLE);
    next << grouped;
    for (int i=1; i<N(l); ++i) next << l[i];
    return pre_scripts (next);
  }

  scheme_tree tail (TUPLE);
  for (int i=1; i<N(l); ++i) tail << l[i];
  if (check_double_script (tail, false, false)) {
    bool second_sub= head_is (l[1], "rsub");
    bool third_sub= N(l) > 2 && head_is (l[2], "rsub");
    int take= second_sub == third_sub ? 2 : 3;
    scheme_tree concat= stree_apply ("concat");
    for (int i=0; i<take && i<N(l); ++i) concat << l[i];
    scheme_tree grouped= stree_apply ("!group");
    grouped << concat;
    scheme_tree next (TUPLE);
    next << grouped;
    for (int i=take; i<N(l); ++i) next << l[i];
    return pre_scripts (next);
  }

  scheme_tree rest= pre_scripts (tail);
  scheme_tree out (TUPLE);
  out << l[0];
  for (int i=0; i<N(rest); ++i) out << rest[i];
  return out;
}

bool
head_in (scheme_tree t, const char* const* names, int count) {
  if (!stree_list (t) || N(t) == 0 || !is_atomic (t[0])) return false;
  for (int i=0; i<count; ++i) if (t[0]->label == names[i]) return true;
  return false;
}

bool
disable_large (scheme_tree x, int level) {
  if (string_atom (x)) return true;
  if (!stree_list (x) || N(x) == 0) return false;
  if (head_is (x, "concat")) {
    for (int i=1; i<N(x); ++i)
      if (!disable_large (x[i], level)) return false;
    return true;
  }
  static const char* lr[]= {"left", "mid", "right"};
  if (head_in (x, lr, 3)) return true;
  static const char* scripts[]= {"lsub", "lsup", "rsub", "rsup"};
  if (head_in (x, scripts, 4))
    return level > 0 && N(x) > 1 && disable_large (x[1], level - 1);
  static const char* primes[]= {"lprime", "rprime"};
  if (head_in (x, primes, 2)) return true;
  static const char* wides[]= {"wide", "wide*"};
  if (head_in (x, wides, 2))
    return N(x) > 1 && disable_large (x[1], level - 1);
  static const char* wrappers[]= {"with", "rigid", "locus"};
  if (head_in (x, wrappers, 3))
    return N(x) > 1 && disable_large (x[N(x)-1], level);
  static const char* fonts[]= {
    "math-up", "math-ss", "math-tt", "math-bf", "math-it", "math-sl"
  };
  if (head_in (x, fonts, 6))
    return N(x) == 2 && disable_large (x[1], level);
  return false;
}

scheme_tree
make_small (scheme_tree s) {
  if (!string_atom (s)) return stree_string ("<nobracket>");
  string text= atom_text (s);
  if (text == ".") return stree_string ("<nobracket>");
  if (N(text) <= 1) return s;
  if (text[0] == '<' && text[N(text)-1] == '>') return s;
  return stree_string ("<" * text * ">");
}

scheme_tree
make_small_bracket (scheme_tree x) {
  static const char* lr[]= {"left", "mid", "right"};
  if (head_in (x, lr, 3) && N(x) > 1) return make_small (x[1]);
  return x;
}

int
find_paired_right (scheme_tree l) {
  if (!stree_list (l)) return 0;
  for (int i=1; i<N(l); ++i) {
    if (head_is (l[i], "left")) return 0;
    if (head_is (l[i], "right")) return i + 1;
  }
  return 0;
}

scheme_tree
mark_paired_middles (scheme_tree l) {
  scheme_tree out (TUPLE);
  if (!stree_list (l)) return out;
  for (int i=0; i<N(l); ++i) {
    if (head_is (l[i], "mid")) {
      scheme_tree x= stree_apply ("!middle");
      for (int j=1; j<N(l[i]); ++j) x << l[i][j];
      out << x;
    }
    else out << l[i];
  }
  return out;
}

scheme_tree
pre_brackets (scheme_tree l) {
  if (!stree_list (l) || N(l) == 0) return l;
  if (head_is (l[0], "left")) {
    int n= find_paired_right (l);
    if (n == 0) {
      scheme_tree tail (TUPLE);
      for (int i=1; i<N(l); ++i) tail << l[i];
      scheme_tree rest= pre_brackets (tail);
      scheme_tree out (TUPLE); out << l[0];
      for (int i=0; i<N(rest); ++i) out << rest[i];
      return out;
    }
    scheme_tree m (TUPLE), tail (TUPLE);
    for (int i=0; i<n; ++i) m << l[i];
    for (int i=n; i<N(l); ++i) tail << l[i];
    scheme_tree rest= pre_brackets (tail);
    scheme_tree concat= stree_apply ("concat");
    for (int i=0; i<N(m); ++i) concat << m[i];
    scheme_tree first= disable_large (concat, 2) ? scheme_tree (TUPLE)
                                                  : mark_paired_middles (m);
    if (disable_large (concat, 2))
      for (int i=0; i<N(m); ++i) first << make_small_bracket (m[i]);
    for (int i=0; i<N(rest); ++i) first << rest[i];
    return first;
  }
  scheme_tree tail (TUPLE);
  for (int i=1; i<N(l); ++i) tail << l[i];
  scheme_tree rest= pre_brackets (tail);
  scheme_tree out (TUPLE); out << l[0];
  for (int i=0; i<N(rest); ++i) out << rest[i];
  return out;
}

scheme_tree
pre_brackets_recurse (scheme_tree l) {
  while (true) {
    scheme_tree r= pre_brackets (l);
    if (r == l) return r;
    l= r;
  }
}

string
large_decode (scheme_tree value) {
  if (!string_atom (value)) return ".";
  string s= atom_text (value);
  static const char* direct[]= {"(", ")", "[", "]", "|", "/", "."};
  if (one_of (s, direct, 7)) return s;
  if (s == "||" || s == "<||>") return "\\|";
  if (s == "\\") return "\\backslash";
  if (N(s) >= 2 && s[0] == '<' && s[N(s)-1] == '>')
    return "\\" * s (1, N(s)-1);
  return "\\" * s;
}

string
large_decode_text (scheme_tree value) {
  if (!string_atom (value)) return "";
  string s= atom_text (value);
  if (s == ".") return "";
  static const char* direct[]= {"(", ")", "[", "]", "|", "/"};
  if (one_of (s, direct, 6)) return s;
  if (s == "{" || s == "}") return "\\" * s;
  cout << "ATHENA] non converted bracket: " << s << "\n";
  return "";
}

string
big_decode (scheme_tree value) {
  if (!string_atom (value)) return "bignone";
  string s= atom_text (value);
  static const char* direct[]= {
    "sum", "prod", "int", "fint", "oint", "coprod",
    "iint", "iiint", "iiiint", "idotsint", "oiint", "oiiint"
  };
  if (one_of (s, direct, 12)) return s;
  if (s == "amalg") return "coprod";
  if (s == "pluscup") return "uplus";
  if (s == ".") return "bignone";
  return "big" * s;
}

scheme_tree
decode_long_arrow (scheme_tree value) {
  if (!string_atom (value)) return tree ("#f");
  string s= atom_text (value);
  if (N(s) >= 10 && s (0, 8) == "<rubber-" && s[N(s)-1] == '>')
    return decode_long_arrow (stree_string (s (8, N(s)-1)));
  static const char* native[]= {
    "minus", "leftarrow", "rightarrow", "leftrightarrow",
    "equal", "Leftarrow", "Rightarrow", "Leftrightarrow",
    "mapsto", "mapsfrom"
  };
  if (one_of (s, native, 10)) return tree ("x" * s);
  static const char* long_names[]= {
    "leftrightarrows", "leftleftarrows", "threeleftarrows", "fourleftarrows",
    "rightleftarrows", "rightrightarrows", "threerightarrows", "fourrightarrows"
  };
  if (one_of (s, long_names, 8)) return stree_string ("<long" * s * ">");
  if (s == "Lleftarrow") return stree_string ("<Llongleftarrow>");
  if (s == "Rrightarrow") return stree_string ("<Llongrightarrow>");
  if (s == "LRleftrightarrow") return stree_string ("<Llongleftrightarrow>");
  return stree_string ("<" * s * ">");
}


} // namespace

namespace latex_export_internal {

scheme_tree
filter_known_style_macros (scheme_tree t) {
  return filter_style_macros_native (t);
}

} // namespace latex_export_internal

object
latex_export_collect_user_defs (scheme_tree t) {
  hashmap<string,bool> defs (false);
  scheme_tree preamble= filter_preamble (t);
  for (int i=0; i<N(preamble); ++i) collect_user_defs (preamble[i], defs);
  array<object> out;
  iterator<string> it= iterate (defs);
  while (it->busy ()) {
    string name= it->next ();
    if (defs[name]) out << symbol_object (name);
  }
  return as_list_object (out);
}

scheme_tree
latex_export_filter_preamble (scheme_tree t) {
  return filter_preamble (t);
}

scheme_tree
latex_export_filter_body (scheme_tree t) {
  return filter_body (t);
}

scheme_tree
latex_export_filter_duplicates (scheme_tree t) {
  hashmap<string,bool> seen (false);
  return filter_duplicates_list (t, seen);
}

scheme_tree
latex_export_clean_body (scheme_tree t) {
  return clean_body (t);
}

scheme_tree
latex_export_filter_style_macros (scheme_tree t, scheme_tree protected_names) {
  return filter_style_macros (t, protected_names);
}

scheme_tree
latex_export_apply_init (scheme_tree body, scheme_tree init) {
  return apply_initial_environment (body, init);
}

scheme_tree
latex_tmtex_math_concat_spaces (scheme_tree t) {
  return math_concat_spaces (t);
}

scheme_tree
latex_tmtex_rewrite_no_break (scheme_tree t) {
  return rewrite_no_break (t);
}

scheme_tree
latex_tmtex_pre_scripts (scheme_tree t) {
  return pre_scripts (t);
}

scheme_tree
latex_tmtex_pre_brackets_recurse (scheme_tree t) {
  return pre_brackets_recurse (t);
}

string
latex_tmtex_large_decode (scheme_tree t) {
  return large_decode (t);
}

string
latex_tmtex_large_decode_text (scheme_tree t) {
  return large_decode_text (t);
}

string
latex_tmtex_big_decode (scheme_tree t) {
  return big_decode (t);
}

scheme_tree
latex_tmtex_decode_long_arrow (scheme_tree t) {
  return decode_long_arrow (t);
}

scheme_tree
latex_tex_concat (scheme_tree t) {
  return tex_concat_native (t);
}

scheme_tree
latex_tex_concat_strings (scheme_tree t) {
  return tex_concat_native (concat_strings (t));
}

scheme_tree
latex_tmtex_concat_sep (scheme_tree t) {
  return concat_with_separator (t, "tmsep");
}

scheme_tree
latex_tmtex_concat_Sep (scheme_tree t) {
  return concat_with_separator (t, "tmSep");
}

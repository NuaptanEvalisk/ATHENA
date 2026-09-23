/******************************************************************************
* MODULE     : mathml_import.cpp
* DESCRIPTION: native Presentation MathML import used by HTML conversion
* COPYRIGHT  : (C) 2002, 2004, 2005 Joris van der Hoeven and David Allouche
*               (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
*******************************************************************************/

#include "convert.hpp"
#include "analyze.hpp"
#include "scheme.hpp"
#include "unicode_text.hpp"

#include <cstdlib>
#include <initializer_list>
#include <string>

namespace {

tree
node (string name, std::initializer_list<tree> args) {
  tree r (make_tree_label (name), (int) args.size ());
  int i= 0;
  for (const tree& x: args) r[i++]= x;
  return r;
}

tree
node (string name, const array<tree>& args) {
  tree r (make_tree_label (name), N(args));
  for (int i=0; i<N(args); ++i) r[i]= args[i];
  return r;
}

tree
named_math_symbol (string identity) {
  return tree (NAMED_SYMBOL, identity);
}

tree
styled_math_symbol (string style, tree value) {
  return node (style, {value});
}

tree
stretched_math_accent (tree value) {
  return tree (WITH, "math-accent-stretch", "true", value);
}

void
append_serial (array<tree>& out, tree t) {
  if (is_atomic (t) && t == "") return;
  if (is_func (t, CONCAT)) out << A(t);
  else out << t;
}

tree
serial (const array<tree>& in) {
  array<tree> out;
  for (int i=0; i<N(in); ++i) append_serial (out, in[i]);
  if (N(out) == 0) return "";
  if (N(out) == 1) return out[0];
  return node ("concat", out);
}

bool
scheme_string (scheme_tree t) {
  return is_atomic (t) && is_quoted (t->label);
}

string
scheme_text (scheme_tree t) {
  if (!is_atomic (t)) return "";
  return is_quoted (t->label) ? scm_unquote (t->label) : t->label;
}

bool
scheme_head (scheme_tree t, string head) {
  return is_tuple (t) && N(t) > 0 && is_atomic (t[0]) && t[0]->label == head;
}

string
local_name (string name) {
  int pos= search_forwards (":", name);
  return pos < 0 ? name : name (pos + 1, N(name));
}

string
element_name (scheme_tree t) {
  if (!is_tuple (t) || N(t) == 0 || !is_atomic (t[0])) return "";
  return local_name (t[0]->label);
}

int
content_start (scheme_tree t) {
  if (!is_tuple (t) || N(t) == 0) return 0;
  if (!is_atomic (t[0])) return 0;
  return N(t) > 1 && scheme_head (t[1], "@") ? 2 : 1;
}

scheme_tree
attribute_list (scheme_tree t) {
  if (N(t) > 1 && scheme_head (t[1], "@")) return t[1];
  return scheme_tree (TUPLE);
}

string
attribute (scheme_tree t, string key) {
  scheme_tree attrs= attribute_list (t);
  for (int i=1; i<N(attrs); ++i) {
    scheme_tree a= attrs[i];
    if (!is_tuple (a) || N(a) < 2 || !is_atomic (a[0])) continue;
    if (local_name (a[0]->label) == key) return scheme_text (a[1]);
  }
  return "";
}

bool
attribute_is (scheme_tree t, string key, string value) {
  return attribute (t, key) == value;
}

array<scheme_tree>
content (scheme_tree t) {
  array<scheme_tree> r;
  for (int i=content_start (t); i<N(t); ++i) r << t[i];
  return r;
}

bool
xml_space (char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

string
collapse_spaces (string s) {
  string r;
  bool space= false;
  for (int i=0; i<N(s); ++i) {
    if (xml_space (s[i])) space= true;
    else {
      if (space && N(r) != 0) r << ' ';
      space= false;
      r << s[i];
    }
  }
  if (space && N(r) != 0) r << ' ';
  return r;
}

string
trim_left (string s) {
  int i= 0;
  while (i<N(s) && xml_space (s[i])) ++i;
  return s (i, N(s));
}

string
trim_right (string s) {
  int n= N(s);
  while (n>0 && xml_space (s[n-1])) --n;
  return s (0, n);
}

array<scheme_tree>
clean_mixed (scheme_tree t) {
  array<scheme_tree> raw= content (t);
  array<scheme_tree> merged;
  for (int i=0; i<N(raw); ++i) {
    if (scheme_string (raw[i])) {
      string s= scheme_text (raw[i]);
      if (N(merged) > 0 && scheme_string (merged[N(merged)-1])) {
        string prev= scheme_text (merged[N(merged)-1]);
        merged[N(merged)-1]= scm_quote (prev * s);
      }
      else merged << tree (scm_quote (s));
    }
    else merged << raw[i];
  }
  for (int i=0; i<N(merged); ++i)
    if (scheme_string (merged[i])) {
      string s= collapse_spaces (scheme_text (merged[i]));
      if (i == 0) s= trim_left (s);
      if (i == N(merged)-1) s= trim_right (s);
      merged[i]= scm_quote (s);
    }
  return merged;
}

array<scheme_tree>
clean_elements (scheme_tree t) {
  array<scheme_tree> r;
  array<scheme_tree> raw= content (t);
  for (int i=0; i<N(raw); ++i)
    if (!scheme_string (raw[i])) r << raw[i];
  return r;
}

struct MathmlMaps {
  hashmap<string,tree> mathml_constant;
  hashmap<string,tree> mathml_operator;
  hashmap<string,tree> mathml_symbol;
  hashmap<string,tree> mathml_above;
  hashmap<string,tree> mathml_below;
  hashmap<string,tree> mathml_above_below;
  hashmap<string,tree> mathml_left;
  hashmap<string,tree> tmtm_left;
  hashmap<string,tree> mathml_right;
  hashmap<string,tree> tmtm_right;
  hashmap<string,tree> mathml_big;
  hashmap<string,tree> tmtm_big;

  MathmlMaps ():
    mathml_constant (UNINIT), mathml_operator (UNINIT), mathml_symbol (UNINIT),
    mathml_above (UNINIT), mathml_below (UNINIT), mathml_above_below (UNINIT),
    mathml_left (UNINIT), tmtm_left (UNINIT), mathml_right (UNINIT), tmtm_right (UNINIT),
    mathml_big (UNINIT), tmtm_big (UNINIT) {
#define ADD(map, key, value) map (key)= value
#include "mathml_symbol_maps.inc"
#undef ADD
  }
};

MathmlMaps&
maps () {
  static MathmlMaps value;
  return value;
}

bool
lookup (hashmap<string,tree>& map, string key, tree& value) {
  if (!map->contains (key)) return false;
  value= map[key];
  return true;
}

bool
lookup_above (string key, tree& value) {
  return lookup (maps ().mathml_above, key, value) ||
         lookup (maps ().mathml_above_below, key, value);
}

bool
lookup_below (string key, tree& value) {
  return lookup (maps ().mathml_below, key, value) ||
         lookup (maps ().mathml_above_below, key, value);
}

string
xml_text_to_tm (string s) {
  athena::text::require_utf8 (
    std::string_view (s.data (), static_cast<std::size_t> (N(s))));
  return s;
}

tree
entity_to_tm (string s) {
  int n= N(s);
  string style;
  if (n == 6 && ends (s, "opf;")) style= "math-alpha-bbb";
  else if (n == 6 && ends (s, "scr;")) style= "math-alpha-cal";
  else if (n == 5 && ends (s, "fr;")) style= "math-alpha-frak";
  if (style != "" && n > 2) return node (style, {tree (s (1, 2))});
  return tree (s);
}

tree
mapped_symbol (string s) {
  if (s == "&ThinSpace;") return node ("hspace", {tree ("0.1666667em")});
  if (s == "&MediumSpace;") return node ("hspace", {tree ("0.2222222em")});
  if (s == "&ThickSpace;") return node ("hspace", {tree ("0.2777778em")});
  if (s == " ") return node ("hspace", {tree ("1em")});
  if (s == " ") return node ("hspace", {tree ("1en")});

  tree v;
  if (lookup (maps ().mathml_symbol, s, v) ||
      lookup (maps ().mathml_constant, s, v) ||
      lookup (maps ().mathml_operator, s, v))
    return v;

  if (s == "&bigcup;") return node ("big", {tree ("⋃")});
  if (s == "&bigcap;") return node ("big", {tree ("⋂")});
  if (s == "&Integral;") return node ("big", {tree ("∫")});
  if (s == "&fpartint;") return node ("big", {tree ("⨍")});
  if (s == "&prod;") return node ("big", {tree ("∏")});
  if (s == "&coprod;") return node ("big", {tree ("∐")});
  if (s == "&sum;") return node ("big", {tree ("∑")});
  if (s == "&mnplus;") return tree ("∓");
  if (starts (s, "&")) return entity_to_tm (s);
  return tree (xml_text_to_tm (s));
}

tree convert_mathml (scheme_tree t);

tree
convert_list (const array<scheme_tree>& items) {
  array<tree> out;
  for (int i=0; i<N(items); ++i) append_serial (out, convert_mathml (items[i]));
  return serial (out);
}

tree
convert_children_mixed (scheme_tree t) {
  return convert_list (clean_mixed (t));
}

tree
convert_children_elements (scheme_tree t) {
  return convert_list (clean_elements (t));
}

array<tree>
style_pairs (scheme_tree t) {
  array<tree> r;
  scheme_tree attrs= attribute_list (t);
  for (int i=1; i<N(attrs); ++i) {
    scheme_tree a= attrs[i];
    if (!is_tuple (a) || N(a) < 2 || !is_atomic (a[0])) continue;
    string key= local_name (a[0]->label);
    string val= scheme_text (a[1]);
    if (key == "mathcolor" || key == "color")
      r << tree ("color") << tree (val);
    else if (key == "displaystyle")
      r << tree ("math-display") << tree (val);
    else if (key == "mathvariant" &&
             (val == "bold" || val == "bold-italic"))
      r << tree ("math-font-series") << tree ("bold");
    else if (key == "mathvariant" &&
             (val == "sans-serif" || val == "sans-serif-italic" ||
              val == "sans-serif--italic"))
      r << tree ("math-font-family") << tree ("ms");
    else if (key == "mathvariant" && val == "monospace")
      r << tree ("math-font-family") << tree ("mt");
    else if (key == "mathsize")
      r << tree ("font-base-size") << tree (val);
    else if (key == "scriptlevel") {
      std::string sv (as_charp (val), N(val));
      char* end= nullptr;
      long level= std::strtol (sv.c_str (), &end, 10);
      if (end != sv.c_str () && *end == '\0') {
        if (level >= 0) r << tree ("math-level") << tree (val);
        else if (level == -1) r << tree ("font-size") << tree ("1.189");
        else if (level == -2) r << tree ("font-size") << tree ("1.414");
        else if (level == -3) r << tree ("font-size") << tree ("1.682");
        else r << tree ("font-size") << tree ("2");
      }
    }
    else if (key == "style") {
      std::string css (as_charp (val), N(val));
      size_t start= 0;
      while (start < css.size ()) {
        size_t semi= css.find (';', start);
        std::string decl= css.substr (start,
          semi == std::string::npos ? std::string::npos : semi - start);
        size_t colon= decl.find (':');
        if (colon != std::string::npos) {
          auto trim= [] (std::string x) {
            size_t a= x.find_first_not_of (" \t\r\n");
            size_t b= x.find_last_not_of (" \t\r\n");
            return a == std::string::npos ? std::string () : x.substr (a, b-a+1);
          };
          std::string k= trim (decl.substr (0, colon));
          std::string v= trim (decl.substr (colon+1));
          if (k == "color")
            r << tree ("color") << tree (string (v.c_str ()));
        }
        if (semi == std::string::npos) break;
        start= semi + 1;
      }
    }
  }
  return r;
}

tree
apply_styles (scheme_tree source, tree body) {
  array<tree> attrs= style_pairs (source);
  if (N(attrs) == 0) return body;
  array<tree> args= attrs;
  args << body;
  return node ("with", args);
}

tree
decorate_id (scheme_tree source, tree body) {
  string id= attribute (source, "id");
  if (id == "") return body;
  array<tree> out;
  out << node ("label", {tree (id)});
  append_serial (out, body);
  return serial (out);
}

bool
zero_length (string s) {
  std::string v (as_charp (s), N(s));
  char* end= nullptr;
  double x= std::strtod (v.c_str (), &end);
  return end != v.c_str () && x == 0.0;
}

bool
prime_string (tree t) {
  if (!is_atomic (t)) return false;
  string s= t->label;
  int i= 0;
  while (i<N(s)) {
    if (s[i] == '\'' || s[i] == '`') { ++i; continue; }
    if (i+8 <= N(s) && s (i, i+8) == "<dagger>") { i += 8; continue; }
    return false;
  }
  return N(s) != 0;
}

tree
script_node (string ordinary, string prime, tree value) {
  return node (prime_string (value) ? prime : ordinary, {value});
}

tree
with_scripts (tree base, tree lsub, tree lsup, tree rsub, tree rsup) {
  array<tree> out;
  if (!(is_atomic (lsub) && lsub == "")) out << node ("lsub", {lsub});
  if (!(is_atomic (lsup) && lsup == "")) out << script_node ("lsup", "lprime", lsup);
  append_serial (out, base);
  if (!(is_atomic (rsub) && rsub == "")) out << node ("rsub", {rsub});
  if (!(is_atomic (rsup) && rsup == "")) out << script_node ("rsup", "rprime", rsup);
  return serial (out);
}

bool
stretchy (scheme_tree source, tree converted) {
  if (element_name (source) != "mo" || !attribute_is (source, "stretchy", "true"))
    return false;
  if (is_func (converted, NAMED_SYMBOL, 1)) return true;
  if (!is_atomic (converted)) return false;
  std::string_view bytes (converted->label.data (),
                          static_cast<std::size_t> (N(converted->label)));
  return !bytes.empty () && athena::text::valid_utf8 (bytes) &&
         athena::text::next_scalar (bytes, 0) == bytes.size ();
}

tree
convert_mo (scheme_tree t) {
  array<scheme_tree> c= clean_mixed (t);
  if (N(c) == 0) return "";
  tree r= convert_list (c);
  if (N(c) != 1 || !scheme_string (c[0]))
    return apply_styles (t, r);

  string source= scheme_text (c[0]);
  tree v;
  if (lookup (maps ().mathml_left, source, v))
    return apply_styles (t, node ("left", {v}));
  if (lookup (maps ().mathml_right, source, v))
    return apply_styles (t, node ("right", {v}));
  if (lookup (maps ().mathml_big, source, v))
    return apply_styles (t, node ("big", {v}));
  if (is_atomic (r)) {
    if (lookup (maps ().tmtm_left, r->label, v))
      return apply_styles (t, node ("left", {v}));
    if (lookup (maps ().tmtm_right, r->label, v))
      return apply_styles (t, node ("right", {v}));
    if (lookup (maps ().tmtm_big, r->label, v))
      return apply_styles (t, node ("big", {v}));
  }
  return apply_styles (t, r);
}

string
halign (string s) {
  if (s == "left") return "l";
  if (s == "center") return "c";
  if (s == "right") return "r";
  return "";
}

string
valign (string s) {
  if (s == "bottom") return "b";
  if (s == "baseline") return "B";
  if (s == "axis") return "f";
  if (s == "center") return "c";
  if (s == "top") return "t";
  return "";
}

array<string>
split_words (string s) {
  array<string> out;
  int i= 0;
  while (i<N(s)) {
    while (i<N(s) && xml_space (s[i])) ++i;
    int start= i;
    while (i<N(s) && !xml_space (s[i])) ++i;
    if (i > start) out << s (start, i);
  }
  return out;
}

void
append_cwith (array<tree>& formats, string r1, string r2, string c1, string c2,
              string key, string value) {
  if (value == "") return;
  formats << node ("cwith", {tree (r1), tree (r2), tree (c1), tree (c2),
                              tree (key), tree (value)});
}

tree
convert_table (scheme_tree t) {
  array<scheme_tree> rows_src= clean_elements (t);
  array<tree> rows;
  array<tree> formats;
  int max_cols= 0;

  for (int ri=0; ri<N(rows_src); ++ri) {
    scheme_tree row_src= rows_src[ri];
    if (element_name (row_src) == "mlabeledtr") {
      array<scheme_tree> x= clean_elements (row_src);
      scheme_tree synthetic (TUPLE);
      synthetic << tree ("mtr");
      for (int j=1; j<N(x); ++j) synthetic << x[j];
      row_src= synthetic;
    }
    else if (element_name (row_src) != "mtr") {
      scheme_tree synthetic (TUPLE);
      synthetic << tree ("mtr") << row_src;
      row_src= synthetic;
    }

    array<scheme_tree> cells_src= clean_elements (row_src);
    array<tree> cells;
    for (int ci=0; ci<N(cells_src); ++ci) {
      scheme_tree cell_src= cells_src[ci];
      if (element_name (cell_src) != "mtd") {
        scheme_tree synthetic (TUPLE);
        synthetic << tree ("mtd") << cell_src;
        cell_src= synthetic;
      }
      tree body= convert_children_elements (cell_src);
      cells << node ("cell", {body});
      string rown= as_string (ri+1), coln= as_string (ci+1);
      string ch= halign (attribute (cell_src, "columnalign"));
      string cv= valign (attribute (cell_src, "rowalign"));
      append_cwith (formats, rown, rown, coln, coln, "cell-halign", ch);
      append_cwith (formats, rown, rown, coln, coln, "cell-valign", cv);
      array<tree> styles= style_pairs (cell_src);
      for (int k=0; k+1<N(styles); k+=2)
        append_cwith (formats, rown, rown, coln, coln,
                      styles[k]->label, styles[k+1]->label);
    }
    if (N(cells) > max_cols) max_cols= N(cells);

    array<string> cols= split_words (attribute (row_src, "columnalign"));
    for (int ci=0; ci<N(cols); ++ci)
      append_cwith (formats, as_string (ri+1), as_string (ri+1),
                    as_string (ci+1), as_string (ci+1),
                    "cell-halign", halign (cols[ci]));
    append_cwith (formats, as_string (ri+1), as_string (ri+1), "1", "-1",
                  "cell-valign", valign (attribute (row_src, "rowalign")));
    rows << node ("row", cells);
  }

  for (int ri=0; ri<N(rows); ++ri)
    while (N(rows[ri]) < max_cols)
      rows[ri] << node ("cell", {tree ("")});

  array<string> table_cols= split_words (attribute (t, "columnalign"));
  for (int ci=0; ci<N(table_cols); ++ci)
    append_cwith (formats, "1", "-1", as_string (ci+1), as_string (ci+1),
                  "cell-halign", halign (table_cols[ci]));
  array<string> table_rows= split_words (attribute (t, "rowalign"));
  for (int ri=0; ri<N(table_rows); ++ri)
    append_cwith (formats, as_string (ri+1), as_string (ri+1), "1", "-1",
                  "cell-valign", valign (table_rows[ri]));
  array<tree> table_styles= style_pairs (t);
  for (int k=0; k+1<N(table_styles); k+=2)
    append_cwith (formats, "1", "-1", "1", "-1",
                  table_styles[k]->label, table_styles[k+1]->label);

  tree table= node ("table", rows);
  if (N(formats) != 0) {
    array<tree> args= formats;
    args << table;
    table= node ("tformat", args);
  }
  return node ("tabular", {table});
}

tree
replace_around (tree t) {
  if (is_atomic (t)) return t;
  string name= as_string (L(t));
  if (is_func (t, AROUND)) name= "around*";
  tree r (make_tree_label (name), N(t));
  for (int i=0; i<N(t); ++i) r[i]= replace_around (t[i]);
  return r;
}

tree
convert_semantics (scheme_tree t) {
  array<scheme_tree> c= clean_elements (t);
  if (N(c) == 0) return "";
  if (get_preference ("mathml->texmacs:latex-annotations", "on") == "on") {
    for (int i=1; i<N(c); ++i) {
      if (element_name (c[i]) != "annotation") continue;
      string enc= attribute (c[i], "encoding");
      if (!(enc == "application/x-tex" || enc == "TeX")) continue;
      array<scheme_tree> body= content (c[i]);
      if (N(body) != 1 || !scheme_string (body[0])) continue;
      string source= "$" * scheme_text (body[0]) * "$";
      return latex_to_tree (parse_latex (source));
    }
  }
  return convert_mathml (c[0]);
}

tree
convert_mathml (scheme_tree t) {
  if (scheme_string (t)) return mapped_symbol (scheme_text (t));
  if (!is_tuple (t) || N(t) == 0) return "";

  string tag= element_name (t);
  if (tag == "" || tag == "@" || starts (tag, "*")) {
    array<scheme_tree> c= content (t);
    return convert_list (c);
  }
  if (tag == "none" || tag == "mspace" || tag == "mglyph") return "";
  if (tag == "mo") return convert_mo (t);

  if (tag == "math") {
    tree body= upgrade_mathml (convert_children_mixed (t));
    body= replace_around (body);
    tree result= attribute_is (t, "display", "block")
      ? node ("document", {node ("equation*", {body})})
      : node ("math", {body});
    return apply_styles (t, result);
  }
  if (tag == "mtext" || tag == "ms")
    return apply_styles (t, node ("with", {tree ("mode"), tree ("text"),
                                            convert_children_mixed (t)}));
  if (tag == "mfrac") {
    array<scheme_tree> c= clean_elements (t);
    if (N(c) != 2) return node ("with", {tree ("color"), tree ("red"),
                                         tree ("bad mfrac")});
    tree a= convert_mathml (c[0]), b= convert_mathml (c[1]);
    tree result;
    if (zero_length (attribute (t, "linethickness"))) {
      tree table= node ("table", {
        node ("row", {node ("cell", {a})}),
        node ("row", {node ("cell", {b})})});
      result= node ("stack", {node ("tformat", {table})});
    }
    else result= node ("frac", {a, b});
    return apply_styles (t, result);
  }
  if (tag == "msqrt")
    return apply_styles (t, node ("sqrt", {convert_children_mixed (t)}));
  if (tag == "mroot") {
    array<scheme_tree> c= clean_elements (t);
    if (N(c) != 2) return tree ("bad mroot");
    return apply_styles (t, node ("sqrt", {convert_mathml (c[0]),
                                            convert_mathml (c[1])}));
  }
  if (tag == "merror")
    return node ("with", {tree ("color"), tree ("red"),
                           convert_children_mixed (t)});
  if (tag == "mphantom")
    return apply_styles (t, node ("phantom", {convert_children_mixed (t)}));
  if (tag == "menclose") {
    tree body= convert_children_mixed (t);
    array<string> notes= split_words (attribute (t, "notation"));
    for (int i=0; i<N(notes); ++i)
      if (notes[i] == "updiagonalstrike") body= node ("neg", {body});
    return apply_styles (t, body);
  }
  if (tag == "mfenced") {
    string left= attribute (t, "open"); if (left == "") left= "(";
    string right= attribute (t, "close"); if (right == "") right= ")";
    tree lv= tree (left), rv= tree (right);
    (void) lookup (maps ().mathml_left, left, lv);
    (void) lookup (maps ().mathml_right, right, rv);
    array<scheme_tree> c= clean_elements (t);
    array<string> seps= split_words (attribute (t, "separators"));
    array<tree> inside;
    for (int i=0; i<N(c); ++i) {
      append_serial (inside, convert_mathml (c[i]));
      if (i+1 < N(c) && i < N(seps)) append_serial (inside, mapped_symbol (seps[i]));
    }
    return apply_styles (t, node ("around*", {lv, serial (inside), rv}));
  }
  if (tag == "msub" || tag == "msup" || tag == "msubsup") {
    array<scheme_tree> c= clean_elements (t);
    int needed= tag == "msubsup" ? 3 : 2;
    if (N(c) != needed) return tree ("bad math script");
    tree base= convert_mathml (c[0]);
    tree sub= tag == "msup" ? tree ("") : convert_mathml (c[1]);
    tree sup= tag == "msub" ? tree ("") : convert_mathml (c[needed-1]);
    return apply_styles (t, with_scripts (base, "", "", sub, sup));
  }
  if (tag == "mmultiscripts") {
    array<scheme_tree> c= clean_elements (t);
    if (N(c) == 0) return tree ("bad mmultiscripts");
    tree base= convert_mathml (c[0]);
    array<tree> lsubs, lsups, rsubs, rsups;
    bool right= true;
    for (int i=1; i<N(c);) {
      if (element_name (c[i]) == "mprescripts") { right= false; ++i; continue; }
      tree sub= convert_mathml (c[i]);
      tree sup= i+1 < N(c) ? convert_mathml (c[i+1]) : tree ("");
      if (right) { append_serial (rsubs, sub); append_serial (rsups, sup); }
      else { append_serial (lsubs, sub); append_serial (lsups, sup); }
      i += 2;
    }
    return apply_styles (t, with_scripts (base, serial (lsubs), serial (lsups),
                                           serial (rsubs), serial (rsups)));
  }
  if (tag == "munder" || tag == "mover" || tag == "munderover") {
    array<scheme_tree> c= clean_elements (t);
    int needed= tag == "munderover" ? 3 : 2;
    if (N(c) != needed) return tree ("bad math limit");
    tree base= convert_mathml (c[0]);
    tree sub= tag == "mover" ? tree ("") : convert_mathml (c[1]);
    tree sup= tag == "munder" ? tree ("") : convert_mathml (c[needed-1]);
    if (stretchy (c[0], base)) {
      if (tag == "munder")
        return apply_styles (t, node ("long-arrow", {base, tree (""), sub}));
      if (tag == "mover")
        return apply_styles (t, node ("long-arrow", {base, sup}));
      return apply_styles (t, node ("long-arrow", {base, sup, sub}));
    }
    tree mapped;
    if (tag != "mover" && is_atomic (sub) && lookup_below (sub->label, mapped))
      sub= node ("wide*", {base, mapped});
    else if (tag != "mover") sub= node ("below", {base, sub});
    if (tag == "munder") return apply_styles (t, sub);
    if (is_atomic (sup) && lookup_above (sup->label, mapped))
      return apply_styles (t, node ("wide", {tag == "munderover" ? sub : base,
                                              mapped}));
    return apply_styles (t, node ("above", {tag == "munderover" ? sub : base, sup}));
  }
  if (tag == "mtable") return convert_table (t);
  if (tag == "semantics") return apply_styles (t, convert_semantics (t));

  // mi, mn, mrow, mstyle, mpadded, maction and unknown Presentation MathML
  // elements are transparent containers, matching the inherited importer.
  tree result= convert_children_mixed (t);
  result= decorate_id (t, result);
  return apply_styles (t, result);
}

} // namespace

tree
mathml_to_tree (scheme_tree t) {
  return convert_mathml (t);
}

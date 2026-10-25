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
#include "utf8_edit.hpp"
#include "math_tree_v3.hpp"
#include "node_metadata.hpp"
#include "iterator.hpp"

#include <cstdlib>
#include <initializer_list>
#include <string>
#include <map>
#include <set>
#include <stdexcept>

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

  explicit MathmlMaps (bool native_v3= false):
    mathml_constant (UNINIT), mathml_operator (UNINIT), mathml_symbol (UNINIT),
    mathml_above (UNINIT), mathml_below (UNINIT), mathml_above_below (UNINIT),
    mathml_left (UNINIT), tmtm_left (UNINIT), mathml_right (UNINIT), tmtm_right (UNINIT),
    mathml_big (UNINIT), tmtm_big (UNINIT) {
    // Instantiate the same declarative mappings with native constructors.
    // The v3 importer never obtains legacy symbol/style/accent adapter trees.
    auto named_math_symbol= [native_v3] (string identity) -> tree {
      if (!native_v3) return ::named_math_symbol (identity);
      string name= identity (8, N(identity)); // table identities use texmacs:
      if (name == "mathd") name= "differential-d";
      else if (name == "idotsint") name= "multiple-integral-with-dots";
      else if (name == "backassign") name= "backward-assignment";
      else if (name == "longhookrightarrow") name= "long-hookrightarrow";
      return tree (MATH_SYMBOL, name);
    };
    auto styled_math_symbol= [native_v3] (string style, tree value) -> tree {
      if (!native_v3) return ::styled_math_symbol (style, value);
      if (style == "math-alpha-bbb") return tree (MATH_VARIANT, "bb", value);
      if (style == "math-alpha-cal") return tree (MATH_VARIANT, "cal", value);
      throw std::runtime_error ("Unsupported native MathML symbol variant");
    };
    auto stretched_math_accent= [native_v3] (tree value) -> tree {
      if (!native_v3) return ::stretched_math_accent (value);
      tree result (MATH_ACCENT, value, "o", "");
      athena::node::metadata meta;
      meta.properties["math:accent-stretch"]= athena::node::property (true);
      athena::node::set (result, meta);
      return result;
    };
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
      tree parsed= latex_to_tree (parse_latex (source));
      // SEMANTICS supplies a math body; the enclosing MATH owns inline/display.
      return is_compound (parsed, "math", 1) ? parsed[0] : parsed;
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

namespace native_mathml {

using athena::node::property;

[[noreturn]] void fail (scheme_tree source, string reason) {
  const string message= "Native v3 MathML <" * element_name (source) * ">: " * reason;
  throw std::runtime_error (std::string (message.data (), std::size_t (N(message))));
}

bool one_of (string name, std::initializer_list<const char*> names) {
  for (const char* value: names) if (name == value) return true;
  return false;
}

void set_property (tree& value, const char* key, property data) {
  athena::node::metadata meta;
  if (const auto* existing= athena::node::get (value)) meta= *existing;
  meta.properties[key]= std::move (data);
  athena::node::set (value, meta);
}

MathmlMaps& native_maps () {
  static MathmlMaps value (true);
  return value;
}

bool has (scheme_tree source, string key) {
  const scheme_tree attrs= attribute_list (source);
  for (int i=1; i<N(attrs); ++i)
    if (attrs[i][0] == key) return true;
  return false;
}

bool boolean (scheme_tree source, string key, bool fallback) {
  return has (source, key) ? attribute_is (source, key, "true") : fallback;
}

string text (scheme_tree source) {
  string result;
  const auto parts= content (source);
  for (int i=0; i<N(parts); ++i) {
    const auto& part= parts[i];
    if (scheme_head (part, "*LITERAL-AMP*")) {
      // Internal lexical marker emitted only by parse_xml_mathml_v3 for a
      // source-level &amp; or numeric ampersand entity. It must survive the
      // token decoder without a second (incorrect) named-entity expansion.
      result << '\001';
      continue;
    }
    if (!scheme_string (part)) fail (source, "token must contain text only");
    result << scheme_text (part);
  }
  return trim_right (trim_left (collapse_spaces (result)));
}

// This entry consumes parsed SXML, not XML bytes. Validate its grammar and
// namespace/attribute boundaries before conversion; parse_xml itself is tolerant.
class Importer {
  std::set<std::string> ids;
  std::size_t nodes= 0, bytes= 0;
  using Namespaces= std::map<std::string, string>;

  void validate (scheme_tree source, Namespaces ns, int depth, string parent= "") {
    if (++nodes > 1000000 || depth > 256) fail (source, "resource limit exceeded");
    if (scheme_string (source)) {
      const string value= scheme_text (source);
      for (int i=0; i<N(value); ++i)
        if (value[i] == '\001') fail (source, "reserved MathML lexical marker");
      bytes += std::size_t (N(value));
      if (bytes > 64u * 1024u * 1024u) fail (source, "text budget exceeded");
      athena::text::require_utf8 (std::string_view (value.data (), std::size_t (N(value))));
      return;
    }
    if (scheme_head (source, "*LITERAL-AMP*")) {
      if (N(source) != 1 ||
          !one_of (parent, {"mi", "mn", "mo", "mtext"}))
        fail (source, "literal XML ampersand outside a MathML token");
      return;
    }
    if (!is_tuple (source) || N(source) == 0 || !is_atomic (source[0]) ||
        scheme_string (source[0])) fail (source, "malformed element");
    const string tag= element_name (source);
    if ((tag == "math" && parent != "" && parent != "*TOP*") ||
        (tag == "*TOP*" && parent != "")) fail (source, "nested math/document root");
    if (!one_of (tag, {"*TOP*", "math", "mrow", "mstyle", "mi", "mn", "mo",
                      "mtext", "mfrac", "msqrt", "mroot", "mfenced", "msub", "msup",
                      "msubsup", "mmultiscripts", "none", "mprescripts", "munder",
                      "mover", "munderover", "mtable", "mtr", "mtd", "semantics"}))
      fail (source, "unsupported element (including annotations and labeled rows)");
    std::set<std::string> seen;
    const scheme_tree attrs= attribute_list (source);
    for (int i=1; i<N(attrs); ++i) {
      const scheme_tree entry= attrs[i];
      if (!is_tuple (entry) || N(entry) != 2 || !is_atomic (entry[0]) ||
          scheme_string (entry[0]) || !scheme_string (entry[1]))
        fail (source, "malformed attribute");
      const string name= entry[0]->label, value= scheme_text (entry[1]);
      validate (entry[1], ns, depth+1);
      const std::string key (name.data (), std::size_t (N(name)));
      if (!seen.insert (key).second) fail (source, "duplicate attribute " * name);
      if (name == "xmlns" || starts (name, "xmlns:")) {
        if (value != "http://www.w3.org/1998/Math/MathML")
          fail (source, "foreign namespace");
        const string prefix= name == "xmlns" ? string ("") : name (6, N(name));
        ns[std::string (prefix.data (), std::size_t (N(prefix)))]= value;
        continue;
      }
      bool allowed= one_of (name, {"id", "mathvariant", "mathcolor", "displaystyle"});
      if (tag == "math") allowed |= name == "display";
      if (tag == "mo") allowed |= one_of (name,
        {"fence", "form", "separator", "stretchy", "largeop", "movablelimits", "accent"});
      if (tag == "mfenced") allowed |= one_of (name, {"open", "close", "separators"});
      if (tag == "mfrac") allowed |= one_of (name, {"linethickness", "bevelled"});
      if (tag == "mover" || tag == "munderover") allowed |= name == "accent";
      if (tag == "munder" || tag == "munderover") allowed |= name == "accentunder";
      if (tag == "mtable" || tag == "mtr" || tag == "mtd") allowed |= name == "columnalign";
      if (tag == "mtd") allowed |= one_of (name, {"rowspan", "columnspan", "mathbackground"});
      if (!allowed || tag == "*TOP*" || tag == "none" || tag == "mprescripts")
        fail (source, "unsupported attribute " * name);
      if (one_of (name, {"displaystyle", "fence", "separator", "stretchy", "largeop",
                        "movablelimits", "accent", "accentunder", "bevelled"}) &&
          value != "true" && value != "false") fail (source, "invalid boolean " * name);
      if (name == "display" && value != "block" && value != "inline")
        fail (source, "invalid display");
      if ((name == "mathcolor" || name == "mathbackground") && value == "")
        fail (source, "empty color");
      if (name == "form" && !one_of (value, {"prefix", "infix", "postfix"}))
        fail (source, "invalid operator form");
      if (name == "id") {
        const std::string id (value.data (), std::size_t (N(value)));
        if (id.empty () || N(split_words (value)) != 1 ||
            trim_left (trim_right (value)) != value || !ids.insert (id).second)
          fail (source, "empty, whitespace-containing or duplicate id");
      }
    }
    if (tag != "*TOP*") {
      const string qualified= source[0]->label;
      const int colon= search_forwards (":", qualified);
      if (colon >= 0) {
        const string prefix= qualified (0, colon);
        if (!ns.count (std::string (prefix.data (), std::size_t (N(prefix)))))
          fail (source, "unbound namespace prefix");
      }
    }
    const auto c= content (source);
    for (int i=0; i<N(c); ++i) validate (c[i], ns, depth+1, tag);
  }

  array<scheme_tree> children (scheme_tree source, int arity= -1) {
    array<scheme_tree> result;
    const auto c= content (source);
    for (int i=0; i<N(c); ++i) {
      const auto& child= c[i];
      if (scheme_string (child)) {
        if (trim_left (trim_right (scheme_text (child))) != "")
          fail (source, "text is only allowed inside token elements");
      }
      else result << child;
    }
    if (arity >= 0 && N(result) != arity) fail (source, "wrong child count");
    return result;
  }

  tree mapped (string value, scheme_tree source) {
    if (value == "&Backslash;") return tree ("\\");
    auto& m= native_maps ();
    tree result;
    if (lookup (m.mathml_symbol, value, result) ||
        lookup (m.mathml_constant, value, result) ||
        lookup (m.mathml_operator, value, result) ||
        lookup (m.mathml_big, value, result) ||
        lookup (m.mathml_left, value, result) ||
        lookup (m.mathml_right, value, result)) {
      if (!athena::math_v3::valid_native_math (result))
        fail (source, "unregistered semantic symbol");
      return result;
    }
    // These aliases are the explicit extra entities in mapped_symbol above.
    const std::pair<const char*, const char*> aliases[]= {
      {"&bigcup;", "&Union;"}, {"&bigcap;", "&Intersection;"},
      {"&fpartint;", "&#x2A0D;"}, {"&prod;", "&Product;"},
      {"&coprod;", "&Coproduct;"}, {"&sum;", "&Sum;"}
    };
    for (const auto& alias: aliases)
      if (value == alias.first) return m.mathml_big[string (alias.second)];
    if (value == "&mnplus;") return tree ("∓");
    if (value == "&ThinSpace;") return tree (" ");
    if (value == "&MediumSpace;") return tree (" ");
    if (value == "&ThickSpace;") return tree ("  ");
    if (starts (value, "&") && ends (value, ";")) {
      if (N(value) == 6 && (ends (value, "opf;") || ends (value, "scr;")) &&
          is_alpha (value[1]))
        return tree (MATH_VARIANT, ends (value, "opf;") ? "bb" : "cal",
                     value (1, 2));
      fail (source, "unsupported entity " * value);
    }
    return tree (xml_text_to_tm (value));
  }

  tree token (string value, scheme_tree source) {
    array<tree> pieces;
    int start= 0;
    for (int i=0; i<N(value); ++i) {
      if (value[i] == '\001') {
        if (i > start) pieces << mapped (value (start, i), source);
        pieces << tree ("&");
        start= i+1;
        continue;
      }
      if (value[i] != '&') continue;
      const int end= search_forwards (";", i+1, value);
      if (end < 0) continue; // A literal XML &amp; has already been decoded.
      if (i > start) pieces << mapped (value (start, i), source);
      pieces << mapped (value (i, end+1), source);
      i= end; start= end+1;
    }
    if (start < N(value)) pieces << mapped (value (start, N(value)), source);
    return serial (pieces);
  }

  string glyph (scheme_tree source) {
    const tree value= token (text (source), source);
    if (!is_atomic (value) || !athena::math_v3::glyph (value->label))
      fail (source, "expected one Unicode glyph");
    return value->label;
  }

  bool mapped_value (hashmap<string,tree>& values, string value) {
    for (iterator<string> it= iterate (values); it->busy (); )
      if (values[it->next ()] == value) return true;
    return false;
  }

  bool integral (string value) {
    return one_of (value, {"∫", "∬", "∭", "⨌", "∮", "∯", "∰", "⨍"});
  }

  bool big (string value) {
    return integral (value) || mapped_value (native_maps ().mathml_big, value);
  }

  bool arrow (string value) {
    return one_of (value, {"→", "←", "↔", "⇒", "⇐", "⇔", "↦", "↪", "↩",
                          "⟶", "⟵", "⟷", "⟹", "⟸", "⟺", "⟼"});
  }

  string variant_kind (scheme_tree source, string variant) {
    const std::pair<const char*, const char*> variants[]= {
      {"normal", "rm"}, {"bold", "mbfu"}, {"italic", "it"},
      {"bold-italic", "mbf"},
      {"double-struck", "bb"}, {"script", "cal"},
      {"fraktur", "frak"},
      {"sans-serif", "ss"}, {"monospace", "tt"}
    };
    for (const auto& entry: variants) if (variant == entry.first) return entry.second;
    fail (source, "unsupported mathvariant " * variant);
  }

  tree finish (scheme_tree source, tree result) {
    if (has (source, "mathcolor") && element_name (source) != "mtable")
      result= tree (WITH, "color", attribute (source, "mathcolor"), result);
    if (has (source, "displaystyle"))
      result= tree (WITH, "math-display", attribute (source, "displaystyle"), result);
    return decorate_id (source, result);
  }

  void plain_operator (scheme_tree source) {
    if (has (source, "mathvariant") || has (source, "mathcolor") ||
        has (source, "displaystyle") || has (source, "id"))
      fail (source, "styled or identified fence/accent glyph is unsupported");
  }

  // Roles identify source endpoints, never infer a missing matching glyph.
  int fence_role (scheme_tree source, int position, int count) {
    if (element_name (source) != "mo" || attribute_is (source, "fence", "false")) return 0;
    const tree value= token (text (source), source);
    if (!is_atomic (value)) return 0;
    const string g= value->label;
    auto& m= native_maps ();
    const bool explicit_fence= attribute_is (source, "fence", "true");
    if ((g == "/" || g == "\\" || g == "\\\\") && !explicit_fence) return 0;
    const bool left= mapped_value (m.mathml_left, g) || mapped_value (m.tmtm_left, g);
    const bool right= mapped_value (m.mathml_right, g) || mapped_value (m.tmtm_right, g);
    if (!explicit_fence && !left && !right) return 0;
    plain_operator (source);
    if (has (source, "largeop") || has (source, "movablelimits") ||
        has (source, "accent") || attribute_is (source, "separator", "true"))
      fail (source, "incompatible fence operator properties");
    if (attribute_is (source, "form", "prefix")) return 1;
    if (attribute_is (source, "form", "postfix")) return -1;
    if (has (source, "form")) fail (source, "infix fence is unsupported");
    if (left && !right) return 1;
    if (right && !left) return -1;
    if (position == 0) return 1;
    if (position == count-1) return -1;
    fail (source, "ambiguous fence role; explicit form required");
  }

  tree sequence_part (const array<scheme_tree>& c, int& position,
                      string variant, bool enclosed) {
    array<tree> result;
    while (position < N(c)) {
      const scheme_tree current= c[position];
      const int role= fence_role (current, position, N(c));
      if (role < 0) {
        if (!enclosed) fail (current, "unpaired closing fence");
        break;
      }
      ++position;
      if (role == 0) result << convert (current, variant, false, enclosed);
      else {
        const tree body= sequence_part (c, position, variant, true);
        if (position == N(c)) fail (current, "unpaired opening fence");
        const scheme_tree close= c[position++];
        const bool stretch= boolean (current, "stretchy", true);
        if (stretch != boolean (close, "stretchy", true))
          fail (current, "independent automatic/natural fence sizing is unsupported");
        result << athena::math_v3::delimited (
          glyph (current), glyph (close), stretch ? 'a' : 'f', body);
      }
    }
    return serial (result);
  }

  tree sequence (scheme_tree source, string variant, bool enclosed= false) {
    const auto c= children (source);
    int position= 0;
    tree result= sequence_part (c, position, variant, enclosed);
    if (position != N(c)) fail (source, "closing fence crosses a container boundary");
    return result;
  }

  scheme_tree core (scheme_tree source) {
    const string tag= element_name (source);
    if (tag == "mo") return source;
    if (tag == "mrow" || tag == "mstyle") {
      const auto c= children (source);
      if (N(c) == 1) return core (c[0]);
    }
    return tree ("");
  }

  tree script_child (scheme_tree source, string variant) {
    if (element_name (source) == "none") {
      children (source, 0);
      return "";
    }
    return convert (source, variant);
  }

  bool accent_spec (scheme_tree source, bool above, string& g, bool& stretch) {
    if (element_name (source) != "mo") return false;
    auto& m= native_maps ();
    tree value;
    const string raw= text (source);
    auto lookup_accent= [&] (string key) {
      return lookup (above ? m.mathml_above : m.mathml_below, key, value) ||
             lookup (m.mathml_above_below, key, value);
    };
    bool found= lookup_accent (raw);
    if (!found) {
      const tree decoded= token (raw, source);
      found= is_atomic (decoded) && lookup_accent (decoded->label);
      if (!found && is_atomic (decoded)) {
        auto find_value= [&] (hashmap<string,tree>& accents) {
          for (iterator<string> it= iterate (accents); it->busy (); ) {
            const string key= it->next ();
            const tree candidate= accents[key];
            const tree mark= is_func (candidate, MATH_ACCENT) ? candidate[0] : candidate;
            tree visible;
            if (decoded == mark ||
                (lookup (m.mathml_operator, key, visible) && decoded == visible)) {
              value= candidate; return true;
            }
          }
          return false;
        };
        found= find_value (above ? m.mathml_above : m.mathml_below) ||
               find_value (m.mathml_above_below);
      }
    }
    if (!found) return false;
    stretch= is_func (value, MATH_ACCENT);
    g= stretch ? value[0]->label : value->label;
    stretch= boolean (source, "stretchy", stretch);
    return true;
  }

  tree limits (scheme_tree source, string variant) {
    const string tag= element_name (source);
    const auto c= children (source, tag == "munderover" ? 3 : 2);
    const scheme_tree op= core (c[0]);
    tree base= convert (c[0], variant, true);
    bool movable= false;
    string op_glyph;
    if (element_name (op) == "mo") {
      const tree value= token (text (op), op);
      if (is_atomic (value)) op_glyph= value->label;
      if (has (op, "movablelimits")) movable= boolean (op, "movablelimits", false);
      else if (big (op_glyph)) movable= !integral (op_glyph);
      else if (!arrow (op_glyph))
        fail (source, "operator limit policy requires explicit movablelimits");
    }
    tree below (""), above ("");
    bool decorated= false;
    for (int i=1; i<N(c); ++i) {
      const bool over= tag == "mover" || (tag == "munderover" && i == 2);
      string g;
      bool stretch= false;
      const bool known= accent_spec (c[i], over, g, stretch);
      const bool as_accent= boolean (source, over ? "accent" : "accentunder",
        boolean (c[i], "accent", known));
      if (as_accent) {
        if (!known || movable) fail (source, "unsupported accent/limit combination");
        plain_operator (c[i]);
        for (const char* key: {"largeop", "movablelimits", "fence", "separator", "form"})
          if (has (c[i], key)) fail (c[i], "unsupported accent operator property");
        base= tree (MATH_ACCENT, g, over ? "o" : "u", base);
        set_property (base, "math:accent-stretch", property (stretch));
        decorated= true;
      }
      else if (over) above= convert (c[i], variant);
      else below= convert (c[i], variant);
    }
    if (below == "" && above == "") return base;
    if (movable || (big (op_glyph) && boolean (op, "largeop", true)))
      return athena::math_v3::scripts (base, "", "", below, above, movable ? 'd' : 'o');
    if (!decorated && arrow (op_glyph) && boolean (op, "stretchy", true)) {
      plain_operator (op);
      if (base != tree (op_glyph))
        fail (source, "styled or embellished long arrow is unsupported");
      return tree (MATH_LONG_ARROW, op_glyph, below, above);
    }
    return tree (MATH_ANNOTATION, base, below, above);
  }

  string column_alignment (scheme_tree source, int column, string fallback) {
    if (!has (source, "columnalign")) return fallback;
    const auto columns= split_words (attribute (source, "columnalign"));
    if (N(columns) == 0) fail (source, "empty column alignment");
    for (int i=0; i<N(columns); ++i)
      if (halign (columns[i]) == "") fail (source, "unsupported column alignment");
    return halign (columns[min (column, N(columns)-1)]);
  }

  tree table (scheme_tree source, string variant) {
    const auto rows= children (source);
    if (N(rows) == 0) fail (source, "empty table");
    tree result (MATH_GRID);
    int width= -1;
    for (int i=0; i<N(rows); ++i) {
      const auto& row_src= rows[i];
      if (element_name (row_src) != "mtr") fail (source, "expected mtr");
      if (has (row_src, "id") || has (row_src, "displaystyle"))
        fail (row_src, "row identity/display overrides are unsupported");
      const auto cells= children (row_src);
      if (N(cells) == 0 || (width >= 0 && N(cells) != width))
        fail (row_src, "empty or nonrectangular table row");
      width= N(cells);
      tree row (ROW);
      const string row_variant= has (row_src, "mathvariant") ?
        attribute (row_src, "mathvariant") : variant;
      if (row_variant != "") variant_kind (row_src, row_variant);
      for (int j=0; j<N(cells); ++j) {
        const scheme_tree cell_src= cells[j];
        if (element_name (cell_src) != "mtd") fail (row_src, "expected mtd");
        for (const char* span: {"rowspan", "columnspan"})
          if (has (cell_src, span) && attribute (cell_src, span) != "1")
            fail (cell_src, "spanning cells are unsupported");
        if (N(split_words (attribute (cell_src, "columnalign"))) > 1)
          fail (cell_src, "cell alignment must be a single value");
        const string cell_variant= has (cell_src, "mathvariant") ?
          attribute (cell_src, "mathvariant") : row_variant;
        if (cell_variant != "") variant_kind (cell_src, cell_variant);
        tree body= sequence (cell_src, cell_variant);
        if (has (cell_src, "displaystyle"))
          body= tree (WITH, "math-display", attribute (cell_src, "displaystyle"), body);
        body= decorate_id (cell_src, body);
        tree cell (CELL, body);
        string alignment= column_alignment (source, j, "c");
        alignment= column_alignment (row_src, j, alignment);
        alignment= column_alignment (cell_src, 0, alignment);
        property::dictionary format;
        format["align"]= property (std::string (alignment.data (), std::size_t (N(alignment))));
        string color= attribute (source, "mathcolor");
        if (has (row_src, "mathcolor")) color= attribute (row_src, "mathcolor");
        if (has (cell_src, "mathcolor")) color= attribute (cell_src, "mathcolor");
        if (color != "") format["color"]= property (
          std::string (color.data (), std::size_t (N(color))));
        if (has (cell_src, "mathbackground")) {
          const string value= attribute (cell_src, "mathbackground");
          format["background"]= property (std::string (value.data (), std::size_t (N(value))));
        }
        set_property (cell, "math:cell-format", property (std::move (format)));
        row << cell;
      }
      result << row;
    }
    // MathML tables carry no matrix/align/eqnarray semantic kind. Keep a
    // generic grid with explicit cell alignments; do not infer numbering.
    set_property (result, "math:grid-kind", property (std::string ("generic")));
    return result;
  }

  tree convert (scheme_tree source, string variant= "", bool operator_base= false,
                bool enclosed= false) {
    const string tag= element_name (source);
    if (has (source, "mathvariant")) {
      variant= attribute (source, "mathvariant");
      variant_kind (source, variant);
    }
    tree result;
    if (tag == "*TOP*") {
      const auto c= children (source, 1);
      if (element_name (c[0]) != "math") fail (source, "expected one math root");
      return convert (c[0]);
    }
    if (tag == "mi" || tag == "mn" || tag == "mtext" || tag == "mo") {
      result= token (text (source), source);
      if (tag == "mo") {
        const string g= is_atomic (result) ? result->label : string ("");
        if (has (source, "movablelimits") && !operator_base)
          fail (source, "movablelimits requires a script/limit owner");
        if (boolean (source, "largeop", big (g))) {
          if (!big (g)) fail (source, "unmapped large operator");
          if (variant != "" && variant != "normal") fail (source, "styled large operator");
          result= athena::math_v3::big_operator (g);
          variant= "";
        }
        else if (boolean (source, "stretchy", false) && !operator_base) {
          if (enclosed && (g == "|" || g == "‖")) result= tree (MATH_MIDDLE, g);
          else fail (source, "stretchy operator needs a fence or limit owner");
        }
        if (attribute_is (source, "fence", "true") ||
            (!has (source, "fence") && fence_role (source, 0, 1) != 0))
          fail (source, "standalone fence is unsupported");
        if (attribute_is (source, "separator", "true") &&
            !one_of (g, {",", ";", "|", "‖"}))
          fail (source, "unsupported separator class");
      }
      if (tag == "mtext") {
        if (variant != "") fail (source, "mathvariant on text is unsupported");
        result= tree (WITH, "mode", "text", result);
      }
      else if (variant != "") result= tree (MATH_VARIANT, variant_kind (source, variant), result);
      else if (tag == "mi" && is_atomic (result) && result != "" &&
               utf8_grapheme_next (result->label, 0) < N(result->label))
        result= tree (MATH_VARIANT, "rm", result);
    }
    else if (tag == "math") {
      result= sequence (source, variant);
      if (attribute_is (source, "display", "block")) {
        result= tree (MATH_BLOCK, result);
        set_property (result, "math:block-numbered", property (false));
      }
      else result= node ("math", {result});
    }
    else if (tag == "mrow" || tag == "mstyle" || tag == "semantics") {
      if (tag == "semantics") result= convert (children (source, 1)[0], variant, operator_base, enclosed);
      else if (operator_base && N(children (source)) == 1)
        result= convert (children (source)[0], variant, true, enclosed);
      else result= sequence (source, variant, enclosed);
    }
    else if (tag == "mfrac" || tag == "mroot") {
      const auto c= children (source, 2);
      const tree first= convert (c[0], variant), second= convert (c[1], variant);
      if (tag == "mroot") result= tree (SQRT, first, second);
      else {
        if (boolean (source, "bevelled", false)) fail (source, "bevelled fraction");
        const string thickness= attribute (source, "linethickness");
        if (one_of (thickness, {"0", "0pt", "0px", "0em", "0ex", "0%"})) {
          result= tree (MATH_GRID, tree (ROW, tree (CELL, first)), tree (ROW, tree (CELL, second)));
          set_property (result, "math:grid-kind", property (std::string ("stack")));
        }
        else if (!has (source, "linethickness") || thickness == "medium")
          result= tree (FRAC, first, second);
        else fail (source, "custom fraction rule thickness");
      }
    }
    else if (tag == "msqrt") result= tree (SQRT, sequence (source, variant));
    else if (tag == "msub" || tag == "msup" || tag == "msubsup") {
      const auto c= children (source, tag == "msubsup" ? 3 : 2);
      const tree base= convert (c[0], variant, true);
      const tree sub= tag == "msup" ? tree ("") : convert (c[1], variant);
      const tree sup= tag == "msub" ? tree ("") : convert (c[N(c)-1], variant);
      result= athena::math_v3::scripts (base, "", "", sub, sup, 'r');
    }
    else if (tag == "mmultiscripts") {
      const auto c= children (source);
      if (N(c) == 0) fail (source, "missing multiscript base");
      tree lsub (""), lsup (""), rsub (""), rsup ("");
      bool left= false, seen_right= false, seen_left= false;
      for (int i=1; i<N(c);) {
        if (element_name (c[i]) == "mprescripts") {
          children (c[i], 0);
          if (left) fail (source, "duplicate mprescripts");
          left= true; ++i; continue;
        }
        if (i+1 >= N(c) || element_name (c[i+1]) == "mprescripts")
          fail (source, "incomplete script pair");
        if (left ? seen_left : seen_right)
          fail (source, "multiple tensor-script columns are unsupported");
        const tree sub= script_child (c[i], variant), sup= script_child (c[i+1], variant);
        if (left) { lsub= sub; lsup= sup; seen_left= true; }
        else { rsub= sub; rsup= sup; seen_right= true; }
        i+= 2;
      }
      result= athena::math_v3::scripts (convert (c[0], variant, true), lsub, lsup, rsub, rsup, 'r');
    }
    else if (tag == "munder" || tag == "mover" || tag == "munderover")
      result= limits (source, variant);
    else if (tag == "mfenced") {
      auto endpoint= [&] (const char* name, const char* fallback) {
        const string raw= has (source, name) ? attribute (source, name) : string (fallback);
        const tree value= token (trim_left (trim_right (collapse_spaces (raw))), source);
        if (!is_atomic (value) || !athena::math_v3::glyph (value->label, true))
          fail (source, "multi-glyph fence");
        return value->label;
      };
      const string left= endpoint ("open", "("), right= endpoint ("close", ")");
      const tree decoded= token (has (source, "separators") ?
        attribute (source, "separators") : string (","), source);
      if (!is_atomic (decoded)) fail (source, "structured separator");
      const string raw= decoded->label;
      const std::string_view view (raw.data (), std::size_t (N(raw)));
      array<string> separators;
      for (std::size_t i=0; i<view.size ();) {
        const auto end= athena::text::next_scalar (view, i);
        const string separator= raw (int(i), int(end));
        if (!(N(separator) == 1 && xml_space (separator[0]))) {
          if (!one_of (separator, {",", ";", "|", "‖"}))
            fail (source, "unsupported separator class");
          separators << separator;
        }
        i= end;
      }
      const auto c= children (source);
      array<tree> inside;
      for (int i=0; i<N(c); ++i) {
        inside << convert (c[i], variant);
        if (i+1<N(c) && N(separators)>0) {
          const string separator= separators[min (i, N(separators)-1)];
          inside << ((separator == "|" || separator == "‖") ?
            tree (MATH_MIDDLE, separator) : tree (separator));
        }
      }
      result= serial (inside);
      if (left != "" || right != "")
        result= athena::math_v3::delimited (left, right, 'a', result);
    }
    else if (tag == "mtable") result= table (source, variant);
    else fail (source, "element is not valid in this position");
    return finish (source, result);
  }

public:
  tree run (scheme_tree source) {
    validate (source, Namespaces (), 0);
    if (scheme_string (source)) fail (source, "expected an element, not text");
    return convert (source);
  }
};

} // namespace native_mathml

} // namespace

tree
mathml_to_tree (scheme_tree t) {
  return convert_mathml (t);
}

tree
mathml_to_tree_v3 (scheme_tree t) {
  return native_mathml::Importer ().run (t);
}

tree
mathml_xml_to_tree_v3 (string raw_xml) {
  // Only explicit source-string MathML v3 routes use lexical entity-preserving
  // parsing. Existing v2 HTML/MathML and already-parsed SXML callers retain
  // their old contracts, which cannot retroactively recover escaped text.
  return native_mathml::Importer ().run (parse_xml_mathml_v3 (raw_xml));
}

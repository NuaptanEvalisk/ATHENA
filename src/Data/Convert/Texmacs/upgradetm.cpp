
/******************************************************************************
* MODULE     : upgradetm.cpp
* DESCRIPTION: normalize imported TeX and MathML trees
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "convert.hpp"
#include "converter.hpp"
#include "hashset.hpp"
#include "path.hpp"
#include "vars.hpp"
#include "drd_std.hpp"
#include <stdio.h>
#include "scheme.hpp"
#include "tree_correct.hpp"
#include "merge_sort.hpp"

static thread_local bool upgrade_tex_flag= false;
double get_magnification (string s);

/******************************************************************************
* Macro expansion predicates
******************************************************************************/

static bool
is_expand (tree t) {
  return ((L(t) == EXPAND) || (L(t) == VAR_EXPAND) || (L(t) == HIDE_EXPAND));
}

static bool
is_expand (tree t, string s, int n) {
  return is_expand (t) && (N(t) == n+1) && (t[0] == s);
}

/******************************************************************************
* Upgrade lambda application -> macro expansion and value keyword
******************************************************************************/

tree
upgrade_apply_expand_value (tree t, hashset<string> H) {
  if (is_atomic (t)) return t;
  else {
    int i, n= arity (t);
    tree r (t, n);
    if (is_func (t, APPLY))
      if ((n >= 1) && is_atomic (t[0]) && H->contains (t[0]->label)) {
        if (n == 1) r= tree (VALUE, n);
        else r= tree (EXPAND, n);
      }
    for (i=0; i<n; i++)
      r[i]= upgrade_apply_expand_value (t[i], H);
    return r;
  }
}

typedef const char* charp;
static charp apply_expand_value_strings[]= {
  "part", "part*", "chapter", "chapter*", "appendix",
  "section", "section*", "subsection", "subsection*",
  "subsubsection", "subsubsection*",
  "paragraph", "paragraph*", "subparagraph", "subparagraph*",
  "footnote", "item*", "overline", "underline",
  "mathord", "mathbin", "mathopen", "mathpunct",
  "mathop", "mathrel", "mathclose", "mathalpha",
  "op", "strong", "em", "tt", "name", "samp", "abbr",
  "dfn", "kbd", "var", "acronym", "person",
  "menu", "submenu", "subsubmenu", "tmdef", "tmref",
  "key", "skey", "ckey", "akey", "mkey", "hkey", "binom",
  //"ma", "mb", "md", "me", "mf", "mg", "mh", "mi", "mj", "mk",
  //"mm", "mn", "mu", "mv", "mw", "my", "mz",
  //"MA", "MB", "MD", "ME", "MF", "MG", "MH", "MI", "MJ", "MK",
  //"MM", "MN", "MU", "MV", "MW", "MY", "MZ",
  ""
};

tree
upgrade_apply_expand_value (tree t) {
  int i;
  hashset<string> H;
  for (i=0; apply_expand_value_strings[i][0] != '\0'; i++)
    H->insert (apply_expand_value_strings[i]);
  return upgrade_apply_expand_value (t, H);
}

/******************************************************************************
* Subroutines for upgrading set/reset -> with, begin/end -> apply
******************************************************************************/

static bool
matching (tree open, tree close) {
  if (is_func (open, SET, 2))
    return is_func (close, RESET, 1) && (open[0] == close[0]);
  if (is_func (open, BEGIN)) {
    if (is_func (close, END, 1))
      return open[0] == close[0];
    if (is_func (close, END, 2))
      return open[0] == "algo-repeat" && open[0] == close[0];
  }
  return false;
}

static tree
with_replace (tree var, tree val, tree body) {
  if (is_func (body, WITH)) {
    int i, n= N(body);
    tree t= tree (WITH, n+2);
    t[0]= var;
    t[1]= val;
    for (i=0; i<n; i++) t[i+2]= body[i];
    return t;
  }
  else return tree (WITH, var, val, body);
}

static tree
expand_replace (tree begin, tree end, tree body) {
  int i, j, k= N(begin), l= N(end);
  tree expand (EXPAND, k + l);
  for (i=0; i<k; i++) expand[i]= begin[i];
  for (j=1; j<l; i++, j++) expand[i]= end[j];
  expand[i]= body;
  return expand;
}

static void
concat_search (tree t, int& i, tree open= "") {
  bool set_reset= (open == "") || is_func (open, SET) || is_func (open, RESET);
  bool begin_end= (open == "") || is_func (open, BEGIN) || is_func (open, END);
  int n= N(t);
  while (i<n) {
    if (set_reset && is_func (t[i], SET, 2)) return;
    if (set_reset && is_func (t[i], RESET, 1)) return;
    if (begin_end && is_func (t[i], BEGIN)) return;
    if (begin_end && is_func (t[i], END, 1)) return;
    if (begin_end && is_func (t[i], END, 2) &&
        t[i][0] == "algo-repeat") return;
    i++;
  }
}

static tree
concat_replace (tree t, int i1, int i2) {
  int i;
  tree v (CONCAT);
  for (i=i1+1; i<i2; i++) v << t[i];
  if (N(v)==0) v= "";
  else if (N(v)==1) v= v[0];
  if (is_func (t[i1], SET))
    return with_replace (t[i1][0], t[i1][1], v);
  else return expand_replace (t[i1], t[i2], v);
}

static tree
document_explode (tree t) {
  int i, n= N(t);
  tree u (t, n);
  for (i=0; i<n; i++)
    if (is_concat (t[i])) u[i]= t[i];
    else u[i]= tree (CONCAT, t[i]);
  return u;
}

static tree
document_contract (tree t) {
  int i, n= N(t);
  tree u (t, n);
  for (i=0; i<n; i++)
    if (N(t[i]) == 0) u[i]= "";
    else if (N(t[i]) == 1) u[i]= t[i][0];
    else u[i]= t[i];
  return u;
}

static void
document_search (tree t, int& i, int& j, tree open= "") {
  bool set_reset= (open == "") || is_func (open, SET) || is_func (open, RESET);
  bool begin_end= (open == "") || is_func (open, BEGIN) || is_func (open, END);
  int n= N(t);
  while (i<n) {
    int k= N(t[i]);
    while (j<k) {
      if (set_reset && is_func (t[i][j], SET, 2)) return;
      if (set_reset && is_func (t[i][j], RESET, 1)) return;
      if (begin_end && is_func (t[i][j], BEGIN)) return;
      if (begin_end && is_func (t[i][j], END, 1)) return;
      if (begin_end && is_func (t[i][j], END, 2) &&
          t[i][j][0] == "algo-repeat") return;
      j++;
    }
    i++;
    j=0;
  }
}

static void
document_inc (tree doc_t, int& doc_i, int& con_i) {
  con_i++;
  if (con_i == N(doc_t[doc_i])) {
    doc_i++;
    con_i= 0;
  }
}

static void
document_inc (tree& doc, tree& con, tree doc_t, int& doc_i, int& con_i) {
  con_i++;
  if (con_i == N(doc_t[doc_i])) {
    doc << con;
    con= tree (CONCAT);
    doc_i++;
    con_i= 0;
  }
}

static void
document_merge (tree& doc, tree& con, tree doc_t,
                int doc_1, int con_1, int doc_2, int con_2)
{
  int doc_i= doc_1, con_i= con_1;
  while ((doc_i<doc_2) || ((doc_i==doc_2) && (con_i<con_2))) {
    con << doc_t[doc_i][con_i];
    document_inc (doc, con, doc_t, doc_i, con_i);
  }
}

static tree
document_replace (tree doc_t, int doc_1, int con_1, int doc_2, int con_2) {
  tree doc_b (DOCUMENT), con_b (CONCAT);
  int doc_i= doc_1, con_i= con_1;
  document_inc (doc_b, con_b, doc_t, doc_i, con_i);
  document_merge (doc_b, con_b, doc_t, doc_i, con_i, doc_2, con_2);
  doc_b << con_b;
  doc_b= document_contract (doc_b);
  bool flag= (doc_1!=doc_2) || ((con_1==0) && (con_2==N(doc_t[doc_2])-1));
  /*
  if (N(doc_b) != (doc_2-doc_1+1))
    cout << (doc_2-doc_1+1) << ", " << doc_b << "\n";
  */
  if ((!flag) && (N(doc_b)==1)) doc_b= doc_b[0];
  if (is_func (doc_t[doc_1][con_1], SET))
    return with_replace (doc_t[doc_1][con_1][0],
                         doc_t[doc_1][con_1][1],
                         doc_b);
  else
    return expand_replace (doc_t[doc_1][con_1],
                           doc_t[doc_2][con_2],
                           doc_b);
}

/******************************************************************************
* Upgrade set/reset -> with, begin/end -> apply
******************************************************************************/

static tree upgrade_set_begin (tree t);

static tree
upgrade_set_begin_default (tree t) {
  int i, n= N(t);
  tree u (t, n);
  for (i=0; i<n; i++)
    u[i]= upgrade_set_begin (t[i]);
  return u;
}

static tree
upgrade_set_begin_concat_once (tree t) {
  // cout << "in : " << t << "\n";
  int i=0, n= N(t);
  tree u (CONCAT);
  while (i<n) {
    int i0=i, i1, i2;
    concat_search (t, i);
    i1= i;
    for (i=i0; i<i1; i++) u << t[i];
    if (i==n) {
      // cout << "  " << i0 << ", " << i1 << "\n";
      break;
    }
    i++;
    concat_search (t, i, t[i1]);
    i2= i;
    // cout << "  " << i0 << ", " << i1 << ", " << i2 << "\n";
    if ((i2<n) && matching (t[i1], t[i2])) {
      u << concat_replace (t, i1, i2);
      i= i2+1;
    }
    else {
      i= i1;
      if (i == i0) u << t[i++];
    }
  }
  // cout << "out: " << u << "\n";
  // cout << "-------------------------------------------------------------\n";
  // fflush (stdout);
  return u;
}

static tree
upgrade_set_begin_concat (tree t) {
  tree u= t;
  do { t= u; u= upgrade_set_begin_concat_once (t); } while (u != t);
  u= upgrade_set_begin_default (u);
  if (N(u) == 1) return u[0];
  return u;
}

static void
upgrade_verbatim_expand (tree& doc, tree& con, tree ins) {
  tree& body= ins[N(ins)-1];
  if (is_document (body) && (N(body)>1)) {
    int n= N(body);
    int start=0, end=n;
    if (body[0] == "") start= 1;
    if (body[n-1] == "") end= n-1;
    body= body (start, end);
    if (start != 0) {
      doc << con;
      con= tree (CONCAT);
    }
    con << ins;
    if (end != n) {
      doc << con;
      con= tree (CONCAT);
    }
  }
  else con << ins;
}

static void
upgrade_abstract_expand (tree& doc, tree& con, tree ins) {
  (void) doc;
  tree& body= ins[N(ins)-1];
  if (is_document (body) && (N(body) > 1) && (body[0] == ""))
    body= body (1, N(body));
  con << ins;
}

static tree
upgrade_set_begin_document_once (tree doc_t) {
  // cout << "in : " << doc_t << "\n";
  int doc_i=0, con_i=0;
  tree doc (DOCUMENT), con (CONCAT);
  while ((doc_i < N(doc_t)) && (con_i < N(doc_t[doc_i]))) {
    int doc_0= doc_i, con_0= con_i;
    document_search (doc_t, doc_i, con_i);
    int doc_1= doc_i, con_1= con_i;
    // cout << "  0: " << doc_0 << ", " << con_0 << "\n";
    // cout << "  1: " << doc_1 << ", " << con_1 << "\n";
    document_merge (doc, con, doc_t, doc_0, con_0, doc_1, con_1);
    if (doc_i == N(doc_t)) break;
    document_inc (doc_t, doc_i, con_i);
    document_search (doc_t, doc_i, con_i, doc_t[doc_1][con_1]);
    int doc_2= doc_i, con_2= con_i;
    // cout << "  2: " << doc_2 << ", " << con_2 << "\n";
    if ((doc_2 < N(doc_t)) &&
        matching (doc_t[doc_1][con_1], doc_t[doc_2][con_2]))
      {
        tree ins= document_replace (doc_t, doc_1, con_1, doc_2, con_2);
        // cout << "ins: " << ins << "\n";
        if (is_func (ins, EXPAND, 2)) {
          if ((ins[0] == "verbatim") || (ins[0] == "code") ||
              (upgrade_tex_flag && is_verbatim (compound (as_string(ins[0])))))
            upgrade_verbatim_expand (doc, con, ins);
          else if (ins[0] == "abstract")
            upgrade_abstract_expand (doc, con, ins);
          else con << ins;
        }
        else if (is_func (ins, WITH) &&
                 is_func (ins[N(ins)-1], DOCUMENT, 1)) {
          ins[N(ins)-1]= ins[N(ins)-1][0];
          con << ins;
        }
        else con << ins;
        document_inc (doc, con, doc_t, doc_i, con_i);
      }
    else {
      doc_i= doc_1; con_i= con_1;
      if ((doc_i == doc_0) && (con_i == con_0)) {
        con << doc_t[doc_i][con_i];
        document_inc (doc, con, doc_t, doc_i, con_i);     
      }
    }
  }
  // cout << "out: " << doc << "\n";
  // cout << "-------------------------------------------------------------\n";
  fflush (stdout);
  return doc;
}

static tree
upgrade_set_begin_document (tree t) {
  tree u= t;
  do {
    t= u;
    u= document_explode (u);
    u= upgrade_set_begin_document_once (u);
    u= document_contract (u);
  } while (u != t);
  u= upgrade_set_begin_default (u);
  return u;
}

static tree
upgrade_set_begin_surround (tree t, tree search, bool& found) {
  if (t == search) {
    found= true;
    if (upgrade_tex_flag)
      return tree (DOCUMENT, copy (t));
    return copy (t);
  }
  if (is_func (t, WITH) || is_func (t, EXPAND)) {
    tree u= copy (t);
    u[N(u)-1]= upgrade_set_begin_surround (u[N(u)-1], search, found);
    return u;
  }
  if (is_concat (t)) {
    int i, n= N(t), searching= !found;
    tree left (CONCAT), middle, right (CONCAT);
    for (i=0; i<n; i++) {
      middle= upgrade_set_begin_surround (t[i], search, found);
      if (searching && found) break;
      else left << middle;
    }
    if (i==n) return copy (t);
    for (i++; i<n; i++)
      right << upgrade_set_begin_surround (t[i], search, found);
    if (N(left) == 0) left= "";
    else if (N(left) == 1) left= left[0];
    if (N(right) == 0) right= "";
    else if (N(right) == 1) right= right[0];
    return tree (SURROUND, left, right, middle);
  }
  return copy (t);
}

static tree
upgrade_env_args (tree t, tree env) {
  if (is_atomic (t)) return t;
  else if (is_func (t, APPLY, 1)) {
    int i, k= N(env);
    for (i=0; i<k-2; i++)
      if (t[0] == env[i])
        return tree (ARG, t[0]);
    return t;
  }
  else {
    int i, n= N(t);
    tree r (t, n);
    for (i=0; i<n; i++)
      r[i]= upgrade_env_args (t[i], env);
    return r;
  }
}

static tree
upgrade_set_begin_env (tree t) {
  //cout << "in  : " << t << "\n";
  int i, n= N(t);
  tree u (MACRO, n);
  for (i=0; i<n-2; i++)
    u[i]= upgrade_set_begin (t[i]);
  string s= "body";
  for (i=0; i<n-2; i++)
    if (t[i] == "body") s= "body*";
  u[n-2]= copy (s);

  tree begin= t[n-2], end= t[n-1], body (CONCAT);
  if (begin == "") begin= tree (CONCAT);
  else if (!is_concat (begin)) begin= tree (CONCAT, begin);
  if (end == "") end= tree (CONCAT);
  else if (!is_concat (end)) end= tree (CONCAT, end);
  body << A(begin) << tree (ARG, copy (s)) << A(end);
  //cout << "mid1: " << body << "\n";
  body= upgrade_set_begin_concat (body);
  body= upgrade_env_args (body, t);
  //cout << "mid2: " << body << "\n";
  bool found= false;
  u[n-1]= upgrade_set_begin_surround (body, tree (ARG, s), found);
  //cout << "out : " << u << "\n";
  //cout << "-------------------------------------------------------------\n";
  return u;
}

static tree
upgrade_set_begin (tree t) {
  if (is_atomic (t)) return copy (t);
  else {
    if (is_concat (t)) return upgrade_set_begin_concat (t);
    else if (is_document (t)) return upgrade_set_begin_document (t);
    else if (is_func (t, ENV)) return upgrade_set_begin_env (t);
    else return upgrade_set_begin_default (t);
  }
}

static tree
eliminate_set_begin (tree t) {
  if (is_atomic (t)) return t;
  if (is_func (t, SET) || is_func (t, RESET) ||
      is_func (t, BEGIN) || is_func (t, END) ||
      is_func (t, ENV)) return "";

  int i, n= N(t);
  if (is_concat (t)) {
    tree r (CONCAT);
    for (i=0; i<n; i++) {
      tree u= eliminate_set_begin (t[i]);
      if (u != "") r << u;
    }
    if (N(r) == 0) return "";
    if (N(r) == 1) return r[0];
    return r;
  }
  else {
    tree r (t, n);
    for (i=0; i<n; i++)
      r[i]= eliminate_set_begin (t[i]);
    return r;
  }
}

/******************************************************************************
* Upgrade surround, indentation after and final routine
******************************************************************************/

bool
expand_needs_surrounding (string s) {
  return
    (s == "maketitle") || (s == "abstract") ||
    (s == "theorem") || (s == "proposition") || (s == "lemma") ||
    (s == "corollary") || (s == "proof") || (s == "axiom") ||
    (s == "definition") || (s == "notation") || (s == "conjecture") ||
    (s == "remark") || (s == "note") || (s == "example") ||
    (s == "exercise") || (s == "warning") ||
    (s == "convention") || (s == "acknowledgments") ||
    (s == "code") || (s == "quote") ||
    (s == "quotation") || (s == "verse") || (s == "center") ||
    (s == "indent") || (s == "body") || (s == "description") ||
    starts (s, "itemize") || starts (s, "enumerate");
}

static bool
with_needs_surrounding (string s) {
  return
    (s == "paragraph mode") || (s == "paragraph hyphenation") ||
    (s == "paragraph width") || (s == "left margin") ||
    (s == "right margin") || (s == "first indentation") ||
    (s == "last indentation") || (s == "no first indentation") ||
    (s == "no last indentation") || (s == "interline space") ||
    (s == "horizontal ink separation") || (s == "line stretch") ||
    (s == "interparagraph space");
}

static bool
needs_surrounding (tree t) {
  if (is_multi_paragraph (t)) return true;
  if ((is_func (t, APPLY) || is_func (t, EXPAND)) && is_atomic (t[0])) {
    if (t[0] == "verbatim") return (N(t)==2) && is_multi_paragraph (t[1]);
    return expand_needs_surrounding (t[0]->label);
  }
  if (is_func (t, WITH)) {
    int i, n= N(t)-1;
    for (i=0; i<n; i+=2)
      if (is_atomic (t[i]) && with_needs_surrounding (t[i]->label))
        return true;
  }
  return false;
}

static bool
needs_transfer (tree t) {
  return
    is_func (t, EXPAND, 2) &&
    ((t[0] == "equation") || (t[0] == "equation*") ||
     (t[0] == "eqnarray*") || (t[0] == "leqnarray*"));
}

static tree
upgrade_surround (tree t) {
  if (upgrade_tex_flag || is_atomic (t)) return t;
  int i, n= N(t);
  tree r (t, n);
  for (i=0; i<n; i++) {
    tree u= t[i];
    if (is_document (t) && is_concat (u) && (N(u)>1)) {
      int j, k= N(u);
      for (j=0; j<k; j++)
        if (needs_surrounding (u[j])) {
          tree before= u (0  , j);
          tree after = u (j+1, k);
          tree body  = upgrade_surround (u[j]);
          if (N(before)==0) before= "";
          if (N(before)==1) before= before[0];
          if (N(after )==0) after = "";
          if (N(after )==1) after = after [0];
          before= upgrade_surround (before);
          after = upgrade_surround (after );
          r[i]= tree (SURROUND, before, after, body);
          break;
        }
        else if (needs_transfer (u[j])) {
          tree temp= upgrade_surround (u[j][1]);
          if (!is_concat (temp)) temp= tree (CONCAT, temp);
          tree body= u (0, j);
          body << A (temp) << A (u (j+1, k));
          r[i]= tree (EXPAND, u[j][0], body);
          break;
        }
      if (j<k) continue;
    }
    r[i]= upgrade_surround (u);
  }
  return r;
}

static tree
upgrade_indent (tree t) {
  if (is_atomic (t)) return t;
  else if (t == tree (ASSIGN, "no first indentation", "true"))
    return tree (FORMAT, "no indentation after");
  else if (t == tree (ASSIGN, "no first indentation", "false"))
    return tree (FORMAT, "enable indentation after");
  else {
    int i, n= N(t);
    tree r (t, n);
    for (i=0; i<n; i++)
      r[i]= upgrade_indent (t[i]);
    return r;
  }
}

static tree
upgrade_equations (tree t) {
  if (!upgrade_tex_flag || is_atomic (t)) return t;
  else if (needs_transfer (t) && !is_document (t[1])) {
    t[1]= document (upgrade_equations (t[1]));
    return t;
  }
  else {
    int i, n= N(t);
    tree r (t, n);
    for (i=0; i<n; i++)
      r[i]= upgrade_equations (t[i]);
    return r;
  }
}

static tree
upgrade_new_environments (tree t) {
  t= upgrade_set_begin (t);
  t= eliminate_set_begin (t);
  t= upgrade_surround (t);
  t= upgrade_indent (t);
  t= upgrade_equations (t);
  return t;
}

/******************************************************************************
* Upgrade items
******************************************************************************/

static tree
upgrade_items (tree t) {
  if (is_atomic (t)) return t;
  else if ((t == tree (APPLY, "item")) || (t == tree (VALUE, "item")))
    return tree (EXPAND, "item");
  else {
    int i, n= N(t);
    tree r (t, n);
    for (i=0; i<n; i++)
      r[i]= upgrade_items (t[i]);
    return r;
  }
}

/******************************************************************************
* Normalize TeX equation/table line markers
******************************************************************************/

static tree
normalize_tex_table_breaks (tree t, bool eq= false) {
  int i, n= N(t);
  if (is_atomic (t)) return t;
  else if (is_concat (t)) {
    tree r (CONCAT);
    int nr_rows=1, nr_cols=1, sep=1;
    for (i=0; i<n; i++) {
      tree u= normalize_tex_table_breaks (t[i]);
      if (u == tree (FORMAT, "line separator")) sep++;
      if (u == tree (FORMAT, "next line")) {
        nr_cols= max (sep, nr_cols);
        sep= 1;
        nr_rows++;
      }
      r << u;
    }
    nr_cols= max (sep, nr_cols);
    if (nr_cols == 1 && nr_rows == 1 && !eq) return r;
    else {
      int col=0, row=0;
      tree T (TABLE, nr_rows);
      for (row=0; row<nr_rows; row++) {
        tree R (ROW, nr_cols);
        for (col=0; col<nr_cols; col++) R[col]= tree (CELL, "");
        T[row]= R;
      }

      tree u (CONCAT);
      row= col= 0;
      for (i=0; i<N(r); i++)
        if ((r[i] == tree (FORMAT, "line separator")) ||
            (r[i] == tree (FORMAT, "next line")))
          {
            if (N(u) == 0) u= "";
            else if (N(u) == 1) u= u[0];
            T[row][col][0]= u;
            u= tree (CONCAT);
            if (r[i] == tree (FORMAT, "line separator")) col++;
            else {
              row++;
              col= 0;
            }
          }
        else u << r[i];
      if (N(u) == 0) u= "";
      else if (N(u) == 1) u= u[0];
      T[row][col][0]= u;
      r= T;
    }

    tree tf (TFORMAT);
    if (r == tree (CONCAT)) r= "";
    else if (is_func (r, CONCAT, 1)) r= r[0];
    tf << r;
    return tf;
  }
  else {
    tree r (t, n);
    if (n == 1 || is_func (t, EXPAND, 2)) {
      string s= as_string (L(t));
      if (is_func (t, EXPAND, 2) && is_atomic (t[0])) s= t[0]->label;
      if (ends (s, "*")) s= s (0, N(s)-1);
      if (s == "eqnarray" || s == "align" || s == "multline" ||
          s == "gather" || s == "eqsplit") {
        tree arg= t[n-1];
        if (is_func (arg, DOCUMENT, 1)) arg= arg[0];
        if (!is_concat (arg)) arg= tree (CONCAT, arg);
        r= copy (t);
        r[n-1]= normalize_tex_table_breaks (arg, true);
        return r;
      }
    }
    else if (upgrade_tex_flag && (n == 2 || is_func (t, EXPAND, 3))) {
      string s= as_string (L(t));
      if (is_func (t, EXPAND, 3) && is_atomic (t[0])) s= t[0]->label;
      if (ends (s, "*")) s= s (0, N(s)-1);
      if (s == "alignat") {
        tree arg= t[n-1];
        if (is_func (arg, DOCUMENT, 1)) arg= arg[0];
        if (!is_concat (arg)) arg= tree (CONCAT, arg);
        r= copy (t);
        r[n-1]= normalize_tex_table_breaks (arg, true);
        return r;
      }
    }
    for (i=0; i<n; i++)
      r[i]= normalize_tex_table_breaks (t[i]);
    return r;
  }
}

/******************************************************************************
* Upgrade modified symbols
******************************************************************************/

static bool
is_with (tree t, string var, string val) {
  return is_func (t, WITH, 3) && (t[0] == var) && (t[1] == val);
}

static bool
is_var_with (tree t, string var, string val) {
  return is_with (t, var, val) || is_with (t, replace (var, " ", "-"), val);
}

static bool
is_alpha (tree t) {
  if (is_compound (t)) return false;
  string s= t->label;
  return (N(s) == 1) && is_alpha (s[0]);
}

static bool
is_alpha_numeric (tree t) {
  if (is_compound (t)) return false;
  string s= t->label;
  return (N(s) == 1) && (is_alpha (s[0]) || is_numeric (s[0]));
}

static bool
is_upper (tree t) {
  if (is_compound (t)) return false;
  string s= t->label;
  return (N(s) == 1) && is_upcase (s[0]);
}

static bool
is_bold (tree t) {
  if (is_compound (t)) return false;
  if (is_alpha_numeric (t)) return true;
  string s= locase_all (t->label);
  return
    (s == "<alpha>") || (s == "<beta>") || (s == "<gamma>") ||
    (s == "<delta>") || (s == "<epsilon>") || (s == "<zeta>") ||
    (s == "<eta>") || (s == "<theta>") || (s == "<iota>") ||
    (s == "<kappa>") || (s == "<lambda>") || (s == "<mu>") ||
    (s == "<nu>") || (s == "<xi>") || (s == "<omicron>") ||
    (s == "<pi>") || (s == "<rho>") || (s == "<sigma>") ||
    (s == "<tau>") || (s == "<upsilon>") || (s == "<phi>") ||
    (s == "<psi>") || (s == "<chi>") || (s == "<omega>") ||
    (s == "<varepsilon>") || (s == "<vartheta>") || (s == "<varkappa>") ||
    (s == "<varpi>") || (s == "<varrho>") || (s == "<varsigma>") ||
    (s == "<varphi>") || (s == "<backepsilon>") || (s == "<mho>") ||
    (s == "<Backepsilon>") || (s == "<Mho>") || (s == "<ell>");
}

static tree
upgrade_mod_symbol (string prefix, string s) {
  if (N(s) == 1) return "<" * prefix * s * ">";
  else return "<" * prefix * s (1, N(s)-1) * ">";
}

static tree
upgrade_mod_symbols (tree t) {
  if (is_atomic (t)) return t;
  if (is_func (t, WITH) && N(t) > 3 &&
      (t[0] == "mode" ||
       (is_atomic (t[0]) && starts (t[0]->label, "math"))) &&
      is_atomic (t[2]) && starts (t[2]->label, "math")) {
    tree u (WITH, t[0], t[1], t (2, N(t)));
    tree r= upgrade_mod_symbols (u);
    if (is_atomic (r)) return r;
    if (is_with (r, "mode", "math") && is_atomic (r[2])) return r;
  }
  if (is_var_with (t, "math font series", "bold") && is_bold (t[2]))
    return upgrade_mod_symbol ("b-", t[2]->label);
  else if (is_var_with (t, "math font", "cal") && is_upper (t[2]))
    return upgrade_mod_symbol ("cal-", t[2]->label);
  else if (is_var_with (t, "math font", "Euler") && is_alpha (t[2]))
    return upgrade_mod_symbol ("frak-", t[2]->label);
  else if (is_var_with (t, "math font", "Bbb") && is_alpha (t[2]))
    return upgrade_mod_symbol ("bbb-", t[2]->label);
  else if (is_var_with (t, "math font", "Bbb*") && is_alpha (t[2]))
    return upgrade_mod_symbol ("bbb-", t[2]->label);
  else if (is_var_with (t, "math font series", "bold") &&
           is_var_with (t[2], "math font", "cal") && is_upper (t[2][2]))
    return upgrade_mod_symbol ("b-cal-", t[2][2]->label);
  else if (is_var_with (t, "math font", "cal") &&
           is_var_with (t[2], "math font series", "bold") && is_upper (t[2][2]))
    return upgrade_mod_symbol ("b-cal-", t[2][2]->label);
  //else if ((is_func (t, VALUE, 1) || is_func (t, EXPAND, 1) ||
  //         is_func (t, APPLY, 1)) && (is_atomic (t[0]))) {
  //  string s= t[0]->label;
  //  if ((N(s) == 2) && ((s[0]=='m') && (s[1]>='a') && s[1]<='z'))
  //    return upgrade_mod_symbol ("frak-", s(1,2));
  //  if ((N(s) == 2) && ((s[0]=='M') && (s[1]>='A') && s[1]<='Z'))
  //    return upgrade_mod_symbol ("frak-", s(1,2));
  //  return t;
  //}
  else {
    int i, n= N(t);
    tree r (t, n);
    for (i=0; i<n; i++)
      r[i]= upgrade_mod_symbols (t[i]);
    return r;
  }
}

/******************************************************************************
* Upgrading menus in the help
******************************************************************************/

static tree
upgrade_menus_in_help (tree t) {
  if (is_atomic (t)) return t;
  if (is_expand (t, "menu", 1) || is_expand (t, "submenu", 2) ||
      is_expand (t, "subsubmenu", 3) || is_expand (t, "subsubsubmenu", 4)) {
    int i, n= N(t);
    tree r (APPLY, n);
    r[0]= "menu";
    for (i=1; i<n; i++) r[i]= t[i];
    return r;
  }
  else {
    int i, n= N(t);
    tree r (t, n);
    for (i=0; i<n; i++)
      r[i]= upgrade_menus_in_help (t[i]);
    return r;
  }
}

static tree
capitalize_sub (tree t) {
  if (is_atomic (t)) return upcase_first (t->label);
  else return t;
}

static tree
upgrade_capitalize_menus (tree t) {
  if (is_atomic (t)) return t;
  if (is_func (t, APPLY) && (t[0] == "menu")) {
    int i, n= N(t);
    tree r (APPLY, n);
    r[0]= "menu";
    for (i=1; i<n; i++) r[i]= capitalize_sub (t[i]);
    return r;
  }
  else {
    int i, n= N(t);
    tree r (t, n);
    for (i=0; i<n; i++)
      r[i]= upgrade_capitalize_menus (t[i]);
    return r;
  }
}

/******************************************************************************
* Upgrade sessions
******************************************************************************/

static tree
upgrade_formatting (tree t) {
  if (is_atomic (t)) return t;
  else if (is_func (t, FORMAT, 1)) {
    string name= replace (t[0]->label, " ", "-");
    if (name == "line-separator") name= "line-sep";
    else if (name == "no-line-break") name= "no-break";
    else if (name == "no-first-indentation") name= "no-indent";
    else if (name == "enable-first-indentation") name= "yes-indent";
    else if (name == "no-indentation-after") name= "no-indent*";
    else if (name == "enable-indentation-after") name= "yes-indent*";
    else if (name == "page-break-before") name= "page-break*";
    else if (name == "no-page-break-before") name= "no-page-break*";
    else if (name == "no-page-break-after") name= "no-page-break";
    else if (name == "new-page-before") name= "new-page*";
    else if (name == "new-double-page-before") name= "new-dpage*";
    else if (name == "new-double-page") name= "new-dpage";
    return tree (as_tree_label (name));
  }
  else {
    int i, n= N(t);
    tree r (t, n);
    for (i=0; i<n; i++)
      r[i]= upgrade_formatting (t[i]);
    return r;
  }
}

/******************************************************************************
* Upgrade expand
******************************************************************************/

static tree
upgrade_expand (tree t, tree_label WHICH_EXPAND) {
  if (is_atomic (t)) return t;
  else if (is_func (t, WHICH_EXPAND) && is_atomic (t[0])) {
    int i, n= N(t)-1;
    string s= t[0]->label;
    if (s == "quote") s= s * "-env";
    tree_label l= make_tree_label (s);
    tree r (l, n);
    for (i=0; i<n; i++)
      r[i]= upgrade_expand (t[i+1], WHICH_EXPAND);
    return r;
  }
  else if (is_func (t, ASSIGN, 2) &&
           (t[0] == "quote") &&
           is_func (t[1], MACRO)) {
    tree arg= upgrade_expand (t[1], WHICH_EXPAND);
    return tree (ASSIGN, t[0]->label * "-env", arg);
  }
  else {
    int i, n= N(t);
    tree r (t, n);
    for (i=0; i<n; i++)
      r[i]= upgrade_expand (t[i], WHICH_EXPAND);
    return r;
  }
}

static tree
upgrade_xexpand (tree t) {
  if (is_atomic (t)) return t;
  else {
    int i, n= N(t);
    tree r (t, n);
    if (is_expand (t))
      r= tree (COMPOUND, n);
    for (i=0; i<n; i++)
      r[i]= upgrade_xexpand (t[i]);
    return r;
  }
}

/******************************************************************************
* Upgrade apply
******************************************************************************/

static tree
upgrade_apply (tree t) {
  if (is_atomic (t)) return t;
  /*
  if (is_func (t, APPLY))
    cout << t[0] << "\n";
  */
  if (is_func (t, APPLY) && is_atomic (t[0])) {
    int i, n= N(t)-1;
    string s= t[0]->label;
    tree_label l= make_tree_label (s);
    tree r (l, n);
    for (i=0; i<n; i++)
      r[i]= upgrade_apply (t[i+1]);
    return r;
  }

  int i, n= N(t);
  tree r (t, n);
  if (is_func (t, APPLY))
    r= tree (COMPOUND, n);
  for (i=0; i<n; i++)
    r[i]= upgrade_apply (t[i]);
  return r;
}

static tree
upgrade_function_arg (tree t, tree var) {
  if (is_atomic (t)) return t;
  else if ((t == tree (APPLY, var)) || (t == tree (VALUE, var)))
    return tree (ARG, var);
  else {
    int i, n= N(t);
    tree r (t, n);
    for (i=0; i<n; i++)
      r[i]= upgrade_function_arg (t[i], var);
    return r;
  }
}

static tree
upgrade_function (tree t) {
  if (is_atomic (t)) return t;
  if (is_func (t, ASSIGN, 2) && is_func (t[1], FUNC)) {
    int i, n= N(t[1])-1;
    for (i=0; i<n; i++)
      if (ends (as_string (t[1][i]), "*"))
        cout << "ATHENA] Deprecated argument list '" << t[1][i]
             << "' in function '" << t[0] << "'\n"
             << "ATHENA] You should use the 'xmacro' primitive now\n";
  }
  /*
  if (is_func (t, ASSIGN, 2) && is_func (t[1], FUNC) && (N(t[1])>1)) {
    cout << "Function: " << t[0] << "\n";
  }
  */
  if (is_func (t, FUNC)) {
    int i, n= N(t)-1;
    tree u= t[n], r (MACRO, n+1);
    for (i=0; i<n; i++) {
      u= upgrade_function_arg (u, t[i]);
      r[i]= copy (t[i]);
    }
    r[n]= upgrade_function (u);
    /*
    if (n > 0) {
      cout << "t= " << t << "\n";
      cout << "r= " << r << "\n";
      cout << HRULE;
    }
    */
    return r;
  }
  else {
    int i, n= N(t);
    tree r (t, n);
    for (i=0; i<n; i++)
      r[i]= upgrade_function (t[i]);
    return r;
  }
}

/******************************************************************************
* Renaming environment variables
******************************************************************************/

static charp var_rename []= {
  "shrinking factor", "sfactor",
  "info flag", "info-flag",

  "font family", "font-family",
  "font series", "font-series",
  "font shape", "font-shape",
  "font size", "font-size",
  "font base size", "font-base-size",
  "background color", "bg-color",
  "atom decorations", "atom-decorations",
  "line decorations", "line-decorations",
  "page decorations", "page-decorations",
  "xoff decorations", "xoff-decorations",
  "yoff decorations", "yoff-decorations",

  "math language", "math-language",
  "math font", "math-font",
  "math font family", "math-font-family",
  "math font series", "math-font-series",
  "math font shape", "math-font-shape",
  "index level", "math-level",
  "formula style", "math-display",
  "math condensed", "math-condensed",
  "vertical position", "math-vpos",

  "prog language", "prog-language",
  "prog font", "prog-font",
  "prog font family", "prog-font-family",
  "prog font series", "prog-font-series",
  "prog font shape", "prog-font-shape",
  "this session", "prog-session",

  "paragraph mode", "par-mode",
  "paragraph hyphenation", "par-hyphen",
  "paragraph width", "par-width",
  "left margin", "par-left",
  "right margin", "par-right",
  "first indentation", "par-first",
  "no first indentation", "par-no-first",
  "interline space", "par-sep",
  "horizontal ink separation", "par-hor-sep",
  "line stretch", "par-line-sep",
  "interparagraph space", "par-par-sep",
  "interfootnote space", "par-fnote-sep",
  "nr columns", "par-columns",
  "column separation", "par-columns-sep",

  "page medium", "page-medium",
  "page type", "page-type",
  "page orientation", "page-orientation",
  "page breaking", "page-breaking",
  "page flexibility", "page-flexibility",
  "page number", "page-nr",
  "thepage", "page-the-page",
  "page width", "page-width",
  "page height", "page-height",
  "odd page margin", "page-odd",
  "even page margin", "page-even",
  "page right margin", "page-right",
  "page top margin", "page-top",
  "page bottom margin", "page-bot",
  "page extend", "page-extend",
  "page shrink", "page-shrink",
  "page header separation", "page-head-sep",
  "page footer separation", "page-foot-sep",
  "odd page header", "page-odd-header",
  "odd page footer", "page-odd-footer",
  "even page header", "page-even-header",
  "even page footer", "page-even-footer",
  "this page header", "page-this-header",
  "this page footer", "page-this-footer",
  "reduction page left margin", "page-reduce-left",
  "reduction page right margin", "page-reduce-right",
  "reduction page top margin", "page-reduce-top",
  "reduction page bottom margin", "page-reduce-bot",
  "show header and footer", "page-show-hf",
  "footnote separation", "page-fnote-sep",
  "footnote bar length", "page-fnote-barlen",
  "float separation", "page-float-sep",
  "marginal note separation", "page-mnote-sep",
  "marginal note width", "page-mnote-width",

  "table width", "table-width",
  "table height", "table-height",
  "table hmode", "table-hmode",
  "table vmode", "table-vmode",
  "table halign", "table-halign",
  "table valign", "table-valign",
  "table row origin", "table-row-origin",
  "table col origin", "table-col-origin",
  "table lsep", "table-lsep",
  "table rsep", "table-rsep",
  "table bsep", "table-bsep",
  "table tsep", "table-tsep",
  "table lborder", "table-lborder",
  "table rborder", "table-rborder",
  "table bborder", "table-bborder",
  "table tborder", "table-tborder",
  "table hyphen", "table-hyphen",
  "table min rows", "table-min-rows",
  "table min cols", "table-min-cols",
  "table max rows", "table-max-rows",
  "table max cols", "table-max-cols",

  "cell format", "cell-format",
  "cell decoration", "cell-decoration",
  "cell background", "cell-background",
  "cell orientation", "cell-orientation",
  "cell width", "cell-width",
  "cell height", "cell-height",
  "cell hpart", "cell-hpart",
  "cell vpart", "cell-vpart",
  "cell hmode", "cell-hmode",
  "cell vmode", "cell-vmode",
  "cell halign", "cell-halign",
  "cell valign", "cell-valign",
  "cell lsep", "cell-lsep",
  "cell rsep", "cell-rsep",
  "cell bsep", "cell-bsep",
  "cell tsep", "cell-tsep",
  "cell lborder", "cell-lborder",
  "cell rborder", "cell-rborder",
  "cell bborder", "cell-bborder",
  "cell tborder", "cell-tborder",
  "cell vcorrect", "cell-vcorrect",
  "cell hyphen", "cell-hyphen",
  "cell row span", "cell-row-span",
  "cell col span", "cell-col-span",
  "cell row nr", "cell-row-nr",
  "cell col nr", "cell-col-nr",

  "line width", "line-width",
  "line style", "line-style",
  "line arrows", "line-arrows",
  "line caps", "line-join",
  "fill mode", "fill-mode",
  "fill color", "fill-color",
  "fill style", "fill-style",

  "graphical frame", "gr-frame",
  "graphical clip", "gr-clip",
  "graphical mode", "gr-mode",
  "graphical color", "gr-color",
  "graphical line width", "gr-line-width",
  
  ""
};

static thread_local hashmap<string,string> var_rename_table ("?");

static hashmap<string,string>
cached_renamer (charp* T, hashmap<string,string>& H) {
  if (N (H) == 0) {
    int i;
    for (i=0; T[i][0] != '\0'; i+=2)
      H (T[i])= T[i+1];
  }
  return H;
}


static tree
rename_vars (tree t, hashmap<string,string> H, bool flag) {
  if (is_atomic (t)) return t;
  else {
    int i, n= N(t);
    tree r (t, n);
    static tree_label MARKUP= make_tree_label ("markup");
    for (i=0; i<n; i++) {
      tree u= rename_vars (t[i], H, flag);
      if (is_atomic (u) && H->contains (u->label))
        if (((L(t) == WITH) && ((i%2) == 0) && (i < n-1)) ||
            ((L(t) == ASSIGN) && (i == 0)) ||
            ((L(t) == VALUE) && (i == 0)) ||
            ((L(t) == CWITH) && (i == 4)) ||
            ((L(t) == TWITH) && (i == 0)) ||
            ((L(t) == ASSOCIATE) && (i == 0)) ||
            ((L(t) == MARKUP) && (i == 0)))
          u= copy (H[u->label]);
      r[i]= u;
    }
    if (flag) {
      if (H->contains (as_string (L(t)))) {
        tree_label l= make_tree_label (H[as_string (L(t))]);
        r= tree (l, A(r));
      }
    }
    else {
      if ((n == 0) && H->contains (as_string (L(t)))) {
        string v= H[as_string (L(t))];
        r= tree (VALUE, copy (v));
        if (v == "page-the-page") r= tree (make_tree_label ("page-the-page"));
      }
    }
    return r;
  }
}

tree
upgrade_env_vars (tree t) {
  return rename_vars (t, cached_renamer (var_rename, var_rename_table), false);
}

/******************************************************************************
* Normalize names of tags in the style files
******************************************************************************/

static charp style_rename []= {
  "thelabel", "the-label",

  "leftflush", "left-flush",
  "rightflush", "right-flush",
  "mathord", "math-ord",
  "mathopen", "math-open",
  "mathclose", "math-close",
  "mathpunct", "math-punct",
  "mathbin", "math-bin",
  "mathrel", "math-rel",
  "mathop", "math-op",
  "thetoc", "the-toc",
  "theidx", "the-idx",
  "thegly", "the-gly",
  "theitem", "the-item",
  "tocnr", "toc-nr",
  "idxnr", "idx-nr",
  "glynr", "gly-nr",
  "itemnr", "item-nr",
  "itemname", "item-name",
  "newitemize", "new-itemize",
  "newenumerate", "new-enumerate",
  "newdescription", "new-description",

  "nextnumber", "next-number",
  "eqnumber", "eq-number",
  "leqnumber", "leq-number",
  "reqnumber", "req-number",
  "nonumber", "no-number",
  "thefootnote", "the-footnote",
  "theequation", "the-equation",
  "thetheorem", "the-theorem",
  "theproposition", "the-proposition",
  "thelemma", "the-lemma",
  "thecorollary", "the-corollary",
  "theaxiom", "the-axiom",
  "thedefinition", "the-definition",
  "thenotation", "the-notation",
  "theconjecture", "the-conjecture",
  "theremark", "the-remark",
  "theexample", "the-example",
  "thenote", "the-note",
  "thewarning", "the-warning",
  "theconvention", "the-convention",
  "theacknowledgments", "the-acknowledgments",
  "theexercise", "the-exercise",
  "theproblem", "the-problem",
  "thefigure", "the-figure",
  "thetable", "the-table",
  "footnotenr", "footnote-nr",
  "equationnr", "equation-nr",
  "theoremnr", "theorem-nr",
  "propositionnr", "proposition-nr",
  "lemmanr", "lemma-nr",
  "corollarynr", "corollary-nr",
  "axiomnr", "axiom-nr",
  "definitionnr", "definition-nr",
  "notationnr", "notation-nr",
  "conjecturenr", "conjecture-nr",
  "remarknr", "remark-nr",
  "examplenr", "example-nr",
  "notenr", "note-nr",
  "warningnr", "warning-nr",
  "conventionnr", "convention-nr",
  "acknowledgmentsnr", "acknowledgments-nr",
  "exercisenr", "exercise-nr",
  "problemnr", "problem-nr",
  "figurenr", "figure-nr",
  "tablenr", "table-nr",
  "theoremname", "theorem-name",
  "figurename", "figure-name",
  "exercisename", "exercise-name",
  "theoremsep", "theorem-sep",
  "figuresep", "figure-sep",
  "exercisesep", "exercise-sep",
  "footnotesep", "footnote-sep",
  "newtheorem", "new-theorem",
  "newremark", "new-remark",
  "newexercise", "new-exercise",
  "newfigure", "new-figure",

  "theprefix", "the-prefix",
  "thechapter", "the-chapter",
  "theappendix", "the-appendix",
  "thesection", "the-section",
  "thesubsection", "the-subsection",
  "thesubsubsection", "the-subsubsection",
  "theparagraph", "the-paragraph",
  "thesubparagraph", "the-subparagraph",
  "chapternr", "chapter-nr",
  "appendixnr", "appendix-nr",
  "sectionnr", "section-nr",
  "subsectionnr", "subsection-nr",
  "subsubsectionnr", "subsubsection-nr",
  "paragraphnr", "paragraph-nr",
  "subparagraphnr", "subparagraph-nr",
  "sectionsep", "section-sep",
  "subsectionsep", "subsection-sep",
  "subsubsectionsep", "subsubsection-sep",

  "theorem*", "render-theorem",
  "remark*", "render-remark",
  "exercise*", "render-exercise",
  "proof*", "render-proof",
  "small-figure*", "render-small-figure",
  "big-figure*", "render-big-figure",

  "theanswer", "the-answer",
  "thealgorithm", "the-algorithm",
  "answernr", "answer-nr",
  "algorithmnr", "algorithm-nr",

  ""
};

static thread_local hashmap<string,string> style_rename_table ("?");

static tree
upgrade_style_rename_sub (tree t) {
  if (is_atomic (t)) return t;
  else if (is_func (t, MERGE, 2) && (t[0] == "the"))
    return tree (MERGE, "the-", t[1]);
  else if (is_func (t, MERGE, 2) && (t[1] == "nr"))
    return tree (MERGE, t[0], "-nr");
  else {
    int i, n= N(t);
    tree r (t, n);
    for (i=0; i<n; i++)
      r[i]= upgrade_style_rename_sub (t[i]);
    return r;
  }
}

static tree
upgrade_style_rename (tree t) {
  t= upgrade_style_rename_sub (t);
  return
    rename_vars (t, cached_renamer (style_rename, style_rename_table), true);
}

/******************************************************************************
* Remove trailing punctuation in item* tags
******************************************************************************/

static tree
upgrade_item_punct (tree t) {
  if (is_atomic (t)) return t;
  else {
    int i, n= N(t);
    tree r (t, n);
    for (i=0; i<n; i++)
      r[i]= upgrade_item_punct (t[i]);
    if (is_compound (r, "item*", 1)) {
      tree& item= r[0];
      if (is_atomic (item)) {
        string s= item->label;
        if (ends (s, ".") || ends (s, ":") || ends (s, " "))
          item= s (0, N(s)-1);
      }
      else if (is_concat (item) && is_atomic (item[N(item)-1])) {
        string s= item [N(item)-1] -> label;
        if ((s == ".") || (s == ":") || (s == " ")) {
          if (N(item) == 2) item= item[0];
          else item= item (0, N(item) - 1);
        }
      }
    }
    return r;
  }
}

/******************************************************************************
* Substitutions
******************************************************************************/

tree
substitute (tree t, tree which, tree by) {
  if (t == which) return by;
  else if (is_atomic (t)) return t;
  else {
    int i, n= N(t);
    tree r (t, n);
    for (i=0; i<n; i++)
      r[i]= substitute (t[i], which, by);
    return r;
  }
}

/******************************************************************************
* Upgrade mathematical formulas
******************************************************************************/

static thread_local hashset<string> existing_styles;



tree
upgrade_math (tree t) {
  int i;
  if (is_atomic (t)) return t;
  else if (is_func (t, WITH, 3) && t[0] == MODE && t[1] == "math")
    return compound ("math", upgrade_math (t[2]));
  else if (is_func (t, WITH, 3) && t[0] == MODE && t[1] == "text")
    return compound ("text", upgrade_math (t[2]));
  else if (is_func (t, WITH) && N(t) >= 5 && t[0] == MODE && t[1] == "math")
    return compound ("math", upgrade_math (t (2, N(t))));
  else if (is_func (t, WITH) && N(t) >= 5 && t[0] == MODE && t[1] == "text")
    return compound ("text", upgrade_math (t (2, N(t))));
  else {
    int n= N(t);
    tree r (t, n);
    for (i=0; i<n; i++)
      r[i]= upgrade_math (t[i]);
    return r;
  }
}

/******************************************************************************
* Upgrade resize and clipped
******************************************************************************/

tree
upgrade_resize_arg (tree t) {
  if (is_func (t, MERGE, 2)) {
    if (is_atomic (t[0]) && is_atomic (t[1]))
      return upgrade_resize_arg (t[0]->label * t[1]->label);
    tree u= upgrade_resize_arg (t[0]);
    if (is_func (u, PLUS, 2) || is_func (u, MINUS, 2) ||
        is_func (u, MINIMUM, 2) || is_func (u, MAXIMUM, 2))
      if (u[1] == "") {
        u[1]= upgrade_resize_arg (t[1]);
        return u;
      }
    cout << "ATHENA] warning, resize argument " << t << " not upgraded\n";
    return t;
  }
  if (is_func (t, ARG, 1) ||
      is_func (t, PLUS, 2) || is_func (t, MINUS, 2) ||
      is_func (t, MINIMUM, 2) || is_func (t, MAXIMUM, 2))
    return t;
  if (!is_atomic (t)) {
    cout << "ATHENA] warning, resize argument " << t << " not upgraded\n";
    return t;
  }
  string s= t->label;
  if (starts (s, "c")) {
    cout << "ATHENA] warning, resize argument " << t << " not upgraded\n";
    return t;
  }
  if (s == "l") return "1l";
  if (s == "r") return "1r";
  if (s == "t") return "1t";
  if (s == "b") return "1b";
  if (N(s) < 2) return t;
  if (s[0] != 'l' && s[0] != 'b' && s[0] != 'r' && s[0] != 't') return t;
  string s1= "1" * s (0, 1);
  string s2= s (2, N(s));
  if (s[1] == '+') return tree (PLUS, s1, s2);
  if (s[1] == '-') return tree (MINUS, s1, s2);
  if (s[1] == '[') return tree (MINIMUM, s1, s2);
  if (s[1] == ']') return tree (MAXIMUM, s1, s2);
  return t;
}

tree
upgrade_resize_clipped (tree t) {
  if (is_atomic (t)) return t;
  else if (N(t) >= 5 && (is_func (t, RESIZE) || is_func (t, CLIPPED))) {
    if (is_func (t, CLIPPED))
      t= tree (CLIPPED, t[4], t[0], t[1], t[2], t[3]);
    int i, n= 5;
    tree r (t, n);
    r[0]= upgrade_resize_clipped (t[0]);
    for (i=1; i<n; i++)
      r[i]= upgrade_resize_arg (t[i]);
    return r;
  }
  else {
    int i, n= N(t);
    tree r (t, n);
    for (i=0; i<n; i++)
      r[i]= upgrade_resize_clipped (t[i]);
    return r;
  }
}

/******************************************************************************
* Upgrade images
******************************************************************************/

tree
upgrade_image_length (tree t, string unit) {
  if (!is_atomic (t)) return t;
  string s= t->label;
  if (starts (s, "*") || starts (s, "/")) {
    double mag= get_magnification (s);
    return as_string (mag) * unit;
  }
  else return t;
}

tree
upgrade_image (tree t) {
  if (is_atomic (t)) return t;
  else if (is_func (t, IMAGE, 7))
    return tree (IMAGE, t[0],
                 upgrade_image_length (t[1], "w"),
                 upgrade_image_length (t[2], "h"),
                 "", "");
  else {
    int i, n= N(t);
    tree r (t, n);
    for (i=0; i<n; i++)
      r[i]= upgrade_image (t[i]);
    return r;
  }
}

/******************************************************************************
* Upgrade mathematical operators
******************************************************************************/

tree
upgrade_math_ops (tree t) {
  if (is_atomic (t)) return t;
  int i, n= N(t);
  tree r (t, n);
  for (i=0; i<n; i++)
    r[i]= upgrade_math_ops (t[i]);
  if (is_func (t, WITH, 3) &&
      is_atomic (t[2]) &&
      is_alpha (t[2]->label) &&
      t[0] == "math-font-family")
    {
      if (t[1] == "trm") return compound ("math-up", t[2]);
      if (t[1] == "tss") return compound ("math-ss", t[2]);
      if (t[1] == "ttt") return compound ("math-tt", t[2]);
      if (t[1] == "rm") return compound ("math-up", t[2]);
      if (t[1] == "up") return compound ("math-up", t[2]);
      if (t[1] == "bf") return compound ("math-bf", t[2]);
      if (t[1] == "sl") return compound ("math-sl", t[2]);
      if (t[1] == "it") return compound ("math-it", t[2]);
      if (t[1] == "ss") return compound ("math-ss", t[2]);
      if (t[1] == "tt") return compound ("math-tt", t[2]);
    }
  if (n == 1 && starts (as_string (L(t)), "math-")) {
    if (is_compound (t, "math-ord")) return compound ("math-ordinary", t[0]);
    if (is_compound (t, "math-punct")) return compound ("math-separator", t[0]);
    if (is_compound (t, "math-bin")) return compound ("math-plus", t[0]);
    if (is_compound (t, "math-rel")) return compound ("math-relation", t[0]);
    if (is_compound (t, "math-op")) return compound ("math-big", t[0]);
  }
  return r;
}

/******************************************************************************
* Cleaning spurious spaces in the document
******************************************************************************/

static bool
only_spaces (string s) {
  for (int i=0; i<N(s); i++)
    if (s[i] != ' ') return false;
  return true;
}

static bool
eat_spaces (tree t, bool after) {
  (void) after;
  if (is_atomic (t)) return false;
  return
    is_compound (t, "hide-preamble") ||
    is_compound (t, "doc-data") ||
    is_compound (t, "abstract");
}

static tree
clean_spaces (tree t) {
  if (is_atomic (t)) return t;
  int i, n= N(t);
  tree r (t, n);
  for (i=0; i<n; i++)
    r[i]= clean_spaces (t[i]);
  if (!is_func (r, CONCAT)) return r;
  t= r;
  r= tree (CONCAT);
  for (i=0; i<n; i++)
    if (!is_atomic (t[i])) r << t[i];
    else {
      string s= t[i]->label;
      if (i>0 && eat_spaces (t[i-1], true))
        while (starts (s, " ")) s= s (1, N(s));
      if (i<N(t)-1 && eat_spaces (t[i+1], false))
        while (ends (s, " ")) s= s (0, N(s)-1);
      if (s != "") r << tree (s);
    }
  if (N(r) == 0) return "";
  if (N(r) == 1) return r[0];
  return r;
}

/******************************************************************************
* Cleaning the document header
******************************************************************************/

static tree
search_header_tag (tree t, string which, tree& h) {
  if (is_compound (t, which)) {
    h << t;
    return "";
  }
  if (is_func (t, DOCUMENT)) {
    tree r (DOCUMENT);
    for (int i=0; i<N(t); i++) {
      if (is_compound (t[i], which)) h << t[i];
      else if (is_func (t[i], SURROUND, 3)) {
        tree x= search_header_tag (t[i], which, h);
        if (x != "") r << x;
      }
      else r << t[i];
    }
    return r;
  }
  if (is_func (t, SURROUND, 3)) {
    tree r (SURROUND, 3);
    r[0]= search_header_tag (t[0], which, h);
    r[2]= search_header_tag (t[2], which, h);
    r[1]= search_header_tag (t[1], which, h);
    if (r[0] == "" && r[1] == "") r= r[2];
    else if (r[0] == "" && r[2] == "") r= r[1];
    else if (r[1] == "" && r[2] == "") r= r[0];
    return r;
  }
  return t;
}

tree
clean_header (tree t) {
  if (!is_func (t, DOCUMENT)) return t;
  tree r        (DOCUMENT);
  tree preamble (DOCUMENT);
  tree title    (DOCUMENT);
  tree abstract (DOCUMENT);
  t= search_header_tag (t, "hide-preamble", preamble);
  t= search_header_tag (t, "doc-data", title);
  t= search_header_tag (t, "abstract", abstract);
  while (N(t) > 0 && is_atomic (t[0]) && only_spaces (t[0]->label))
    t= t (1, N(t));
  r << A (preamble);
  r << A (title);
  r << A (abstract);
  r << A (t);
  return r;
}

/******************************************************************************
* Upgrade document language
******************************************************************************/

tree
upgrade_doc_language (tree t) {
  tree style= extract (t, "style");
  tree init = extract (t, "initial");
  if (init == "" || N(init) == 0) return t;

  string lan= "";
  for (int i=0; i<N(init); i++)
    if (is_func (init[i], ASSOCIATE, 2) && init[i][0] == LANGUAGE)
      lan= as_string (init[i][1]);

  tree new_style= copy (style);
  tree new_init = tree (COLLECTION);
  for (int i=0; i<N(init); i++)
    if (is_func (init[i], ASSOCIATE, 2) && init[i][0] == LANGUAGE)
      new_style << init[i][1];
    else if (is_func (init[i], ASSOCIATE, 2) && init[i][0] == FONT) {
      if (init[i][1] == "cyrillic" &&
          (lan == "bulgarian" || lan == "russian" || lan == "ukrainian"));
      else if ((init[i][1] == "sys-chinese" || init[i][1] == "fireflysung") &&
               (lan == "chinese" || lan == "taiwanese"));
      else if ((init[i][1] == "sys-japanese" || init[i][1] == "ipa") &&
               lan == "japanese");
      else if ((init[i][1] == "sys-korean" || init[i][1] == "unbatang") &&
               lan == "korean");
      else new_init << init[i];
    }
    else new_init << init[i];

  if (new_style != style)
    t= change_doc_attr (t, "style", new_style);
  if (new_init != init)
    t= change_doc_attr (t, "initial", new_init);
  return t;
}

/******************************************************************************
* Upgrade quotes
******************************************************************************/

static thread_local hashset<string> std_textual_envs;

static array<string>&
operator << (array<string>& a, const char* s) {
  return a << string (s);
}

static bool
is_std_textual_env (string s) {
  if (N(std_textual_envs) == 0) {
    array<string> a;
    a << "part" << "chapter" << "section" << "subsection"
      << "subsubsection" << "paragraph" << "subparagraph" << "appendix"
      << "part*" << "chapter*" << "section*" << "subsection*"
      << "subsubsection*" << "paragraph*" << "subparagraph*" << "appendix*"
      << "abstract" << "theorem" << "proposition" << "lemma"
      << "corollary" << "proof" << "axiom" << "definition"
      << "notation" << "conjecture" << "remark" << "note"
      << "example" << "exercise" << "warning" << "convention"
      << "theorem*" << "proposition*" << "lemma*"
      << "corollary*" << "proof*" << "axiom*" << "definition*"
      << "notation*" << "conjecture*" << "remark*" << "note*"
      << "example*" << "exercise*" << "warning*" << "convention*"
      << "acknowledgments" << "quote" << "quotation" << "verse"
      << "indent" << "compact" << "jump-in"
      << "algorithm" << "body" << "render-code"
      << "center" << "left-aligned" << "right-aligned"
      << "small-table" << "big-table" << "small-figure" << "big-figure"
      << "tabular" << "tabular*" << "block" << "block*" << "descriptive-table"
      << "description" << "itemize" << "enumerate"
      << "db-field" << "db-entry"
      << "itemize-minus" << "enumerate-roman" << "enumerate-alpha"
      << "strong" << "em" << "dfn" << "sample"
      << "name" << "person" << "cite*" << "abbr" << "acronym"
      << "really-tiny" << "tiny" << "very-small" << "small" << "flat-size"
      << "normal-size" << "large" << "very-large" << "huge" << "really-huge"
      << "underline" << "overline" << "pastel" << "greyed" << "light"
      << "british" << "bulgarian" << "chinese" << "croatian"
      << "czech" << "danish" << "dutch" << "english" << "finnish"
      << "french" << "german" << "greek" << "hungarian" << "italian"
      << "japanese" << "korean" << "polish" << "portuguese" << "romanian"
      << "russian" << "slovak" << "slovene" << "spanish" << "swedish"
      << "taiwanese" << "ukrainian"
      << "switch" << "screens" << "tiny-switch"
      << "shown" << "hidden" << "shown*" << "hidden*"
      << "unroll" << "unroll-compressed" << "unroll-phantoms" << "unroll-greyed"
      << "folded" << "unfolded";
    for (int i=0; i<N(a); i++) std_textual_envs->insert (a[i]);
  }
  return std_textual_envs->contains (s);
}

static string
upgrade_quotes (string s) {
  string r;
  int i, n= N(s);
  for (i=0; i<n; )
    if (s[i] == '<') {
      int start= i;
      tm_char_forwards (s, i);
      r << s (start, i);
    }
    else if (s[i] == '-' && i+1 < n && s[i+1] == '-') { r << "\25"; i += 2; }
    else if (s[i] == '\'' && i+1 < n && s[i+1] == '\'') { r << "\21"; i += 2; }
    else if (s[i] == '`' && i+1 < n && s[i+1] == '`') { r << "\20"; i += 2; }
    else { r << s[i]; i++; }
  return r;
}

tree
upgrade_quotes (tree t) {
  if (is_atomic (t))
    return upgrade_quotes (t->label);
  else if (is_concat (t)) {
    int i, n= N(t);
    tree r (t, n);
    bool adjust= false;
    for (i=0; i<n; i++) {
      adjust= adjust || is_compound (t[i], "emdash", 0);
      r[i]= upgrade_quotes (t[i]);
    }
    if (adjust) r= simplify_correct (r);
    return r;
  }
  else if (is_compound (t, "emdash", 0))
    return "\26";
  else if (is_compound (t, "body", 1))
    return compound ("body", upgrade_quotes (t[0]));
  else {
    int i, n= N(t);
    tree r (t, n);
    for (i=0; i<n; i++) {
      if (is_std_textual_env (as_string (L(t))))
        r[i]= upgrade_quotes (t[i]);
      else if (standard_drd_for_thread ()->get_type_child (t, i) != TYPE_REGULAR ||
               standard_drd_for_thread ()->get_env_child (t, i, MODE, "text") != "text")
        r[i]= t[i];
      else r[i]= upgrade_quotes (t[i]);
    }
    return r;
  }
}

/******************************************************************************
* Upgrade ancient
******************************************************************************/

static charp equation_tags[]= {
  "equation", "equation*", "eqnarray", "eqnarray*",
  "leqnarray", "leqnarray*", "align", "align*",
  "falign", "falign*", "aligned", "aligned*",
  "multline", "multline*", "gather", "gather*",
  "eqsplit", "eqsplit*",
  ""
};

bool
is_equation_env (tree t) {
  if (is_atomic (t) || N(t) != 1) return false;
  static thread_local hashset<tree_label> H;
  if (N(H) == 0)
    for (int i=0; equation_tags[i][0] != '\0'; i++)
      H->insert (as_tree_label (equation_tags[i]));
  return H->contains (L(t));
}

tree
upgrade_ancient (tree t) {
  // Miscellaneous upgradings for old documents
  if (is_atomic (t)) return t;
  else if (is_func (t, INACTIVE, 1) && is_func (t[0], RIGID))
    return upgrade_ancient (t[0]);
  else if (is_equation_env (t) && !is_func (t[0], DOCUMENT))
    return tree (L(t), tree (DOCUMENT, upgrade_ancient (t[0])));
  else {
    int i, n= N(t);
    tree r (t, n);
    for (i=0; i<n; i++)
      r[i]= upgrade_ancient (t[i]);
    if (is_func (r, WITH)) {
      bool font_series= false;
      for (i=0; i+1<n; i+=2)
        if (r[i] == FONT_SERIES) font_series= true;
      for (i=0; i+1<n; i+=2)
        if (r[i] == MATH_FONT_SERIES && !font_series) {
          tree ins= tree (WITH, FONT_SERIES, copy (r[i+1]));
          return r (0, i) * ins * r (i, N(r));
        }
    }
    return r;
  }
}

/******************************************************************************
* Upgrade from previous versions
******************************************************************************/

tree
upgrade_tex (tree t) {
  upgrade_tex_flag= true;
  t= upgrade_apply_expand_value (t);
  t= upgrade_new_environments (t);
  t= upgrade_items (t);
  t= normalize_tex_table_breaks (t, false);
  t= simplify_correct (upgrade_mod_symbols (t));
  t= upgrade_menus_in_help (t);
  t= upgrade_capitalize_menus (t);
  t= upgrade_formatting (t);
  t= upgrade_expand (t, EXPAND);
  t= upgrade_expand (t, HIDE_EXPAND);
  t= upgrade_expand (t, VAR_EXPAND);
  t= upgrade_xexpand (t);
  t= upgrade_function (t);
  t= upgrade_apply (t);
  t= upgrade_env_vars (t);
  t= upgrade_style_rename (t);
  t= upgrade_item_punct (t);
  t= substitute (t, tree (VALUE, "hrule"), compound ("hrule"));
  t= upgrade_math (t);
  t= upgrade_resize_clipped (t);
  t= with_correct (t);
  t= superfluous_with_correct (t);
  t= upgrade_brackets (t);
  t= move_brackets (t);
  t= upgrade_image (t);
  t= upgrade_math_ops (t);
  t= clean_spaces (t);
  t= clean_header (t);
  t= upgrade_doc_language (t);
  t= upgrade_quotes (t);
  t= upgrade_ancient (t);
  upgrade_tex_flag= false;
  return t;
}

tree
upgrade_mathml (tree t) {
  t= upgrade_brackets (t, "math");
  t= downgrade_big (t);
  return t;
}

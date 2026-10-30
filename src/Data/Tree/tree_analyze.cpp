
/******************************************************************************
* MODULE     : tree_analyze.cpp
* DESCRIPTION: routines for analyzing trees
* COPYRIGHT  : (C) 2010  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "tree_analyze.hpp"
#include "Data/NodeContracts/syntax_query.hpp"
#include "Data/NodeContracts/editing_query.hpp"
#include "Data/NodeContracts/builtin_contracts.hpp"
#include "node_metadata.hpp"
#include "convert.hpp"

/******************************************************************************
* Tokenize mathematical concats and recomposition
******************************************************************************/

array<tree>
concat_tokenize (tree t) {
  language lan= math_language ("std-math");
  array<tree> r;
  if (is_atomic (t)) {
    int i= 0;
    while (i<N(t->label)) {
      int start= i;
      (void) lan->advance (t, i);
      r << tree (t->label (start, i));
    }
  }
  else if (is_concat (t))
    for (int i=0; i<N(t); i++)
      r << concat_tokenize (t[i]);
  else if (is_func (t, BIG, 1) && t[0] == "."); // NOTE: discard old <big|.>
  else r << t;
  return r;
}

array<tree>
concat_decompose (tree t) {
  array<tree> r;
  if (athena::node::get (t)) r << t;
  else if (t == "");
  else if (is_atomic (t)) r << t;
  else if (is_concat (t))
    for (int i=0; i<N(t); i++)
      r << concat_decompose (t[i]);
  else r << t;
  return r;
}

static bool
is_texmacs_symbol_string (string s) {
  // Keep TeXmacs symbol atoms isolated; otherwise "<gtr>" can be retokenized
  // later as the raw characters '<', 'g', 't', 'r', '>'.
  return N(s) >= 3 && s[0] == '<' && s[N(s)-1] == '>';
}

tree
concat_recompose (array<tree> a) {
  array<tree> r;
  string s;
  for (int i=0; i<N(a); i++)
    if (is_atomic (a[i]) && !athena::node::get (a[i])) {
      if (is_texmacs_symbol_string (a[i]->label)) {
        if (s != "") r << tree (s);
        r << a[i];
        s= "";
      }
      else s << a[i]->label;
    }
    else {
      if (s != "") r << tree (s);
      r << a[i];
      s= "";
    }
  if (s != "") r << tree (s);
  if (N(r) == 0) return "";
  else if (N(r) == 1) return r[0];
  else return tree (CONCAT, r);
}

namespace {
void tokenize_source_child (tree t, array<tree>& tokens) {
  if (athena::node::get (t)) tokens << t;
  else if (is_concat (t))
    for (int i= 0; i < N(t); ++i) tokenize_source_child (t[i], tokens);
  else tokens << concat_tokenize (t);
}
}

array<tree>
concat_tokenize_source (tree t) {
  if (!is_concat (t)) return concat_tokenize (t);
  array<tree> tokens;
  for (int i= 0; i < N(t); ++i) tokenize_source_child (t[i], tokens);
  return tokens;
}

tree
concat_recompose_source (tree source, array<tree> a) {
  tree result= concat_recompose (a);
  if (!athena::node::get (source) ||
      (!is_atomic (source) && !is_concat (source))) return result;
  if (is_concat (source)) {
    // A sole identified child is not the same object as its parent. Retain
    // the parent instead of transferring its header onto that child.
    if (!is_concat (result) || athena::node::get (result))
      result= N(a) == 0 ? tree (CONCAT) : tree (CONCAT, result);
  }
  athena::node::copy_metadata (source, result);
  return result;
}

/******************************************************************************
* Subroutines for WITH-like macros
******************************************************************************/

bool
is_with_like (tree t, const athena::contracts::editing_query& query) {
  return is_compound (t) && N(t) > 0 && query.describe (L(t)).with_like;
}

tree&
with_body (tree w) {
  return w[N(w)-1];
}

bool
with_same_type (tree w1, tree w2) {
  ASSERT (is_compound (w1) && is_compound (w2) && N(w1) && N(w2), "wrappers expected");
  return w1 (0, N(w1)-1) == w2 (0, N(w2)-1);
}

bool
with_similar_type (tree w1, tree w2) {
  ASSERT (is_compound (w1) && is_compound (w2) && N(w1) && N(w2), "wrappers expected");
  if (is_compound (w1, "math") || is_compound (w1, "text"))
    return is_compound (w2, "math") || is_compound (w2, "text");
  if (!is_func (w1, WITH) || !is_func (w2, WITH))
    return with_same_type (w1, w2);
  if (N(w1) != N(w2)) return false;
  for (int i=0; i<N(w1)-1; i+=2)
    if (w1[i] != w2[i]) return false;
  return true;
}

static bool
same_wrapper_definition (tree w1, tree w2,
    const athena::contracts::editing_query& first,
    const athena::contracts::editing_query& second) {
  if (L(w1) != L(w2) || athena::contracts::builtin_label (as_string (L(w1)))) return true;
  const auto left= first.scope.definition (L(w1));
  const auto right= second.scope.definition (L(w2));
  // Formatting compares definitions, not execution receipts: independent
  // cold preparations of the same source create distinct binding identities.
  return left == right || (left && right &&
    left->value->expression == right->value->expression);
}

bool
with_same_type (tree w1, tree w2,
    const athena::contracts::editing_query& first,
    const athena::contracts::editing_query& second) {
  return is_with_like (w1, first) && is_with_like (w2, second) &&
    with_same_type (w1, w2) && same_wrapper_definition (w1, w2, first, second);
}

bool
with_similar_type (tree w1, tree w2,
    const athena::contracts::editing_query& first,
    const athena::contracts::editing_query& second) {
  return is_with_like (w1, first) && is_with_like (w2, second) &&
    with_similar_type (w1, w2) && same_wrapper_definition (w1, w2, first, second);
}

tree
with_recompose (tree w, array<tree> a) {
  tree r= w (0, N(w));
  athena::node::copy_metadata (w, r);
  with_body (r)= concat_recompose (a);
  return r;
}

/******************************************************************************
* Determine symbol type
******************************************************************************/

int
symbol_type (tree t) {
  tree r= athena::contracts::syntax_query {{athena::contracts::current_evaluation_state ().contracts}, athena::contracts::current_evaluation_state ().values}.expand (t);
  if (r != UNINIT) {
    if (is_compound (t, "text")) return SYMBOL_SKIP;
    else if (is_compound (t, "eq-number")) return SYMBOL_SKIP;
    else if (is_compound (t, "bl")) return SYMBOL_OPEN;
    else if (is_compound (t, "br")) return SYMBOL_CLOSE;
    else return symbol_type (r);
  }
  else if (is_atomic (t) || is_func (t, NAMED_SYMBOL, 1)) {
    int pos= 0;
    int op= math_language ("std-math")->advance (t, pos)->op_type;
    switch (op) {
    case OP_UNKNOWN:
    case OP_TEXT:
    case OP_SKIP:
    case OP_SYMBOL:
    case OP_UNARY:
    case OP_BINARY:
    case OP_N_ARY:
      return SYMBOL_BASIC;
    case OP_PREFIX:
      return SYMBOL_PREFIX;
    case OP_POSTFIX:
      return SYMBOL_POSTFIX;
    case OP_INFIX:
      return SYMBOL_INFIX;
    case OP_PREFIX_INFIX:
      return SYMBOL_PREFIX_INFIX;
    case OP_SEPARATOR:
      return SYMBOL_SEPARATOR;
    case OP_OPENING_BRACKET:
      return SYMBOL_PROBABLE_OPEN;
    case OP_MIDDLE_BRACKET:
      return SYMBOL_PROBABLE_MIDDLE;
    case OP_CLOSING_BRACKET:
      return SYMBOL_PROBABLE_CLOSE;
    default:
      return SYMBOL_BASIC;
    }
  }
  else switch (L(t)) {
    case HSPACE:
    case VAR_VSPACE:
    case VSPACE:
    case SPACE:
    case HTAB:
      return SYMBOL_SKIP;

    case LEFT:
      return SYMBOL_OPEN;
    case MID:
      return SYMBOL_MIDDLE;
    case RIGHT:
      return SYMBOL_CLOSE;
    case BIG:
      if (is_func (t, BIG, 1) && t[0] == ".") return SYMBOL_CLOSE_BIG;
      else return SYMBOL_OPEN_BIG;
    case LPRIME:
    case RPRIME:
    case LSUB:
    case LSUP:
    case RSUB:
    case RSUP:
      return SYMBOL_SCRIPT;

    case WITH:
    case STYLE_WITH:
    case VAR_STYLE_WITH:
      if (N(t) == 0) return SYMBOL_SKIP;
      return symbol_type (t[N(t)-1]);
    case LABEL:
      return SYMBOL_SKIP;

    default:
      return SYMBOL_BASIC;
  }
}

array<int>
symbol_types (array<tree> a) {
  array<int> tp (N(a));
  for (int i=0; i<N(a); i++)
    tp[i]= symbol_type (a[i]);
  return tp;
}

/******************************************************************************
* Determine symbol priority
******************************************************************************/

#define PRIORITY_SEPARATOR          0
#define PRIORITY_ASSIGN             1
#define PRIORITY_FLUX               2
#define PRIORITY_MODELS             3
#define PRIORITY_IMPLY              4
#define PRIORITY_OR                 5
#define PRIORITY_AND                6
#define PRIORITY_RELATION           7
#define PRIORITY_ARROW              8
#define PRIORITY_UNION              9
#define PRIORITY_INTERSECTION      10
#define PRIORITY_PLUS              11
#define PRIORITY_TIMES             12
#define PRIORITY_POWER             13
#define PRIORITY_RADICAL           14

int
symbol_priority (tree t) {
  language lan= math_language ("std-math");
  if (is_atomic (t) || is_func (t, NAMED_SYMBOL, 1)) {
    string g= lan->get_group (t);
    if (starts (g, "Separator")) return PRIORITY_SEPARATOR;
    if (starts (g, "Ponctuation")) return PRIORITY_SEPARATOR;
    if (starts (g, "Assign")) return PRIORITY_ASSIGN;
    if (starts (g, "Flux")) return PRIORITY_FLUX;
    if (starts (g, "Model")) return PRIORITY_MODELS;
    if (starts (g, "Imply")) return PRIORITY_IMPLY;
    if (starts (g, "Or")) return PRIORITY_OR;
    if (starts (g, "And")) return PRIORITY_AND;
    if (starts (g, "Relation")) return PRIORITY_RELATION;
    if (starts (g, "Arrow")) return PRIORITY_ARROW;
    if (starts (g, "Union")) return PRIORITY_UNION;
    if (starts (g, "Exclude")) return PRIORITY_UNION;
    if (starts (g, "Intersection")) return PRIORITY_INTERSECTION;
    if (starts (g, "Plus")) return PRIORITY_PLUS;
    if (starts (g, "Minus")) return PRIORITY_PLUS;
    if (starts (g, "Times")) return PRIORITY_TIMES;
    if (starts (g, "Over")) return PRIORITY_TIMES;
    if (starts (g, "Power")) return PRIORITY_POWER;
    return PRIORITY_RADICAL;
  }
  else if (is_func (t, BIG, 1) && is_atomic (t[0])) {
    string s= t[0]->label;
    if (s == "parallel") return PRIORITY_SEPARATOR;
    if (s == "interleave") return PRIORITY_SEPARATOR;
    if (s == "vee") return PRIORITY_OR;
    if (s == "curlyvee") return PRIORITY_OR;
    if (s == "wedge") return PRIORITY_AND;
    if (s == "curlywedge") return PRIORITY_AND;
    if (s == "cup") return PRIORITY_UNION;
    if (s == "sqcup") return PRIORITY_UNION;
    if (s == "amalg") return PRIORITY_UNION;
    if (s == "uplus") return PRIORITY_UNION;
    if (s == "box") return PRIORITY_UNION;
    if (s == "cap") return PRIORITY_INTERSECTION;
    if (s == "sqcap") return PRIORITY_INTERSECTION;
    if (s == "int") return PRIORITY_PLUS;
    if (s == "fint") return PRIORITY_PLUS;
    if (s == "oint") return PRIORITY_PLUS;
    if (s == "intlim") return PRIORITY_PLUS;
    if (s == "ointlim") return PRIORITY_PLUS;
    if (s == "sum") return PRIORITY_PLUS;
    if (s == "oplus") return PRIORITY_PLUS;
    if (s == "triangledown") return PRIORITY_PLUS;
    if (s == "prod") return PRIORITY_TIMES;
    if (s == "otimes") return PRIORITY_TIMES;
    if (s == "odot") return PRIORITY_TIMES;
    if (s == "triangleup") return PRIORITY_TIMES;
    return PRIORITY_RADICAL;
  }
  else return PRIORITY_RADICAL;
}

array<int>
symbol_priorities (array<tree> a) {
  array<int> tp (N(a));
  for (int i=0; i<N(a); i++)
    tp[i]= symbol_priority (a[i]);
  return tp;
}

/******************************************************************************
* Further routines
******************************************************************************/

bool
is_correctable_child (tree t, int i, bool noaround) {
  return is_correctable_child (t, i, athena::contracts::editing_query {
    athena::contracts::current_evaluation_state ().contracts}, noaround);
}

bool
is_correctable_child (tree t, int i,
    const athena::contracts::editing_query& query, bool noaround) {
  int type= query.child_type (t, i);
  if (is_compound (t, "body", 1)) return true;
  else if (!is_concat (t)) {
    switch (type) {
    case TYPE_INVALID:
    case TYPE_REGULAR:
    case TYPE_GRAPHICAL:
    case TYPE_UNKNOWN:
      return true;
    default:
      return false;
    }
  }
  else if (is_atomic (t[i]) ||
	   (noaround && is_func (t[i], AROUND)) ||
	   (noaround && is_func (t[i], VAR_AROUND)) ||
	   (noaround && is_func (t[i], BIG_AROUND)) ||
	   is_func (t[i], LEFT) ||
	   is_func (t[i], MID) ||
	   is_func (t[i], RIGHT) ||
	   is_func (t[i], BIG) ||
	   is_compound (t[i], "bl") ||
	   is_compound (t[i], "br"))
    return false;
  else return true;
}

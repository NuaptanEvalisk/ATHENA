
/******************************************************************************
* MODULE     : tree_math_stats.cpp
* DESCRIPTION: compile statistics for math formulas in documents
* COPYRIGHT  : (C) 2022  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "tree_math_stats.hpp"
#include "Data/NodeContracts/builtin_contracts.hpp"
#include "tree_analyze.hpp"
#include "analyze.hpp"
#include "vars.hpp"
#include <chrono>

/******************************************************************************
* Compile statistics
******************************************************************************/

static void
record (hashmap<tree,int>& counts, tree key) {
  // Keys must not share mutable source leaves with the live document.
  if (counts->contains (key)) counts (key)= counts[key] + 1;
  else counts (copy (key))= 1;
}

static tree
strip_decorations (tree t) {
  if (is_func (t, WIDE) || is_func (t, VAR_WIDE))
    return strip_decorations (t[0]);
  else if (is_func (t, NEG))
    return strip_decorations (t[0]);
  else if (is_atomic (t) && is_numeric (t->label))
    return "1";
  else if (is_func (t, SQRT))
    return tree (SQRT, "1");
  else if (is_func (t, FRAC))
    return tree (FRAC, "1", "1");
  else return t;
}

static tree
tmconcat (tree a1, tree a2) {
  tree c= tree (CONCAT, a1, a2);
  if (is_atomic (a1) && is_atomic (a2)) c= a1->label * a2->label;
  return c;
}

static tree
tmconcat (tree a1, tree a2, tree a3) {
  tree c= tree (CONCAT, a1, a2, a3);
  if (is_atomic (a1) && is_atomic (a2) && is_atomic (a3))
    c= a1->label * a2->label * a3->label;
  else if (is_atomic (a1) && is_atomic (a2))
    c= tree (CONCAT, a1->label * a2->label, a3);
  else if (is_atomic (a2) && is_atomic (a3))
    c= tree (CONCAT, a1, a2->label * a3->label);
  return c;
}

static int
root_index (array<tree> a, int i) {
  int j= i;
  while (j>0 && (is_func (a[j], RSUB) ||
                 is_func (a[j], RSUP) ||
                 is_func (a[j], RPRIME))) j--;
  return j;
}

void
math_statistics::compile_tokens (array<tree> a, tree parent) {
  //array<int> tp= symbol_types (a);
  for (int i=0; i<N(a); i++) {
    record (occurrences, a[i]);

    if (i == 0 && is_atomic (a[i]) &&
        (is_func (parent, RSUB) || is_func (parent, RSUP))) {
      tree u (L(parent), a[i]);
      record (roles, u);
    }

    if (i == 0 && is_atomic (a[i]) &&
        (is_func (parent, AROUND) || is_func (parent, VAR_AROUND))) {
      tree u (L(parent), copy (parent[0]), a[i], copy (parent[2]));
      record (roles, u);
    }

    if ((i+2) < N(a) &&
        //is_atomic (a[i]) && tp[i] == SYMBOL_BASIC &&
        //is_atomic (a[i+2]) && tp[i+2] == SYMBOL_BASIC &&
        is_atomic (a[i+1])) {
      int  j = root_index (a, i);
      tree l = strip_decorations (a[j]);
      tree op= a[i+1];
      tree r = strip_decorations (a[i+2]);
      if (op == "-") op= "+";
      tree C= tmconcat (a[j], op, a[i+2]);
      record (roles, C);
      tree c= tmconcat (l, op, r);
      if (c != C) record (roles, c);
      c= tmconcat (l, op);
      record (roles, c);
    }

    if ((i+1) < N(a) &&
        //is_atomic (a[i]) && tp[i] == SYMBOL_BASIC &&
        (is_func (a[i+1], AROUND, 3) || is_func (a[i+1], VAR_AROUND, 3))) {
      int  j= root_index (a, i);
      tree l= strip_decorations (a[j]);
      tree b= a[i+1];
      tree C (CONCAT, a[j], tree (AROUND, b[0], b[1], b[2]));
      record (roles, C);
      tree c (CONCAT, l, tree (AROUND, b[0], strip_decorations (b[1]), b[2]));
      if (c != C) record (roles, c);
      c= tree (CONCAT, l, tree (AROUND, b[0], "", b[2]));
      record (roles, c);

      array<tree> v= concat_tokenize (b[1]);
      if (N(v) >= 1) {
        c= tree (CONCAT, l, tree (AROUND, b[0], strip_decorations (v[0]), b[2]));
        record (roles, c);
      }
      for (int k=0; k<N(v)-1; k++) {
        if (v[k] == "," || v[k] == ";")
          if (is_atomic (v[k+1])) {
            tree sc= tmconcat (v[k], strip_decorations (v[k+1]));
            c= tree (CONCAT, l, tree (AROUND, b[0], sc, b[2]));
            record (roles, c);
          }
      }
    }

    if ((i+1) < N(a) &&
        //is_atomic (a[i]) && tp[i] == SYMBOL_BASIC &&
        (is_func (a[i+1], RSUB, 1) || is_func (a[i+1], RSUP, 1))) {
      tree l= strip_decorations (a[i]);
      tree s= a[i+1];
      tree C (CONCAT, a[i], s);
      record (roles, C);
      tree c (CONCAT, l, tree (L(s), strip_decorations (s[0])));
      if (c != C) record (roles, c);
      c= tree (CONCAT, l, tree (L(s), ""));
      record (roles, c);

      array<tree> v= concat_tokenize (s[0]);
      if (N(v) >= 1) {
        c= tree (CONCAT, l, tree (L(s), strip_decorations (v[0])));
        record (roles, c);
      }
      for (int k=0; k<N(v)-1; k++) {
        if (v[k] == "+" || v[k] == "-" || v[k] == "," || v[k] == ";") {
          tree op= v[k];
          if (op == "-") op= "+";
          tree sc= tmconcat (op, strip_decorations (v[k+1]));
          if (is_atomic (v[k+1])) {
            c= tree (CONCAT, l, tree (L(s), sc));
            record (roles, c);
          }
        }
      }
    }
  }
}

static string
get_submode (tree t, int i, string mode,
    const athena::contracts::editing_query& query) {
  if (is_func (t, WITH) && i == N(t)-1)
    for (int j=0; j<N(t)-1; j+=2)
      if (t[j] == MATH_FONT_FAMILY) return "text";
  tree tmode= query.value (t, i, MODE, mode);
  return (is_atomic (tmode)? tmode->label: string ("text"));
}

void
math_statistics::compile_node (tree t, tree parent) {
  // A document is a container of formulae, not itself an input token. In an
  // initial math environment, counting it would copy the whole buffer at once.
  if (!is_document (t)) {
    array<tree> a= concat_tokenize (t);
    if (N(a) > 1) {
      record (occurrences, t);
      array<tree> b= copy (a);
      tree last= strip_decorations (b[N(b)-1]);
      if (b[N(b)-2] == "-" || b[N(b)-1] != last) {
        if (b[N(b)-2] == "-") b[N(b)-2]= "+";
        b[N(b)-1]= last;
        tree u= concat_recompose (b);
        record (roles, u);
      }
    }
    compile_tokens (a, parent);
  }
}

/******************************************************************************
* User interface
******************************************************************************/

void
math_statistics::begin (tree t, string mode, path source) {
  ready= false;
  occurrences= hashmap<tree,int> (0);
  roles= hashmap<tree,int> (0);
  pending.clear ();
  pending.push_back ({t, tree (DOCUMENT, t), mode, source});
  progress= math_statistics_progress::pending;
}

math_statistics_progress
math_statistics::advance (
    const athena::contracts::source_contract_preparation& contexts, unsigned budget) {
  using namespace athena::contracts;
  const auto deadline= std::chrono::steady_clock::now () + std::chrono::milliseconds (4);
  while (!pending.empty () && budget-- != 0) {
    auto& frame= pending.back ();
    const tree t= frame.value;
    if (is_func (t, MACRO) || is_func (t, XMACRO)) pending.pop_back ();
    else if (is_compound (t) && frame.child == -1) {
      if (!builtin_label (as_string (L(t)))) {
        frame.context= contexts (frame.source);
        if (!frame.context) {
          pending.clear ();
          return progress= math_statistics_progress::unavailable;
        }
      }
      frame.child= 0;
    }
    else if (is_compound (t) && frame.child < N(t)) {
      const int i= frame.child++;
      const editing_query query {frame.context ? *frame.context : builtin_contracts ()};
      if ((!is_func (t, WITH) || i == N(t)-1) &&
          !is_compound (t, "math-ss") && !is_compound (t, "math-tt") &&
          is_correctable_child (t, i, query)) {
        const string mode= get_submode (t, i, frame.mode, query);
        const path source= frame.source * i;
        pending.push_back ({t[i], t, mode, source});
      }
    }
    else {
      if (frame.mode == "math") compile_node (t, frame.parent);
      pending.pop_back ();
    }
    if (std::chrono::steady_clock::now () >= deadline) break;
  }
  if (pending.empty () && progress == math_statistics_progress::pending) {
    ready= true;
    progress= math_statistics_progress::ready;
  }
  return progress;
}

bool
math_statistics::compile (tree t, string mode,
    const athena::contracts::source_contract_preparation& contexts, path source) {
  begin (t, mode, source);
  while (advance (contexts) == math_statistics_progress::pending) {}
  return ready;
}

int
math_statistics::number_occurrences (tree t) const {
  return ready ? occurrences[t] : 0;
}

int
math_statistics::number_in_role (tree t) const {
  return ready ? roles[t] : 0;
}

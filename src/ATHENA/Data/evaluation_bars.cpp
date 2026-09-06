/******************************************************************************
* MODULE     : evaluation_bars.cpp
* DESCRIPTION: Structural conversion of ordinary bars to evaluation bars
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include "ATHENA/Data/evaluation_bars.hpp"
#include "drd_std.hpp"
#include "tree_analyze.hpp"
#include "vars.hpp"

tree
athena_promote_evaluation_bars (tree t, int& promoted, string mode) {
  if (is_func (t, LEFT) || is_func (t, MID) || is_func (t, RIGHT) ||
      is_func (t, BIG) || is_func (t, MACRO) || is_func (t, XMACRO) ||
      is_func (t, ASSIGN) || is_func (t, QUOTE)) return t;
  tree r= t;
  if (is_compound (t)) {
    r= tree (t, N(t));
    for (int i=0; i<N(t); ++i) {
      // Atomic concat children are converted together with their preceding
      // expression, not independently. DRD excludes delimiter/attribute data.
      bool delimiter= (is_func (t, AROUND) || is_func (t, VAR_AROUND)) &&
                      i != 1;
      delimiter= delimiter || (is_func (t, BIG_AROUND) && i == 0);
      bool binding= is_func (t, WITH) && i != N(t)-1;
      if (!delimiter && !binding && is_correctable_child (t, i)) {
        tree child_mode= the_drd->get_env_child (t, i, MODE, mode);
        r[i]= athena_promote_evaluation_bars (
          t[i], promoted, is_atomic (child_mode) ? child_mode->label : "text");
      }
      else r[i]= t[i];
    }
  }
  if (mode != "math" || (!is_atomic (r) && !is_concat (r))) return r;

  array<tree> tokens= concat_tokenize (r), result;
  bool changed= false;
  for (int i=0; i<N(tokens); ++i) {
    // Named relation tokens (mid, divides, shortmid, etc.) are not bars.
    if (tokens[i] == "|" || tokens[i] == "<vert>") {
      tree expression= concat_recompose (result);
      result= array<tree> ();
      result << tree (VAR_AROUND, "<nobracket>", expression, "|");
      ++promoted;
      changed= true;
    }
    else result << tokens[i];
  }
  return changed ? concat_recompose (result) : r;
}

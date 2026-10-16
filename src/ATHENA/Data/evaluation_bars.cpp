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
#include "math_tree_v3.hpp"
#include "node_metadata.hpp"
#include <stdexcept>

tree
athena_promote_evaluation_bars (tree t, int& promoted, string mode,
                               athena::document::xml_format_version version) {
  const bool native= version == athena::document::xml_format_version::v3;
  if (native && is_func (t, MATH_BLOCK)) mode= "math";
  if (is_func (t, LEFT) || is_func (t, MID) || is_func (t, RIGHT) ||
      is_func (t, BIG) || is_func (t, MACRO) || is_func (t, XMACRO) ||
      is_func (t, ASSIGN) || is_func (t, QUOTE) || is_func (t, QUASI) ||
      is_func (t, QUASIQUOTE) || is_func (t, NAMED_SYMBOL) ||
      is_func (t, MATH_SYMBOL) || is_func (t, MATH_MIDDLE) ||
      is_func (t, BIG_OPERATOR)) return t;
  if (native && mode == "math" && is_func (t, MATH_SCRIPTS, 5) &&
      is_atomic (t[0]) && t[0]->label == "|")
    throw std::runtime_error (
      "Evaluation-bar maintenance cannot rebind a scripted native bar to its preceding expression");
  tree r= t;
  if (is_compound (t)) {
    r= tree (t, N(t));
    athena::node::copy_metadata (t, r);
    for (int i=0; i<N(t); ++i) {
      // Atomic concat children are converted together with their preceding
      // expression, not independently. DRD excludes delimiter/attribute data.
      bool delimiter= (is_func (t, AROUND) || is_func (t, VAR_AROUND)) &&
                      i != 1;
      delimiter= delimiter || (is_func (t, BIG_AROUND) && i == 0);
      delimiter= delimiter || (is_func (t, DELIMITED) && i != 5) ||
        (is_func (t, MATH_ACCENT) && i != 2) ||
        ((is_func (t, MATH_VARIANT) || is_func (t, MATH_LONG_ARROW)) && i == 0);
      bool binding= is_func (t, WITH) && i != N(t)-1;
      if (!delimiter && !binding && is_correctable_child (t, i)) {
        tree child_mode= the_drd->get_env_child (t, i, MODE, mode);
        r[i]= athena_promote_evaluation_bars (
          t[i], promoted, is_atomic (child_mode) ? child_mode->label : "text", version);
      }
      else r[i]= t[i];
    }
  }
  if (mode != "math" || (!is_atomic (r) && !is_concat (r))) return r;

  array<tree> tokens= concat_tokenize_source (r), result;
  bool changed= false;
  for (int i=0; i<N(tokens); ++i) {
    // Named relation tokens (mid, divides, shortmid, etc.) are not bars.
    const bool bar= is_atomic (tokens[i]) &&
      (tokens[i]->label == "|" || (!native && tokens[i]->label == "<vert>"));
    if (bar) {
      tree expression= concat_recompose (result);
      result= array<tree> ();
      tree replacement= native ? athena::math_v3::delimited ("", "|", 'a', expression) :
                                 tree (VAR_AROUND, ".", expression, "|");
      athena::node::copy_metadata (tokens[i], replacement);
      result << replacement;
      ++promoted;
      changed= true;
    }
    else {
      if (athena::node::get (tokens[i]) && is_atomic (tokens[i])) {
        const std::string text (tokens[i]->label.data (), N(tokens[i]->label));
        if (text.find ('|') != std::string::npos ||
            (!native && text.find ("<vert>") != std::string::npos))
          throw std::runtime_error (
            "Evaluation-bar maintenance cannot split an attributed inline text span");
      }
      result << tokens[i];
    }
  }
  return changed ? concat_recompose_source (t, result) : r;
}

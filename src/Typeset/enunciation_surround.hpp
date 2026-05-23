/******************************************************************************
* MODULE     : enunciation_surround.hpp
* DESCRIPTION: ATHENA-specific enunciation title/display isolation
******************************************************************************/

#ifndef ENUNCIATION_SURROUND_H
#define ENUNCIATION_SURROUND_H

#include "env.hpp"
#include "tree_label.hpp"

static inline string
athena_tree_tag (tree t) {
  if (is_atomic (t)) return "";
  if (L(t) == COMPOUND && N(t) > 0 && is_atomic (t[0]))
    return t[0]->label;
  return as_string (L(t));
}

static inline bool
athena_is_enunciation_surround (tree t) {
  if (L(t) == COMPOUND)
    return N(t) == 4 && is_atomic (t[0]) &&
           t[0]->label == "enunciation-surround";
  return as_string (L(t)) == "enunciation-surround" && N(t) == 3;
}

static inline int
athena_enunciation_surround_delta (tree t) {
  return L(t) == COMPOUND ? 1 : 0;
}

static inline bool
athena_display_math_tag (string tag) {
  return tag == "equation" || tag == "equation*" ||
         tag == "equation-lab" || tag == "equations-base" ||
         tag == "eqnarray" || tag == "eqnarray*" ||
         tag == "leqnarray" || tag == "leqnarray*" ||
         tag == "align" || tag == "align*" ||
         tag == "alignat" || tag == "alignat*" ||
         tag == "aligned" || tag == "aligned*" ||
         tag == "alignedat" || tag == "alignedat*" ||
         tag == "flalign" || tag == "flalign*" ||
         tag == "gather" || tag == "gather*" ||
         tag == "subequations";
}

static inline bool
athena_blank_atomic (tree t) {
  if (!is_atomic (t)) return false;
  string s= t->label;
  for (int i=0; i<N(s); i++)
    if (s[i] != ' ' && s[i] != '\t' && s[i] != '\n') return false;
  return true;
}

static inline bool
athena_with_requests_display_layout (tree t) {
  if (!(is_func (t, WITH) || is_func (t, STYLE_WITH) ||
        is_func (t, VAR_STYLE_WITH)))
    return false;
  int last= N(t) - 1;
  for (int i=0; i + 1 < last; i += 2)
    if (is_atomic (t[i])) {
      string var= t[i]->label;
      string val= "";
      if (is_atomic (t[i+1]))
        val= t[i+1]->label;
      else if (is_func (t[i+1], QUOTE, 1) && is_atomic (t[i+1][0]))
        val= t[i+1][0]->label;
      if (var == "math-display" && val == "true") return true;
      if (var == "par-mode" && val == "center") return true;
    }
  return false;
}

static inline bool
athena_enunciation_starts_display (edit_env env, tree t, int depth= 0) {
  if (depth > 32) return false;
  if (is_atomic (t)) return false;
  if (is_func (t, ARG, 1) && is_atomic (t[0]) &&
      (!is_nil (env->macro_arg)) &&
      env->macro_arg->item->contains (t[0]->label))
    return athena_enunciation_starts_display (
      env, env->macro_arg->item [t[0]->label], depth + 1);

  string tag= athena_tree_tag (t);
  if (athena_display_math_tag (tag)) return true;
  if (athena_with_requests_display_layout (t)) return true;
  if (is_func (t, DOCUMENT) || is_func (t, CONCAT)) {
    for (int i=0; i<N(t); i++) {
      if (athena_blank_atomic (t[i]) ||
          is_func (t[i], NO_INDENT, 0) ||
          is_func (t[i], VAR_NO_INDENT, 0) ||
          is_func (t[i], NO_PAGE_BREAK, 0) ||
          is_func (t[i], VAR_NO_PAGE_BREAK, 0))
        continue;
      return athena_enunciation_starts_display (env, t[i], depth + 1);
    }
    return false;
  }
  if (is_func (t, WITH) || is_func (t, STYLE_WITH) ||
      is_func (t, VAR_STYLE_WITH) || is_func (t, SURROUND) ||
      tag == "padded" || tag == "padded*" || tag == "small" ||
      tag == "indent-left" || tag == "indent-right")
    return N(t) > 0 &&
           athena_enunciation_starts_display (env, t[N(t)-1], depth + 1);
  return false;
}

static inline tree
athena_enunciation_surround_rewrite (edit_env env, tree t) {
  if (!athena_is_enunciation_surround (t)) return t;
  int d= athena_enunciation_surround_delta (t);
  tree left = t[d];
  tree right= t[d + 1];
  tree body = t[d + 2];
  bool display= athena_enunciation_starts_display (env, body);
  if ((!display) && is_func (body, ARG, 1))
    display= athena_enunciation_starts_display (env, env->exec (body));
  if (display) {
    tree title_line= tree (WITH, "par-mode", "left",
                           tree (CONCAT, tree (NO_INDENT), left));
    return tree (DOCUMENT, title_line, body);
  }
  return tree (SURROUND, left, right, body);
}

#endif // ENUNCIATION_SURROUND_H

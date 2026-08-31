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
    return (N(t) == 4 || N(t) == 5) && is_atomic (t[0]) &&
           t[0]->label == "enunciation-surround";
  return as_string (L(t)) == "enunciation-surround" &&
         (N(t) == 3 || N(t) == 4);
}

static inline int
athena_enunciation_surround_delta (tree t) {
  return L(t) == COMPOUND ? 1 : 0;
}

static inline bool
athena_enunciation_surround_has_color (tree t) {
  if (!athena_is_enunciation_surround (t)) return false;
  int d= athena_enunciation_surround_delta (t);
  return N(t) == d + 4;
}

static inline tree
athena_enunciation_surround_color (tree t) {
  if (!athena_enunciation_surround_has_color (t)) return tree ("none");
  int d= athena_enunciation_surround_delta (t);
  return t[d + 3];
}

static inline tree
athena_set_enunciation_surround_color (tree t, tree color, bool& found) {
  if (is_atomic (t)) return t;
  if (athena_is_enunciation_surround (t)) {
    int d= athena_enunciation_surround_delta (t);
    tree out (t, d + 4);
    for (int i=0; i<d+3; i++) out[i]= t[i];
    out[d + 3]= color;
    found= true;
    return out;
  }
  tree out (t, N(t));
  for (int i=0; i<N(t); i++)
    out[i]= athena_set_enunciation_surround_color (t[i], color, found);
  return out;
}

static inline bool
athena_is_enunciation_background (tree t) {
  if (L(t) == COMPOUND)
    return N(t) == 3 && is_atomic (t[0]) &&
           t[0]->label == "athena-enunciation-background";
  return as_string (L(t)) == "athena-enunciation-background" && N(t) == 2;
}

static inline int
athena_enunciation_background_delta (tree t) {
  return L(t) == COMPOUND ? 1 : 0;
}

static inline tree
athena_enunciation_background_render_rewrite (tree t) {
  int d= athena_enunciation_background_delta (t);
  return tree (WITH, "ornament-color", t[d],
               "ornament-shape", "rectangular",
               "ornament-border", "0ln", tree (ORNAMENT, t[d + 1]));
}

static inline bool
athena_is_proof_qed_layout (tree t) {
  if (L(t) == COMPOUND)
    return N(t) == 5 && is_atomic (t[0]) &&
           t[0]->label == "proof-qed-layout";
  return as_string (L(t)) == "proof-qed-layout" && N(t) == 4;
}

static inline int
athena_proof_qed_layout_delta (tree t) {
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
      env->macro_arg->item->contains (t[0]->label)) {
    string name= t[0]->label;
    tree value= env->macro_arg->item [name];
    list<hashmap<string,tree> > old_arg= env->macro_arg;
    list<hashmap<string,path> > old_src= env->macro_src;
    if (!is_nil (env->macro_arg)) env->macro_arg= env->macro_arg->next;
    if (!is_nil (env->macro_src)) env->macro_src= env->macro_src->next;
    bool r= athena_enunciation_starts_display (env, value, depth + 1);
    env->macro_arg= old_arg;
    env->macro_src= old_src;
    return r;
  }

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
athena_resolve_macro_argument (edit_env env, tree t) {
  if (!is_func (t, ARG, 1) || !is_atomic (t[0])) return t;
  list<hashmap<string,tree> > frames= env->macro_arg;
  list<hashmap<string,path> > sources= env->macro_src;
  while (!is_nil (frames)) {
    if (frames->item->contains (t[0]->label)) {
      tree value= frames->item [t[0]->label];
      if (value != t) {
        if (!is_nil (sources) && sources->item->contains (t[0]->label)) {
          path source= sources->item [t[0]->label];
          if (is_accessible (source)) return attach_dip (value, source);
        }
        return value;
      }
    }
    frames= frames->next;
    if (!is_nil (sources)) sources= sources->next;
  }
  return t;
}

static inline tree
athena_shallow_copy (tree t) {
  tree out (t, N(t));
  for (int i=0; i<N(t); i++) out[i]= t[i];
  return out;
}

static inline tree
athena_resolve_macro_argument_tree (edit_env env, tree t) {
  tree resolved= athena_resolve_macro_argument (env, t);
  if (resolved != t) return resolved;
  if (!(is_func (t, DOCUMENT) || is_func (t, CONCAT))) return t;
  tree out= athena_shallow_copy (t);
  for (int i=0; i<N(t); i++)
    out[i]= athena_resolve_macro_argument_tree (env, t[i]);
  return out;
}

static inline tree
athena_append_inline_qed (tree t, tree qed) {
  tree out (CONCAT);
  out << t << qed;
  return out;
}

static inline bool
athena_append_qed_to_table_tail (tree t, tree qed, tree& out) {
  if (is_atomic (t) || N(t) == 0) return false;
  string tag= athena_tree_tag (t);
  if (tag == "document" || tag == "concat" ||
      tag == "tformat" || tag == "table") {
    tree child;
    if (!athena_append_qed_to_table_tail (t[N(t)-1], qed, child))
      return false;
    out= athena_shallow_copy (t);
    out[N(t)-1]= child;
    return true;
  }
  if (tag == "row") {
    out= athena_shallow_copy (t);
    tree& tail= out[N(t)-1];
    if (athena_tree_tag (tail) == "cell" && N(tail) > 0) {
      tree cell= athena_shallow_copy (tail);
      cell[0]= athena_append_inline_qed (tail[0], qed);
      tail= cell;
    }
    else
      tail= athena_append_inline_qed (tail, qed);
    return true;
  }
  if (tag == "cell") {
    out= athena_shallow_copy (t);
    out[N(t)-1]= athena_append_inline_qed (out[N(t)-1], qed);
    return true;
  }
  return false;
}

static inline bool
athena_append_qed_to_terminal_eqnarray (tree t, tree qed, tree& out) {
  if (is_atomic (t)) return false;
  string tag= athena_tree_tag (t);
  if (tag == "eqnarray" || tag == "eqnarray*" ||
      tag == "leqnarray" || tag == "leqnarray*") {
    tree body;
    if (!athena_append_qed_to_table_tail (t[N(t)-1], qed, body))
      return false;
    out= athena_shallow_copy (t);
    out[N(t)-1]= body;
    return true;
  }
  if (is_func (t, DOCUMENT) || is_func (t, CONCAT)) {
    int i= N(t) - 1;
    while (i >= 0 && athena_blank_atomic (t[i])) i--;
    if (i < 0) return false;
    tree child;
    if (!athena_append_qed_to_terminal_eqnarray (t[i], qed, child))
      return false;
    out= athena_shallow_copy (t);
    out[i]= child;
    return true;
  }
  if (is_func (t, WITH) || is_func (t, STYLE_WITH) ||
      is_func (t, VAR_STYLE_WITH) || tag == "padded" ||
      tag == "padded*" || tag == "small" || tag == "indent-left" ||
      tag == "indent-right") {
    tree child;
    if (!athena_append_qed_to_terminal_eqnarray (t[N(t)-1], qed, child))
      return false;
    out= athena_shallow_copy (t);
    out[N(t)-1]= child;
    return true;
  }
  return false;
}

static inline tree
athena_proof_qed_layout_rewrite (edit_env env, tree t) {
  if (!athena_is_proof_qed_layout (t)) return t;
  int d= athena_proof_qed_layout_delta (t);
  tree qed = env->exec (t[d + 2]);
  tree body= athena_resolve_macro_argument_tree (env, t[d + 3]);
  tree normal= tree (COMPOUND, t[d], t[d + 1], t[d + 3]);
  if (qed == "") return normal;
  tree rewritten;
  if (athena_append_qed_to_terminal_eqnarray (body, qed, rewritten)) {
    return tree (COMPOUND, t[d], t[d + 1], rewritten);
  }
  return tree (SURROUND, "", t[d + 2], normal);
}

static inline tree
athena_enunciation_surround_rewrite (edit_env env, tree t) {
  if (!athena_is_enunciation_surround (t)) return t;
  int d= athena_enunciation_surround_delta (t);
  tree left = t[d];
  tree right= t[d + 1];
  tree body = t[d + 2];
  bool display= athena_enunciation_starts_display (env, body);
  if (display) {
    tree title_line= tree (WITH, "par-mode", "left",
                           tree (CONCAT, tree (NO_INDENT), left));
    return tree (DOCUMENT, title_line, body);
  }
  return tree (SURROUND, left, right, body);
}

static inline tree
athena_enunciation_surround_render_rewrite (edit_env env, tree t) {
  tree body= athena_enunciation_surround_rewrite (env, t);
  tree color= athena_enunciation_surround_color (t);
  if (is_atomic (color) && color->label == "none") return body;
  return tree (WITH, "ornament-color", color,
               "ornament-shape", "rectangular",
               "ornament-border", "0ln", tree (ORNAMENT, body));
}

#endif // ENUNCIATION_SURROUND_H

// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "native_math_edit.hpp"
#include "editor.hpp"
#include "tree_correct.hpp"
#include "Scheme/Scheme/native_interfaces.hpp"

bool math_script_context (tree t) {
  return is_func (t, LSUB) || is_func (t, LSUP) ||
         is_func (t, RSUB) || is_func (t, RSUP);
}

bool math_script_only (tree t) {
  path ip= obtain_ip (t);
  if (!ip_attached (ip) || is_nil (ip)) return false;
  tree root= get_current_editor ()->the_root ();
  tree parent= subtree (root, reverse (ip->next));
  int index= ip->item;
  return is_func (parent, CONCAT) &&
    (index == 0 || !math_script_context (parent[index-1])) &&
    (index+1 == N(parent) || !math_script_context (parent[index+1]));
}

void math_sqrt_toggle (tree t) {
  if (!is_func (t, SQRT)) return;
  if (N(t) == 1) {
    t= tree_insert (t, 1, tree (TUPLE, ""));
    get_current_editor ()->go_to (reverse (obtain_ip (t)) * path (1, 0));
  }
  else if (N(t) == 2) tree_remove (t, 1, 1);
}

void math_correct_all () {
  editor ed= get_current_editor ();
  tree root= ed->the_root ();
  path p= ed->the_buffer_path ();
  if (ed->selection_active_any ()) {
    p= ed->selection_get_path ();
    if (ed->selection_get () != subtree (root, p)) {
      ed->set_message ("Only implemented for complete subtrees", "correct formula");
      return;
    }
  }
  tree source= subtree (root, p);
  tree corrected= manual_correct (source);
  if (corrected != source) tree_assign (source, corrected);
}

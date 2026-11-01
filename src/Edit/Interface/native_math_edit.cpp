// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "native_math_edit.hpp"
#include "editor.hpp"
#include "tree_correct.hpp"
#include "convert.hpp"
#include "Data/Convert/Xml/math_source_normalization.hpp"
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

namespace {
bool bracket_wrapper (tree t) {
  return is_func (t, AROUND) || is_func (t, VAR_AROUND) ||
    is_func (t, BIG_AROUND) || is_compound (t, "temp-slot");
}

bool restore_bracket_cursor (editor ed, tree t, path p) {
  if (is_compound (t, "temp-slot")) {
    ed->go_to (p * path (0, 0));
    ed->remove_structure_upwards ();
    return true;
  }
  if (is_compound (t))
    for (int i=0; i<N(t); ++i)
      if (restore_bracket_cursor (ed, t[i], p * i)) return true;
  return false;
}
}

void math_brackets_refresh () {
  editor ed= get_current_editor ();
  ed->insert_tree (compound ("temp-slot", ""), path (0, 0));
  tree root= ed->the_root ();
  path p= path_up (ed->the_path ());
  const path buffer= ed->the_buffer_path ();
  while (p != buffer) {
    path parent= path_up (p);
    tree outer= subtree (root, parent);
    if (!bracket_wrapper (subtree (root, p)) &&
        !bracket_wrapper (outer) && !is_func (outer, CONCAT)) break;
    p= parent;
  }
  tree source= subtree (root, p);
  tree replacement= downgrade_big (
    upgrade_brackets (downgrade_brackets (source, true, false), "math"));
  tree_assign (source, replacement);
  restore_bracket_cursor (ed, subtree (root, p), p);
}

void math_open_latex_dialog () {
  const array<string> values= athena_native_latex_formula_dialog ();
  if (N(values) < 2) return;
  const string mode= values[0];
  string input= values[1];
  if (mode == "inline") input= "$" * input * "$";
  else if (mode == "display") input= "\\[" * input * "\\]";
  tree converted= latex_to_tree (parse_latex (input));
  editor ed= get_current_editor ();
  if (mode == "inline") {
    if (is_func (converted, DOCUMENT) && N(converted) > 0) converted= converted[0];
    if (is_func (converted, WITH) && N(converted) >= 3 &&
        converted[0] == "mode" && converted[1] == "math")
      converted= converted[N(converted)-1];
    ed->insert_tree (athena::document::normalize_imported_math (
      compound ("math", converted)));
  }
  else {
    converted= athena::document::normalize_imported_math (converted);
    if (is_func (converted, DOCUMENT))
      for (int i=0; i<N(converted); ++i) ed->insert_tree (converted[i]);
    else ed->insert_tree (converted);
  }
}

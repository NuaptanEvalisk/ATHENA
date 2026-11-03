// Copyright (C) 2015 Joris van der Hoeven, 2026 ATHENA contributors.
// GPL-3.0-or-later.
#include "Editor/Commands/insertion_commands.hpp"
#include "Editor/editor.hpp"
#include "native_interfaces.hpp"
#include "Editor/Commands/native_text_edit.hpp"

void insertion_insert (tree content) {
  get_current_editor ()->insert_tree (content);
}

void insertion_make (tree_label tag, int count) {
  editor ed= get_current_editor ();
  tree wrapper (tag, "");
  if (text_node_in_group (wrapper, "make-wrapped-tag")) {
    bool selected= ed->selection_active_any ();
    ed->selection_cut ("wrapbuf");
    ed->make_compound (tag, count);
    if (selected) ed->selection_paste ("wrapbuf");
  }
  else if (text_node_in_group (wrapper, "make-inline-tag")) {
    if (!ed->selection_active_any () || ed->selection_active_small () || ed->selection_active_table ()) {
      ed->make_compound (tag, count);
      return;
    }
    tree selected= compound ("par-block", ed->selection_get ());
    ed->selection_cut ("wrapbuf");
    ed->make_return_after ();
    ed->make_compound (tag, count);
    tree_set_diff (ed->the_subtree (path_up (ed->the_path ())), selected);
    path container= ed->search_upwards (as_string (tag));
    if (!is_nil (container)) {
      ed->go_to (ed->command_tree_end (ed->the_root (), container));
      ed->make_return_before ();
    }
  }
  else if (ed->command_tree_with_like (wrapper) && ed->selection_active_any ()) {
    wrapper[0]= ed->selection_get ();
    path end= ed->command_tree_end (wrapper, path (0));
    ed->selection_cut ("nowhere");
    ed->var_insert_tree (wrapper, end);
  }
  else ed->make_compound (tag, count);
}

void insertion_make_with (string variable, tree value) {
  editor ed= get_current_editor ();
  if (ed->selection_active_table ()) ed->cell_set_format (variable, value);
  else if (is_atomic (value)) ed->make_with (variable, value->label);
  else ed->var_insert_tree (tree (WITH, variable, value, ""), path (2, 0));
}

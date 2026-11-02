// Copyright (C) 2015 Joris van der Hoeven, 2026 ATHENA contributors.
// GPL-3.0-or-later.
#include "insertion_commands.hpp"
#include "editor.hpp"
#include "native_interfaces.hpp"
#include "native_text_edit.hpp"
#include <stdexcept>

path insertion_path (tree content, object selectors) {
  tree current= content;
  path prefix;
  editor ed= get_current_editor ();
  for (; !is_null (selectors); selectors= cdr (selectors)) {
    object selector= car (selectors);
    if (is_null (cdr (selectors))) {
      if (selector == keyword_object ("start")) return ed->command_tree_start (content, prefix);
      if (selector == keyword_object ("end")) return ed->command_tree_end (content, prefix);
    }
    if (is_atomic (current)) return prefix * as_path (selectors);
    int index;
    if (selector == keyword_object ("first")) index= 0;
    else if (selector == keyword_object ("last")) index= N(current)-1;
    else if (is_int (selector)) index= as_int (selector);
    else throw std::runtime_error ("path-in: invalid path selector");
    if (index < 0 || index >= N(current))
      throw std::runtime_error ("path-in: child index out of range");
    prefix= prefix * index;
    current= current[index];
  }
  return prefix;
}

void insertion_insert (tree content, object selectors) {
  editor ed= get_current_editor ();
  if (is_null (selectors)) ed->insert_tree (content);
  else ed->var_insert_tree (content, insertion_path (content, selectors));
}

void insertion_make (tree_label tag, object arity) {
  const int count= is_null (arity) ? -1 : as_int (car (arity));
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

void insertion_make_with (string variable, object value) {
  editor ed= get_current_editor ();
  if (ed->selection_active_table ()) ed->cell_set_format (variable, content_to_tree (value));
  else if (is_string (value)) ed->make_with (variable, as_string (value));
  else ed->var_insert_tree (tree (WITH, variable, content_to_tree (value), ""), path (2, 0));
}

// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "Editor/Commands/editing_utilities.hpp"
#include "native_interfaces.hpp"
#include "analyze.hpp"
#include "Editor/editor.hpp"
#include "Editor/Commands/generic_editor_commands.hpp"
#include "Editor/Commands/native_text_edit.hpp"
#include "Editor/Commands/presentation_commands.hpp"
#include "Document/NodeContracts/node_groups.hpp"

namespace {
bool parent (tree& t) {
  if (admits_edit_observer (t)) return false;
  path ip= obtain_ip (t);
  if (!ip_attached (ip) || is_nil (ip)) return false;
  tree root= get_current_editor ()->the_root ();
  t= subtree (root, reverse (ip->next));
  return true;
}
void mouse_fold (tree t, bool fold) {
  path ip= obtain_ip (t);
  if (!ip_attached (ip)) return;
  get_current_editor ()->go_to_start (reverse (ip));
  while (parent (t)) {
    if (text_node_in_group (t, fold ? "alternate-second-tag" : "alternate-first-tag")) {
      editing_alternate_toggle (t);
      return;
    }
  }
}
}

bool editing_unnumbered_symbol (tree_label tag) {
  string name= as_string (tag);
  if (!ends (name, "*")) return false;
  return text_node_in_group (tree (as_tree_label (name (0, N(name)-1))), "numbered-tag");
}

bool editing_variant_context (tree t) {
  if (text_node_in_group (t, "variant-tag")) return true;
  string name= as_string (L(t));
  return editing_unnumbered_symbol (L(t)) &&
    text_node_in_group (tree (as_tree_label (name (0, N(name)-1))), "variant-tag");
}

void editing_variant_set (tree t, tree_label tag) {
  editor ed= get_current_editor ();
  path ip= obtain_ip (t);
  if (!ip_attached (ip)) return;
  path p= reverse (ip), cursor= ed->the_path ();
  path relative= p <= cursor ? cursor / p : path ();
  const int child= is_nil (relative) ? -1 : relative->item;
  t= tree_assign_node (t, tag);
  if (child >= 0 && !generic_tree_accessible_child (t, child)) {
    array<tree> children= generic_tree_accessible_children (t);
    if (N(children) > 0) ed->go_to_start (reverse (obtain_ip (children[0])));
  }
  ed->manual_focus_set (p);
}

void editing_alternate_toggle (tree t) {
  do {
    if (presentation_alternate (t)) return;
    if (text_node_in_group (t, "alternate-tag")) {
      string name= as_string (L(t));
      auto next= athena::contracts::standard_node_groups ().alternate (
        std::string_view (name.data (), N(name)));
      editing_variant_set (t, as_tree_label (string (next.c_str ())));
      return;
    }
  } while (parent (t));
}

void editing_mouse_fold (tree t) { mouse_fold (t, true); }
void editing_mouse_unfold (tree t) { mouse_fold (t, false); }

void editing_select_enlarge () {
  editor ed= get_current_editor ();
  if (ed->selection_active_enlarging ()) ed->select_enlarge ();
  else {
    ed->selection_cancel ();
    ed->selection_set_start ();
    ed->select_from_keyboard (true);
  }
}

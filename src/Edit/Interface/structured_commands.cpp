/******************************************************************************
* MODULE     : structured_commands.cpp
* DESCRIPTION: Actor-owned structured insertion, removal and navigation
* COPYRIGHT  : (C) 2001 Joris van der Hoeven, 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
*******************************************************************************/

#include "structured_commands.hpp"
#include "editor.hpp"
#include "native_interfaces.hpp"
#include "new_view.hpp"
#include "tree_traverse.hpp"

namespace {

bool parent_tree (tree t, tree& parent) {
  if (admits_edit_observer (t)) return false;
  path ip= obtain_ip (t);
  if (!ip_attached (ip) || is_nil (ip)) return false;
  tree root= get_current_editor ()->the_root ();
  parent= subtree (root, reverse (ip->next));
  return true;
}

bool attached_path (tree t, path& p) {
  path ip= obtain_ip (t);
  if (!ip_attached (ip)) return false;
  p= reverse (ip);
  return true;
}

bool descendant_index (path base, path cursor, int& index) {
  while (!is_nil (base)) {
    if (is_nil (cursor) || base->item != cursor->item) return false;
    base= base->next;
    cursor= cursor->next;
  }
  if (is_nil (cursor)) return false;
  index= cursor->item;
  return true;
}

bool down_index (tree t, int& index) {
  path p;
  return attached_path (t, p) &&
    descendant_index (p, get_current_editor ()->the_path (), index) &&
    index >= 0 && index < N (t);
}

bool down_tree (tree t, tree& child, path& child_path) {
  int index;
  path p;
  if (!attached_path (t, p) || !down_index (t, index)) return false;
  child= t[index];
  child_path= p * index;
  return true;
}

bool is_tree_branch (tree t) {
  return is_compound (t, "tree");
}

tree branch_active (tree t) {
  int index;
  if (!down_index (t, index) || index != 0) return t;
  tree parent;
  return parent_tree (t, parent) && is_tree_branch (parent) ? parent : t;
}

tree cursor_container () {
  editor ed= get_current_editor ();
  path p= ed->the_path ();
  if (is_nil (p)) return tree ();
  p= path_up (p);
  tree root= ed->the_root ();
  return has_subtree (root, p) ? subtree (root, p) : tree ();
}

void go_child (tree t, int index, bool at_end) {
  if (index < 0 || index >= N (t)) return;
  path p;
  if (!attached_path (t, p)) return;
  editor ed= get_current_editor ();
  path child= p * index;
  ed->go_to (at_end ? end (ed->the_root (), child)
                     : start (ed->the_root (), child));

  // A tree branch stores its label in child zero.  Preserve the historical
  // branch-go-to behavior when a move lands directly on a nested tree node.
  tree current= cursor_container ();
  if (is_tree_branch (current) && N (current) > 0)
    go_child (current, 0, at_end);
}

struct focus_pointer {
  observer pointer;
  explicit focus_pointer (tree t): pointer (tree_pointer_new (t)) {}
  ~focus_pointer () { tree_pointer_delete (pointer); }
  void restore () {
    tree t= obtain_tree (pointer);
    path ip= obtain_ip (t);
    if (ip_attached (ip)) get_current_editor ()->manual_focus_set (reverse (ip));
  }
};

bool scheme_predicate (const char* name, tree t) {
  return as_bool (call (name, object (t)));
}

tree focus_tree () {
  editor ed= get_current_editor ();
  path p= ed->focus_get ();
  return ed->test_subtree (p) ? ed->the_subtree (p) : tree ();
}

void dispatch_focus (const char* command, bool flag) {
  call (command, object (focus_tree ()), object (flag));
}

void dispatch_focus (const char* command, object direction) {
  call (command, object (focus_tree ()), direction);
}

bool simple_tag (tree t) {
  return is_func (t, CONCAT) || is_func (t, DOCUMENT) ||
    is_func (t, TFORMAT) || is_func (t, TABLE) || is_func (t, ROW) ||
    is_func (t, CELL) || is_compound (t, "shown") ||
    is_compound (t, "hidden");
}

void outward (tree t, const char* command, bool direction) {
  tree parent;
  if (parent_tree (t, parent))
    call (command, object (parent), object (direction));
}

void outward (tree t, const char* command, object argument) {
  tree parent;
  if (parent_tree (t, parent)) call (command, object (parent), argument);
}

void outward (tree t, const char* command) {
  tree parent;
  if (parent_tree (t, parent)) call (command, object (parent));
}

void tree_insert_horizontal (tree t, bool forwards) {
  int index;
  if (!down_index (t, index)) return;
  if (index == 0) {
    tree parent;
    if (parent_tree (t, parent)) {
      t= parent;
      if (!is_tree_branch (t) || !down_index (t, index)) return;
    }
  }
  if (!is_tree_branch (t)) return;
  int pos= index + (forwards ? 1 : 0);
  t= tree_insert (t, pos, tree (TUPLE, ""));
  go_child (t, pos, false);
}

void tree_remove_horizontal (tree t, bool forwards) {
  int pos;
  if (!down_index (t, pos)) return;
  if (pos == 0) {
    tree parent;
    if (parent_tree (t, parent)) {
      t= parent;
      if (!is_tree_branch (t) || !down_index (t, pos)) return;
    }
  }
  if (!is_tree_branch (t) || pos <= 0) return;
  if (forwards) {
    t= tree_remove (t, pos, 1);
    if (pos == N (t)) {
      path p;
      if (attached_path (t, p)) get_current_editor ()->go_to (end (get_current_editor ()->the_root (), p));
    }
    else go_child (t, pos, false);
  }
  else if (pos == 1) go_child (t, 0, true);
  else (void) tree_remove (t, pos - 1, 1);
}

void tree_insert_vertical (tree t, bool downwards) {
  int index;
  if (!down_index (t, index)) return;
  if (downwards && index == 0) {
    int pos= N (t);
    t= tree_insert (t, pos, tree (TUPLE, ""));
    go_child (t, pos, false);
    return;
  }

  tree child;
  path child_path;
  if (index != 0 && !down_tree (t, child, child_path)) return;
  if (index == 0) child= t;
  tree old= copy (child);
  tree wrapper= downwards ? compound ("tree", old, tree (""))
                            : compound ("tree", tree (""), old);
  child= tree_assign (child, wrapper);
  go_child (child, downwards ? 1 : 0, false);
}

void tree_horizontal (tree t, bool forwards) {
  t= branch_active (t);
  int index;
  if (!down_index (t, index)) return;
  if (!forwards && index > 1) go_child (t, index - 1, true);
  else if (forwards && index != 0 && index < N (t) - 1)
    go_child (t, index + 1, false);
}

void tree_vertical (tree original, bool downwards) {
  tree t= branch_active (original);
  int index;
  if (!down_index (t, index)) return;
  if (!downwards && index != 0) go_child (t, 0, true);
  else if (downwards) {
    int original_index;
    if (down_index (original, original_index) && original_index == 0)
      go_child (original, N (original) / 2, false);
  }
}

void tree_extremal (tree t, bool forwards) {
  t= branch_active (t);
  if (N (t) <= 1) return;
  go_child (t, forwards ? N (t) - 1 : 1, forwards);
}

void generic_horizontal_once (tree t, bool forwards) {
  if (is_tree_branch (t)) { tree_horizontal (t, forwards); return; }
  if (!scheme_predicate ("structured-horizontal?", t)) {
    outward (t, "structured-horizontal", forwards);
    return;
  }
  tree child;
  path child_path;
  if (!down_tree (t, child, child_path)) return;
  focus_pointer focus (t);
  path next= forwards ? next_argument (get_current_editor ()->the_root (), child_path)
                      : previous_argument (get_current_editor ()->the_root (), child_path);
  if (!is_nil (next)) get_current_editor ()->go_to (next);
  focus.restore ();
}

void generic_vertical_once (tree t, bool downwards) {
  if (is_tree_branch (t)) { tree_vertical (t, downwards); return; }
  outward (t, "structured-vertical", downwards);
}

object similar_labels (tree t) {
  return call ("similar-to", symbol_object (as_string (L (t))));
}

bool label_in_list (tree t, object labels) {
  if (!is_list (labels)) return false;
  string label= as_string (L (t));
  array<object> items= as_array_object (labels);
  for (int i= 0; i < N (items); ++i)
    if (is_symbol (items[i]) && as_symbol (items[i]) == label) return true;
  return false;
}

tree find_similar_upwards (tree t, object labels) {
  tree current= t;
  while (!is_nil (current)) {
    if (label_in_list (current, labels)) return current;
    tree parent;
    if (!parent_tree (current, parent)) break;
    current= parent;
  }
  return tree ();
}

void refocus_similar (object labels) {
  editor ed= get_current_editor ();
  ed->selection_cancel ();
  tree target= find_similar_upwards (focus_tree (), labels);
  path p;
  if (!is_nil (target) && attached_path (target, p)) ed->manual_focus_set (p);
}

void move_to_similar_tag (object labels, bool forwards) {
  call (forwards ? "go-to-next-tag" : "go-to-previous-tag", labels);
}

} // namespace

void generic_traverse_horizontal (tree, bool forwards) {
  call (forwards ? "go-to-next-word" : "go-to-previous-word");
}

void generic_traverse_vertical (tree t, bool downwards) {
  if (is_func (t, DOCUMENT)) {
    call (downwards ? "go-to-next-tag" : "go-to-previous-tag",
          symbol_object ("document"));
    return;
  }
  outward (t, "traverse-vertical", downwards);
}

void generic_traverse_incremental (tree t, bool forwards) {
  object labels= similar_labels (t);
  move_to_similar_tag (labels, forwards);
  refocus_similar (labels);
}

void generic_traverse_extremal (tree t, bool forwards) {
  object labels= similar_labels (t);
  editor ed= get_current_editor ();
  while (true) {
    path before= copy (ed->the_path ());
    move_to_similar_tag (labels, forwards);
    if (ed->the_path () == before) break;
  }
  call ("structured-inner-extremal", object (t), object (forwards));
  refocus_similar (labels);
}

void generic_traverse_previous () { dispatch_focus ("traverse-incremental", false); }
void generic_traverse_next () { dispatch_focus ("traverse-incremental", true); }
void generic_traverse_first () { dispatch_focus ("traverse-extremal", false); }
void generic_traverse_last () { dispatch_focus ("traverse-extremal", true); }
void generic_traverse_left () { dispatch_focus ("traverse-horizontal", false); }
void generic_traverse_right () { dispatch_focus ("traverse-horizontal", true); }
void generic_traverse_up () { dispatch_focus ("traverse-vertical", false); }
void generic_traverse_down () { dispatch_focus ("traverse-vertical", true); }

void generic_traverse_previous_section_title () {
  object labels= call ("similar-to", symbol_object ("section"));
  call ("go-to-previous-tag", labels);
}

void generic_swipe_horizontal (tree t, bool forwards) {
  outward (t, "swipe-horizontal", forwards);
}

void generic_swipe_vertical (tree t, bool downwards) {
  outward (t, "swipe-vertical", downwards);
}

void generic_swipe_left () { dispatch_focus ("swipe-horizontal", false); }
void generic_swipe_right () { dispatch_focus ("swipe-horizontal", true); }
void generic_swipe_up () { dispatch_focus ("swipe-vertical", false); }
void generic_swipe_down () { dispatch_focus ("swipe-vertical", true); }

void generic_structured_maximize (tree t) { outward (t, "structured-maximize"); }
void generic_structured_minimize (tree t) { outward (t, "structured-minimize"); }

bool generic_wheel_capture () { return false; }
void generic_wheel_event (object, object) {}

bool generic_focus_has_variants (tree t) {
  object variants= call ("focus-variants-of", object (t));
  return is_list (variants) && N (as_array_object (variants)) > 1;
}

bool generic_focus_has_toggles (tree t) {
  return as_bool (call ("numbered-context?", object (t))) ||
         as_bool (call ("alternate-context?", object (t)));
}

bool generic_focus_can_move (tree) { return true; }

bool generic_focus_can_insert_remove (tree t) {
  bool structured=
    as_bool (call ("structured-horizontal?", object (t))) ||
    as_bool (call ("structured-vertical?", object (t)));
  return structured && as_bool (call ("cursor-inside?", object (t)));
}

bool generic_focus_can_insert (tree t) { return N (t) < maximal_arity (t); }
bool generic_focus_can_remove (tree t) { return N (t) > minimal_arity (t); }
bool generic_focus_has_geometry (tree) { return false; }

bool generic_focus_has_parameters (tree t) {
  return as_bool (call ("focus-has-preferences?", object (t)));
}

bool generic_focus_can_search (tree) { return false; }
bool generic_focus_has_search_menu (tree) { return false; }

void generic_structured_insert_left () { dispatch_focus ("structured-insert-horizontal", false); }
void generic_structured_insert_right () { dispatch_focus ("structured-insert-horizontal", true); }
void generic_structured_remove_left () { dispatch_focus ("structured-remove-horizontal", false); }
void generic_structured_remove_right () { dispatch_focus ("structured-remove-horizontal", true); }
void generic_structured_insert_up () { dispatch_focus ("structured-insert-vertical", false); }
void generic_structured_insert_down () { dispatch_focus ("structured-insert-vertical", true); }
void generic_structured_remove_up () { dispatch_focus ("structured-remove-vertical", false); }
void generic_structured_remove_down () { dispatch_focus ("structured-remove-vertical", true); }
void generic_structured_insert_start () { dispatch_focus ("structured-insert-extremal", false); }
void generic_structured_insert_end () { dispatch_focus ("structured-insert-extremal", true); }
void generic_structured_insert_top () { dispatch_focus ("structured-insert-incremental", false); }
void generic_structured_insert_bottom () { dispatch_focus ("structured-insert-incremental", true); }
void generic_structured_left () { dispatch_focus ("structured-horizontal", false); }
void generic_structured_right () { dispatch_focus ("structured-horizontal", true); }
void generic_structured_up () { dispatch_focus ("structured-vertical", false); }
void generic_structured_down () { dispatch_focus ("structured-vertical", true); }
void generic_structured_start () { dispatch_focus ("structured-extremal", false); }
void generic_structured_end () { dispatch_focus ("structured-extremal", true); }
void generic_structured_top () { dispatch_focus ("structured-incremental", false); }
void generic_structured_bottom () { dispatch_focus ("structured-incremental", true); }
void generic_structured_exit_left () { dispatch_focus ("structured-exit", false); }
void generic_structured_exit_right () { dispatch_focus ("structured-exit", true); }
void generic_special_back () { dispatch_focus ("special-navigate", keyword_object ("previous")); }
void generic_special_forward () { dispatch_focus ("special-navigate", keyword_object ("next")); }
void generic_special_return () { dispatch_focus ("special-navigate", keyword_object ("first")); }
void generic_special_shift_return () { dispatch_focus ("special-navigate", keyword_object ("last")); }
void generic_special_left () { dispatch_focus ("special-horizontal", false); }
void generic_special_right () { dispatch_focus ("special-horizontal", true); }
void generic_special_up () { dispatch_focus ("special-vertical", false); }
void generic_special_down () { dispatch_focus ("special-vertical", true); }
void generic_special_first () { dispatch_focus ("special-extremal", false); }
void generic_special_last () { dispatch_focus ("special-extremal", true); }
void generic_special_previous () { dispatch_focus ("special-incremental", false); }
void generic_special_next () { dispatch_focus ("special-incremental", true); }

bool generic_context (tree) { return true; }

bool generic_complex_context (tree t) {
  return is_compound (t) && !simple_tag (t);
}

bool generic_simple_context (tree t) {
  if (is_atomic (t)) return true;
  if (!simple_tag (t)) return false;
  tree child;
  path p;
  return down_tree (t, child, p) && generic_simple_context (child);
}

bool generic_document_context (tree t) {
  return is_func (t, DOCUMENT);
}

bool generic_table_markup_context (tree t) {
  if (is_func (t, TABLE) || is_func (t, TFORMAT)) return true;
  if (N (t) != 1) return false;
  tree child= t[0];
  return is_func (child, TABLE) || is_func (child, TFORMAT) ||
    (is_func (child, DOCUMENT, 1) &&
     (is_func (child[0], TABLE) || is_func (child[0], TFORMAT)));
}

bool generic_structured_horizontal_context (tree t) {
  return is_dynamic (t) || generic_table_markup_context (t);
}

bool generic_structured_vertical_context (tree t) {
  return is_tree_branch (t) || generic_table_markup_context (t);
}

void generic_structured_insert_horizontal (tree t, bool forwards) {
  if (is_tree_branch (t)) { tree_insert_horizontal (t, forwards); return; }
  if (scheme_predicate ("structured-horizontal?", t)) {
    tree child;
    path p;
    if (down_tree (t, child, p)) get_current_editor ()->insert_argument (p, forwards);
    return;
  }
  outward (t, "structured-insert-horizontal", forwards);
}

void generic_structured_insert_vertical (tree t, bool downwards) {
  if (is_tree_branch (t)) { tree_insert_vertical (t, downwards); return; }
  outward (t, "structured-insert-vertical", downwards);
}

void generic_structured_remove_horizontal (tree t, bool forwards) {
  if (is_tree_branch (t)) { tree_remove_horizontal (t, forwards); return; }
  if (scheme_predicate ("structured-horizontal?", t)) {
    tree child;
    path p;
    if (down_tree (t, child, p)) get_current_editor ()->remove_argument (p, forwards);
    return;
  }
  outward (t, "structured-remove-horizontal", forwards);
}

void generic_structured_remove_vertical (tree t, bool downwards) {
  outward (t, "structured-remove-vertical", downwards);
}

void generic_structured_insert_extremal (tree t, bool forwards) {
  call ("structured-extremal", object (t), object (forwards));
  call ("structured-insert-horizontal", object (t), object (forwards));
}

void generic_structured_insert_incremental (tree t, bool downwards) {
  call ("structured-incremental", object (t), object (downwards));
  call ("structured-insert-vertical", object (t), object (downwards));
}

void generic_structured_horizontal (tree t, bool forwards) {
  generic_horizontal_once (t, forwards);
}

void generic_structured_vertical (tree t, bool downwards) {
  generic_vertical_once (t, downwards);
}

void generic_structured_inner_extremal (tree t, bool forwards) {
  if (!scheme_predicate ("structured-horizontal?", t)) {
    outward (t, "structured-inner-extremal", forwards);
    return;
  }
  tree child;
  path child_path;
  if (!down_tree (t, child, child_path)) return;
  focus_pointer focus (t);
  editor ed= get_current_editor ();
  ed->go_to (forwards ? end (ed->the_root (), child_path)
                      : start (ed->the_root (), child_path));
  focus.restore ();
}

void generic_structured_extremal (tree t, bool forwards) {
  if (is_tree_branch (t)) { tree_extremal (t, forwards); return; }
  editor ed= get_current_editor ();
  while (true) {
    path before= copy (ed->the_path ());
    call ("structured-horizontal", object (t), object (forwards));
    if (ed->the_path () == before) break;
  }
  call ("structured-inner-extremal", object (t), object (forwards));
}

void generic_structured_incremental (tree t, bool downwards) {
  if (is_tree_branch (t)) {
    editor ed= get_current_editor ();
    while (true) {
      path before= copy (ed->the_path ());
      call (downwards ? "structured-down" : "structured-up");
      if (ed->the_path () == before) break;
    }
    return;
  }
  editor ed= get_current_editor ();
  while (true) {
    path before= copy (ed->the_path ());
    call ("structured-vertical", object (t), object (downwards));
    if (ed->the_path () == before) break;
  }
  call ("structured-inner-extremal", object (t), object (downwards));
}

void generic_structured_exit (tree t, bool forwards) {
  if (!as_bool (call ("complex-context?", object (t)))) return;
  path p;
  if (!attached_path (t, p)) return;
  editor ed= get_current_editor ();
  ed->go_to (forwards ? end (ed->the_root (), p) : start (ed->the_root (), p));
}

void generic_special_navigate (tree t, object direction) {
  outward (t, "special-navigate", direction);
}

void generic_special_horizontal (tree t, bool forwards) {
  outward (t, "special-horizontal", forwards);
}

void generic_special_vertical (tree t, bool downwards) {
  outward (t, "special-vertical", downwards);
}

void generic_special_extremal (tree t, bool forwards) {
  outward (t, "special-extremal", forwards);
}

void generic_special_incremental (tree t, bool downwards) {
  outward (t, "special-incremental", downwards);
}

/******************************************************************************
* MODULE     : generic_keyboard_commands.cpp
* DESCRIPTION: Actor-owned generic keyboard editing fallbacks
* COPYRIGHT  : (C) 2001 Joris van der Hoeven, 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
*******************************************************************************/

#include "generic_keyboard_commands.hpp"
#include "editor.hpp"
#include "new_view.hpp"
#include "scheme.hpp"

namespace {

bool parent_tree (tree t, tree& parent) {
  if (admits_edit_observer (t)) return false;
  path ip= obtain_ip (t);
  if (!ip_attached (ip) || is_nil (ip)) return false;
  tree root= get_current_editor ()->the_root ();
  parent= subtree (root, reverse (ip->next));
  return true;
}

void outward (tree t, const char* command, bool flag) {
  tree parent;
  if (parent_tree (t, parent)) call (command, object (parent), object (flag));
}

bool completion_context (tree t) {
  if (!(is_compound (t, "label") || is_compound (t, "reference") ||
        is_compound (t, "pageref") || is_compound (t, "eqref") ||
        is_compound (t, "smart-ref")))
    return false;
  return as_bool (call ("cursor-inside?", object (t)));
}

} // namespace

void generic_kbd_space_bar (tree t, bool shift) {
  (void) shift;
  if (!admits_edit_observer (t)) { outward (t, "kbd-space-bar", shift); return; }
  get_current_editor ()->insert_tree (tree (" "));
}

void generic_kbd_enter (tree t, bool shift) {
  (void) shift;
  if (!admits_edit_observer (t)) { outward (t, "kbd-enter", shift); return; }
  (void) get_current_editor ()->insert_return ();
}

void generic_kbd_control_enter (tree t, bool shift) {
  if (!admits_edit_observer (t)) outward (t, "kbd-control-enter", shift);
}

void generic_kbd_alternate_enter (tree t, bool shift) {
  if (!admits_edit_observer (t)) outward (t, "kbd-alternate-enter", shift);
}

void generic_kbd_remove (tree t, bool forwards) {
  if (!admits_edit_observer (t)) { outward (t, "kbd-remove", forwards); return; }
  editor ed= get_current_editor ();
  if (ed->selection_active_any ()) {
    ed->selection_cut ("nowhere");
    ed->selection_clear ("nowhere");
  }
  else ed->remove_text (forwards);
}

void generic_kbd_variant (tree t, bool forwards) {
  editor ed= get_current_editor ();
  if (completion_context (t)) {
    (void) ed->complete_try ();
    return;
  }
  if (!admits_edit_observer (t)) { outward (t, "kbd-variant", forwards); return; }
  if (!ed->complete_try () && forwards) {
    object binding= call ("kbd-find-inv-binding",
                          list_object (symbol_object ("kbd-alternate-tab")));
    string shortcut= as_string (call ("kbd-system-rewrite", binding));
    ed->set_message (
      tree (CONCAT, "Use ", shortcut, " in order to insert a tab"), "tab");
  }
}

void generic_kbd_alternate_variant (tree t, bool forwards) {
  (void) forwards;
  if (!admits_edit_observer (t)) {
    outward (t, "kbd-alternate-variant", forwards);
    return;
  }
  get_current_editor ()->make_htab ("5mm");
}

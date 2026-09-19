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

bool hybrid_command (path& hybrid_path, string& command, bool& atomic) {
  editor ed= get_current_editor ();
  hybrid_path= ed->search_upwards (HYBRID);
  if (is_nil (hybrid_path)) return false;
  tree root= ed->the_root ();
  tree hybrid= subtree (root, hybrid_path);
  if (N (hybrid) < 1) return false;
  atomic= is_atomic (hybrid[0]);
  command= atomic ? as_string (hybrid[0]) : string ("");
  return true;
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

void generic_hybrid_kbd_space () {
  editor ed= get_current_editor ();
  ed->activate_hybrid (false);
  ed->insert_tree (tree (" "));
}

void generic_hybrid_kbd_formula_open (string bracket) {
  path p;
  string command;
  bool atomic;
  if (!hybrid_command (p, command, atomic)) return;
  editor ed= get_current_editor ();
  if (atomic && command == "") {
    assign (p * 0, tree (bracket));
    ed->activate_hybrid (false);
  }
  else ed->insert_tree (tree (bracket));
}

void generic_hybrid_kbd_curly_left () {
  path p;
  string command;
  bool atomic;
  if (!hybrid_command (p, command, atomic)) return;
  editor ed= get_current_editor ();
  if (atomic && command == "") {
    assign (p * 0, tree ("eqnarray"));
    ed->activate_hybrid (false);
  }
  else if (!atomic || command == "begin") ed->insert_tree (tree ("{"));
  else if (command == "left\\" || command == "right\\") {
    ed->insert_tree (tree ("{"));
    ed->activate_hybrid (false);
  }
  else ed->activate_hybrid (false);
}

void generic_hybrid_kbd_curly_right () {
  path p;
  string command;
  bool atomic;
  if (!hybrid_command (p, command, atomic)) return;
  editor ed= get_current_editor ();
  if (!atomic) ed->activate_hybrid (false);
  else if (starts (command, "begin{")) {
    remove (p * 0 * 0, 6);
    ed->activate_hybrid (false);
  }
  else if (command == "left\\" || command == "right\\") {
    ed->insert_tree (tree ("}"));
    ed->activate_hybrid (false);
  }
  else ed->activate_hybrid (false);
}

void generic_hybrid_kbd_backslash () {
  path p;
  string command;
  bool atomic;
  if (!hybrid_command (p, command, atomic)) return;
  editor ed= get_current_editor ();
  if (atomic && (command == "left" || command == "right"))
    ed->insert_tree (tree ("\\"));
  else {
    ed->activate_hybrid (false);
    ed->make_hybrid ();
  }
}

void generic_hybrid_kbd_sub () {
  editor ed= get_current_editor ();
  ed->activate_hybrid (false);
  ed->make_script (false, true);
}

void generic_hybrid_kbd_sup () {
  editor ed= get_current_editor ();
  ed->activate_hybrid (false);
  ed->make_script (true, true);
}

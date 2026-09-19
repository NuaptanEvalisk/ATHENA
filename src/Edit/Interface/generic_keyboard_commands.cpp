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
#include "file.hpp"
#include "hashset.hpp"
#include "new_view.hpp"
#include "scheme.hpp"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

namespace {

QString keyboard_json_key (string value) {
  c_string bytes (value);
  return QString::fromUtf8 (bytes, N (value));
}

string keyboard_native_string (const QString& value) {
  QByteArray bytes= value.toUtf8 ();
  return string (bytes.constData (), bytes.size ());
}

struct generic_keyboard_action_data {
  QJsonObject escape_actions;
  QJsonArray escape_rules;
  QJsonObject handwriting;

  generic_keyboard_action_data () {
    string source;
    bool failed= load_string (
      url ("$ATHENA_PATH/misc/input/generic-keyboard-actions.json"), source, false);
    ASSERT (!failed, "cannot read generic-keyboard-actions.json");
    c_string bytes (source);
    QJsonParseError error;
    QJsonDocument document=
      QJsonDocument::fromJson (QByteArray (bytes, N (source)), &error);
    ASSERT (error.error == QJsonParseError::NoError && document.isObject (),
            "invalid generic-keyboard-actions.json");
    QJsonObject root= document.object ();
    ASSERT (root.value ("version").toInt () == 1,
            "unsupported generic keyboard action schema");
    ASSERT (root.value ("escape_actions").isObject () &&
            root.value ("escape_rules").isArray () &&
            root.value ("handwriting").isObject (),
            "missing generic keyboard action definitions");
    escape_actions= root.value ("escape_actions").toObject ();
    escape_rules= root.value ("escape_rules").toArray ();
    handwriting= root.value ("handwriting").toObject ();
    ASSERT (handwriting.value ("command_prefix").isString () &&
            handwriting.value ("description_suffix").isString () &&
            handwriting.value ("direct_unprefixed").isBool () &&
            handwriting.value ("direct_text").isObject (),
            "invalid handwriting keyboard action definitions");
  }
};

generic_keyboard_action_data& keyboard_action_data () {
  static generic_keyboard_action_data data;
  return data;
}

bool json_keyboard_string (QJsonValue value, string capture, string& result) {
  if (value.isString ()) {
    result= keyboard_native_string (value.toString ());
    return true;
  }
  if (!value.isObject ()) return false;
  QJsonObject object= value.toObject ();
  if (object.value ("capture").toString () != "suffix") return false;
  result= capture;
  return true;
}

bool json_keyboard_tree (QJsonValue value, string capture, tree& result) {
  string atomic;
  if (json_keyboard_string (value, capture, atomic)) {
    result= tree (atomic);
    return true;
  }
  if (!value.isArray ()) return false;
  QJsonArray items= value.toArray ();
  if (items.isEmpty () || !items[0].isString ()) return false;
  array<tree> children;
  for (int i= 1; i < items.size (); ++i) {
    tree child;
    if (!json_keyboard_tree (items[i], capture, child)) return false;
    children << child;
  }
  result= tree (as_tree_label (keyboard_native_string (items[0].toString ())),
                children);
  return true;
}

bool json_keyboard_argument (QJsonValue value, string capture, object& result) {
  string text;
  if (json_keyboard_string (value, capture, text)) {
    result= object (text);
    return true;
  }
  if (value.isBool ()) {
    result= object (value.toBool ());
    return true;
  }
  if (!value.isObject ()) return false;
  QJsonObject obj= value.toObject ();
  QJsonValue symbol= obj.value ("symbol");
  if (!symbol.isString ()) return false;
  result= symbol_object (keyboard_native_string (symbol.toString ()));
  return true;
}

bool execute_json_keyboard_action (QJsonObject descriptor, string capture= "") {
  string kind= keyboard_native_string (descriptor.value ("kind").toString ());
  if (kind == "tree") {
    tree inserted;
    ASSERT (json_keyboard_tree (descriptor.value ("value"), capture, inserted),
            "invalid tree keyboard action descriptor");
    get_current_editor ()->insert_tree (inserted);
    return true;
  }
  if (kind == "text") {
    QJsonValue value= descriptor.value ("value");
    string text;
    ASSERT (json_keyboard_string (value, capture, text),
            "invalid text keyboard action descriptor");
    get_current_editor ()->insert_tree (tree (text));
    return true;
  }
  return false;
}

object json_keyboard_dispatch_result (QJsonObject descriptor,
                                      string capture= "") {
  if (execute_json_keyboard_action (descriptor, capture)) return object (true);
  string kind= keyboard_native_string (descriptor.value ("kind").toString ());
  if (kind != "scheme-call") return object (false);

  QJsonValue function_value= descriptor.value ("function");
  QJsonValue args_value= descriptor.value ("args");
  ASSERT (function_value.isString () && args_value.isArray (),
          "invalid Scheme keyboard action descriptor");
  array<object> command;
  command << symbol_object (keyboard_native_string (function_value.toString ()));
  for (QJsonValue value: args_value.toArray ()) {
    object arg;
    ASSERT (json_keyboard_argument (value, capture, arg),
            "invalid keyboard action call argument");
    command << arg;
  }
  return as_list_object (command);
}

tree focus_tree () {
  editor ed= get_current_editor ();
  path p= ed->focus_get ();
  return ed->test_subtree (p) ? ed->the_subtree (p) : tree ();
}

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

void dispatch_focus (const char* command, bool flag) {
  call (command, object (focus_tree ()), object (flag));
}

bool generic_context_at_cursor () {
  editor ed= get_current_editor ();
  path p= ed->the_path ();
  if (is_nil (p)) return false;
  p= path_up (p);
  if (!ed->test_subtree (p)) return false;
  return as_bool (call ("generic-context?", object (ed->the_subtree (p))));
}

enum generic_move_kind {
  MOVE_HORIZONTAL,
  MOVE_VERTICAL,
  MOVE_EXTREMAL,
  MOVE_INCREMENTAL
};

void move_once (generic_move_kind kind, bool forwards) {
  editor ed= get_current_editor ();
  switch (kind) {
  case MOVE_HORIZONTAL:
    if (forwards) ed->go_right ();
    else ed->go_left ();
    break;
  case MOVE_VERTICAL:
    if (forwards) ed->go_down ();
    else ed->go_up ();
    break;
  case MOVE_EXTREMAL:
    if (forwards) ed->go_end_line ();
    else ed->go_start_line ();
    break;
  case MOVE_INCREMENTAL:
    if (forwards) ed->go_page_down ();
    else ed->go_page_up ();
    break;
  }
}

void generic_move_until_context (generic_move_kind kind, bool forwards) {
  editor ed= get_current_editor ();
  path original= copy (ed->the_path ());
  hashset<path> visited;
  while (true) {
    path before= copy (ed->the_path ());
    move_once (kind, forwards);
    path after= copy (ed->the_path ());
    if (after == before || visited->contains (after) || generic_context_at_cursor ())
      break;
    visited->insert (after);
  }
  if (!generic_context_at_cursor ()) ed->go_to (original);
}

} // namespace

void generic_kbd_horizontal (tree t, bool forwards) {
  if (!admits_edit_observer (t)) {
    outward (t, "kbd-horizontal", forwards);
    return;
  }
  generic_move_until_context (MOVE_HORIZONTAL, forwards);
}

void generic_kbd_vertical (tree t, bool downwards) {
  if (!admits_edit_observer (t)) {
    outward (t, "kbd-vertical", downwards);
    return;
  }
  generic_move_until_context (MOVE_VERTICAL, downwards);
}

void generic_kbd_extremal (tree t, bool forwards) {
  if (!admits_edit_observer (t)) {
    outward (t, "kbd-extremal", forwards);
    return;
  }
  generic_move_until_context (MOVE_EXTREMAL, forwards);
}

void generic_kbd_incremental (tree t, bool downwards) {
  if (!admits_edit_observer (t)) {
    outward (t, "kbd-incremental", downwards);
    return;
  }
  generic_move_until_context (MOVE_INCREMENTAL, downwards);
}

void generic_kbd_left_raw () { dispatch_focus ("kbd-horizontal", false); }
void generic_kbd_right_raw () { dispatch_focus ("kbd-horizontal", true); }
void generic_kbd_up_raw () { dispatch_focus ("kbd-vertical", false); }
void generic_kbd_down_raw () { dispatch_focus ("kbd-vertical", true); }
void generic_kbd_start_line_raw () { dispatch_focus ("kbd-extremal", false); }
void generic_kbd_end_line_raw () { dispatch_focus ("kbd-extremal", true); }
void generic_kbd_page_up_raw () { dispatch_focus ("kbd-incremental", false); }
void generic_kbd_page_down_raw () { dispatch_focus ("kbd-incremental", true); }

void generic_kbd_plain_move (object move) {
  get_current_editor ()->select_from_keyboard (false);
  (void) call (move);
}

void generic_kbd_left () {
  get_current_editor ()->select_from_keyboard (false);
  generic_kbd_left_raw ();
}

void generic_kbd_right () {
  get_current_editor ()->select_from_keyboard (false);
  generic_kbd_right_raw ();
}

void generic_kbd_up () {
  get_current_editor ()->select_from_keyboard (false);
  generic_kbd_up_raw ();
}

void generic_kbd_down () {
  get_current_editor ()->select_from_keyboard (false);
  generic_kbd_down_raw ();
}

void generic_kbd_start_line () {
  get_current_editor ()->select_from_keyboard (false);
  generic_kbd_start_line_raw ();
}

void generic_kbd_end_line () {
  get_current_editor ()->select_from_keyboard (false);
  generic_kbd_end_line_raw ();
}

void generic_kbd_page_up () {
  get_current_editor ()->select_from_keyboard (false);
  generic_kbd_page_up_raw ();
}

void generic_kbd_page_down () {
  get_current_editor ()->select_from_keyboard (false);
  generic_kbd_page_down_raw ();
}

void generic_kbd_select (object move) {
  editor ed= get_current_editor ();
  ed->select_from_shift_keyboard ();
  (void) call (move);
  ed->select_from_cursor ();
}

void generic_kbd_select_if_active (object move) {
  editor ed= get_current_editor ();
  (void) call (move);
  ed->select_from_cursor_if_active ();
}

void generic_insert_return () { (void) get_current_editor ()->insert_return (); }
void generic_kbd_space () { dispatch_focus ("kbd-space-bar", false); }
void generic_kbd_shift_space () { dispatch_focus ("kbd-space-bar", true); }
void generic_kbd_return () { dispatch_focus ("kbd-enter", false); }
void generic_kbd_shift_return () { dispatch_focus ("kbd-enter", true); }
void generic_kbd_control_return () { dispatch_focus ("kbd-control-enter", false); }
void generic_kbd_shift_control_return () { dispatch_focus ("kbd-control-enter", true); }
void generic_kbd_alternate_return () { dispatch_focus ("kbd-alternate-enter", false); }
void generic_kbd_shift_alternate_return () { dispatch_focus ("kbd-alternate-enter", true); }
void generic_kbd_backspace () { dispatch_focus ("kbd-remove", false); }
void generic_kbd_delete () { dispatch_focus ("kbd-remove", true); }
void generic_kbd_tab () { dispatch_focus ("kbd-variant", true); }
void generic_kbd_shift_tab () { dispatch_focus ("kbd-variant", false); }
void generic_kbd_alternate_tab () { dispatch_focus ("kbd-alternate-variant", true); }
void generic_kbd_shift_alternate_tab () { dispatch_focus ("kbd-alternate-variant", false); }
void generic_kbd_copy () { get_current_editor ()->selection_copy ("primary"); }
void generic_kbd_cut () { get_current_editor ()->selection_cut ("primary"); }
void generic_kbd_paste () { get_current_editor ()->selection_paste ("primary"); }
void generic_kbd_cancel () { get_current_editor ()->selection_clear ("primary"); }

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

object
generic_escape_symbol_dispatch (string action) {
  generic_keyboard_action_data& data= keyboard_action_data ();
  QJsonValue descriptor= data.escape_actions.value (keyboard_json_key (action));
  if (descriptor.isObject ())
    return json_keyboard_dispatch_result (descriptor.toObject ());

  for (QJsonValue value: data.escape_rules) {
    if (!value.isObject ()) continue;
    QJsonObject rule= value.toObject ();
    QJsonValue prefix_value= rule.value ("prefix");
    QJsonValue action_value= rule.value ("action");
    if (!prefix_value.isString () || !action_value.isObject ()) continue;
    string prefix= keyboard_native_string (prefix_value.toString ());
    if (!starts (action, prefix)) continue;
    string capture= action (N (prefix), N (action));
    return json_keyboard_dispatch_result (action_value.toObject (), capture);
  }
  return object (false);
}

object
generic_key_press_command (string key) {
  object binding= call ("kbd-find-key-binding", object (key));
  if (is_bool (binding) && !as_bool (binding)) return object (false);
  return call ("car", binding);
}

namespace {

string handwriting_symbol_command_key (string command) {
  QJsonObject config= keyboard_action_data ().handwriting;
  string prefix= keyboard_native_string (config.value ("command_prefix").toString ());
  if (prefix != "" && starts (command, prefix))
    return command (N (prefix), N (command));
  return command;
}

bool handwriting_symbol_direct_text (string command, string& direct) {
  QJsonObject config= keyboard_action_data ().handwriting;
  QJsonObject direct_text= config.value ("direct_text").toObject ();
  QJsonValue mapped= direct_text.value (keyboard_json_key (command));
  if (mapped.isString ()) {
    direct= keyboard_native_string (mapped.toString ());
    return true;
  }
  string prefix= keyboard_native_string (config.value ("command_prefix").toString ());
  if (config.value ("direct_unprefixed").toBool () &&
      (prefix == "" || !starts (command, prefix))) {
    direct= command;
    return true;
  }
  return false;
}

bool handwriting_symbol_command (string command, object& procedure) {
  object entry= call ("kbd-get-command", object (handwriting_symbol_command_key (command)));
  if (!as_bool (call ("pair?", entry))) return false;
  procedure= call ("cdr", entry);
  return as_bool (call ("procedure?", procedure));
}

} // namespace

string
generic_handwriting_symbol_input_description (string command) {
  string direct;
  if (handwriting_symbol_direct_text (command, direct)) return direct;
  object procedure;
  if (!handwriting_symbol_command (command, procedure)) return "";
  QJsonObject config= keyboard_action_data ().handwriting;
  return keyboard_native_string (config.value ("command_prefix").toString ()) *
         handwriting_symbol_command_key (command) *
         keyboard_native_string (config.value ("description_suffix").toString ());
}

void
generic_handwriting_symbol_insert (string command) {
  string direct;
  if (handwriting_symbol_direct_text (command, direct)) {
    get_current_editor ()->insert_tree (tree (direct));
    return;
  }

  object procedure;
  if (handwriting_symbol_command (command, procedure)) {
    (void) call (procedure);
    return;
  }
  (void) call ("set-message",
               object ("Unsupported symbol command: " * command),
               object ("Handwritten Symbol"));
}

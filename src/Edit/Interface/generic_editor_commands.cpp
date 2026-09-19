/******************************************************************************
* MODULE     : generic_editor_commands.cpp
* DESCRIPTION: Actor-owned generic editor commands
* COPYRIGHT  : (C) 2001 Joris van der Hoeven, 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
*******************************************************************************/

#include "generic_editor_commands.hpp"
#include "analyze.hpp"
#include "editor.hpp"
#include "new_buffer.hpp"
#include "new_view.hpp"

namespace {

bool innermost_document (tree root, path cursor, path& document_path) {
  if (!is_nil (cursor)) cursor= path_up (cursor);
  while (!is_nil (cursor)) {
    if (has_subtree (root, cursor) && is_func (subtree (root, cursor), DOCUMENT)) {
      document_path= cursor;
      return true;
    }
    cursor= path_up (cursor);
  }
  if (is_func (root, DOCUMENT)) {
    document_path= path ();
    return true;
  }
  return false;
}

void add_history (object optional_from) {
  if (is_null (optional_from)) return;
  array<object> args= as_array_object (optional_from);
  if (N (args) > 0) call ("cursor-history-add", args[0]);
}

bool first_relative_index (path prefix, path cursor, int& index) {
  while (!is_nil (prefix)) {
    if (is_nil (cursor) || prefix->item != cursor->item) return false;
    prefix= prefix->next;
    cursor= cursor->next;
  }
  if (is_nil (cursor)) return false;
  index= cursor->item;
  return true;
}

bool parent_tree (tree t, tree& parent) {
  if (admits_edit_observer (t)) return false;
  path ip= obtain_ip (t);
  if (!ip_attached (ip) || is_nil (ip)) return false;
  tree root= get_current_editor ()->the_root ();
  parent= subtree (root, reverse (ip->next));
  return true;
}

string delta_unix (url target) {
  url base= get_master_buffer (get_current_buffer_safe ());
  if (is_rooted (target) && !is_none (base)) target= delta (base, target);
  return as_unix_string (target);
}

bool note_ref (tree t) {
  return is_compound (t, "note-ref") || is_compound (t, "note-ref*");
}

bool note_text (tree t) {
  return is_compound (t, "note-inline") || is_compound (t, "note-inline*") ||
    is_compound (t, "note-wide") || is_compound (t, "note-wide*") ||
    is_compound (t, "note-footnote") || is_compound (t, "note-footnote*");
}

void collect_note_ids (tree t, array<tree>& refs, array<tree>& texts) {
  if (note_ref (t) && N (t) >= 1) refs << copy (t[0]);
  if (note_text (t) && N (t) >= 2) texts << copy (t[1]);
  if (is_compound (t))
    for (int i= 0; i < N (t); ++i) collect_note_ids (t[i], refs, texts);
}

bool contains_tree (const array<tree>& values, tree value) {
  for (int i= 0; i < N (values); ++i)
    if (values[i] == value) return true;
  return false;
}

tree propose_note_id (bool for_reference) {
  array<tree> refs, texts;
  collect_note_ids (get_current_editor ()->the_buffer (), refs, texts);
  const array<tree>& candidates= for_reference ? texts : refs;
  const array<tree>& used= for_reference ? refs : texts;
  bool found= false;
  tree result;
  for (int i= 0; i < N (candidates); ++i)
    if (!contains_tree (used, candidates[i])) {
      result= copy (candidates[i]);
      found= true;
    }
  if (found) return result;
  return tree (as_string (call ("create-unique-id")));
}

bool innermost_named (string name, path& p, tree& t) {
  editor ed= get_current_editor ();
  p= ed->search_upwards (name);
  if (is_nil (p)) return false;
  tree root= ed->the_root ();
  t= subtree (root, p);
  return true;
}

bool innermost_float (path& p, tree& t) {
  editor ed= get_current_editor ();
  array<string> names;
  names << string ("float") << string ("wide-float") << string ("phantom-float");
  path best;
  for (int i= 0; i < N (names); ++i) {
    path q= ed->search_upwards (names[i]);
    if (!is_nil (q) && (is_nil (best) || N (q) > N (best))) best= q;
  }
  if (is_nil (best)) return false;
  p= best;
  tree root= ed->the_root ();
  t= subtree (root, p);
  return true;
}

tree current_focus_tree () {
  editor ed= get_current_editor ();
  path p= ed->focus_get ();
  return ed->test_subtree (p) ? ed->the_subtree (p) : tree ();
}

bool image_payload (object values, url& target,
                    string& w, string& h, string& x, string& y) {
  if (!is_list (values)) return false;
  array<object> items= as_array_object (values);
  if (N (items) != 5 || !is_url (items[0])) return false;
  for (int i= 1; i < 5; ++i)
    if (!is_string (items[i])) return false;
  target= as_url (items[0]);
  w= as_string (items[1]);
  h= as_string (items[2]);
  x= as_string (items[3]);
  y= as_string (items[4]);
  return true;
}

object focus_search_label_impl (tree t);

object focus_list_search_label_impl (object children) {
  if (!is_list (children)) return object (false);
  array<object> items= as_array_object (children);
  for (int i= 0; i < N (items); ++i) {
    if (!is_tree (items[i])) continue;
    object found= focus_search_label_impl (as_tree (items[i]));
    if (is_tree (found)) return found;
  }
  return object (false);
}

object focus_search_label_impl (tree t) {
  if (is_compound (t, "label") && N (t) == 1) return object (t);
  if (!is_compound (t)) return object (false);
  string label= as_string (L (t));
  if (label == "document" || label == "concat" || label == "table" ||
      label == "row" || label == "cell") {
    array<object> children;
    for (int i= 0; i < N (t); ++i) children << object (t[i]);
    return focus_list_search_label_impl (as_list_object (children));
  }
  if (label == "tformat" || label == "with" || label == "surround") {
    if (N (t) == 0) return object (false);
    return focus_search_label_impl (t[N (t) - 1]);
  }
  return object (false);
}

} // namespace

void
generic_go_to_line (int line, object optional_from) {
  add_history (optional_from);
  editor ed= get_current_editor ();
  tree root= ed->the_root ();
  path document_path;
  if (!innermost_document (root, ed->the_path (), document_path)) return;
  tree document= subtree (root, document_path);
  if (line < 0 || line >= N (document)) return;
  ed->go_to (document_path * line * 0);
}

void
generic_go_to_column (int column, object optional_from) {
  add_history (optional_from);
  editor ed= get_current_editor ();
  tree root= ed->the_root ();
  path document_path;
  if (!innermost_document (root, ed->the_path (), document_path)) return;
  int line;
  if (!first_relative_index (document_path, ed->the_path (), line)) return;
  tree document= subtree (root, document_path);
  if (line < 0 || line >= N (document)) return;
  if (column < 0) column= 0;
  ed->go_to (document_path * line * column);
}

object
generic_select_word (string word, tree t, int column) {
  if (!is_atomic (t)) return object (false);
  string text= as_string (t);
  int pos= max (0, column - N (word));
  int begin= search_forwards (word, pos, text);
  if (begin < 0) return object (false);

  path ip= obtain_ip (t);
  if (!ip_attached (ip)) return object (false);
  path p= reverse (ip);
  editor ed= get_current_editor ();
  ed->go_to (p * begin);
  ed->selection_set_start ();
  ed->go_to (p * (begin + N (word)));
  ed->selection_set_end ();
  return object (begin);
}

object
generic_search_parameters (object label) {
  string name;
  if (is_string (label)) name= as_string (label);
  else if (is_symbol (label)) name= as_symbol (label);
  else return object (false);

  if (name == "reference" || name == "pageref" || name == "eqref" ||
      name == "smart-ref" || name == "hlink")
    return call ("standard-parameters", object ("locus"));
  return object (false);
}

void
generic_label_insert (tree t) {
  if (admits_edit_observer (t)) {
    call ("make", symbol_object ("label"));
    return;
  }
  tree parent;
  if (parent_tree (t, parent)) call ("label-insert", object (parent));
}

void
generic_recenter_window () {
  editor ed= get_current_editor ();
  ed->scroll_to (ed->get_cursor_x (), ed->get_cursor_y ());
  ed->invalidate_all ();
}

void
generic_make_label () {
  call ("label-insert", object (current_focus_tree ()));
}

void
generic_make_inline_image (object values) {
  url target;
  string w, h, x, y;
  if (!image_payload (values, target, w, h, x, y)) return;
  get_current_editor ()->make_image (delta_unix (target), false, w, h, x, y);
}

void
generic_make_link_image (object values) {
  url target;
  string w, h, x, y;
  if (!image_payload (values, target, w, h, x, y)) return;
  get_current_editor ()->make_image (delta_unix (target), true, w, h, x, y);
}

object
generic_focus_label (tree) {
  return object (false);
}

object
generic_focus_get_label (tree t) {
  object label= call ("focus-label", object (t));
  if (!is_tree (label)) return object (false);
  tree l= as_tree (label);
  if (N (l) != 1 || !is_atomic (l[0])) return object (false);
  return object (as_string (l[0]));
}

object
generic_focus_set_label (tree t, string value) {
  object label= call ("focus-label", object (t));
  if (!is_tree (label)) return object (false);
  return call ("tree-set", label, object (0), object (value));
}

object
generic_focus_list_search_label (object children) {
  return focus_list_search_label_impl (children);
}

object
generic_focus_search_label (tree t) {
  return focus_search_label_impl (t);
}

void
generic_make_specific (string format) {
  editor ed= get_current_editor ();
  tree specific= compound ("specific", format, "");
  if (format == "texmacs" || ed->in_source ())
    ed->var_insert_tree (specific, path (1, 0));
  else
    ed->var_insert_tree (compound ("inactive", specific), path (0, 1, 0));
}

void
generic_make_include (url target) {
  get_current_editor ()->insert_tree (
    compound ("include", delta_unix (target)));
}

void
generic_make_experimental_build_warning () {
  get_current_editor ()->insert_tree (compound ("experimental-build-warning"));
}

void
generic_make_note_ref () {
  get_current_editor ()->insert_tree (
    compound ("note-ref", propose_note_id (true)));
}

void
generic_make_note_inline () {
  get_current_editor ()->var_insert_tree (
    compound ("note-inline", "", propose_note_id (false)), path (0, 0));
}

void
generic_make_note_wide () {
  get_current_editor ()->var_insert_tree (
    compound ("note-wide", tree (DOCUMENT, ""), propose_note_id (false)),
    path (0, 0, 0));
}

void
generic_make_note_footnote () {
  get_current_editor ()->var_insert_tree (
    compound ("note-footnote", tree (DOCUMENT, ""), propose_note_id (false)),
    path (0, 0, 0));
}

void
generic_make_marginal_note () {
  editor ed= get_current_editor ();
  bool wrap= ed->selection_active_small ();
  if (wrap) ed->selection_cut ("wrapbuf");
  else ed->selection_cancel ();
  ed->var_insert_tree (
    compound ("inactive", compound ("marginal-note", "normal", "c", "")),
    path (0, 2, 0));
  if (wrap) ed->selection_paste ("wrapbuf");
}

bool
generic_test_marginal_note_hpos (string position) {
  path p;
  tree t;
  return innermost_named ("marginal-note", p, t) && N (t) >= 1 &&
         t[0] == tree (position);
}

void
generic_set_marginal_note_hpos (string position) {
  path p;
  tree t;
  if (innermost_named ("marginal-note", p, t) && N (t) >= 1)
    assign (p * 0, tree (position));
}

bool
generic_test_marginal_note_valign (string alignment) {
  path p;
  tree t;
  return innermost_named ("marginal-note", p, t) && N (t) >= 2 &&
         t[1] == tree (alignment);
}

void
generic_set_marginal_note_valign (string alignment) {
  path p;
  tree t;
  if (innermost_named ("marginal-note", p, t) && N (t) >= 2)
    assign (p * 1, tree (alignment));
}

void
generic_make_insertion (string type) {
  string position= type == "float" ? "tbh" : "";
  get_current_editor ()->var_insert_tree (
    compound ("float", type, position, tree (DOCUMENT, "")), path (2, 0, 0));
}

void
generic_insertion_positioning (string position, bool allowed) {
  path p;
  tree t;
  if (!innermost_float (p, t) || N (t) < 2 || !is_atomic (t[1])) return;
  string current= as_string (t[1]);
  string next= allowed ? string_union (current, position)
                       : string_minus (current, position);
  assign (p * 1, tree (next));
}

bool
generic_test_insertion_positioning (string position) {
  if (N (position) == 0) return false;
  path p;
  tree t;
  if (!innermost_float (p, t) || N (t) < 2 || !is_atomic (t[1])) return false;
  string current= as_string (t[1]);
  return search_forwards (position (0, 1), 0, current) >= 0;
}

bool
generic_not_test_insertion_positioning (string position) {
  return !generic_test_insertion_positioning (position);
}

void
generic_toggle_insertion_positioning (string position) {
  generic_insertion_positioning (
    position, !generic_test_insertion_positioning (position));
}

void
generic_toggle_insertion_positioning_not (string position) {
  generic_toggle_insertion_positioning (position);
}

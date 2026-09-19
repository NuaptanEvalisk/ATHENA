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

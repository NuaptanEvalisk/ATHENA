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
#include "new_document.hpp"
#include "editor.hpp"
#include "file.hpp"
#include "language.hpp"
#include "new_buffer.hpp"
#include "new_view.hpp"
#include "tree_select.hpp"

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

bool innermost_balloon (tree& result) {
  editor ed= get_current_editor ();
  path p= path_up (ed->the_path ());
  while (!is_nil (p)) {
    if (ed->test_subtree (p)) {
      tree t= ed->the_subtree (p);
      if (as_bool (call ("balloon-context?", object (t)))) {
        result= t;
        return true;
      }
    }
    p= path_up (p);
  }
  return false;
}

bool tree_label_in_scheme_list (tree t, object labels) {
  if (!is_list (labels)) return false;
  string label= as_string (L (t));
  array<object> items= as_array_object (labels);
  for (int i= 0; i < N (items); ++i) {
    if (is_symbol (items[i]) && as_symbol (items[i]) == label) return true;
    if (is_string (items[i]) && as_string (items[i]) == label) return true;
  }
  return false;
}

bool embedded_image_context_impl (tree t) {
  return is_compound (t, "image", 5) &&
         is_compound (t[0], "tuple") && N (t[0]) > 0 &&
         is_compound (t[0][0], "raw-data");
}

bool linked_image_context_impl (tree t) {
  return is_compound (t, "image", 5) && !embedded_image_context_impl (t);
}

bool embedded_image_name (tree t, string& name) {
  if (!embedded_image_context_impl (t) || N (t[0]) < 2 || !is_atomic (t[0][1]))
    return false;
  name= cork_to_utf8 (as_string (t[0][1]));
  return true;
}

bool embedded_image_data (tree t, string& data) {
  if (!embedded_image_context_impl (t) || N (t[0][0]) < 1 || !is_atomic (t[0][0][0]))
    return false;
  data= as_string (t[0][0][0]);
  return true;
}

bool embedded_proposal_string (tree t, int number, string& proposal) {
  string file;
  if (!embedded_image_name (t, file)) return false;
  string ext= suffix (url (file));
  url current= get_current_buffer_safe ();
  string root= basename (tail (current));
  string fallback= root * "-image-" * as_string (number) * "." * file;
  string name= ext == "" ? fallback : file;
  proposal= as_standard_string (relative (current, url (name)));
  return true;
}

url numbered_embedded_url (url u, int number) {
  string ext= suffix (u);
  string num= "-" * as_string (number);
  if (ext == "") return glue (u, num);
  return glue (unglue (u, N (ext) + 1), num * "." * ext);
}

url free_embedded_url (url u, int number) {
  if (!exists (u)) return u;
  url numbered= numbered_embedded_url (u, number);
  if (!exists (numbered)) return numbered;
  return free_embedded_url (u, number + 1);
}

void collect_embedded_images (tree t, array<tree>& images) {
  if (embedded_image_context_impl (t)) images << t;
  if (is_atomic (t)) return;
  for (int i= 0; i < N (t); ++i) collect_embedded_images (t[i], images);
}

bool innermost_linked_image (tree& result) {
  editor ed= get_current_editor ();
  path p= path_up (ed->the_path ());
  while (!is_nil (p)) {
    if (ed->test_subtree (p)) {
      tree t= ed->the_subtree (p);
      if (linked_image_context_impl (t)) {
        result= t;
        return true;
      }
    }
    p= path_up (p);
  }
  return false;
}

bool innermost_embedded_image (tree& result) {
  editor ed= get_current_editor ();
  path p= path_up (ed->the_path ());
  while (!is_nil (p)) {
    if (ed->test_subtree (p)) {
      tree t= ed->the_subtree (p);
      if (embedded_image_context_impl (t)) {
        result= t;
        return true;
      }
    }
    p= path_up (p);
  }
  return false;
}

bool spell_live_current_range (path& start_path, path& end_path) {
  editor ed= get_current_editor ();
  range_set sels= ed->get_alt_selection ("spell-live");
  path cursor= ed->the_path ();
  for (int i= 0; i + 1 < N (sels); i += 2)
    if (path_less_eq (sels[i], cursor) && path_less (cursor, sels[i + 1])) {
      start_path= sels[i];
      end_path= sels[i + 1];
      return true;
    }
  return false;
}

bool spell_live_current_word_impl (path start_path, path end_path, string& word) {
  tree selected= selection_compute (get_current_editor ()->the_root (),
                                    start_path, end_path);
  if (!is_atomic (selected) || N (selected->label) == 0) return false;
  word= selected->label;
  return true;
}

string spell_live_language_at (path start_path) {
  editor ed= get_current_editor ();
  tree language= ed->get_env_value ("language", start_path);
  return is_atomic (language) ? as_string (language): ed->get_init_string ("language");
}

void replace_matching_embedded_source (tree t, tree source, string replacement) {
  if (t == source) {
    (void) call ("tree-set-diff", object (t), object (tree (replacement)));
    return;
  }
  if (is_atomic (t)) return;
  for (int i= 0; i < N (t); ++i)
    replace_matching_embedded_source (t[i], source, replacement);
}

string cardlink_destination_string (tree destination) {
  if (is_atomic (destination)) return as_string (destination);
  object converted= call ("tree->string", object (destination));
  return is_string (converted) ? as_string (converted) : string ("");
}

bool cardlink_extension_in (string value, const char* const* extensions, int count) {
  string ext= locase_all (suffix (url (value)));
  for (int i= 0; i < count; ++i)
    if (ext == extensions[i]) return true;
  return false;
}

string cardlink_type (tree destination) {
  string value= locase_all (cardlink_destination_string (destination));
  if (starts (value, "http://") || starts (value, "https://")) return "Web";
  if (starts (value, "tmfs://")) return "TMFS";
  if (ends (value, "/")) return "Folder";
  if (ends (value, ".pdf")) return "PDF";

  static const char* const image_exts[]= {"png", "jpg", "jpeg", "gif", "svg", "webp"};
  static const char* const audio_exts[]= {"mp3", "ogg", "wav", "flac", "m4a"};
  static const char* const video_exts[]= {"mp4", "mkv", "mov", "webm", "avi"};
  static const char* const archive_exts[]= {"zip", "tar", "gz", "bz2", "xz", "7z"};
  static const char* const text_exts[]= {"txt", "md", "tm", "ath", "tex", "html", "htm"};
  static const char* const office_exts[]= {
    "doc", "docx", "odt", "rtf", "ppt", "pptx", "odp", "xls", "xlsx", "ods"
  };
  if (cardlink_extension_in (value, image_exts, 6)) return "Image";
  if (cardlink_extension_in (value, audio_exts, 5)) return "Audio";
  if (cardlink_extension_in (value, video_exts, 5)) return "Video";
  if (cardlink_extension_in (value, archive_exts, 6)) return "Archive";
  if (cardlink_extension_in (value, text_exts, 7)) return "Text";
  if (cardlink_extension_in (value, office_exts, 10)) return "Office";
  return "File";
}

string cardlink_type_display_name (string type) {
  return type == "TMFS" ? string ("TMFS Link") : type * " Document";
}

string cardlink_type_default_link_name (string type) {
  return type == "TMFS" ? string ("TMFS link") : type * " document";
}

tree cardlink_icon_from_string (string icon, string type) {
  if (icon != "") return compound ("image", icon, "1.35em", "", "", "");

  array<tree> args;
  args << tree ("font-family") << tree ("ss")
       << tree ("font-series") << tree ("bold")
       << tree ("color") << tree ("#404040")
       << tree ("[" * upcase_all (type) * "]");
  return compound ("with", args);
}

bool cardlink_empty_body (tree body) {
  if (as_bool (call ("tm-equal?", object (body), object ("")))) return true;
  return is_atomic (body) && as_string (body) == "";
}

tree cardlink_replace_whole (tree target, tree replacement) {
  path ip= obtain_ip (target);
  bool active= is_nil (ip) || last_item (ip) != DETACHED;
  object result= active
    ? call ("tree-set-diff", object (target), object (replacement))
    : call ("tree-assign", object (target), object (replacement));
  return is_tree (result) ? as_tree (result) : replacement;
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

void
generic_make_thumbnails_sub (object files, int columns) {
  if (!is_list (files) || columns <= 0) return;
  array<object> items= as_array_object (files);
  double ratio= (1.0 / ((double) columns)) - 0.02;
  object width_obj= call ("number->string", object (ratio));
  if (!is_string (width_obj)) return;
  string width= as_string (width_obj) * "par";

  array<tree> rows;
  int row_count= (N (items) + columns - 1) / columns;
  if (row_count == 0) row_count= 1;
  for (int r= 0; r < row_count; ++r) {
    array<tree> cells;
    for (int c= 0; c < columns; ++c) {
      int i= r * columns + c;
      tree content= tree ("");
      if (i < N (items) && is_url (items[i])) {
        string file= delta_unix (as_url (items[i]));
        content= compound ("image", file, width, "", "", "");
      }
      cells << compound ("cell", content);
    }
    rows << compound ("row", cells);
  }

  tree table= compound ("table", rows);
  array<tree> format;
  format << compound ("twith", "table-width", "1par")
         << compound ("twith", "table-hyphen", "yes")
         << table;
  tree tabular= compound ("tabular*", compound ("tformat", format));
  get_current_editor ()->insert_tree (tabular);
}

void
generic_notify_activated (tree) {
}

void
generic_notify_disactivated (tree) {
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
generic_search_next () {
  (void) get_current_editor ()->search_keypress ("next");
}

void
generic_search_previous () {
  (void) get_current_editor ()->search_keypress ("previous");
}

void
generic_focus_open_search_tool (tree) {
}

bool
generic_mini_flow_context (tree t) {
  return tree_label_in_scheme_list (t, call ("mini-flow-tag-list"));
}

bool
generic_in_main_flow () {
  editor ed= get_current_editor ();
  path p= path_up (ed->the_path (), 2);
  while (!is_nil (p)) {
    if (ed->test_subtree (p) && generic_mini_flow_context (ed->the_subtree (p)))
      return false;
    p= path_up (p);
  }
  return true;
}

bool
generic_balloon_context (tree t) {
  return tree_label_in_scheme_list (t, call ("balloon-tag-list"));
}

bool
generic_image_context (tree t) {
  return is_compound (t, "image", 5);
}

bool
generic_embedded_image_context (tree t) {
  return embedded_image_context_impl (t);
}

bool
generic_linked_image_context (tree t) {
  return linked_image_context_impl (t);
}

object
generic_embedded_suffix (tree t) {
  string file;
  if (!embedded_image_name (t, file)) return object (false);
  string ext= suffix (url (file));
  return object (ext == "" ? file : ext);
}

object
generic_embedded_propose (tree t, int number) {
  string proposal;
  if (!embedded_proposal_string (t, number, proposal)) return object (false);
  return object (proposal);
}

void
generic_save_embedded_image (tree t, url name) {
  string data;
  if (!embedded_image_data (t, data)) return;
  (void) save_string (name, data, false);
}

void
generic_link_embedded_image (tree t, url name) {
  if (!embedded_image_context_impl (t)) return;
  generic_save_embedded_image (t, name);
  string rel= as_standard_string (delta (get_current_buffer_safe (), name));
  (void) call ("tree-set", object (t), object (0), object (rel));
}

void
generic_link_embedded_image_copies (tree t, url name) {
  if (!embedded_image_context_impl (t)) return;
  generic_save_embedded_image (t, name);
  string rel= as_standard_string (delta (get_current_buffer_safe (), name));
  tree source= copy (t[0]);
  replace_matching_embedded_source (current_document_tree (), source, rel);
}

void
generic_embedded_saver (url name) {
  tree image;
  if (innermost_embedded_image (image)) generic_save_embedded_image (image, name);
}

void
generic_embedded_linker (url name) {
  tree image;
  if (innermost_embedded_image (image)) generic_link_embedded_image (image, name);
}

void
generic_embedded_linker_copies (url name) {
  tree image;
  if (innermost_embedded_image (image)) generic_link_embedded_image_copies (image, name);
}

void
generic_save_all_embedded_images () {
  array<tree> images;
  collect_embedded_images (current_document_tree (), images);
  array<string> proposals;
  for (int i= 0; i < N (images); ++i) {
    string proposal;
    if (embedded_proposal_string (images[i], i + 1, proposal)) proposals << proposal;
    else proposals << string ("");
  }
  for (int i= 0; i < N (images); ++i)
    if (proposals[i] != "")
      generic_save_embedded_image (images[i], free_embedded_url (url (proposals[i]), 2));
}

void
generic_link_all_embedded_images () {
  array<tree> images;
  collect_embedded_images (current_document_tree (), images);
  array<string> proposals;
  for (int i= 0; i < N (images); ++i) {
    string proposal;
    if (embedded_proposal_string (images[i], i + 1, proposal)) proposals << proposal;
    else proposals << string ("");
  }
  for (int i= 0; i < N (images); ++i)
    if (proposals[i] != "")
      generic_link_embedded_image (images[i], free_embedded_url (url (proposals[i]), 2));
}

void
generic_embed_image (tree t) {
  if (!linked_image_context_impl (t) || !is_atomic (t[0])) return;
  string file= as_string (t[0]);
  url source= relative (get_current_buffer_safe (), url (file));
  if (!exists (source)) return;

  string data;
  if (load_string (source, data, false)) return;
  tree raw= compound ("tuple",
                      compound ("raw-data", data),
                      as_standard_string (tail (url (file))));
  (void) call ("tree-set", object (t), object (0), object (raw));
}

void
generic_embed_images (tree t) {
  if (is_atomic (t)) return;
  if (linked_image_context_impl (t)) {
    generic_embed_image (t);
    return;
  }
  for (int i= 0; i < N (t); ++i) generic_embed_images (t[i]);
}

void
generic_embed_this_image () {
  tree image;
  if (innermost_linked_image (image)) generic_embed_image (image);
}

void
generic_embed_all_images () {
  generic_embed_images (current_document_tree ());
}

object
generic_spell_live_current_selection () {
  path start_path, end_path;
  if (!spell_live_current_range (start_path, end_path)) return object (false);
  return list_object (object (start_path), object (end_path));
}

object
generic_spell_live_current_word () {
  path start_path, end_path;
  string word;
  if (!spell_live_current_range (start_path, end_path) ||
      !spell_live_current_word_impl (start_path, end_path, word))
    return object (false);
  return object (word);
}

object
generic_spell_live_current_language () {
  path start_path, end_path;
  if (!spell_live_current_range (start_path, end_path)) return object (false);
  return object (spell_live_language_at (start_path));
}

object
generic_spell_live_current_suggestions () {
  path start_path, end_path;
  string word;
  if (!spell_live_current_range (start_path, end_path) ||
      !spell_live_current_word_impl (start_path, end_path, word))
    return object (list<string> ());

  tree checked= spell_check (spell_live_language_at (start_path), word);
  list<string> suggestions;
  if (is_tuple (checked))
    for (int i= 1; i < N (checked) && i <= 9; ++i)
      if (is_atomic (checked[i])) suggestions << as_string (checked[i]);
  return object (suggestions);
}

bool
generic_test_balloon_halign (string value) {
  tree balloon;
  return innermost_balloon (balloon) && N (balloon) > 2 &&
         is_atomic (balloon[2]) && as_string (balloon[2]) == value;
}

void
generic_set_balloon_halign (string value) {
  tree balloon;
  if (innermost_balloon (balloon))
    (void) call ("tree-set", object (balloon), object (2), object (value));
}

bool
generic_test_balloon_valign (string value) {
  tree balloon;
  return innermost_balloon (balloon) && N (balloon) > 3 &&
         is_atomic (balloon[3]) && as_string (balloon[3]) == value;
}

void
generic_set_balloon_valign (string value) {
  tree balloon;
  if (innermost_balloon (balloon))
    (void) call ("tree-set", object (balloon), object (3), object (value));
}

string
generic_cardlink_native_type (tree destination) {
  return cardlink_type (destination);
}

tree
generic_cardlink_native_render (tree body, string icon_path, string type) {
  tree icon= cardlink_icon_from_string (icon_path, type);
  tree display= cardlink_empty_body (body) ? tree (cardlink_type_display_name (type)) : body;
  tree content= compound ("concat", icon, "  ", display);
  tree ornament= compound ("ornament", content);
  tree resized= compound ("resize", ornament, "", "", "", "");
  array<tree> args;
  args << tree ("ornament-shape") << tree ("rectangular")
       << tree ("ornament-border") << tree ("1ln")
       << tree ("ornament-color") << tree ("#f8f8f8")
       << tree ("ornament-hpadding") << tree ("1spc")
       << tree ("ornament-vpadding") << tree ("0.75spc")
       << resized;
  return compound ("with", args);
}

string
generic_cardlink_default_link_body (tree destination) {
  return cardlink_type_default_link_name (cardlink_type (destination));
}

tree
generic_display_link_as_card (tree t) {
  if (N (t) < 2) return t;
  return cardlink_replace_whole (t, compound ("cardlink", t[0], t[1]));
}

tree
generic_display_card_as_link (tree t) {
  if (N (t) < 2) return t;
  tree body= cardlink_empty_body (t[0]) ? tree (generic_cardlink_default_link_body (t[1])) : t[0];
  return cardlink_replace_whole (t, compound ("hlink", body, t[1]));
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

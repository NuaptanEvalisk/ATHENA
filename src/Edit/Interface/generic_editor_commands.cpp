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
#include "format_commands.hpp"
#include "format_geometry.hpp"
#include "document_commands.hpp"
#include "language.hpp"
#include "new_buffer.hpp"
#include "new_view.hpp"
#include "tree_select.hpp"
#include "tree_traverse.hpp"
#include "basic.hpp"
#include "hashmap.hpp"

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

enum generic_parameter_mode_kind {
  GENERIC_PARAMETER_INVALID,
  GENERIC_PARAMETER_GLOBAL,
  GENERIC_PARAMETER_LOCAL
};

struct generic_parameter_mode {
  generic_parameter_mode_kind kind= GENERIC_PARAMETER_INVALID;
  string focus_label;
};

generic_parameter_mode parse_parameter_mode (object mode) {
  generic_parameter_mode result;
  if (mode == keyword_object ("global")) {
    result.kind= GENERIC_PARAMETER_GLOBAL;
    return result;
  }
  if (!is_list (mode)) return result;
  array<object> items= as_array_object (mode);
  if (N (items) != 2 || items[0] != keyword_object ("local") ||
      !is_symbol (items[1]))
    return result;
  result.kind= GENERIC_PARAMETER_LOCAL;
  result.focus_label= as_symbol (items[1]);
  return result;
}

bool parameter_focus_matches (const generic_parameter_mode& mode, tree& focus) {
  if (mode.kind != GENERIC_PARAMETER_LOCAL) return false;
  focus= current_focus_tree ();
  return is_compound (focus) && as_string (L (focus)) == mode.focus_label;
}

bool generic_parameter_content (object value) {
  return is_string (value) || is_tree (value);
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

bool
generic_string_variable_name (tree t, int i) {
  if (i < 0 || i >= N (t)) return false;
  if (get_child_type (t, i) != "variable") return false;
  bool variable_container=
    is_compound (t, "with") || is_compound (t, "attr") ||
    is_compound (t, "style-with") || is_compound (t, "style-with*");
  return variable_container && is_atomic (t[i]) && as_string (t[i]) != "";
}

string
generic_type_to_format (string type) {
  if (type == "adhoc" || type == "raw" || type == "graphical" ||
      type == "point" || type == "obsolete" || type == "unknown" ||
      type == "error")
    return "n.a.";
  if (type == "url") return gui_is_qt () ? "string" : "smart-file";
  return "string";
}

bool
generic_hidden_child (tree t, int i) {
  if (i < 0 || i >= N (t)) return false;
  return !is_accessible_child (t, i) && !generic_string_variable_name (t, i) &&
         generic_type_to_format (get_child_type (t, i)) != "n.a.";
}

namespace {

int generic_hidden_child_count (tree t) {
  int count= 0;
  for (int i= 0; i < N (t); ++i)
    if (generic_hidden_child (t, i)) ++count;
  return count;
}

string generic_tree_child_name_impl (tree t, int i, bool long_name) {
  if (i < 0 || i >= N (t)) return "";
  string name= long_name ? get_child_long_name (t, i) : get_child_name (t, i);
  if (name != "") return name;
  if (i > 0 && generic_string_variable_name (t, i - 1)) {
    string variable= as_string (t[i - 1]);
    return replace (variable, "-", " ");
  }
  if (generic_hidden_child_count (t) > 1) return "";
  string type= get_child_type (t, i);
  return type == "regular" ? string ("") : type;
}

} // namespace

string
generic_tree_child_name_star (tree t, int i) {
  return generic_tree_child_name_impl (t, i, false);
}

string
generic_tree_child_long_name_star (tree t, int i) {
  return generic_tree_child_name_impl (t, i, true);
}

string
generic_type_to_width (string type) {
  if (type == "boolean" || type == "integer" || type == "length" ||
      type == "numeric" || type == "duration")
    return "5em";
  if (type == "identifier") return "8em";
  return "1w";
}

bool
generic_inputter_active (tree t, string type) {
  return type == "length" ? geometry_rich_length (t) : is_atomic (t);
}

string
generic_inputter_decode (tree t, string type) {
  if (type == "length") return geometry_rich_length_string (t);
  return is_atomic (t) ? as_string (t) : string ("");
}

scheme_tree
generic_inputter_encode (string value, string type) {
  if (type == "length") return geometry_parse_rich_length (value);
  return tree (scm_quote (value));
}

bool
generic_parameter_test (string name, object value, object mode_object) {
  if (!generic_parameter_content (value)) return false;
  generic_parameter_mode mode= parse_parameter_mode (mode_object);
  tree expected= content_to_tree (value);
  if (mode.kind == GENERIC_PARAMETER_GLOBAL)
    return get_current_editor ()->get_init_value (name) == expected;

  tree focus;
  if (!parameter_focus_matches (mode, focus)) return false;
  object current= format_tree_with_get (object (focus), tree (name));
  return is_tree (current) && as_tree (current) == expected;
}

void
generic_parameter_set (string name, object value, object mode_object) {
  if (!generic_parameter_content (value)) return;
  generic_parameter_mode mode= parse_parameter_mode (mode_object);
  if (mode.kind == GENERIC_PARAMETER_GLOBAL) {
    document_set_init_env (name, content_to_tree (value));
    return;
  }

  tree focus;
  if (!parameter_focus_matches (mode, focus)) return;
  format_tree_with_set (focus, list_object (object (name), value));
}

object
generic_parameter_get (string name, object mode_object) {
  generic_parameter_mode mode= parse_parameter_mode (mode_object);
  tree value;
  if (mode.kind == GENERIC_PARAMETER_GLOBAL)
    value= get_current_editor ()->get_init_value (name);
  else if (mode.kind == GENERIC_PARAMETER_LOCAL)
    value= get_current_editor ()->get_env_value (name);
  else
    return object ("");

  if (is_compound (value, "macro", 1) && is_atomic (value[0]))
    return object (as_string (value[0]));
  return tree_to_stree (value);
}

string
generic_parameter_get_string (string name, object mode) {
  object value= generic_parameter_get (name, mode);
  return is_string (value) ? as_string (value) : string ("");
}

bool
generic_parameter_default (string name, object mode_object) {
  generic_parameter_mode mode= parse_parameter_mode (mode_object);
  if (mode.kind == GENERIC_PARAMETER_GLOBAL)
    return !get_current_editor ()->defined_in_init (name);

  tree focus;
  if (!parameter_focus_matches (mode, focus)) return false;
  object current= format_tree_with_get (object (focus), tree (name));
  return is_bool (current) && !as_bool (current);
}

void
generic_parameter_reset (string name, object mode_object) {
  generic_parameter_mode mode= parse_parameter_mode (mode_object);
  if (mode.kind == GENERIC_PARAMETER_GLOBAL) {
    init_default_current_view (name);
    return;
  }

  tree focus;
  if (!parameter_focus_matches (mode, focus)) return;
  format_tree_with_reset (object (focus), tree (name));
}

bool
generic_parameter_enabled (string name, object mode) {
  return generic_parameter_test (name, object ("true"), mode);
}

namespace {

string focus_doc_avoid_conflict (string name, array<string> previous) {
  for (int suffix= 1; ; ++suffix) {
    string candidate= suffix == 1 ? name : name * as_string (suffix);
    bool found= false;
    for (int i= 0; i < N (previous); ++i)
      if (previous[i] == candidate) {
        found= true;
        break;
      }
    if (!found) return candidate;
  }
}

string focus_doc_arg_name (tree t, int i, array<string> previous) {
  string name= get_child_name (t, i);
  if (name == "") {
    string type= get_child_type (t, i);
    name= type == "regular" ? string ("body") : type;
  }
  return focus_doc_avoid_conflict (name, previous);
}

} // namespace

object
generic_focus_doc_arg_names (tree t, int start, object previous_names) {
  array<string> previous;
  if (is_list (previous_names)) {
    array<object> items= as_array_object (previous_names);
    for (int i= 0; i < N (items); ++i)
      if (is_string (items[i])) previous << as_string (items[i]);
  }

  array<object> result;
  for (int i= start; i < N (t); ++i) {
    string name= focus_doc_arg_name (t, i, previous);
    result << object (name);
    previous << name;
  }
  return as_list_object (result);
}

string
generic_parameter_name (string name) {
  object tree_name= call ("tree-name", list_object (symbol_object (name)));
  if (!is_string (tree_name)) return "";
  object display= call ("focus-tag-name", symbol_object (as_string (tree_name)));
  return is_string (display) ? as_string (display) : string ("");
}

bool
generic_parameter_show_in_menu (string name) {
  object theme= call ("member->theme", object (name));
  return is_bool (theme) && !as_bool (theme);
}

bool
generic_parameter_value (object value) {
  if (is_string (value)) return true;
  if (!is_list (value)) return false;
  array<object> items= as_array_object (value);
  return N (items) == 2 && is_string (items[0]);
}

object
generic_focus_variants_of (tree t) {
  return call ("variants-of", symbol_object (as_string (L (t))));
}

string
generic_focus_tag_name (object label) {
  object raw= call ("symbol->string", label);
  if (!is_string (raw)) return "";
  string name= as_string (raw);

  object theme= call ("member->theme", object (name));
  if (!(is_bool (theme) && !as_bool (theme)) && is_string (theme)) {
    string prefix= as_string (theme);
    int start= min (N (name), N (prefix) + 1);
    object nested= call ("focus-tag-name",
                         symbol_object (name (start, N (name))));
    return is_string (nested) ? as_string (nested) : string ("");
  }

  if (as_bool (call ("symbol-unnumbered?", label))) {
    object nested= call ("focus-tag-name",
                         call ("symbol-drop-right", label, object (1)));
    return is_string (nested) ? as_string (nested) : string ("");
  }

  object tree_name= call ("tree-name", object (tree (as_tree_label (name))));
  if (!is_string (tree_name)) return "";
  object upper= call ("upcase-first", tree_name);
  if (!is_string (upper)) return "";
  object display= call ("string-replace", upper, object ("-"), object (" "));
  return is_string (display) ? as_string (display) : string ("");
}

object
generic_child_proposals (tree, int) {
  return object (false);
}

namespace {

hashmap<string,array<string>> focus_parameters_cache=
  hashmap<string,array<string>> (array<string> ());

bool scheme_truthy (object value) {
  return !(is_bool (value) && !as_bool (value));
}

array<string> string_list (object value) {
  array<string> result;
  if (!is_list (value)) return result;
  array<object> items= as_array_object (value);
  for (int i= 0; i < N (items); ++i)
    if (is_string (items[i])) result << as_string (items[i]);
  return result;
}

object strings_object (array<string> values) {
  array<object> result;
  for (int i= 0; i < N (values); ++i) result << object (values[i]);
  return as_list_object (result);
}

bool string_array_contains (array<string> values, string value) {
  for (int i= 0; i < N (values); ++i)
    if (values[i] == value) return true;
  return false;
}

string focus_parameters_cache_key (tree t, object mode) {
  string label= as_string (L (t));
  string mode_key= object_to_string (mode);
  string style_key= object_to_string (tree_to_stree (get_current_editor ()->get_style ()));
  return label * "\n" * mode_key * "\n" * style_key;
}

} // namespace

object
generic_focus_parameters_list (tree t, object mode) {
  object found= call ("search-parameters", symbol_object (as_string (L (t))));
  array<string> parameters= string_list (found);
  array<string> visible;
  for (int i= 0; i < N (parameters); ++i)
    if (as_bool (call ("parameter-show-in-menu?", object (parameters[i]))))
      visible << parameters[i];

  bool global= mode == keyword_object ("global");
  array<string> customizable;
  if (!global) {
    object definitions= format_customizable_parameters_memo (t);
    if (is_list (definitions)) {
      array<object> items= as_array_object (definitions);
      for (int i= 0; i < N (items); ++i) {
        if (!is_list (items[i])) continue;
        array<object> pair= as_array_object (items[i]);
        if (N (pair) > 0 && is_string (pair[0]))
          customizable << as_string (pair[0]);
      }
    }
  }

  object inhibited= eval (global ? "inhibit-global-table" : "inhibit-local-table");
  array<string> result;
  for (int i= 0; i < N (visible); ++i) {
    string parameter= visible[i];
    if (string_array_contains (customizable, parameter)) continue;
    if (scheme_truthy (call ("ahash-ref", inhibited, object (parameter)))) continue;
    result << parameter;
  }
  return strings_object (result);
}

object
generic_focus_parameters_list_memo (tree t, object mode) {
  string key= focus_parameters_cache_key (t, mode);
  if (!focus_parameters_cache->contains (key))
    focus_parameters_cache (key)= string_list (generic_focus_parameters_list (t, mode));
  return strings_object (focus_parameters_cache[key]);
}

void
generic_focus_parameters_cache_clear () {
  focus_parameters_cache= hashmap<string,array<string>> (array<string> ());
}

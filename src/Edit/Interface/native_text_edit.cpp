// Copyright (C) 2001 Joris van der Hoeven, 2026 ATHENA contributors.
// GPL-3.0-or-later.
#include "native_text_edit.hpp"
#include "native_interfaces.hpp"
#include "editor.hpp"
#include "tree_traverse.hpp"
#include "Data/NodeContracts/node_groups.hpp"

namespace {
bool parent_tree (tree t, tree& parent) {
  const path ip= obtain_ip (t);
  if (!ip_attached (ip) || is_nil (ip)) return false;
  tree root= get_current_editor ()->the_root ();
  parent= subtree (root, reverse (ip->next));
  return true;
}
bool floatable_tag (tree t) {
  const string tag= as_string (L(t));
  return tag == "big-figure" || tag == "big-figure*" ||
    tag == "big-table" || tag == "big-table*" ||
    tag == "algorithm" || tag == "algorithm*";
}
bool wide_wrapper (tree t) {
  return is_func (t, WITH, 3) && t[0] == "par-columns" && t[1] == "1";
}
bool float_ancestor (tree t, tree& result) {
  do {
    if (text_float_context (t) || text_footnote_context (t)) {
      result= t;
      return true;
    }
  } while (parent_tree (t, t));
  return false;
}
path floatable_path (tree t) {
  if (is_func (t, DOCUMENT, 1) || is_func (t, WITH, 3) || floatable_tag (t))
    return path (N(t)-1, floatable_path (t[N(t)-1]));
  return path ();
}

void make_section (tree_label tag, bool unnamed) {
  editor ed= get_current_editor ();
  if (ed->selection_active_any ()) {
    const path cursor= ed->the_path (), start= ed->selection_get_start ();
    while (ed->selection_active_any ()) {
      tree selected= ed->selection_get ();
      if (is_atomic (selected) || N(selected) == 0 || !is_empty (selected[N(selected)-1])) break;
      ed->selection_set_paths (ed->selection_get_start (),
        ed->tree_previous_valid (ed->the_root (), ed->selection_get_end ()));
    }
    if (is_multi_paragraph (ed->selection_get ())) {
      ed->set_message ("make-section error", "invalid multi-paragraph selection");
      return;
    }
    const path end= ed->selection_get_end ();
    call ("make", symbol_object (as_string (tag)));
    if (unnamed) ed->make_return_before ();
    else if (path_less_eq (cursor, start) || path_less (end, cursor)) ed->go_to (cursor);
  }
  else if (!ed->make_return_after ()) {
    call ("make", symbol_object (as_string (tag)));
    if (unnamed) ed->make_return_before ();
  }
}
}

void text_make_section (tree_label tag) { make_section (tag, false); }
void text_make_unnamed_section (tree_label tag) { make_section (tag, true); }

bool text_float_context (tree t) {
  return is_compound (t, "float") || is_compound (t, "wide-float");
}
bool text_footnote_context (tree t) {
  return is_compound (t, "footnote") || is_compound (t, "wide-footnote");
}
bool text_rich_float_context (tree t) {
  do {
    if (text_float_context (t)) return true;
    if (!floatable_tag (t) && !is_func (t, DOCUMENT) && !is_func (t, CONCAT)) return false;
  } while (parent_tree (t, t));
  return false;
}
bool text_floatable_context (tree t) {
  if (!floatable_tag (t) || text_rich_float_context (t)) return false;
  tree parent;
  if (!parent_tree (t, parent)) return false;
  return is_func (parent, DOCUMENT) ||
    (is_func (parent, WITH) && parent_tree (parent, parent) && is_func (parent, DOCUMENT));
}
bool text_float_wide (tree t) {
  tree f;
  return float_ancestor (t, f) &&
    (is_compound (f, "wide-float") || is_compound (f, "wide-footnote"));
}
bool text_floatable_wide (tree t) {
  tree parent;
  return parent_tree (t, parent) && wide_wrapper (parent);
}
void text_float_toggle_wide (tree t) {
  tree f;
  if (!float_ancestor (t, f)) return;
  const string tag= as_string (L(f));
  tree_assign_node (f, as_tree_label (starts (tag, "wide-") ? tag (5, N(tag)) : "wide-" * tag));
}
void text_floatable_toggle_wide (tree t) {
  tree parent;
  if (!parent_tree (t, parent)) return;
  if (wide_wrapper (parent)) tree_remove_node (parent, 2);
  else tree_insert_node (t, 2, tree (WITH, "par-columns", "1"));
}
void text_turn_floating (tree t) {
  if (!text_floatable_context (t)) return;
  tree parent;
  if (parent_tree (t, parent) && is_func (parent, WITH)) t= parent;
  const bool wide= wide_wrapper (t);
  t= tree_assign (t, tree (as_tree_label (wide ? "wide-float" : "float"),
                           "float", "thb", copy (wide ? t[2] : t)));
  editor ed= get_current_editor ();
  ed->go_to_start (reverse (obtain_ip (t)));
  ed->remove_text (false);
  ed->go_to_end (reverse (obtain_ip (t)));
}
void text_turn_non_floating (tree t) {
  if (!text_float_context (t)) return;
  tree body= copy (t[N(t)-1]);
  if (is_compound (t, "wide-float")) body= tree (WITH, "par-columns", "1", body);
  if (!is_func (body, DOCUMENT)) body= tree (DOCUMENT, body);
  editor ed= get_current_editor ();
  ed->cut (reverse (obtain_ip (t)));
  ed->insert_return ();
  ed->insert_tree (body, ed->command_tree_end (body, floatable_path (body)));
}
void text_toggle_anchor () {
  editor ed= get_current_editor ();
  const path p= path_up (ed->the_path ());
  tree t= ed->the_subtree (p);
  if (text_float_context (t)) ed->go_to_start (p * 2);
  else if (text_footnote_context (t)) ed->go_to_start (p * 0);
  else {
    path footnote;
    do {
      if (text_float_context (t)) {
        ed->go_to_end (reverse (obtain_ip (t)));
        return;
      }
      if (is_nil (footnote) && text_footnote_context (t))
        footnote= reverse (obtain_ip (t));
    } while (parent_tree (t, t));
    if (!is_nil (footnote)) ed->go_to_end (footnote);
  }
}

bool text_node_in_group (tree t, const char* group, bool numbered_variants) {
  string tag= as_string (L(t));
  if (numbered_variants && ends (tag, "*")) tag= tag (0, N(tag)-1);
  return athena::contracts::standard_node_groups ().contains (
    group, std::string (tag.data (), N(tag)));
}

void text_make_header (string tag) {
  editor ed= get_current_editor ();
  for (const char* label: {"address", "destination", "cc", "encl"}) {
    const path p= ed->search_upwards (label);
    if (!is_nil (p)) ed->go_to_end (p);
  }
  ed->go_end_line ();
  if (ed->the_line () != "") ed->insert_return ();
  call ("make", symbol_object (tag));
}

void text_section_switch_to (int index) {
  editor ed= get_current_editor ();
  const array<tree> sections= search_sections (ed->the_buffer ());
  if (index < 0 || index >= N(sections)) return;
  tree section= sections[index];
  const path ip= obtain_ip (section);
  if (ip_attached (ip) && text_node_in_group (section, "section-tag", true))
    ed->go_to_end (reverse (ip) * 0);
}

bool text_titled_context (tree t) {
  return is_compound (t, "enunciation") || text_node_in_group (t, "titled-tag", true);
}

void text_note_toggle_custom (tree t) {
  string tag= as_string (L(t));
  tag= ends (tag, "*") ? tag (0, N(tag)-1) : tag * "*";
  if (text_node_in_group (t, "auto-note-tag"))
    t= tree_insert (t, N(t), tree (TUPLE, tree (NAMED_SYMBOL, "texmacs:dag")));
  else t= tree_remove (t, N(t)-1, 1);
  tree_assign_node (t, as_tree_label (tag));
}

void text_titled_toggle_name (tree t) {
  if (text_node_in_group (t, "small-figure-tag", true))
    tree_assign (t, tree (as_tree_label ("render-small-figure"), "", "", t[0], t[1]));
  else if (text_node_in_group (t, "big-figure-tag", true))
    tree_assign (t, tree (as_tree_label ("render-big-figure"), "", "", t[0], t[1]));
  else if (is_compound (t, "render-small-figure"))
    tree_assign (t, tree (as_tree_label ("small-figure"), t[2], t[3]));
  else if (is_compound (t, "render-big-figure"))
    tree_assign (t, tree (as_tree_label ("big-figure"), t[2], t[3]));
  else node_properties_show (t);
}

void text_frame_toggle_title (tree t) {
  const string tag= as_string (L(t));
  if (text_node_in_group (t, "frame-tag")) {
    t= tree_assign (t, tree (as_tree_label (tag * "-titled"), t[0], ""));
    get_current_editor ()->go_to_end (reverse (obtain_ip (t)) * 1);
  }
  else if (text_node_in_group (t, "frame-titled-tag")) {
    t= tree_assign (t, tree (as_tree_label (tag (0, N(tag)-7)), t[0]));
    get_current_editor ()->go_to_end (reverse (obtain_ip (t)) * 0);
  }
}

string text_algorithm_root (string tag) {
  if (ends (tag, "*")) return text_algorithm_root (tag (0, N(tag)-1));
  if (starts (tag, "specified-")) return text_algorithm_root (tag (10, N(tag)));
  if (starts (tag, "named-")) return text_algorithm_root (tag (6, N(tag)));
  return tag;
}

bool text_algorithm_numbered (tree t) {
  const string tag= as_string (L(t)), root= text_algorithm_root (tag);
  return tag == root || tag == "specified-" * root;
}
bool text_algorithm_named (tree t) { return starts (as_string (L(t)), "named-"); }
bool text_algorithm_specified (tree t) {
  const string tag= as_string (L(t));
  return starts (tag, "specified-") || starts (tag, "named-specified-");
}

void text_algorithm_toggle_number (tree t) {
  string tag= text_algorithm_root (as_string (L(t)));
  if (text_algorithm_specified (t)) tag= "specified-" * tag;
  if (text_algorithm_numbered (t)) tag << "*";
  call ("variant-set", object (t), symbol_object (tag));
}

void text_algorithm_toggle_name (tree t) {
  const bool named= text_algorithm_named (t);
  string tag= text_algorithm_root (as_string (L(t)));
  if (text_algorithm_specified (t)) tag= "specified-" * tag;
  if (!named) tag= "named-" * tag;
  t= tree_assign_node (t, as_tree_label (tag));
  if (named) tree_remove (t, 0, 1);
  else {
    t= tree_insert (t, 0, tree (TUPLE, ""));
    get_current_editor ()->go_to_start (reverse (obtain_ip (t)) * 0);
  }
}

void text_algorithm_toggle_specification (tree t) {
  const bool specified= text_algorithm_specified (t);
  const bool named= text_algorithm_named (t);
  const bool numbered= text_algorithm_numbered (t);
  string tag= text_algorithm_root (as_string (L(t)));
  if (!specified) tag= "specified-" * tag;
  if (named) tag= "named-" * tag;
  else if (!numbered) tag << "*";
  t= tree_assign_node (t, as_tree_label (tag));
  if (specified) tree_remove (t, N(t)-2, 1);
  else {
    t= tree_insert (t, N(t)-1, tree (TUPLE, tree (DOCUMENT, "")));
    get_current_editor ()->go_to_start (reverse (obtain_ip (t)) * (N(t)-2));
  }
}

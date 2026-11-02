// Native folding, switches, overlays and slide editing.
// Copyright (C) 2002-2013 Joris van der Hoeven, 2026 ATHENA contributors.
// GPL-3.0-or-later.
#include "presentation_commands.hpp"
#include "editor.hpp"
#include "native_interfaces.hpp"
#include "native_text_edit.hpp"
#include "native_graphics_commands.hpp"
#include "generic_editor_commands.hpp"
#include "document_style_commands.hpp"
#include "Data/NodeContracts/node_groups.hpp"
#include "Data/NodeContracts/node_group_scope.hpp"
#include "Data/NodeContracts/execution_state.hpp"
#include "tree_traverse.hpp"
#include "analyze.hpp"
#include "boot.hpp"
#include "ATHENA/Data/heading_word_count.hpp"
#include "node_metadata.hpp"
#include <algorithm>

namespace {
bool tag (tree t, const char* name) {
  return is_compound (t, name);
}
bool group (tree t, const char* name) {
  const string label= as_string (L(t));
  return athena::contracts::group_contains (
    athena::contracts::current_evaluation_state ().contracts, name,
    std::string_view (label.data (), N(label)));
}
bool toggle (tree t) {
  return group (t, "toggle-tag");
}
bool first (tree t) {
  return group (t, "toggle-first-tag");
}
bool second (tree t) {
  return group (t, "toggle-second-tag");
}
bool switches (tree t) {
  return group (t, "switch-tag");
}
bool alternative (tree t) {
  return group (t, "alternative-tag");
}
bool unroll (tree t) {
  return group (t, "unroll-tag");
}
bool expanded (tree t) {
  return group (t, "expanded-tag");
}
bool hidden (tree t) {
  return group (t, "hidden-tag");
}
bool slideshow (tree t) {
  return is_compound (t, "slideshow", 1) && is_document (t[0]);
}
bool parent (tree t, tree& p) {
  path ip= obtain_ip (t);
  if (!ip_attached (ip) || admits_edit_observer (t) || is_nil (ip))
    return false;
  p= get_current_editor ()->the_subtree (reverse (ip->next));
  return true;
}
tree ancestor (tree t, bool (*predicate) (tree)) {
  for (;;) {
    if (predicate (t))
      return t;
    tree p;
    if (!parent (t, p))
      return tree ();
    t= p;
  }
}
tree focus () {
  auto ed= get_current_editor ();
  return ed->the_subtree (ed->focus_get ());
}
tree cursor_tree () {
  auto ed= get_current_editor ();
  return ed->the_subtree (path_up (ed->the_path ()));
}
int down (tree t) {
  path ip= obtain_ip (t);
  if (!ip_attached (ip))
    return -1;
  path p= reverse (ip), cursor= get_current_editor ()->the_path ();
  if (!(p < cursor))
    return -1;
  int i= (cursor / p)->item;
  return i < N (t) ? i : -1;
}
void go (tree t, bool end= false) {
  path ip= obtain_ip (t);
  if (!ip_attached (ip))
    return;
  auto ed= get_current_editor ();
  if (end)
    ed->go_to_end (reverse (ip));
  else
    ed->go_to_start (reverse (ip));
}
void change_tag (tree& t, const char* name) {
  t= tree_assign_node (t, as_tree_label (name));
}
void switch_set (tree t, int i, bool on) {
  if (i < 0 || i >= N (t))
    return;
  tree child= t[i];
  const char* hide= unroll (t) ? "hidden*" : "hidden";
  if (on && hidden (child))
    tree_assign_node (child, as_tree_label ("shown"));
  else if (!on && tag (child, "shown"))
    tree_assign_node (child, as_tree_label (hide));
  else if (on && !tag (child, "shown"))
    tree_insert_node (child, 0, compound ("shown"));
  else if (!on && !hidden (child))
    tree_insert_node (child, 0, compound (hide));
}
void select (tree t, int i) {
  for (int j= 0; j < N (t); ++j)
    switch_set (t, j, expanded (t) || (unroll (t) ? j <= i : j == i));
}
int visible (tree t) {
  for (int i= N (t) - 1; i >= 0; --i)
    if (!hidden (t[i]))
      return i;
  return -1;
}
void switch_to (tree t, int i, bool end= false) {
  if (i < 0 || i >= N (t))
    return;
  select (t, i);
  go (t[i], end);
}
void toggle_tree (tree t, bool move= true) {
  bool was_first= first (t);
  const string name= as_string (L (t));
  auto next= athena::contracts::standard_node_groups ().alternate (
      std::string_view (name.data (), N (name)));
  t= tree_assign_node (t, as_tree_label (string (next.c_str ())));
  if (move && N (t) > 1)
    go (t[was_first ? 1 : 0]);
}
int number (tree t) {
  return is_atomic (t) ? as_int (t->label) : 0;
}
void overlays_set (tree t, int n) {
  if (number (t[0]) == n)
    return;
  tree_assign (t[0], as_string (n));
  // This native graphics service owns proviso propagation.
  graphics_update_proviso (t[2], as_string (n));
}
void overlay_renumber (tree t, int at, int total, int delta) {
  if (is_atomic (t) || presentation_overlays (t))
    return;
  int begin= presentation_overlay (t) ? 1 : 0;
  for (int i= begin; i < N (t); ++i)
    overlay_renumber (t[i], at, total, delta);
  if (!begin)
    return;
  int old= number (t[0]), next= old >= at ? old + delta : old;
  if (next < at && delta < 0) {
    string name= as_string (L (t));
    if (name == "overlay-from" || name == "alternate-from")
      next= at;
    else if (name == "overlay-this" || name == "alternate-this" ||
             name == "overlay-other" || name == "alternate-other")
      next= 0;
    else if (name != "overlay-until" && name != "alternate-until")
      next= std::clamp (at, 1, total + delta);
  }
  tree_assign (t[0], as_string (next));
}
bool matching_environment (tree t, string name) {
  if (tag (t, "summarized-algorithm") || tag (t, "detailed-algorithm"))
    return name == "algorithm";
  if (toggle (t) && N (t) > 1)
    return matching_environment (t[1], name);
  if (is_func (t, DOCUMENT, 1))
    return matching_environment (t[0], name);
  if (is_atomic (t) || is_document (t) || is_concat (t))
    return name == "text";
  if (name == "proof")
    return tag (t, "proof") || tag (t, "render-proof");
  return as_string (L (t)) == name;
}
void go_graphics () {
  tree t= ancestor (cursor_tree (), [] (tree x) { return tag (x, "screens"); });
  if (!tag (t, "screens"))
    return;
  tree doc= presentation_slide_document (t);
  for (int i= 0; i < std::min (N (doc), 2); ++i)
    if (is_compound (doc[i], "gr-screen", 1)) {
      t= doc[i][0];
      while (is_func (t, DOCUMENT, 1) || is_compound (t, "gr-overlays", 3) ||
             is_func (t, WITH))
        t= t[N (t) - 1];
      if (is_func (t, GRAPHICS) && N (t))
        go (t[0]);
      return;
    }
}
tree slide_template (tree t) {
  if ((tag (t, "shown") || hidden (t)) && N (t) == 1)
    t= t[0];
  if (is_document (t)) {
    tree out (DOCUMENT);
    int n= N (t) > 1 && tag (t[0], "tit") ? 2 : std::min (1, N (t));
    for (int i= 0; i < n; ++i)
      out << slide_template (t[i]);
    return out;
  }
  if (tag (t, "tit")) {
    tree out (L (t), N (t));
    for (int i= 0; i < N (t); ++i)
      out[i]= "";
    return out;
  }
  if (is_compound (t, "gr-screen", 1))
    return compound ("gr-screen", slide_template (t[0]));
  if (is_compound (t, "gr-overlays", 3))
    return compound ("gr-overlays", "1", "1", slide_template (t[2]));
  if ((is_func (t, WITH) || tag (t, "with-screen-color")) && N (t)) {
    tree out (L (t), N (t));
    for (int i= 0; i < N (t) - 1; ++i)
      out[i]= copy (t[i]);
    out[N (t) - 1]= slide_template (t[N (t) - 1]);
    return out;
  }
  return is_func (t, GRAPHICS) ? tree (GRAPHICS, "") : tree ("");
}
void switch_insert (tree t, int i, tree content) {
  if (i < 0 || i > N (t))
    return;
  int v= visible (t);
  t= tree_insert (t, i, tree (TUPLE, compound ("shown", content)));
  select (t, alternative (t) ? i : v + 1);
  go (t[i]);
}
void switch_remove (tree t, int i) {
  if (i < 0) {
    go (t);
    return;
  }
  if (i >= N (t)) {
    go (t, true);
    return;
  }
  if (N (t) <= 1)
    return;
  int v= visible (t);
  t= tree_remove (t, i, 1);
  select (t, alternative (t) ? std::min (i, N (t) - 1) : std::max (0, v - 1));
  go (t[std::min (i, N (t) - 1)]);
}
} // namespace

bool presentation_overlays (tree t) {
  return group (t, "overlays-tag") && N (t) == 3;
}
bool presentation_overlay (tree t) {
  return group (t, "overlay-tag");
}
bool presentation_context (tree t) {
  return toggle (t) || switches (t) || presentation_overlays (t);
}
bool presentation_screens (tree t) {
  return tag (t, "screens") || slideshow (t);
}
int presentation_overlay_current (tree t) {
  t= ancestor (t, presentation_overlays);
  return presentation_overlays (t) ? number (t[0]) : 0;
}
int presentation_overlay_count (tree t) {
  t= ancestor (t, presentation_overlays);
  return presentation_overlays (t) ? number (t[1]) : 0;
}
void presentation_overlay_switch (tree t, int i) {
  t= ancestor (t, presentation_overlays);
  if (presentation_overlays (t))
    overlays_set (t, std::clamp (i, 1, number (t[1])));
}
bool presentation_overlay_visible (tree t, int i) {
  if (is_func (t, WITH)) {
    for (int j= 0; j + 1 < N (t); j+= 2)
      if (is_atomic (t[j]) && t[j]->label == "proviso")
        return presentation_overlay_visible (t[j + 1], i);
    return false;
  }
  if (!presentation_overlay (t) || N (t) == 0 || !is_atomic (t[0]) ||
      !is_int (t[0]->label))
    return false;
  string name= as_string (L (t));
  int ref= number (t[0]);
  if (name == "show-always")
    return true;
  if (ends (name, "-from"))
    return i >= ref;
  if (ends (name, "-until"))
    return i <= ref;
  if (ends (name, "-this"))
    return i == ref;
  if (ends (name, "-other"))
    return i != ref;
  return false;
}
tree presentation_slide_switch (tree t) {
  return slideshow (t) ? t[0] : t;
}
tree presentation_slide_document (tree t) {
  if (slideshow (t))
    t= t[0];
  if (tag (t, "screens") || (is_document (t) && N (t) > 0 && tag (t[0], "slide"))) {
    int i= down (t);
    if (i < 0)
      return tree ();
    t= t[i];
  }
  while ((tag (t, "shown") || hidden (t) || tag (t, "screen") || tag (t, "slide") ||
          is_func (t, WITH) || tag (t, "with-screen-color")) &&
         N (t))
    t= t[N (t) - 1];
  return is_document (t) ? t : tree ();
}
string presentation_slide_name (tree t, int i) {
  t= presentation_slide_document (t);
  string title;
  if (N (t) > 0 && tag (t[0], "tit") && N (t[0]))
    title= athena_plain_text_projection (t[0][0]);
  return "Slide " * as_string (i + 1) * (title == "" ? string ("") : ": " * title);
}
void presentation_slide_title (tree t) {
  t= presentation_slide_document (t);
  if (!is_document (t) || (N (t) > 0 && tag (t[0], "tit")))
    return;
  t= tree_insert (t, 0, tree (TUPLE, compound ("tit", "")));
  go (t[0]);
}
void presentation_slide_graphics (tree t) {
  t= presentation_slide_document (t);
  if (!is_document (t))
    return;
  if (N (t) == 1 && tag (t[0], "tit"))
    t= tree_insert (t, 1, tree (TUPLE, ""));
  tree child= tree_assign (t[N (t) - 1], compound ("gr-screen", tree (DOCUMENT, "")));
  go (child);
  graphics_insert (tree (TUPLE,
    "gr-mode", tree (TUPLE, "hand-edit", "penscript"),
    "gr-frame", tree (TUPLE, "scale", "1cm", tree (TUPLE, "0gw", "1gh")),
    "gr-geometry", tree (TUPLE, "geometry", "1gpar", "1gpag", "axis")));
}
tree presentation_slide_background () {
  tree t= presentation_slide_document (ancestor (cursor_tree (), presentation_screens)), p;
  while (parent (t, p) && is_func (p, WITH))
    t= p;
  return parent (t, p) && tag (p, "with-screen-color") ? p[0] : tree ("");
}
void presentation_set_slide_background (tree color) {
  tree t= presentation_slide_document (ancestor (cursor_tree (), presentation_screens)), p;
  if (!is_document (t))
    return;
  while (parent (t, p) && is_func (p, WITH))
    t= p;
  if (parent (t, p) && tag (p, "with-screen-color")) {
    if (is_atomic (color) && color->label == "")
      tree_remove_node (p, N (p) - 1);
    else
      tree_assign (p[0], color);
  } else if (!(is_atomic (color) && color->label == ""))
    tree_insert_node (t, 1, compound ("with-screen-color", color));
}
void presentation_switch_to (object which) {
  tree t= ancestor (cursor_tree (), presentation_screens);
  if (!presentation_screens (t))
    return;
  bool expanded_view= slideshow (t);
  t= presentation_slide_switch (t);
  int c= down (t), n= N (t), i= c;
  if (is_int (which))
    i= as_int (which);
  else {
    string key= keyword_name (which);
    if (key == "first")
      i= 0;
    else if (key == "last")
      i= n - 1;
    else if (key == "next")
      i= std::min (n - 1, c + 1);
    else if (key == "previous")
      i= std::max (0, c - 1);
  }
  if (i >= 0 && i < n) {
    if (expanded_view)
      go (t[i]);
    else
      switch_to (t, i);
  }
  go_graphics ();
}
void presentation_make (tree_label label) {
  tree probe (label);
  auto ed= get_current_editor ();
  if (toggle (probe))
    ed->insert_tree (tree (label, tree (DOCUMENT, ""), tree (DOCUMENT, "")),
                     path (0, 0, 0));
  else if (switches (probe)) {
    bool big= group (probe, "big-switch-tag");
    ed->insert_tree (
        tree (label, compound ("shown", big ? tree (DOCUMENT, "") : tree (""))),
        big ? path (0, 0, 0, 0) : path (0, 0, 0));
  } else if (group (probe, "overlays-tag")) {
    bool big= as_string (label) == "overlays";
    ed->insert_tree (tree (label, "1", "1", big ? tree (DOCUMENT, "") : tree ("")),
                     big ? path (2, 0, 0) : path (2, 0));
  } else if (group (probe, "overlay-tag")) {
    tree t= ancestor (cursor_tree (), presentation_overlays);
    if (!presentation_overlays (t))
      return;
    tree out (label, copy (t[0]), "");
    if (!group (probe, "unary-overlay-tag"))
      out << tree ("");
    ed->insert_tree (out, path (1, 0));
  } else if (as_string (label) == "alter-colors")
    ed->insert_tree (compound ("alter-colors", "", "#0000", "black"), path (0, 0));
}
void presentation_make_unroll (tree_label label) {
  auto ed= get_current_editor ();
  tree selection= ed->selection_get ();
  if (!ed->selection_active_any () || !group (selection, "list-tag") ||
      N (selection) == 0 || !is_document (selection[0])) {
    presentation_make (label);
    return;
  }
  tree out (label);
  for (int i= 0; i < N (selection[0]); ++i)
    out << compound ("shown", tree (DOCUMENT, selection[0][i]));
  ed->selection_cut ("none");
  tree contents (DOCUMENT, out);
  athena::node::copy_metadata (selection[0], contents);
  tree replacement (L(selection), contents);
  athena::node::copy_metadata (selection, replacement);
  ed->insert_tree (replacement);
}
void presentation_make_list (tree_label switch_tag, tree_label list_tag) {
  auto ed= get_current_editor ();
  bool selected= ed->selection_active_any ();
  bool description= selected && !ed->selection_active_small () &&
    group (tree (list_tag), "description-tag");
  if (selected)
    ed->selection_cut ("wrapbuf");
  else ed->selection_cancel ();
  ed->make_compound (list_tag);
  presentation_make (switch_tag);
  if (selected && description)
    ed->insert_tree (compound ("item*", ""), path (0, 0));
  else
    text_make_item ();
  if (selected)
    ed->selection_paste ("wrapbuf");
}
void presentation_move (bool forwards, bool extremal) {
  tree t= ancestor (focus (), presentation_context);
  if (toggle (t)) {
    if (forwards == first (t))
      toggle_tree (t);
  } else if (switches (t)) {
    int i= extremal ? (forwards ? N (t) - 1 : 0)
                    : std::clamp (down (t) + (forwards ? 1 : -1), 0, N (t) - 1);
    switch_to (t, i, extremal ? forwards : !forwards);
  } else if (presentation_overlays (t)) {
    int i=
        extremal ? (forwards ? number (t[1]) : 1) : number (t[0]) + (forwards ? 1 : -1);
    presentation_overlay_switch (t, i);
    if (!presentation_overlay (ancestor (cursor_tree (), presentation_overlay)))
      go (t[2]);
  }
}
void presentation_first () {
  presentation_move (false, true);
}
void presentation_last () {
  presentation_move (true, true);
}
void presentation_next () {
  presentation_move (true, false);
}
void presentation_previous () {
  presentation_move (false, false);
}
bool presentation_alternate (tree t) {
  if (toggle (t)) {
    toggle_tree (t);
    return true;
  }
  if (switches (t)) {
    switch_to (t, (down (t) + 1) % N (t));
    return true;
  }
  return false;
}
bool presentation_variant (tree t, bool forward) {
  if (!switches (t))
    return false;
  int i= down (t);
  const auto names= athena::contracts::group_members (
    athena::contracts::current_evaluation_state ().contracts, "big-switch-tag");
  string label= as_string (L (t));
  auto at=
      std::find (names.begin (), names.end (), std::string (label.data (), N (label)));
  if (at == names.end () || names.empty ())
    return true;
  int index= (at - names.begin () + (forward ? 1 : names.size () - 1)) % names.size ();
  t= tree_assign_node (t, as_tree_label (string (names[index].c_str ())));
  for (int j= 0; j < N (t); ++j)
    if (hidden (t[j]))
      tree_assign_node (t[j], as_tree_label (unroll (t) ? "hidden*" : "hidden"));
  select (t, i);
  return true;
}
bool presentation_structured (tree t, string operation, bool forward) {
  if (operation == "move" || operation == "extremal") {
    if (!presentation_context (t))
      return false;
    if (toggle (t)) {
      if (forward == first (t))
        toggle_tree (t);
    } else if (switches (t))
      switch_to (t,
                 operation == "extremal"
                     ? (forward ? N (t) - 1 : 0)
                     : std::clamp (down (t) + (forward ? 1 : -1), 0, N (t) - 1),
                 operation == "extremal" ? forward : !forward);
    else {
      presentation_overlay_switch (t, operation == "extremal"
                                          ? (forward ? number (t[1]) : 1)
                                          : number (t[0]) + (forward ? 1 : -1));
      if (!presentation_overlay (ancestor (cursor_tree (), presentation_overlay)))
        go (t[2]);
    }
    return true;
  }
  if (operation == "minimize" || operation == "maximize") {
    if (!(operation == "minimize" ? second (t) : first (t)))
      return false;
    toggle_tree (t);
    return true;
  }
  if (operation == "swipe-horizontal") {
    if (!presentation_context (t))
      return false;
    presentation_switch_to (keyword_object (forward ? "next" : "previous"));
    return true;
  }
  if (operation == "swipe-vertical") {
    if (!presentation_context (t))
      return false;
    presentation_traverse_buffer (keyword_object (forward ? "next" : "previous"));
    return true;
  }
  bool insert= operation == "insert";
  if (presentation_overlays (t)) {
    int cur= number (t[0]), total= number (t[1]);
    int at= insert ? cur + (forward ? 1 : 0) : cur - (forward ? 0 : 1);
    if (!insert && (at <= 0 || total <= 1))
      return true;
    overlay_renumber (t[2], at, total, insert ? 1 : -1);
    tree_assign (t[1], as_string (total + (insert ? 1 : -1)));
    if (insert == forward)
      overlays_set (t, at);
    else if (!insert && cur > total - 1)
      overlays_set (t, total - 1);
    return true;
  }
  if (slideshow (t)) {
    t= t[0];
    int i= down (t) + (insert ? (forward ? 1 : 0) : (forward ? 0 : -1));
    if (i < 0)
      return true;
    if (insert) {
      t= tree_insert (t, i, tree (TUPLE, compound ("slide", tree (DOCUMENT, ""))));
      go (t[i]);
    } else if (N (t) > 1) {
      t= tree_remove (t, i, 1);
      go (t[std::min (i, N (t) - 1)], !forward);
    }
    return true;
  }
  if (!switches (t))
    return false;
  int i= down (t);
  if (insert) {
    tree content= group (t, "big-switch-tag") ? tree (DOCUMENT, "") : tree ("");
    if (tag (t, "screens") && i >= 0) {
      content= slide_template (t[i]);
      if (!is_document (content))
        content= tree (DOCUMENT, content);
    }
    switch_insert (t, i + (forward ? 1 : 0), content);
    if (tag (t, "screens"))
      go_graphics ();
  } else
    switch_remove (t, i - (forward ? 0 : 1));
  return true;
}
bool presentation_enter (tree t, bool shift) {
  if (tag (t, "tit")) {
    go (t, true);
    get_current_editor ()->insert_return ();
    return true;
  }
  tree p, grand;
  if (!switches (t) || !parent (t, p) || !is_document (p) || !parent (p, grand) ||
      !group (grand, "list-tag"))
    return false;
  if (shift)
    get_current_editor ()->make_return_after ();
  else {
    presentation_structured (t, "insert", true);
    text_make_item ();
  }
  return true;
}

void presentation_operate (tree t, string mode, string environment) {
  if (is_atomic (t))
    return;
  if (tag (t, "traversed")) {
    if (mode == "var-last")
      return;
    presentation_operate (t[0], mode, environment);
    if (mode == "unfold" || mode == "expand" || mode == "var-expand" || mode == "first")
      tree_remove_node (t, 0);
    return;
  }
  if (tag (t, "fold-back") || tag (t, "keep-folded")) {
    if (tag (t, "fold-back") && mode == "last")
      mode= "var-last";
    if (tag (t, "keep-folded") && mode == "var-last")
      mode= "last";
    presentation_operate (t[0], mode, environment);
    return;
  }
  for (int i= 0; i < N (t); ++i)
    presentation_operate (t[i], mode, environment);
  if (toggle (t)) {
    bool unfold=
        mode == "unfold" || mode == "expand" || mode == "var-expand" || mode == "last";
    bool fold= mode == "fold" || mode == "compress" || mode == "var-compress" ||
               mode == "first" || mode == "var-last";
    if ((environment == "" || matching_environment (t, environment)) &&
        ((first (t) && unfold) || (second (t) && fold)))
      toggle_tree (t, false);
    if (mode == "var-last")
      tree_insert_node (t, 0, compound ("traversed"));
  } else if (switches (t)) {
    if (mode == "expand")
      for (int i= 0; i < N (t); ++i)
        switch_set (t, i, true);
    else if (mode == "compress")
      for (int i= 0; i < N (t); ++i)
        switch_set (t, i, i == 0);
    else if (mode == "first" || (mode == "var-last" && unroll (t))) {
      select (t, 0);
      if (mode == "var-last")
        tree_insert_node (t, 0, compound ("traversed"));
    } else if (mode == "last")
      select (t, N (t) - 1);
    else if (mode == "var-expand") {
      bool expand= alternative (t) || unroll (t);
      if (alternative (t))
        change_tag (t, "expanded");
      if (expand)
        for (int i= 0; i < N (t); ++i)
          switch_set (t, i, true);
    } else if (mode == "var-compress") {
      bool compress= expanded (t) || unroll (t);
      if (expanded (t))
        change_tag (t, "switch");
      if (compress)
        select (t, 0);
    }
  } else if (presentation_overlays (t) && (mode == "first" || mode == "last"))
    overlays_set (t, mode == "first" ? 1 : number (t[1]));
}
bool presentation_traverse (tree t, string mode) {
  if (is_atomic (t))
    return false;
  bool forward= mode == "next" || mode == "var-next";
  if (tag (t, "traversed")) {
    if (forward)
      return false;
    t= tree_remove_node (t, 0);
    presentation_operate (t, "last");
    if (mode == "var-previous") {
      auto children= generic_tree_accessible_children (t);
      for (int i= 0; i < N (children); ++i)
        presentation_operate (children[i], "var-last");
    }
    return true;
  }
  if (tag (t, "fold-back") || tag (t, "keep-folded")) {
    if (tag (t, "fold-back")) {
      if (mode == "next")
        mode= "var-next";
      if (mode == "previous")
        mode= "var-previous";
    } else {
      if (mode == "var-next")
        mode= "next";
      if (mode == "var-previous")
        mode= "previous";
    }
    return presentation_traverse (t[0], mode);
  }
  if (toggle (t)) {
    bool folded= first (t);
    if (presentation_traverse (t[folded ? 0 : 1], mode))
      return true;
    if (folded && forward) {
      toggle_tree (t);
      presentation_operate (t[1], "first");
      go (t[1], true);
      return true;
    }
    if (!folded && mode == "var-next") {
      toggle_tree (t);
      tree_insert_node (t, 0, compound ("traversed"));
      return true;
    }
    if (!folded && !forward) {
      toggle_tree (t);
      presentation_operate (t[0], mode == "previous" ? "last" : "var-last");
      go (t[0], true);
      return true;
    }
    return false;
  }
  if (alternative (t) || unroll (t)) {
    int i= visible (t);
    if (i < 0)
      return false;
    if (presentation_traverse (t[i], mode))
      return true;
    if (mode == "var-next" && i == N (t) - 1 && unroll (t)) {
      presentation_operate (t, "first");
      tree_insert_node (t, 0, compound ("traversed"));
      return true;
    }
    int next= i + (forward ? 1 : -1);
    if (next < 0 || next >= N (t))
      return false;
    presentation_operate (t[next], forward              ? "first"
                                   : mode == "previous" ? "last"
                                                        : "var-last");
    switch_to (t, next);
    return true;
  }
  if (presentation_overlays (t)) {
    int cur= number (t[0]),
        next= std::clamp (cur + (forward ? 1 : -1), 1, number (t[1]));
    if (cur == next)
      return false;
    overlays_set (t, next);
    if (!presentation_overlay (ancestor (cursor_tree (), presentation_overlay)))
      go (t[2]);
    return true;
  }
  auto children= generic_tree_accessible_children (t);
  for (int j= 0; j < N (children); ++j)
    if (presentation_traverse (children[forward ? j : N (children) - 1 - j], mode))
      return true;
  return false;
}
namespace {
string mode_name (object mode) {
  return is_string (mode) ? as_string (mode)
                          : keyword_name (mode);
}
tree operation_root (string mode) {
  tree slide= ancestor (cursor_tree (), [] (tree t) { return tag (t, "slide"); });
  if (tag (slide, "slide") &&
      (mode == "first" || mode == "last" || mode == "next" || mode == "previous"))
    return slide;
  return get_current_editor ()->the_buffer ();
}
} // namespace
void presentation_operate_buffer (object mode) {
  string environment;
  if (is_list (mode)) {
    auto parts= as_array_object (mode);
    environment= is_symbol (parts[1]) ? as_symbol (parts[1]) : as_string (parts[1]);
    mode= parts[0];
  }
  string name= mode_name (mode);
  tree t= operation_root (name);
  presentation_operate (t, name, environment);
  if (name == "first" || name == "var-first")
    go (t);
  if (name == "last" || name == "var-last")
    go (t, true);
  go_graphics ();
}
void presentation_traverse_buffer (object mode) {
  string name= mode_name (mode);
  presentation_traverse (operation_root (name), name);
  go_graphics ();
}

void presentation_make_screens () {
  auto ed= get_current_editor ();
  tree body= ed->the_buffer ();
  if (!is_document (body))
    return;
  path root= reverse (obtain_ip (body)), cursor= ed->the_path () / root;
  bool preamble= N (body) > 0 &&
                 (tag (body[0], "hide-preamble") || tag (body[0], "show-preamble"));
  tree contents (DOCUMENT);
  for (int i= preamble ? 1 : 0; i < N (body); ++i)
    contents << body[i];
  if (N (contents) == 0)
    contents << tree ("");
  tree replacement (DOCUMENT);
  if (preamble)
    replacement << body[0];
  replacement << compound ("screens", compound ("shown", contents));
  athena::node::copy_metadata (body, replacement);
  body= tree_assign (body, replacement);
  if (!is_nil (cursor) && (!preamble || cursor->item != 0))
    ed->go_to (root * (preamble ? 1 : 0) * 0 * 0 *
               path (cursor->item - (preamble ? 1 : 0), cursor->next));
}
void presentation_style_changed () {
  auto ed= get_current_editor ();
  tree body= ed->the_buffer ();
  if (!is_document (body) || N (body) == 0)
    return;
  tree last= body[N (body) - 1];
  if (ed->defined_at_init ("beamer-style")) {
    if (!tag (last, "screens")) {
      ed->init_env ("page-medium", "beamer");
      presentation_make_screens ();
    }
    return;
  }
  tree doc;
  if (is_compound (last, "screens", 1) && is_compound (last[0], "shown", 1))
    doc= last[0][0];
  else if (slideshow (last) && N (last[0]) == 1 && is_compound (last[0][0], "slide", 1))
    doc= last[0][0][0];
  else
    return;
  if (!is_document (doc))
    return;
  path root= reverse (obtain_ip (body)), source= reverse (obtain_ip (doc)),
       cursor= ed->the_path ();
  bool inside= source < cursor;
  path relative= inside ? cursor / source : path ();
  int prefix= N (body) - 1;
  tree out (DOCUMENT);
  for (int i= 0; i < prefix; ++i)
    out << body[i];
  for (int i= 0; i < N (doc); ++i)
    out << doc[i];
  athena::node::copy_metadata (body, out);
  tree_assign (body, out);
  if (inside)
    ed->go_to (root * path (relative->item + prefix, relative->next));
}
namespace {
tree screens_parent (tree body) {
  for (;;) {
    tree p, grand;
    if (!parent (body, p))
      return tree ();
    if (tag (p, "screens") ||
        (is_document (p) && parent (p, grand) && slideshow (grand)))
      return body;
    body= p;
  }
}
bool page_accept (int p, int c, int n) {
  return n <= 40 || (c <= 12 && p <= 24) || (c >= n - 12 && p >= n - 24) ||
         (p >= c - 12 && p <= c + 12) || p == 0 || p == n - 1 || p % 10 == 9;
}
tree prune_hidden (tree t) {
  if (is_atomic (t))
    return copy (t);
  tree out (L (t));
  athena::node::copy_metadata (t, out);
  for (int i= 0; i < N (t); ++i)
    if (!alternative (t) || !tag (t[i], "hidden"))
      out << prune_hidden (t[i]);
  return out;
}
bool find_alternative (tree t, path& p) {
  if (alternative (t) && N (t) > 1)
    return true;
  if (presentation_overlays (t)) {
    tree_assign (t[0], "1");
    tree_assign_node (t, as_tree_label ("overlays-range"));
    return true;
  }
  if (tag (t, "overlays-range") && number (t[0]) < number (t[1]))
    return true;
  for (int i= 0; i < N (t); ++i) {
    path child;
    if (find_alternative (t[i], child)) {
      p= path (i, child);
      return true;
    }
  }
  return false;
}
void keep_first (tree t) {
  path p;
  while (find_alternative (t, p)) {
    tree found= subtree (t, p);
    if (alternative (found))
      tree_remove (found, 1, N (found) - 1);
    else
      tree_assign (found[1], copy (found[0]));
    p= path ();
  }
}
void expand_slide (tree slide, tree& output) {
  path p;
  if (find_alternative (slide, p)) {
    tree a= copy (slide), b= tree_duplicate_source (slide);
    keep_first (a);
    tree found= subtree (b, p);
    if (alternative (found))
      tree_remove (found, 0, 1);
    else
      tree_assign (found[0], as_string (number (found[0]) + 1));
    expand_slide (a, output);
    expand_slide (b, output);
  } else {
    presentation_operate (slide, "var-expand");
    output << slide;
  }
}
} // namespace
string presentation_screens_index (tree body) {
  tree t= screens_parent (body);
  path ip= obtain_ip (t);
  return ip_attached (ip) && !is_nil (ip) ? as_string (ip->item) : string ("-1");
}
string presentation_screens_arity (tree body) {
  tree t= screens_parent (body), p;
  return parent (t, p) ? as_string (N (p)) : string ("-1");
}
int presentation_page_count () {
  tree t= ancestor (cursor_tree (), presentation_screens);
  return presentation_screens (t) ? N (presentation_slide_switch (t))
                                  : get_current_editor ()->nr_pages ();
}
tree presentation_screens_bar (tree body) {
  tree t= screens_parent (body), p;
  if (!parent (t, p))
    return "";
  int c= obtain_ip (t)->item, n= N (p);
  tree out (CONCAT);
  for (int i= 0; i < n; ++i) {
    if (!page_accept (i, c, n)) {
      if (page_accept (i - 1, c, n))
        out << tree ("⋯ ");
      continue;
    }
    tree nr (as_string (i + 1));
    if (i == c)
      nr= compound ("screens-emphasize", nr);
    out << tree (CONCAT,
                 compound ("action", nr, "(screens-switch-to " * as_string (i) * ")"),
                 " ");
  }
  return out;
}
void presentation_make_slides () {
  auto ed= get_current_editor ();
  ed->init_default ("page-medium");
  document_add_style_package ("slides");
  tree body= ed->the_buffer ();
  if (!is_document (body))
    return;
  if (get_preference ("texmacs->pdf:expand slides") == "on") {
    tree out (DOCUMENT);
    athena::node::copy_metadata (body, out);
    for (int i= 0; i < N (body); ++i) {
      if (!switches (body[i])) {
        out << body[i];
        continue;
      }
      for (int j= 0; j < N (body[i]); ++j) {
        tree slide= copy (body[i][j]);
        if (tag (slide, "shown") || hidden (slide))
          change_tag (slide, "slide");
        if (is_compound (slide, "slide", 1))
          expand_slide (slide, out);
        else
          out << slide;
      }
    }
    if (N (out) && is_compound (out[N (out) - 1], "slide", 1))
      out[N (out) - 1]= out[N (out) - 1][0];
    tree_assign (body, out);
  } else {
    for (int i= 0; i < N (body); ++i)
      if (tag (body[i], "screens")) {
        tree screens= body[i], output= compound ("screens");
        screens= tree_insert (screens, N (screens),
                              tree (TUPLE, compound ("hidden", tree (DOCUMENT, ""))));
        presentation_operate (screens, "first");
        for (int j= 0; j < N (screens) - 1; ++j) {
          tree variants (DOCUMENT);
          do {
            tree snapshot= prune_hidden (screens[j]);
            if (N (variants) != 0) snapshot= tree_duplicate_source (snapshot);
            change_tag (snapshot, "slide");
            variants << snapshot;
          } while (presentation_traverse (screens, "next") && !hidden (screens[j]));
          output << variants;
        }
        if (N (output)) {
          tree last= output[N (output) - 1];
          if (N (last) && is_compound (last[N (last) - 1], "slide", 1))
            last[N (last) - 1]= last[N (last) - 1][0];
        }
        tree_assign (screens, output);
        break;
      }
  }
}

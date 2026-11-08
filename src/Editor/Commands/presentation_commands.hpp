// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once
#include "tree.hpp"
#include "App/Runtime/thumbnail_snapshot.hpp"

bool presentation_context (tree);
bool presentation_screens (tree);
bool presentation_overlays (tree);
bool presentation_overlay (tree);
int presentation_overlay_current (tree);
int presentation_overlay_count (tree);
bool presentation_overlay_visible (tree, int);
void presentation_overlay_switch (tree, int);
tree presentation_slide_document (tree);
tree presentation_slide_switch (tree);
string presentation_slide_name (tree, int);
void presentation_slide_title (tree);
void presentation_slide_graphics (tree);
tree presentation_slide_background ();
void presentation_set_slide_background (tree);
void presentation_switch_to (int);
int presentation_active_screen ();
bool presentation_screen_empty (int index);
tree presentation_screen_copy (int index);
bool presentation_edit_screen (screen_edit_operation operation, int index,
                               tree paste_content= tree (UNINIT));
void presentation_navigate (string direction);
void presentation_make (tree_label);
void presentation_make_unroll (tree_label);
void presentation_make_list (tree_label, tree_label);
void presentation_make_screens ();
void presentation_style_changed ();
void presentation_operate (tree, string mode, string environment= "");
void presentation_operate_buffer (string mode, string environment= "");
bool presentation_traverse (tree, string);
void presentation_traverse_buffer (string);
void presentation_move (bool forwards, bool extremal);
void presentation_first ();
void presentation_last ();
void presentation_next ();
void presentation_previous ();
bool presentation_structured (tree, string operation, bool forwards);
bool presentation_enter (tree, bool shift);
bool presentation_alternate (tree);
bool presentation_variant (tree, bool);
void presentation_make_slides ();
string presentation_screens_index (tree);
string presentation_screens_arity (tree);
tree presentation_screens_bar (tree);
int presentation_page_count ();

// Owner-thread-only context for rendering a detached slide without changing
// the active document. Native presentation macros still get its real index.
class presentation_thumbnail_scope {
  int previous_index_;
  int previous_count_;
public:
  presentation_thumbnail_scope (int index, int count);
  ~presentation_thumbnail_scope ();
  presentation_thumbnail_scope (const presentation_thumbnail_scope&)= delete;
  presentation_thumbnail_scope& operator= (const presentation_thumbnail_scope&)= delete;
};

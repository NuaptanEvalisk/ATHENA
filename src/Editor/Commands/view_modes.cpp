// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "Editor/Commands/view_modes.hpp"
#include "Editor/Commands/view_zoom.hpp"
#include "Editor/Commands/document_commands.hpp"
#include "Editor/editor.hpp"
#include "native_interfaces.hpp"

namespace {
void schedule_zoom (editor_rep* ed, string mode, int fit, int wait= -1) {
  auto& state= ed->view_modes;
  state.pending= true;
  state.zoom_mode= mode;
  state.fit= fit;
  state.fullscreen_wait= wait;
  state.due= texmacs_time ()+25;
}
int fullscreen_state (editor_rep* ed) {
  if (ed->view_modes.fullscreen_wait >= 0) return ed->view_modes.fullscreen_wait;
  auto viewport= ed->ui_viewport ();
  return viewport.full_screen? 1: viewport.full_screen_edit? 2: 0;
}
void fullscreen (int target) {
  editor ed= get_current_editor ();
  int old= fullscreen_state (ed.operator-> ());
  string mode= document_get_init_page_rendering (ed.operator-> ());
  if (old == 0) view_save_zoom (mode);
  if (old == 1) ed->init_env ("info-flag", ed->view_modes.informative_flags);
  if (target == 1) {
    ed->view_modes.informative_flags= copy (ed->get_init_value ("info-flag"));
    ed->init_env ("info-flag", tree ("none"));
  }
  get_server ()->full_screen_mode (target != 0, target == 2);
  schedule_zoom (ed.operator-> (), mode, target == 1? 1: 0, target);
}
void toggle_page_mode (string target) {
  editor ed= get_current_editor ();
  auto& state= ed->view_modes;
  string current= document_get_init_page_rendering (ed.operator-> ());
  string& revert= target == "panorama"? state.panorama_revert: state.slideshow_revert;
  if (current == target) {
    string previous= revert;
    revert= "paper";
    view_init_page_rendering (previous);
    return;
  }
  // Switching between special modes retains their common ordinary return mode.
  revert= current == "panorama"? state.panorama_revert:
          current == "slideshow"? state.slideshow_revert: current;
  view_init_page_rendering (target);
}
}

void view_init_page_rendering (string mode) {
  editor ed= get_current_editor ();
  auto expanded= [] (string value) {
    return value == "paper" || value == "book" ||
           value == "panorama" || value == "slideshow";
  };
  string old= document_get_init_page_rendering (ed.operator-> ());
  if (expanded (mode) && !expanded (old)) view_screens_show_all ();
  if (!expanded (mode) && expanded (old)) view_screens_show_this ();
  document_apply_page_rendering_state (mode);
  schedule_zoom (ed.operator-> (), mode, mode == "panorama"? 2: 0,
                 ed->view_modes.fullscreen_wait);
}
void view_screens_show_all () {
  editor ed= get_current_editor ();
  path p= ed->search_upwards ("screens");
  if (is_nil (p)) return;
  tree screens= ed->the_subtree (p);
  for (int i=0; i<N(screens); ++i)
    if (is_compound (screens[i], "hidden") || is_compound (screens[i], "shown"))
      tree_assign_node (screens[i], as_tree_label ("slide"));
  screens= tree_assign_node (screens, DOCUMENT);
  tree_insert_node (screens, 0, tree (as_tree_label ("slideshow")));
}
void view_screens_show_this () {
  editor ed= get_current_editor ();
  path p= ed->search_upwards ("slideshow");
  if (is_nil (p)) return;
  tree slides= ed->the_subtree (p);
  if (N(slides) != 1 || !is_compound (slides[0]) || L(slides[0]) != DOCUMENT) return;
  slides= tree_remove_node (slides, 0);
  slides= tree_assign_node (slides, as_tree_label ("screens"));
  path relative= ed->the_path () / p;
  for (int i=0; i<N(slides); ++i)
    if (is_func (slides[i], as_tree_label ("slide")))
      tree_assign_node (slides[i], as_tree_label (
        !is_nil (relative) && relative->item == i? "shown": "hidden"));
}
void view_toggle_panorama () { toggle_page_mode ("panorama"); }
void view_toggle_slideshow () { toggle_page_mode ("slideshow"); }
void view_toggle_fullscreen () {
  fullscreen (fullscreen_state (get_current_editor ().operator-> ()) == 1? 0: 1);
}
void view_toggle_fullscreen_edit () {
  fullscreen (fullscreen_state (get_current_editor ().operator-> ()) == 2? 0: 2);
}
void view_tick_modes (editor_rep* ed) {
  auto& state= ed->view_modes;
  if (!state.pending || texmacs_time () < state.due ||
      ed->has_changed (THE_TREE | THE_ENVIRONMENT | THE_EXTENTS)) return;
  auto viewport= ed->ui_viewport ();
  if (!viewport.attached) return;
  int actual= viewport.full_screen? 1: viewport.full_screen_edit? 2: 0;
  if (state.fullscreen_wait >= 0 && actual != state.fullscreen_wait) return;
  state.pending= false;
  state.fullscreen_wait= -1;
  if (state.fit == 2) view_fit_all ();
  else if (state.fit == 1) view_fit_screen ();
  else view_restore_zoom (state.zoom_mode);
}

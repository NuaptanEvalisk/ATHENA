// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#ifndef ATHENA_VIEW_MODES_HPP
#define ATHENA_VIEW_MODES_HPP
#include "tree.hpp"
#include "tm_timer.hpp"
class editor_rep;
struct editor_view_modes {
  tree informative_flags= "default";
  string panorama_revert= "paper", slideshow_revert= "paper";
  string selected_presentation;
  bool pending= false;
  int fullscreen_wait= -1;
  string zoom_mode;
  int fit= 0; // 0: saved zoom, 1: page, 2: entire document
  time_t due= 0;
};
void view_init_page_rendering (string mode);
void view_toggle_panorama ();
void view_toggle_slideshow ();
void view_toggle_fullscreen ();
void view_toggle_fullscreen_edit ();
void view_tick_modes (editor_rep* ed);
#endif

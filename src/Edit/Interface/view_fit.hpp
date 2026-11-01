// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#ifndef ATHENA_VIEW_FIT_HPP
#define ATHENA_VIEW_FIT_HPP
#include "path.hpp"
#include "tm_timer.hpp"
#include <cstdint>
class editor_rep;
struct persistent_view_fit {
  int step= 4;
  time_t started= 0;
  int restore_step= 2;
  time_t fitted= 0;
  path cursor;
  SI dx= 0, dy= 0;
  std::uint64_t user_scroll= 0;
};
void view_schedule_fit_width ();
void view_toggle_fit_width ();
void view_tick_fit_width (editor_rep* ed);
#endif

// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#ifndef ATHENA_VIEW_ZOOM_HPP
#define ATHENA_VIEW_ZOOM_HPP
#include "string.hpp"
void view_change_zoom (double zoom);
void view_other_zoom (string value);
void view_save_zoom (string mode);
void view_restore_zoom (string mode);
void view_zoom_in (double factor);
void view_zoom_out (double factor);
void view_fit_all ();
void view_fit_screen ();
void view_fit_width ();
void view_fit_height ();
#endif

// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#ifndef ATHENA_VIEW_CONTROLS_HPP
#define ATHENA_VIEW_CONTROLS_HPP
#include "string.hpp"
void view_notify_header (string key, string value);
void view_notify_icon_bar (string key, string value);
void view_notify_toolbar (string key, string value);
void view_notify_change (int changes);
void view_notify_footer (string key, string value);
void view_notify_zoom (string key, string value);
void view_toggle_header ();
void view_toggle_footer ();
void view_toggle_icon_bar (int which);
void view_toggle_snap ();
void view_toggle_typewriter ();
#endif

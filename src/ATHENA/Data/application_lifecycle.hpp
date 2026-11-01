// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#ifndef ATHENA_APPLICATION_LIFECYCLE_HPP
#define ATHENA_APPLICATION_LIFECYCLE_HPP
#include "scheme.hpp"

void application_close_buffer ();
void application_close_window (object arguments);
void application_quit ();
void application_restart ();
void application_new_window ();
void application_open_external (url target);
void application_command_line_convert (string from, string to, object next);

#endif

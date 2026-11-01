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
void application_revert_buffer (object arguments);
void application_revert_buffer_now (object arguments);
void application_import_document (url source, string format, object options);
void application_load_document (url source, object options);
void application_load_document_window (url source, object options);
void application_open_loaded_document (url source, object options);
void application_browse_document (url source, object after_open);

#endif

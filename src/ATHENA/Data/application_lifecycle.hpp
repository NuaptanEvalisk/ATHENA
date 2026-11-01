// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#ifndef ATHENA_APPLICATION_LIFECYCLE_HPP
#define ATHENA_APPLICATION_LIFECYCLE_HPP
#include "scheme.hpp"
#include <functional>

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
void application_load_document (url source, object options, std::function<void(url)> completed);
void application_load_document_window (url source, object options);
void application_open_loaded_document (url source, object options);
void application_browse_document (url source, object after_open);
object application_url_modified (url source);
bool application_url_newer (url left, url right);
void application_url_remove (url source);
object application_url_autosave (url source, string suffix);
object application_url_wrap (url source);
object application_buffer_last_save (url source);
string application_propose_filename ();
array<url> application_sorted_buffers ();
void application_open_auxiliary (string name, tree body, object master);
void application_open_document_dialog ();
void application_open_window_dialog ();
void application_open_document_url (url source);
void application_open_window_url (url source);

#endif

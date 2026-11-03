// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#ifndef ATHENA_APPLICATION_LIFECYCLE_HPP
#define ATHENA_APPLICATION_LIFECYCLE_HPP
#include "tree.hpp"
#include "url.hpp"
#include <functional>
#include <optional>

struct application_document_load_options {
  bool background= false, new_window= false, strict= false;
};

void application_close_buffer ();
void application_close_window (std::optional<url> window= std::nullopt);
void application_quit ();
void application_restart ();
void application_new_window ();
void application_open_external (url target);
void application_command_line_convert (string from, string to, std::function<void()> next);
void application_revert_buffer (url source);
void application_revert_buffer ();
void application_load_document (url source, application_document_load_options options= {},
                                std::function<void(url)> completed= {});
void application_load_document_window (url source, application_document_load_options options= {});
void application_import_document (url source, string format,
                                  application_document_load_options options= {});
void application_browse_document (url source);
void application_browse_document (url source, std::function<void(url)> completed);
bool application_url_newer (url left, url right);
void application_url_remove (url source);
string application_propose_filename ();
array<url> application_sorted_buffers ();
void application_open_document_dialog ();
void application_open_window_dialog ();
void application_open_document_url (url source);
void application_queue_open_document (url source);
void application_open_window_url (url source);

#endif

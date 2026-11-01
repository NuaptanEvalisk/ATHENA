// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#ifndef ATHENA_DOCUMENT_SAVE_FRONTEND_HPP
#define ATHENA_DOCUMENT_SAVE_FRONTEND_HPP
#include "url.hpp"
#include "scheme.hpp"
#include <functional>

void document_save_preserve_viewport (url name);
void document_save_post (url name, object options);
bool document_save_now (url name, object options);
bool document_save_as_now (url destination, url source, object options);
void document_save (object arguments);
void document_save_manual (object arguments);
void document_save_as (object destination, object arguments);
void document_save_dialog ();
bool document_cannot_write (url name, string action);
void document_save_before_close (url name, std::function<void()> completion);

#endif

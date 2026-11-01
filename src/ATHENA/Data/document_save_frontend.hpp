// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#ifndef ATHENA_DOCUMENT_SAVE_FRONTEND_HPP
#define ATHENA_DOCUMENT_SAVE_FRONTEND_HPP
#include "url.hpp"
#include "scheme.hpp"

void document_save_preserve_viewport (url name);
void document_save_post (url name, object options);
bool document_save_now (url name, object options);
void document_save_as_now (url destination, url source, object options);

#endif

// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#ifndef ATHENA_RECENT_FILES_HPP
#define ATHENA_RECENT_FILES_HPP
#include "url.hpp"

array<url> recent_file_list (int limit);
void buffer_notify_recent (url name);

#endif

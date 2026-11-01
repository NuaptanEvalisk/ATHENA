// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "recent_files.hpp"
#include "scheme.hpp"
#include "analyze.hpp"

namespace {
bool autosave_file (url name) {
  return !is_rooted_tmfs (name) && ends (as_string (name, URL_SYSTEM), "~");
}
}

array<url> recent_file_list (int limit) {
  array<url> result;
  // The kernel owns interactive-history persistence and most-recent ordering.
  object entries= call ("learned-interactive", object ("recent-buffer"));
  for (; !is_null (entries) && N(result) < limit; entries= cdr (entries)) {
    url name= url_unix (as_string (cdr (car (car (entries)))));
    if (is_rooted_tmfs (name) &&
        !starts (as_string (name, URL_UNIX), "tmfs://part/")) continue;
    if (!autosave_file (name)) result << name;
  }
  return result;
}

void buffer_notify_recent (url name) {
  if (autosave_file (name)) return;
  call ("learn-interactive", symbol_object ("recent-buffer"),
        list_object (cons (object ("0"), object (as_string (name, URL_UNIX)))));
  call ("save-learned");
}

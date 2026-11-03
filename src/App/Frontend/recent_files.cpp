// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "App/Frontend/recent_files.hpp"
#include "App/Window/interactive_commands.hpp"
#include "analyze.hpp"

namespace {
bool autosave_file (url name) {
  return !is_rooted_tmfs (name) && ends (as_string (name, URL_SYSTEM), "~");
}
}

array<url> recent_file_list (int limit) {
  array<url> result;
  const auto entries= interactive_learned ("recent-buffer");
  for (const auto& entry: entries) {
    if (N(result) >= limit) break;
    const auto value= entry.find ("0");
    if (value == entry.end ()) continue;
    url name= url_unix (string (value->second.data (), value->second.size ()));
    if (is_rooted_tmfs (name) &&
        !starts (as_string (name, URL_UNIX), "tmfs://part/")) continue;
    if (!autosave_file (name)) result << name;
  }
  return result;
}

void buffer_notify_recent (url name) {
  if (autosave_file (name)) return;
  const string text= as_string (name, URL_UNIX);
  interactive_learn ("recent-buffer", {{"0", std::string (text.data (), N(text))}});
  interactive_history_save ();
}

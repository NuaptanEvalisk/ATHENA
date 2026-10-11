/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "hodarium_local_notifications.hpp"
#include <mutex>
#include <utility>

namespace athena::hodarium {
namespace {
std::mutex observer_mutex;
local_change_observer current_observer;
}
void set_local_change_observer (local_change_observer observer) {
  std::lock_guard lock (observer_mutex);
  current_observer= std::move (observer);
}
void notify_local_change (const std::filesystem::path& root, bool structure_changed) {
  local_change_observer observer;
  {
    std::lock_guard lock (observer_mutex);
    observer= current_observer;
  }
  if (observer) observer (root, structure_changed);
}
}

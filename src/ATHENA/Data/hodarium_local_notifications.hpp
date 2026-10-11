/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include <filesystem>
#include <functional>

namespace athena::hodarium {
using local_change_observer= std::function<void(const std::filesystem::path&, bool)>;
// Notifications invalidate optional in-process readers, never perform storage
// transactions. Observers can be invoked by a background publication worker.
void set_local_change_observer (local_change_observer observer);
void notify_local_change (const std::filesystem::path& root, bool structure_changed);
}

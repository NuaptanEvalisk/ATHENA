/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
void qtm_hodarium_initialize ();
void qtm_hodarium_reload ();
void qtm_hodarium_show ();
// Actor-safe, nonblocking handoff of successful durable saves only.
void qtm_hodarium_saved (const std::filesystem::path& path, std::string object,
  std::optional<std::string> predecessor, std::shared_ptr<const std::string> bytes);

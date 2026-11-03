/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include <filesystem>
#include <functional>
#include <string>

namespace athena::document_node {
// Background caller. Live trees are modified only by their BufferActor. The
// completion runs on the background/actor thread and carries no tree objects.
// Live changes remain undoable and follow the user's normal save policy.
void repair_source_identity (const std::filesystem::path& vault,
  const std::string& path, const std::string& expected,
  std::function<void(bool live, std::string error)> completed);
}

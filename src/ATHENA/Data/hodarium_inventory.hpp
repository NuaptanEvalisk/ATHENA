/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include "background_workers.hpp"
#include <memory>
#include <map>

namespace athena::hodarium {
struct inventoried_document {
  std::string path, object;
  filesystem::metadata revision;
  std::shared_ptr<const std::string> bytes;
};
struct source_inventory {
  std::optional<filesystem::metadata> root_revision;
  std::vector<inventoried_document> documents;
  std::vector<std::string> errors;
  struct cached_source { filesystem::metadata revision; std::string object; bool published= false; };
  std::map<std::string, cached_source> cache;
};
// Detached read-only trees stay on the calling thread. No source mutation,
// identity assignment, database access or editor/Guile calls occur here.
source_inventory inventory_sources (const std::filesystem::path& root,
  const std::atomic<bool>& cancelled,
  const std::map<std::string, source_inventory::cached_source>& previous= {},
  background::source_watch* watch= nullptr);
}

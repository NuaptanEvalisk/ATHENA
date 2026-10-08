/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace athena::hodarium {

// These identifiers and descriptors travel only inside authenticated E2E sessions.
struct revision {
  std::string id;
  std::string vault;
  std::string object;
  std::string origin_member;
  std::string format;
  std::uint32_t semantic_version= 1;
  std::string relative_path;
  bool deleted= false;
  std::vector<std::string> parents;
  std::string payload;
};

// Normalizes the parent set and binds all metadata and payload bytes to the ID.
revision seal_revision (revision value);

enum class ancestry { same, ancestor, descendant, concurrent };

// One owner thread per store. No editor access and no network side effects.
class revision_store {
public:
  explicit revision_store (const std::filesystem::path& database);
  ~revision_store ();
  revision_store (const revision_store&)= delete;
  revision_store& operator= (const revision_store&)= delete;

  // Parents must already be present. Receipt never changes the applied revision.
  bool receive (const revision& value);
  std::optional<revision> get (const std::string& id) const;
  std::vector<std::string> heads (const std::string& vault,
                                 const std::string& object) const;
  ancestry compare (const std::string& first, const std::string& second) const;
  // Multiple bases are possible; the merge UI must not invent a unique base.
  std::vector<std::string> merge_bases (const std::string& first,
                                       const std::string& second) const;
  std::optional<std::string> applied (const std::string& vault,
                                      const std::string& object) const;
  // Called only by the durable apply coordinator, after its history/apply barrier.
  bool record_applied (const std::string& id,
                       const std::optional<std::string>& expected);

private:
  sqlite3* db_= nullptr;
  bool ancestor_of (const std::string& first, const std::string& second) const;
};

} // namespace athena::hodarium

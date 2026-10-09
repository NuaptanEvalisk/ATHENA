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
struct revision_offer {
  revision metadata;
  std::uint64_t size= 0;
};
struct apply_intent {
  std::string operation, revision_id;
  std::optional<std::string> expected_revision;
  std::string source_path;
  // Null means the source did not exist, not an empty document.
  std::optional<std::string> source_fingerprint;
  std::int64_t protected_history_version= 0;
  bool completed= false;
};

// One owner thread per store. No editor access and no network side effects.
class revision_store {
public:
  explicit revision_store (const std::filesystem::path& database);
  ~revision_store ();
  revision_store (const revision_store&)= delete;
  revision_store& operator= (const revision_store&)= delete;

  // Parents must already be present. Receipt never changes the applied revision.
  bool receive (const revision& value);
  // Metadata carries no payload. A durable offset acknowledges committed bytes,
  // not document application; callers must authorize the Vault before staging.
  std::uint64_t begin_receive (const revision& metadata, std::uint64_t size);
  std::uint64_t receive_chunk (const std::string& id, std::uint64_t offset,
                              const std::string& bytes);
  bool finish_receive (const std::string& id);
  void discard_receive (const std::string& id);
  std::optional<revision_offer> offer (const std::string& id) const;
  std::string payload_chunk (const std::string& id, std::uint64_t offset,
                             std::uint32_t limit= 256*1024) const;
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
  // The coordinator must first obtain a durable File History protection for
  // existing source bytes. These APIs never inspect or mutate document files.
  bool prepare_apply (const apply_intent& intent);
  std::optional<apply_intent> application (const std::string& operation) const;
  std::vector<apply_intent> pending_applications (const std::string& vault,
    const std::string& after= {}, std::uint32_t limit= 64) const;
  // Call only after actor/disk checks, durable publication and rename/delete
  // cleanup. The observed target fingerprint is null only for a tombstone.
  bool finish_apply (const std::string& operation,
                     const std::optional<std::string>& observed_fingerprint);

private:
  sqlite3* db_= nullptr;
  bool ancestor_of (const std::string& first, const std::string& second) const;
};

} // namespace athena::hodarium

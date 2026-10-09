/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include "decisions.hpp"
#include "vault_secret.hpp"

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
struct revision_head {
  std::int64_t sequence;
  std::string id;
};
struct revision_conflict {
  std::string object, representative_path;
  std::int64_t heads= 0;
};
struct revision_path_collision {
  std::string path;
  // Current eligible physical-file heads, ordered by object then revision ID.
  // Payloads are empty; separate objects must never be merged into one identity.
  std::vector<revision> heads;
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
  // Call only with bytes from a successful durable local save. The caller
  // supplies no ID/parents: a save continues only its expected applied branch,
  // never all received heads. Null means a stale base; unchanged saves return
  // the existing ID. Publishing the revision and applied pointer is atomic.
  std::optional<std::string> capture_saved (revision snapshot,
    const std::optional<std::string>& expected,
    const std::optional<std::string>& predecessor_fingerprint= {});
  // Metadata carries no payload. A durable offset acknowledges committed bytes,
  // not document application; callers must authorize the Vault before staging.
  std::uint64_t begin_receive (const revision& metadata, std::uint64_t size);
  std::uint64_t receive_chunk (const std::string& id, std::uint64_t offset,
                              const std::string& bytes);
  bool finish_receive (const std::string& id);
  void discard_receive (const std::string& id);
  std::optional<revision_offer> offer (const std::string& id) const;
  std::string payload_fingerprint (const std::string& id);
  std::string payload_chunk (const std::string& id, std::uint64_t offset,
                             std::uint32_t limit= 256*1024) const;
  std::optional<revision> get (const std::string& id) const;
  std::vector<std::string> heads (const std::string& vault,
                                 const std::string& object) const;
  bool contains (const std::string& vault, const std::string& id) const;
  // Receipt is not permission to consume concurrent branches. Approval is
  // checked against both the scoped Vault secret and the authority signature.
  bool eligible (const std::string& id) const;
  bool accept_resolution (const std::string& id, const authority_pin& pin,
    const vault_secret& secret, const decision_evidence& evidence);
  std::vector<revision> pending_resolutions (const std::string& vault,
    const std::string& after= {}, std::uint32_t limit= 64) const;
  // Connection-local cursors over append-only receipt order. Parents always
  // precede children. Restart discovery from zero after reconnect or DB restore.
  std::int64_t inventory_tip (const std::string& vault) const;
  std::vector<revision_head> inventory_heads (const std::string& vault,
    std::int64_t after, std::int64_t through, std::uint32_t limit= 64) const;
  ancestry compare (const std::string& first, const std::string& second) const;
  // Multiple bases are possible; the merge UI must not invent a unique base.
  std::vector<std::string> merge_bases (const std::string& first,
                                       const std::string& second) const;
  std::optional<std::string> applied (const std::string& vault,
                                      const std::string& object) const;
  // Metadata only, ordered by object identity; receipt-only heads are excluded.
  std::vector<revision> applied_page (const std::string& vault,
    const std::string& after_object= {}, std::uint32_t limit= 64) const;
  // Explicit recovery only, after readmission supplies a NEW member identity.
  // Both journals must be quiescent on this owner thread and physically distinct.
  // Rebase completed old deletion baselines as independent local roots in this
  // journal; never import old parents/approvals or alter files/applied pointers.
  // Run before enabling application/replication, and complete the fresh local
  // inventory before enabling application. Reappeared objects then remain
  // independent roots conflicting with the deletion, not implicit restorations.
  // Page by the last returned object's identity. Empty page means done; repeated
  // pages with the same new_member are idempotent. Returned payloads are empty.
  std::vector<revision> recover_applied_deletions (const revision_store& previous,
    const std::string& vault, const std::string& new_member,
    const std::string& after_object= {}, std::uint32_t limit= 64);
  // Single current heads not yet applied, excluding objects with pending
  // recovery. Callers must recheck the head and source at the commit boundary.
  std::vector<revision> application_candidates (const std::string& vault,
    const std::string& after_object= {}, std::uint32_t limit= 64) const;
  // Metadata-only discovery for manual resolution; never chooses a branch.
  std::vector<revision_conflict> conflicts (const std::string& vault,
    const std::string& after_object= {}, std::uint32_t limit= 64) const;
  // Lexical path cursor, limit counts paths (not individual heads). Includes
  // only surviving native-document/resource heads; excludes logical records.
  std::vector<revision_path_collision> path_collisions (const std::string& vault,
    const std::string& after= {}, std::uint32_t limit= 64) const;
  // Called only by the durable apply coordinator, after its history/apply barrier.
  bool record_applied (const std::string& id,
                       const std::optional<std::string>& expected);
  // The coordinator must first obtain a durable File History protection for
  // existing source bytes. These APIs never inspect or mutate document files.
  bool prepare_apply (const apply_intent& intent);
  // Only the apply owner may call this, before any filesystem mutation. History
  // protection is retained; the intent no longer blocks subsequent local saves.
  bool abandon_unpublished_apply (const std::string& operation);
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

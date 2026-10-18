/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace athena::document {

// External Hodarium records are not inside the Vault directory. This audit is
// deliberately read-only: a signed, content-addressed revision cannot have
// its XML payload replaced without changing its revision ID, causal parents,
// applied pointer and any conflict approval referring to it.
struct vault_v3_hodarium_audit {
  // SHA-256 fingerprint over all read-only audited databases, including their
  // names, in deterministic order. A count-only comparison cannot detect a
  // committed change to an existing device binding or a revision journal.
  std::string state_fingerprint;
  std::size_t bindings= 0;
  std::size_t enabled_bindings= 0;
  std::size_t revision_stores= 0;
  std::size_t revisions_verified= 0;
  std::size_t v2_document_revisions= 0;
  std::size_t applied_revisions= 0;
  std::size_t pending_applications= 0;
  std::size_t incomplete_receives= 0;
  std::size_t conflict_approvals= 0;
  // True even for a disabled binding, because historical remotes/causal
  // descendants may still refer to the old v2 digest graph.
  bool coordinated_cutover_required= false;
};

// STATE_DIR is an explicitly supplied private Hodarium state directory,
// containing device.sqlite, revisions-*.sqlite and (if present) the group's
// membership/conflicts/decisions generation sidecars. No auto-discovery or
// network activity; no writes, journal creation, WAL/SHM or checkpoints.
// A nonempty WAL is a blocker: immutable SQLite must not ignore pending
// source journal changes. Source Vault and external state remain untouched.
vault_v3_hodarium_audit audit_external_hodarium_v3 (
  const std::filesystem::path& vault,
  const std::filesystem::path& external_state);

} // namespace athena::document

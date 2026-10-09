/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once

#include <filesystem>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace athena::hodarium {

// Logical objects, not rows/pages or database paths. All payloads are bounded,
// canonical versioned JSON arrays. Absence is represented by the journal's
// tombstone, not by publishing an empty replacement database.
struct logical_database_object {
  std::string key;
  std::string format;
  std::string payload;
};

struct logical_database_change {
  std::string key;
  std::optional<std::string> expected;
  std::optional<std::string> replacement;
};

// Pure validation; throws a diagnostic before an authority accepts a proposal.
// Structured names are parsed as data, never evaluated. Derived objects use
// adapter keys here (without the coordinator's immutable payload-hash suffix).
void validate_logical_object (const logical_database_object& object);

enum class logical_apply_result { applied, unchanged, stale, dependency_missing };

// Caller owns the captured Vault/database paths and runs these off the GUI
// thread. Export uses one read transaction; apply compares every expected
// object and changes the graph in one write transaction. The caller must first
// durably protect expected payloads in File History and after successful apply
// invalidate the active Vault's namespace ontology. No filesystem program is
// executed by these adapters, and received sorter files remain untrusted.
std::vector<logical_database_object> export_namespace_objects (
  const std::filesystem::path& database, const std::filesystem::path& vault_root);
logical_apply_result apply_namespace_objects (
  const std::filesystem::path& database, const std::filesystem::path& vault_root,
  const std::vector<logical_database_change>& changes,
  const std::function<bool()>& permitted= {});
logical_database_object rename_namespace_object_resources (
  const logical_database_object& object, const std::string& old_path,
  const std::string& new_path, bool directory);

// Derived vectors never replace source/chunk identities, model configuration,
// scan state, document revisions or manual decisions. Pagination is lexical by
// the opaque object key and each page is an internally consistent snapshot.
std::vector<logical_database_object> export_embedding_objects (
  const std::filesystem::path& database, const std::string& after= {},
  unsigned limit= 64);
logical_apply_result apply_embedding_object (
  const std::filesystem::path& database, const logical_database_object& object,
  const std::function<bool()>& permitted= {});

// Range results are immutable derived checkpoints, consumed only after the
// native extractor recomputes the complete model/input fingerprint. They do
// not replace artifact bindings, manual decisions or current range selections.
std::vector<logical_database_object> export_artifact_objects (
  const std::filesystem::path& vault_root, const std::string& after= {},
  unsigned limit= 64);
logical_apply_result apply_artifact_object (
  const std::filesystem::path& vault_root, const logical_database_object& object,
  const std::function<bool()>& permitted= {});

// Explicit rejected names, including structured math, are authoritative. This
// small ordered-settings object has its own revision/conflict, separate from
// machine-local preferences and the rebuildable artifact database.
logical_database_object export_artifact_rejections (
  const std::filesystem::path& vault_root);
logical_apply_result apply_artifact_rejections (
  const std::filesystem::path& vault_root, const logical_database_change& change,
  const std::function<bool()>& permitted= {});

bool material_logical_format (const std::string& format);

// Local disposable outbox, maintained by SQLite triggers for every writer.
// Initialization seeds existing rows once; subsequent pages never scan vector BLOBs.
void initialize_embedding_changes (const std::filesystem::path& database);
struct embedding_change_page {
  std::vector<logical_database_object> objects;
  std::int64_t through= 0;
  bool more= false;
};
embedding_change_page export_embedding_changes (
  const std::filesystem::path& database, std::int64_t after, unsigned limit= 64);
std::vector<logical_database_object> export_material_objects (
  const std::filesystem::path& vault_root);
logical_apply_result apply_material_object (
  const std::filesystem::path& vault_root, const logical_database_change& change,
  const std::function<bool()>& permitted= {});

} // namespace athena::hodarium

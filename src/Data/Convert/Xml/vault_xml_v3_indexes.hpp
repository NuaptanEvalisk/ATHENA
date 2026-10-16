/******************************************************************************
* MODULE     : vault_xml_v3_indexes.hpp
* DESCRIPTION: Canonicalization of staged Vault tree-bearing indexes
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/
#pragma once

#include "ATHENA/Data/vaultfile_json.hpp"
#include <cstddef>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace athena::document {

struct v3_index_migration_stats {
  std::size_t tree_fragments= 0;
  std::size_t warnings= 0;
  std::size_t invalidated_hashes= 0;
  std::size_t invalidated_range_rows= 0;
  std::vector<std::filesystem::path> invalidated_cache_files;
};

struct v3_title_filter_migration {
  std::filesystem::path relative;
  std::string bytes;
  std::size_t fragments= 0;
  std::size_t warnings= 0;
};

// Read-only preparation for the title rejection list, which contains native
// fragments but is not a disposable database index. No notification or writes.
std::optional<v3_title_filter_migration> prepare_vault_title_filter_v3 (
  const std::filesystem::path& root, const AthenaVaultfileInfo& info, bool strict);

// Exact, documented rebuildable files only; no directory-wide deletion.
// Read-only validation shared by preflight and private candidate invalidation.
// Maintenance source snapshots and document history are never caches.
std::vector<std::filesystem::path> vault_xml_v3_derived_cache_files (
  const std::filesystem::path& root, const AthenaVaultfileInfo& info);

// Called only after the Vault has been cloned into a disconnected snapshot.
// Never opens source Vault index files for writing. Preserves Artifact identity,
// user identity decisions, UUID history, and relational links.
v3_index_migration_stats migrate_staged_vault_indexes_v3 (
  const std::filesystem::path& staged_root,
  const AthenaVaultfileInfo& info, bool strict);

// Preflight-only: verify that active Artifact and extracted-identity bindings
// will not point to any v2 source node discarded by v3 normalization. Opens
// the input's SQLite databases in immutable read-only mode (never creates WAL
// or SHM files). Historical evidence is intentionally not rewritten here.
void audit_vault_uuid_references_v3 (
  const std::filesystem::path& original_root,
  const AthenaVaultfileInfo& info,
  const std::set<std::string>& removed_source_ids);

} // namespace athena::document

/******************************************************************************
* MODULE     : vault_format_upgrade.hpp
* DESCRIPTION: Offline whole-vault UTF-8/XML migration with atomic directory commit
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include "vault_xml_v3_hodarium.hpp"
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace athena::document {
using vault_upgrade_progress= std::function<void (
  const char* phase, std::size_t done, std::size_t total, const std::string& path)>;
struct vault_upgrade_result {
  std::filesystem::path backup;
  std::size_t converted= 0, already_xml= 0;
  bool durable= true;
};
// Offline only. Progress may throw to cancel BEFORE commit. Once exchanged,
// the complete original vault remains at backup even if directory fsync fails.
vault_upgrade_result upgrade_vault_format (
  const std::filesystem::path&, const vault_upgrade_progress& = {});
int upgrade_vault_format_cli (const std::filesystem::path&);

struct vault_node_model_upgrade_result {
  std::filesystem::path backup;
  std::size_t migrated= 0, already_v2= 0;
  std::size_t references_rewritten= 0, artifact_bindings= 0;
  std::size_t references_direct_resolved= 0;
  std::size_t references_hint_resolved= 0;
  std::size_t references_failed= 0;
  std::size_t stale_artifacts_pruned= 0;
  std::size_t legacy_anchors_removed= 0;
  bool durable= true;
};
struct vault_node_model_upgrade_options {
  bool drop_all_labels= false;
};
// Separate offline stage after the legacy format upgrader. Input documents must
// already be native UTF-8 XML v1. The result is an XML-v2 source-identity vault.
vault_node_model_upgrade_result upgrade_vault_node_model (
  const std::filesystem::path&, const vault_upgrade_progress& = {},
  const vault_node_model_upgrade_options& = {});
int upgrade_vault_node_model_cli (
  const std::filesystem::path&,
  const vault_node_model_upgrade_options& = {});

struct vault_xml_v3_preflight_result {
  std::size_t documents= 0, warnings= 0;
  std::size_t delimiters= 0, scripts= 0, operators= 0, blocks= 0, grids= 0;
  std::size_t history_versions= 0, protected_history_versions= 0;
};
// Explicit and entirely read-only. No XML-v2 fallback in the production v3
// reader. A destructive whole-vault commit must not be exposed until runtime,
// Artifact, history and Hodarium cutover contracts are validated together.
vault_xml_v3_preflight_result preflight_vault_xml_v3 (
  const std::filesystem::path&, bool strict = false,
  const vault_upgrade_progress& = {});
int preflight_vault_xml_v3_cli (const std::filesystem::path&, bool strict = false);

// XML v3 is a native AST change, not an XML spelling update. The explicit
// preflight never writes to the source Vault, even on failure. Write-mode
// publication is gated separately on complete history, derived-data and
// Hodarium invariants; it must never quietly fall back to opening XML v2.
struct vault_xml_v3_upgrade_options {
  bool check_only= false;
  bool strict= false;
  std::optional<std::filesystem::path> hodarium_state;
};
struct vault_xml_v3_upgrade_result {
  std::filesystem::path backup;
  std::size_t documents= 0, warnings= 0;
  std::size_t delimiters= 0, scripts= 0, operators= 0;
  std::size_t math_blocks= 0, grids= 0;
  std::size_t history_versions= 0, protected_history_versions= 0;
  std::size_t index_tree_fragments= 0;
  std::optional<vault_v3_hodarium_audit> hodarium_external;
  bool durable= true;
};
vault_xml_v3_upgrade_result upgrade_vault_xml_v3 (
  const std::filesystem::path&,
  const vault_xml_v3_upgrade_options&,
  const vault_upgrade_progress& = {});
int upgrade_vault_xml_v3_cli (
  const std::filesystem::path&,
  const vault_xml_v3_upgrade_options&);

struct vault_xml_v3_stage_result {
  std::filesystem::path candidate;
  vault_xml_v3_upgrade_result checked;
  std::size_t migrated_history_versions= 0;
  std::size_t migrated_index_trees= 0;
  std::size_t invalidated_cache_hashes= 0;
};
// Creates a disconnected candidate at a new, explicitly supplied path.
// The original Vault is never modified or exchanged. The candidate is NOT
// safe to open until native runtime and Hodarium format-v3 cutover is ready.
vault_xml_v3_stage_result stage_vault_xml_v3_candidate (
  const std::filesystem::path& source,
  const std::filesystem::path& empty_candidate,
  bool strict= false,
  const vault_upgrade_progress& = {},
  const std::optional<std::filesystem::path>& external_hodarium= {});
int stage_vault_xml_v3_cli (
  const std::filesystem::path& original_vault,
  const std::filesystem::path& empty_candidate,
  bool strict= false,
  const std::optional<std::filesystem::path>& external_hodarium= {});

struct vault_upgrade_revision {
  std::string path, semantic_hash;
  long long size, mtime;
};
// Called only on the private snapshot, before any document bytes are replaced.
void prepare_vault_upgrade_indexes (const std::filesystem::path&,
  const std::vector<vault_upgrade_revision>&, const vault_upgrade_progress& = {});
}

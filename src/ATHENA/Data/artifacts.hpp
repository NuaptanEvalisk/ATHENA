/******************************************************************************
* MODULE     : artifacts.hpp
* DESCRIPTION: Semantic mathematical artifact index for ATHENA vaults
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef ATHENA_ARTIFACTS_HPP
#define ATHENA_ARTIFACTS_HPP

#include "tree.hpp"
#include "path.hpp"
#include "ATHENA/Data/artifact_range_llm.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

struct AthenaArtifactRecord {
  std::string uuid;
  std::string type;
  std::string origin;
  std::string content_uuid;
  std::string proof_uuid;
  std::string relative_path;
  // User-facing text is UTF-8. keyword_tree and definition_candidates retain
  // TeXmacs' internal Cork encoding so they can be parsed back into trees.
  std::string anchor_stem;
  std::string display_text;
  // Semantic names are extracted from artifact content.  anchor_stem is only
  // a source navigation locator and must never be treated as a user-facing
  // name.  Several artifacts may intentionally share the same name.
  std::vector<std::string> semantic_names;
  // Serialized native names, aligned with semantic_names (the display
  // projection). Empty entries denote ordinary text, not mathematical markup.
  std::vector<std::string> semantic_name_trees;
  std::string keyword_tree;
  int keyword_occurrence= 0;
  std::vector<int> paragraph_offsets;
  std::string identity_focus;
  std::string identity_host;
  std::string identity_before;
  std::string identity_after;
  std::string identity_decision;
  std::string identity_evidence;
  std::string keyword_latex;
  std::vector<std::pair<int,std::string>> definition_candidates;
  int document_order= 0;
};

struct AthenaArtifactParagraphLocation {
  path parent;
  int focus_child= -1;
  int first_child= -1;
  int last_child= -1;
};

struct AthenaArtifactsBuildResult {
  size_t documents_seen= 0;
  size_t documents_changed= 0;
  size_t documents_deleted= 0;
  size_t enunciations= 0;
  size_t bold_texts= 0;
  size_t artifacts= 0;
};

enum class AthenaArtifactsBuildPhase {
  Preparing,
  Extracting,
  SelectingDefinitionRanges,
  WritingDatabase,
  Complete
};

struct AthenaArtifactsProgressEvent {
  AthenaArtifactsBuildPhase phase= AthenaArtifactsBuildPhase::Preparing;
  size_t current= 0;
  size_t total= 0;
  std::string path;
  std::string detail;
  size_t delegated_queued= 0;
  size_t delegated_running= 0;
};

using AthenaArtifactsProgress=
  std::function<bool(const AthenaArtifactsProgressEvent& event)>;

using AthenaArtifactRangeSelectionProgress=
  std::function<bool(size_t completed, size_t total,
                     size_t queued, size_t running)>;
using AthenaArtifactRangeSelector= std::function<bool (
  const std::vector<AthenaArtifactRangeRequest>& requests,
  std::vector<std::vector<int>>& results,
  const AthenaArtifactRangeSelectionProgress& progress,
  std::string& error)>;

struct AthenaArtifactsBuildOptions {
  AthenaArtifactRangeSelector range_selector;
};

bool athena_artifacts_build (
  const std::filesystem::path& vault_root,
  const std::vector<std::filesystem::path>& requested_documents,
  bool full_vault, const AthenaArtifactsProgress& progress,
  AthenaArtifactsBuildResult& result, std::string& error,
  const AthenaArtifactsBuildOptions& options= {});

bool athena_artifacts_build_active_vault (
  bool current_document_only, const AthenaArtifactsProgress& progress,
  AthenaArtifactsBuildResult& result, std::string& error,
  const AthenaArtifactsBuildOptions& options= {});

bool athena_artifacts_query (const std::filesystem::path& vault_root,
                             std::vector<AthenaArtifactRecord>& records,
                             std::string& error);

bool athena_artifact_query_uuid (const std::filesystem::path& vault_root,
                                 const std::string& uuid,
                                 AthenaArtifactRecord& record, bool& found,
                                 std::string& error);

bool athena_artifacts_mark_document_stale (
  const std::filesystem::path& vault_root, const std::string& relative_path,
  std::string& error);

// Preserve artifact identity when ATHENA itself renames a Vault document or
// directory.  External filesystem moves have no trustworthy lineage signal
// and are intentionally handled as deletion plus insertion by the builder.
bool athena_artifacts_apply_path_rename (
  const std::filesystem::path& vault_root, const std::string& old_path,
  const std::string& new_path, bool is_directory, std::string& error);

// Exposed for focused extraction tests.  No database or vault state is needed.
bool athena_artifacts_extract_document (const tree& document,
                                        const std::string& relative_path,
                                        std::vector<AthenaArtifactRecord>& records,
                                        std::string& error);

// Reconstruct the source paragraph span recorded for a bold-text artifact.
// This deliberately shares the extractor's paragraph model so database
// records cannot drift from inserter navigation.
bool athena_artifact_locate_paragraph (
  const tree& document, const AthenaArtifactRecord& record,
  AthenaArtifactParagraphLocation& location, std::string& error);

bool athena_artifact_locate_source (
  const tree& document, const AthenaArtifactRecord& record,
  path& source_path, std::string& error);

bool athena_artifact_is_defining_occurrence (
  const tree& document, path source_path,
  const AthenaArtifactRecord& record);

// Exclude the entire definition, including titles and nested body content.
bool athena_artifact_is_inside_definition (
  const tree& document, path source_path);

// Internal process worker used by the incremental parallel extractor.
bool athena_artifacts_run_extract_worker (
  const std::filesystem::path& manifest,
  const std::filesystem::path& output, std::string& error);

#endif // ATHENA_ARTIFACTS_HPP

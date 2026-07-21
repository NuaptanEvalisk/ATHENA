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
  std::string anchor_stem;
  std::string display_text;
  std::string keyword_tree;
  int keyword_occurrence= 0;
  std::vector<int> paragraph_offsets;
  std::string keyword_latex;
  std::vector<std::pair<int,std::string>> definition_candidates;
  int document_order= 0;
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

// Exposed for focused extraction tests.  No database or vault state is needed.
bool athena_artifacts_extract_document (const tree& document,
                                        const std::string& relative_path,
                                        std::vector<AthenaArtifactRecord>& records,
                                        std::string& error);

// Internal process worker used by the incremental parallel extractor.
bool athena_artifacts_run_extract_worker (
  const std::filesystem::path& manifest,
  const std::filesystem::path& output, std::string& error);

#endif // ATHENA_ARTIFACTS_HPP

/******************************************************************************
* MODULE     : artifact_range_llm.hpp
* DESCRIPTION: Small-LLM paragraph range selection for bold-text artifacts
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
******************************************************************************/

#ifndef ATHENA_ARTIFACT_RANGE_LLM_HPP
#define ATHENA_ARTIFACT_RANGE_LLM_HPP

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

struct AthenaArtifactRangeRequest {
  std::string keyword_latex;
  std::vector<std::pair<int,std::string>> paragraphs;
};

using AthenaArtifactRangePass= std::function<
  std::vector<std::vector<int>> (
    const std::vector<AthenaArtifactRangeRequest>&)>;

bool athena_artifact_range_model_available ();

std::string athena_artifact_range_model_path ();

bool athena_artifact_range_model_available (const std::string& model_path);

void athena_artifact_range_model_release ();

int athena_artifact_range_batch_size ();

std::string athena_artifact_definition_range_cache_contract (
  const std::string& model_path);

std::vector<int> athena_artifact_parse_definition_range_output (
  const std::string& output,
  const std::vector<std::pair<int,std::string>>& paragraphs,
  bool fallback_to_paragraph_zero);

std::vector<std::vector<int>> athena_artifact_select_definition_ranges (
  const std::vector<AthenaArtifactRangeRequest>& requests,
  const std::string& model_path, const std::atomic<bool>* cancelled,
  std::atomic<size_t>* completed, bool fallback_to_paragraph_zero= true);

std::vector<std::vector<int>>
athena_artifact_select_definition_ranges_progressively (
  const std::vector<AthenaArtifactRangeRequest>& requests,
  const AthenaArtifactRangePass& pass,
  const std::atomic<bool>* cancelled= nullptr,
  std::atomic<size_t>* completed= nullptr,
  bool fallback_to_paragraph_zero= true);

std::vector<int> athena_artifact_select_definition_range (
  const std::string& keyword_latex,
  const std::vector<std::pair<int,std::string>>& paragraphs);

std::vector<int> athena_artifact_select_definition_range (
  const std::string& keyword_latex,
  const std::vector<std::pair<int,std::string>>& paragraphs,
  const std::string& model_path, const std::atomic<bool>* cancelled);

#endif // ATHENA_ARTIFACT_RANGE_LLM_HPP

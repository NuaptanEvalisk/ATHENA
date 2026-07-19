/******************************************************************************
* MODULE     : artifact_range_llm.hpp
* DESCRIPTION: Small-LLM paragraph range selection for bold-text artifacts
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
******************************************************************************/

#ifndef ATHENA_ARTIFACT_RANGE_LLM_HPP
#define ATHENA_ARTIFACT_RANGE_LLM_HPP

#include <atomic>
#include <string>
#include <utility>
#include <vector>

bool athena_artifact_range_model_available ();

std::string athena_artifact_range_model_path ();

bool athena_artifact_range_model_available (const std::string& model_path);

void athena_artifact_range_model_release ();

std::vector<int> athena_artifact_select_definition_range (
  const std::string& keyword_latex,
  const std::vector<std::pair<int,std::string>>& paragraphs);

std::vector<int> athena_artifact_select_definition_range (
  const std::string& keyword_latex,
  const std::vector<std::pair<int,std::string>>& paragraphs,
  const std::string& model_path, const std::atomic<bool>* cancelled);

#endif // ATHENA_ARTIFACT_RANGE_LLM_HPP

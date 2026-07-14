/******************************************************************************
* MODULE     : artifact_range_llm.hpp
* DESCRIPTION: Small-LLM paragraph range selection for bold-text artifacts
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
******************************************************************************/

#ifndef ATHENA_ARTIFACT_RANGE_LLM_HPP
#define ATHENA_ARTIFACT_RANGE_LLM_HPP

#include <string>
#include <utility>
#include <vector>

bool athena_artifact_range_model_available ();

std::vector<int> athena_artifact_select_definition_range (
  const std::string& keyword_latex,
  const std::vector<std::pair<int,std::string>>& paragraphs);

#endif // ATHENA_ARTIFACT_RANGE_LLM_HPP

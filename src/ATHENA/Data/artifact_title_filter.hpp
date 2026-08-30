/******************************************************************************
* MODULE     : artifact_title_filter.hpp
* DESCRIPTION: Per-vault artifact title rejection list
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#ifndef ATHENA_ARTIFACT_TITLE_FILTER_HPP
#define ATHENA_ARTIFACT_TITLE_FILTER_HPP

#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

struct AthenaArtifactTitleFilter {
  std::vector<std::string> entries;
  std::unordered_set<std::string> normalized;
};

AthenaArtifactTitleFilter athena_artifact_title_filter_defaults ();
AthenaArtifactTitleFilter athena_artifact_title_filter_from_entries (
  const std::vector<std::string>& entries);
bool athena_artifact_title_filter_contains (
  const AthenaArtifactTitleFilter& filter, const std::string& candidate_utf8);
std::string athena_artifact_title_filter_fingerprint (
  const AthenaArtifactTitleFilter& filter);
bool athena_artifact_title_filter_read (
  const std::filesystem::path& vault_root, AthenaArtifactTitleFilter& filter,
  std::string& error);
bool athena_artifact_title_filter_write (
  const std::filesystem::path& vault_root,
  const std::vector<std::string>& entries, std::string& error);

#endif // ATHENA_ARTIFACT_TITLE_FILTER_HPP

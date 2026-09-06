/******************************************************************************
* MODULE     : artifact_radioactive_links.hpp
* DESCRIPTION: Fast automatic links to stable semantic artifacts
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#ifndef ATHENA_ARTIFACT_RADIOACTIVE_LINKS_HPP
#define ATHENA_ARTIFACT_RADIOACTIVE_LINKS_HPP

#include "ATHENA/Data/artifacts.hpp"
#include "url.hpp"
#include "string.hpp"

#include <string>
#include <memory>
#include <vector>

struct AthenaArtifactTitleFilter;

struct AthenaArtifactRadioactiveMatch {
  int start= 0;
  int end= 0;
  std::vector<std::string> uuids;
  std::string disambiguation_key;
};

struct AthenaArtifactRadioactiveTreeMatch {
  path start;
  path end;
  AthenaArtifactRadioactiveMatch link;
};

class AthenaArtifactRadioactiveMatcher {
public:
  explicit AthenaArtifactRadioactiveMatcher (
    const std::vector<AthenaArtifactRecord>& records);
  AthenaArtifactRadioactiveMatcher (
    const std::vector<AthenaArtifactRecord>& records,
    const AthenaArtifactTitleFilter& filter);
  ~AthenaArtifactRadioactiveMatcher ();

  AthenaArtifactRadioactiveMatcher (
    const AthenaArtifactRadioactiveMatcher&) = delete;
  AthenaArtifactRadioactiveMatcher& operator= (
    const AthenaArtifactRadioactiveMatcher&) = delete;

  std::vector<AthenaArtifactRadioactiveMatch> matches (string text) const;
  std::vector<AthenaArtifactRadioactiveTreeMatch> matches_tree (const tree& text) const;

private:
  struct Impl;
  std::shared_ptr<const Impl> impl;
};

std::string athena_artifact_radioactive_destination (
  const AthenaArtifactRadioactiveMatch& match);

string athena_artifact_radioactive_name (const AthenaArtifactRecord& record);

std::string athena_artifact_radioactive_key (
  const AthenaArtifactRecord& record);

std::vector<AthenaArtifactRadioactiveMatch>
athena_artifact_radioactive_matches (string text);

std::vector<AthenaArtifactRadioactiveTreeMatch>
athena_artifact_radioactive_matches_tree (const tree& text);

std::vector<AthenaArtifactRadioactiveMatch>
athena_artifact_radioactive_matches_for_records (
  const std::vector<AthenaArtifactRecord>& records, string text);

bool athena_artifact_radioactive_record (
  const std::string& uuid, AthenaArtifactRecord& record);

bool athena_artifact_radioactive_records_for_key (
  const std::string& key, std::vector<AthenaArtifactRecord>& records);

bool athena_artifact_radioactive_is_defining_occurrence (
  const AthenaArtifactRadioactiveMatch& match, url current_file,
  const tree& document, path source_path);

tree athena_artifact_radioactive_suppress_definitions (
  const tree& document);

void athena_artifact_radioactive_invalidate ();

#endif // ATHENA_ARTIFACT_RADIOACTIVE_LINKS_HPP

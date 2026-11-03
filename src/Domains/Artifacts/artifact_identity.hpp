/******************************************************************************
* MODULE     : artifact_identity.hpp
* DESCRIPTION: Conservative cross-build artifact identity association
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#ifndef ATHENA_ARTIFACT_IDENTITY_HPP
#define ATHENA_ARTIFACT_IDENTITY_HPP

#include <string>
#include <vector>

struct AthenaArtifactIdentityObservation {
  std::string uuid;
  std::string origin;
  std::string type;
  std::string anchor;
  std::string focus;
  std::string host;
  std::string before;
  std::string after;
  std::string display;
  int document_order= 0;
};

enum class AthenaArtifactIdentityDecisionKind {
  Matched,
  New,
  Ambiguous
};

struct AthenaArtifactIdentityDecision {
  AthenaArtifactIdentityDecisionKind kind=
    AthenaArtifactIdentityDecisionKind::New;
  int old_index= -1;
  std::string evidence;
  long long score= 0;
  long long old_margin= 0;
  long long new_margin= 0;
  long long global_delta= 0;
};

struct AthenaArtifactIdentityResult {
  std::vector<AthenaArtifactIdentityDecision> decisions;
  std::vector<int> deleted_old_indices;
};

AthenaArtifactIdentityResult athena_artifact_associate_identities (
  const std::vector<AthenaArtifactIdentityObservation>& old_observations,
  const std::vector<AthenaArtifactIdentityObservation>& new_observations);

const char* athena_artifact_identity_decision_name (
  AthenaArtifactIdentityDecisionKind kind);

#endif // ATHENA_ARTIFACT_IDENTITY_HPP

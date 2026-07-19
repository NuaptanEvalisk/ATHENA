/******************************************************************************
* MODULE     : vault_maintenance_pass_artifacts.cpp
* DESCRIPTION: Incremental ATHENA artifact indexing maintenance pass
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
******************************************************************************/

#include "ATHENA/Data/vault_maintenance_internal.hpp"
#include "ATHENA/Data/artifacts.hpp"

VaultMaintenancePassResult
vault_maintenance_pass_build_artifacts (VaultMaintenanceContext& ctx) {
  AthenaArtifactsBuildResult built;
  std::string error;
  bool ok= athena_artifacts_build (
    ctx.root, {}, true,
    [] (const AthenaArtifactsProgressEvent& event) {
      std::string phase;
      switch (event.phase) {
      case AthenaArtifactsBuildPhase::Preparing: phase= "Preparing artifacts"; break;
      case AthenaArtifactsBuildPhase::Extracting: phase= "Extracting artifacts"; break;
      case AthenaArtifactsBuildPhase::SelectingDefinitionRanges:
        phase= "Selecting artifact ranges"; break;
      case AthenaArtifactsBuildPhase::WritingDatabase:
        phase= "Writing artifacts"; break;
      case AthenaArtifactsBuildPhase::Complete: phase= "Building artifacts"; break;
      }
      print_progress (event.current, event.total, phase, event.path);
      return true;
    }, built, error);
  finish_progress ();
  if (!ok) return VaultMaintenancePassResult::failure (error);
  ctx.summary.artifact_documents_seen= built.documents_seen;
  ctx.summary.artifact_documents_changed= built.documents_changed;
  ctx.summary.artifact_documents_deleted= built.documents_deleted;
  ctx.summary.artifact_enunciations= built.enunciations;
  ctx.summary.artifact_bold_texts= built.bold_texts;
  ctx.summary.artifacts_indexed= built.artifacts;
  log_info ("artifacts: inspected " + std::to_string (built.documents_seen) +
            " document(s), rebuilt " +
            std::to_string (built.documents_changed) + ", purged " +
            std::to_string (built.documents_deleted) + " deleted document(s)");
  return VaultMaintenancePassResult::success ();
}

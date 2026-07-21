/******************************************************************************
* MODULE     : vault_maintenance_pass_artifacts.cpp
* DESCRIPTION: Incremental ATHENA artifact indexing maintenance pass
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
******************************************************************************/

#include "ATHENA/Data/vault_maintenance_internal.hpp"
#include "ATHENA/Data/artifacts.hpp"
#include "QTMDelegationClient.hpp"

#include <limits>

VaultMaintenancePassResult
vault_maintenance_pass_build_artifacts (VaultMaintenanceContext& ctx) {
  AthenaArtifactsBuildResult built;
  std::string error;
  bool have_progress= false;
  AthenaArtifactsBuildPhase last_phase= AthenaArtifactsBuildPhase::Preparing;
  size_t last_current= std::numeric_limits<size_t>::max ();
  size_t last_total= std::numeric_limits<size_t>::max ();
  std::string last_path;
  AthenaArtifactsBuildOptions options;
  if (ctx.summary.artifact_delegation_enabled) {
    ctx.summary.artifact_delegation_attempted= true;
    QTMDelegationServer server;
    if (!qtm_delegation_selected_server (
          QString::fromStdString (ctx.summary.delegation_server), server))
      return VaultMaintenancePassResult::failure (
        "artifact delegation is enabled, but no server is configured");
    ctx.summary.delegation_server= server.url.toStdString ();
    options.range_selector=
      [server] (const std::vector<AthenaArtifactRangeRequest>& requests,
                std::vector<std::vector<int>>& results,
                const AthenaArtifactRangeSelectionProgress& update,
                std::string& selectorError) {
        QString qerror;
        bool ok= qtm_delegation_select_artifact_ranges (
          server, requests, results,
          [&] (size_t completed, size_t total, size_t queued, size_t running) {
            return !update || update (completed, total, queued, running);
          }, &qerror);
        if (!ok) selectorError= qerror.toStdString ();
        return ok;
      };
  }
  bool ok= athena_artifacts_build (
    ctx.root, {}, true,
    [&] (const AthenaArtifactsProgressEvent& event) {
      bool changed= !have_progress || event.phase != last_phase ||
                    event.current != last_current || event.total != last_total ||
                    event.path != last_path;
      if (!changed) return true;
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
      have_progress= true;
      last_phase= event.phase;
      last_current= event.current;
      last_total= event.total;
      last_path= event.path;
      return true;
    }, built, error, options);
  finish_progress ();
  if (!ok) return VaultMaintenancePassResult::failure (error);
  if (ctx.summary.artifact_delegation_attempted)
    ctx.summary.artifact_delegation_succeeded= true;
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

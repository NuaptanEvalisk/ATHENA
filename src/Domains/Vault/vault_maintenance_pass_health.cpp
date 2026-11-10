/******************************************************************************
* MODULE     : vault_maintenance_pass_health.cpp
* DESCRIPTION: Vault maintenance document health check pass
* COPYRIGHT  : (C) 2026  Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "Domains/Vault/vault_maintenance_internal.hpp"

#include "athena_platform.hpp"
#include "QTMDelegationClient.hpp"
#include "boot.hpp"
#include "convert.hpp"
#include "rag_embedding.hpp"
#include "rag_embedding_contract.hpp"
#include "rag_index.hpp"

#include <algorithm>
#include <filesystem>
#include <chrono>
#include <fstream>
#include <thread>
#include <unordered_map>
#include <string>
#include <utility>
#include <vector>

#if ATHENA_ENABLE_CLI && (defined(__unix__) || defined(__APPLE__))
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

static bool
is_legible_ath_file (const fs::path& path, std::string& reason) {
  tree document;
  if (read_document_file (path, document, reason)) return true;
  if (reason.empty ()) reason= "malformed ATHENA document";
  return false;
}

VaultMaintenancePassResult
vault_maintenance_pass_health_check (VaultMaintenanceContext& ctx) {
  std::vector<fs::path> docs = scan_ath_documents (ctx.root);
  ctx.summary.health_files_scanned = docs.size ();
  ctx.summary.health_files_failed = 0;

  log_info ("health check: scanning " + std::to_string (docs.size ()) +
            " .ath file(s)");

  std::vector<std::pair<fs::path, std::string>> malformed;
  for (size_t i=0; i<docs.size (); i++) {
    print_progress (i + 1, docs.size (), "Checking documents",
                    docs[i].filename ().string ());
    std::string reason;
    if (!is_legible_ath_file (docs[i], reason))
      malformed.push_back ({docs[i], reason});
  }
  finish_progress ();

  ctx.summary.health_files_failed = malformed.size ();
  if (!malformed.empty ()) {
    log_error ("health check: found " + std::to_string (malformed.size ()) +
               " malformed .ath file(s)");
    for (const auto& entry : malformed)
      log_error ("health check: malformed file: " + entry.first.string () +
                 " (" + entry.second + ")");
    return VaultMaintenancePassResult::failure (
      "malformed ATHENA documents: " + std::to_string (malformed.size ()));
  }

  log_info ("health check: all " + std::to_string (docs.size ()) +
            " .ath file(s) are legible");
  return VaultMaintenancePassResult::success ();
}

static athena::rag::RagStatus
rag_database_status (const fs::path& root) {
  athena::rag::RagConfig config;
  config.vault_root= root;
  config.db_path= fs::path (athena::rag::rag_default_db_path (root));
  config.load_embedding_model= false;
  config.progress= false;
  athena::rag::RagIndex index;
  if (!index.open (config)) return {};
  return index.status ();
}

static bool
realtime_npu_enabled () {
  return tm_to_std (get_preference ("rag realtime npu enabled", "off")) == "on";
}

static bool
maintenance_embedding_model (fs::path& model, std::string& reason) {
  const fs::path ordinary (tm_to_std (get_preference ("rag embedding model", "")));
  if (!realtime_npu_enabled ()) {
    model= ordinary;
    return true;
  }

  std::vector<fs::path> candidates;
  if (!ordinary.empty ()) candidates.push_back (ordinary);
  fs::path tokenizer (tm_to_std (get_preference ("rag npu tokenizer gguf", "")));
  if (!tokenizer.empty () && tokenizer != ordinary) candidates.push_back (tokenizer);
  for (const fs::path& candidate: candidates) {
    if (athena::rag::rag_embedding_space_id_for_model (candidate.string ()) ==
        athena::rag::bge_m3_embedding_space_id) {
      model= candidate;
      return true;
    }
  }
  reason= candidates.empty ()
    ? "realtime NPU is enabled but no BGE-M3 GGUF is configured for maintenance reconciliation"
    : "realtime NPU is enabled but the maintenance/tokenizer GGUF does not match the active BGE-M3 embedding space";
  return false;
}

static bool
run_local_rag_update (VaultMaintenanceContext& ctx,
                      const fs::path& embedding_model, std::string& error) {
  athena::rag::RagConfig config;
  config.vault_root= ctx.root;
  config.db_path= fs::path (athena::rag::rag_default_db_path (ctx.root));
  config.embedding_model= embedding_model;
  config.embedding_device= tm_to_std (
    get_preference ("rag embedding device", "auto"));
  config.force_reindex= false;
  config.progress= true;

  athena::rag::RagIndex index;
  if (!index.open (config)) {
    error= index.status ().last_error.empty ()
             ? "could not open the local RAG index"
             : index.status ().last_error;
    return false;
  }
  athena::rag::RagStatus before= index.status ();
  ctx.summary.rag_documents_before= before.document_count;
  ctx.summary.rag_chunks_before= before.chunk_count;
  if (!index.scan_once ()) {
    error= index.status ().last_error.empty ()
             ? "local incremental RAG scan failed"
             : index.status ().last_error;
    return false;
  }
  athena::rag::RagStatus after= index.status ();
  ctx.summary.rag_documents_after= after.document_count;
  ctx.summary.rag_chunks_after= after.chunk_count;
  if (!after.embedding_warning.empty ())
    ctx.warnings.push_back ("Continuous RAG: " + after.embedding_warning);
  ctx.summary.rag_result= "local incremental indexing succeeded";
  return true;
}

static bool
run_delegated_rag_update (VaultMaintenanceContext& ctx,
                          const fs::path& embedding_model,
                          std::string& error) {
  athena::rag::RagStatus before= rag_database_status (ctx.root);
  ctx.summary.rag_documents_before= before.document_count;
  ctx.summary.rag_documents_after= before.document_count;
  ctx.summary.rag_chunks_before= before.chunk_count;
  ctx.summary.rag_chunks_after= before.chunk_count;

  QTMDelegationServer server;
  if (!qtm_delegation_selected_server (
        QString::fromStdString (ctx.summary.delegation_server), server)) {
    error= "no configured RAG delegation server";
    return false;
  }
  ctx.summary.delegation_server= server.url.toStdString ();
  QString qerror;
  QString delegated_summary;
  if (!qtm_delegation_run_embedding (
        server, QString::fromStdString (ctx.root.string ()),
        QString::fromStdString (athena::rag::rag_default_db_path (ctx.root)),
        QString::fromStdString (embedding_model.string ()),
        QString::fromStdString (tm_to_std (
          get_preference ("rag embedding device", "auto"))),
        &delegated_summary, &qerror)) {
    error= qerror.isEmpty () ? "delegated embedding failed"
                             : qerror.toStdString ();
    return false;
  }
  athena::rag::RagStatus after= rag_database_status (ctx.root);
  ctx.summary.rag_documents_after= after.document_count;
  ctx.summary.rag_chunks_after= after.chunk_count;
  ctx.summary.rag_result= delegated_summary.toStdString ();
  return true;
}

VaultMaintenancePassResult
vault_maintenance_pass_continuous_rag (VaultMaintenanceContext& ctx) {
  if (!ctx.summary.rag_update_enabled) {
    log_info ("continuous RAG: disabled by preference");
    ctx.summary.rag_result= "disabled";
    return VaultMaintenancePassResult::success ("disabled by preference");
  }

  fs::path embedding_model;
  std::string compatibility_error;
  if (!maintenance_embedding_model (embedding_model, compatibility_error)) {
    ctx.summary.rag_result= compatibility_error + "; RAG reconciliation skipped";
    ctx.warnings.push_back ("Continuous RAG: " + ctx.summary.rag_result);
    log_info ("continuous RAG: " + ctx.summary.rag_result);
    return VaultMaintenancePassResult::success (ctx.summary.rag_result);
  }

  std::string error;
  if (!ctx.summary.rag_delegation_enabled) {
    log_info (realtime_npu_enabled ()
      ? "continuous RAG: reconciling realtime BGE-M3 index locally"
      : "continuous RAG: running incremental local indexing");
    if (run_local_rag_update (ctx, embedding_model, error))
      return VaultMaintenancePassResult::success (ctx.summary.rag_result);
    ctx.summary.rag_result= error;
    return VaultMaintenancePassResult::failure (error);
  }

  ctx.summary.rag_delegation_attempted= true;
  log_info (realtime_npu_enabled ()
    ? "continuous RAG: attempting delegated BGE-M3 reconciliation"
    : "continuous RAG: attempting delegated incremental embedding");
  if (run_delegated_rag_update (ctx, embedding_model, error)) {
    ctx.summary.rag_delegation_succeeded= true;
    return VaultMaintenancePassResult::success (ctx.summary.rag_result);
  }

  std::string delegated_error= "delegated RAG failed: " + error;
  ctx.summary.rag_result= delegated_error;
  if (ctx.summary.rag_fallback_policy == "fail-maintenance")
    return VaultMaintenancePassResult::failure (delegated_error);
  if (ctx.summary.rag_fallback_policy == "continue") {
    ctx.warnings.push_back (delegated_error + "; maintenance continued");
    log_info (delegated_error + "; continuing without RAG update");
    return VaultMaintenancePassResult::success (
      delegated_error + "; continued by policy");
  }

  ctx.summary.rag_local_fallback_used= true;
  ctx.warnings.push_back (delegated_error + "; using local fallback");
  log_info (delegated_error + "; running local fallback");
  std::string local_error;
  if (run_local_rag_update (ctx, embedding_model, local_error)) {
    ctx.summary.rag_result= delegated_error + "; local fallback succeeded";
    return VaultMaintenancePassResult::success (
      ctx.summary.rag_result);
  }
  ctx.summary.rag_result= delegated_error + "; local fallback failed: " +
                          local_error;
  return VaultMaintenancePassResult::failure (ctx.summary.rag_result);
}

/******************************************************************************
* MODULE     : vault_maintenance_pass_health.cpp
* DESCRIPTION: Vault maintenance document health check pass
* COPYRIGHT  : (C) 2026  Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "ATHENA/Data/vault_maintenance_internal.hpp"

#include "convert.hpp"

#include <algorithm>
#include <filesystem>
#include <chrono>
#include <fstream>
#include <thread>
#include <unordered_map>
#include <string>
#include <utility>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

static bool
is_legible_ath_file (const fs::path& path, std::string& reason) {
  std::string text;
  if (!read_file_bytes (path, text)) {
    reason = "failed to read";
    return false;
  }

  try {
    tree doc = texmacs_document_to_tree (std_to_tm (text));
    if (is_func (doc, _ERROR)) {
      reason = "malformed ATHENA document";
      return false;
    }
    return true;
  }
  catch (...) {
    reason = "parser exception";
    return false;
  }
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

static bool
tree_contains_table_of_contents (const tree& t) {
  if (is_compound (t, "table-of-contents") ||
      is_compound (t, "table-of-contents*"))
    return true;
  if (!is_compound (t)) return false;
  for (int i=0; i<N(t); i++)
    if (tree_contains_table_of_contents (t[i])) return true;
  return false;
}

static bool
document_contains_table_of_contents (const fs::path& path) {
  std::string text;
  if (!read_file_bytes (path, text)) return false;
  try {
    tree doc = texmacs_document_to_tree (std_to_tm (text));
    return !is_func (doc, _ERROR) && tree_contains_table_of_contents (doc);
  }
  catch (...) {
    return false;
  }
}

static uint64_t
document_fingerprint (const fs::path& path) {
  std::string text;
  if (!read_file_bytes (path, text)) return 0;
  uint64_t hash = 1469598103934665603ULL;
  for (unsigned char c: text) {
    hash ^= c;
    hash *= 1099511628211ULL;
  }
  return hash;
}

static int
toc_worker_jobs (size_t file_count, int requested) {
  if (file_count == 0) return 0;
  unsigned hw = std::max (1u, std::thread::hardware_concurrency ());
  unsigned jobs = requested < 1 ? hw : (unsigned) requested;
  return (int) std::max (1u, std::min (jobs, (unsigned) file_count));
}

#if defined(__unix__) || defined(__APPLE__)
struct TocWorker {
  fs::path document;
  fs::path marker;
  uint64_t before = 0;
};

static fs::path
current_executable_path () {
#if defined(__linux__)
  std::vector<char> buffer (4096);
  ssize_t n = readlink ("/proc/self/exe", buffer.data (), buffer.size () - 1);
  if (n > 0) {
    buffer[(size_t) n] = '\0';
    return fs::path (buffer.data ());
  }
#endif
  return fs::path ();
}

static pid_t
start_toc_worker (const fs::path& executable, const fs::path& document,
                  const fs::path& marker) {
  pid_t pid = fork ();
  if (pid != 0) return pid;
  std::string exe = executable.string ();
  std::string doc = document.string ();
  std::string mark = marker.string ();
  execl (exe.c_str (), exe.c_str (), "-H", "--skip-fonts-cache",
         "--vault-maintenance-toc-worker", doc.c_str (), mark.c_str (),
         (char*) nullptr);
  _exit (127);
}

static bool
run_toc_workers (const std::vector<fs::path>& docs,
                 VaultMaintenanceSummary& summary) {
  fs::path executable = current_executable_path ();
  if (executable.empty () || !fs::exists (executable)) {
    log_error ("update ToCs: cannot locate the running ATHENA executable");
    return false;
  }

  int jobs = toc_worker_jobs (docs.size (), summary.anchor_reader_processes);
  summary.toc_worker_processes = jobs;
  log_info ("update ToCs: processing " + std::to_string (docs.size ()) +
            " document(s) with " + std::to_string (jobs) +
            " ATHENA worker process(es)");

  std::unordered_map<pid_t, TocWorker> active;
  size_t next = 0;
  size_t completed = 0;
  bool ok = true;
  fs::path marker_root = fs::temp_directory_path ();
  long long parent = (long long) getpid ();

  while (completed < docs.size ()) {
    while (next < docs.size () && (int) active.size () < jobs) {
      fs::path marker = marker_root /
        ("athena-toc-worker-" + std::to_string (parent) + "-" +
         std::to_string (next) + ".status");
      std::error_code ec;
      fs::remove (marker, ec);
      TocWorker worker {docs[next], marker, document_fingerprint (docs[next])};
      pid_t pid = start_toc_worker (executable, worker.document, marker);
      if (pid < 0) {
        log_error ("update ToCs: failed to start worker for " +
                   compact_log_path (worker.document));
        summary.toc_failures++;
        ok = false;
        next++;
        completed++;
        continue;
      }
      active.emplace (pid, std::move (worker));
      next++;
    }

    int status = 0;
    pid_t pid = waitpid (-1, &status, 0);
    if (pid < 0) {
      log_error ("update ToCs: failed while waiting for worker processes");
      ok = false;
      break;
    }
    auto it = active.find (pid);
    if (it == active.end ()) continue;
    TocWorker worker = std::move (it->second);
    active.erase (it);
    completed++;

    std::string marker_text;
    bool marker_ok = read_file_bytes (worker.marker, marker_text) &&
                     trim_copy (marker_text) == "ok";
    std::error_code ec;
    fs::remove (worker.marker, ec);
    bool exited = WIFEXITED (status) && WEXITSTATUS (status) == 0;
    if (!exited || !marker_ok) {
      summary.toc_failures++;
      ok = false;
      log_error ("update ToCs: worker failed for " +
                 compact_log_path (worker.document));
    }
    else if (document_fingerprint (worker.document) != worker.before)
      summary.toc_files_updated++;
    print_progress (completed, docs.size (), "Updating tables of contents",
                    worker.document.filename ().string ());
  }
  finish_progress ();

  for (const auto& entry: active) kill (entry.first, SIGTERM);
  for (const auto& entry: active) {
    int status = 0;
    waitpid (entry.first, &status, 0);
    std::error_code ec;
    fs::remove (entry.second.marker, ec);
  }
  return ok;
}
#endif

VaultMaintenancePassResult
vault_maintenance_pass_update_tables_of_contents (VaultMaintenanceContext& ctx) {
  if (!ctx.summary.toc_update_enabled) {
    log_info ("update ToCs: disabled by preference");
    return VaultMaintenancePassResult::success ("disabled by preference");
  }

  std::vector<fs::path> all_docs = scan_ath_documents (ctx.root);
  ctx.summary.toc_files_scanned = all_docs.size ();
  std::vector<fs::path> docs;
  for (size_t i=0; i<all_docs.size (); i++) {
    print_progress (i + 1, all_docs.size (), "Finding tables of contents",
                    all_docs[i].filename ().string ());
    if (document_contains_table_of_contents (all_docs[i]))
      docs.push_back (all_docs[i]);
  }
  finish_progress ();
  ctx.summary.toc_files_containing_toc = docs.size ();
  if (docs.empty ())
    return VaultMaintenancePassResult::success ("no tables of contents found");

#if defined(__unix__) || defined(__APPLE__)
  if (run_toc_workers (docs, ctx.summary))
    return VaultMaintenancePassResult::success (
      "updated " + std::to_string (ctx.summary.toc_files_updated) + " of " +
      std::to_string (docs.size ()) + " ToC document(s)");
  return VaultMaintenancePassResult::failure (
    "table-of-contents workers failed for " +
    std::to_string (ctx.summary.toc_failures) + " document(s)");
#else
  return VaultMaintenancePassResult::failure (
    "parallel table-of-contents maintenance is unavailable on this platform");
#endif
}

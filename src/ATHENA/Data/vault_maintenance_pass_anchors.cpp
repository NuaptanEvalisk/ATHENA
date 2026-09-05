/******************************************************************************
* MODULE     : vault_maintenance_pass_anchors.cpp
* DESCRIPTION: Vault maintenance enunciation anchor pass
* COPYRIGHT  : (C) 2026  Felix
******************************************************************************/

#include "ATHENA/Data/vault_maintenance_internal.hpp"
#include "ATHENA/Data/vault_map_sqlite.hpp"

#include "scheme.hpp"
#include "url.hpp"

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <filesystem>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

struct AnchorMaintenanceFileResult {
  bool ok = false;
  bool changed = false;
  size_t wrapped = 0;
  size_t removed = 0;
  size_t headings = 0;
  size_t updated = 0;
  std::string message;
  std::string renames;
};

static std::vector<std::string>
split_tabs (const std::string& text) {
  std::vector<std::string> parts;
  size_t begin = 0;
  for (size_t i=0; i<=text.size (); i++) {
    if (i == text.size () || text[i] == '\t') {
      parts.push_back (text.substr (begin, i - begin));
      begin = i + 1;
    }
  }
  return parts;
}

static size_t
parse_count (const std::string& text) {
  try {
    size_t pos = 0;
    unsigned long long value = std::stoull (text, &pos);
    if (pos == text.size ()) return (size_t) value;
  }
  catch (...) {}
  return 0;
}

static AnchorMaintenanceFileResult
parse_anchor_maintenance_result (const std::string& result) {
  AnchorMaintenanceFileResult parsed;
  std::vector<std::string> parts = split_tabs (result);
  if (parts.size () < 7) {
    parsed.message = "malformed result";
    return parsed;
  }
  parsed.ok = parts[0] == "ok";
  parsed.wrapped = parse_count (parts[1]);
  parsed.removed = parse_count (parts[2]);
  parsed.headings = parse_count (parts[3]);
  parsed.updated = parse_count (parts[4]);
  parsed.changed = parts[5] == "1";
  parsed.message = parts[6];
  parsed.renames = parts.size () >= 8 ? parts[7] : "";
  if (!parsed.ok && parsed.message.empty ()) parsed.message = "failed";
  return parsed;
}

static size_t
rewrite_map_anchor_references (const fs::path& root, const fs::path& doc,
                               const std::string& renames) {
  if (renames.empty ()) return 0;
  std::string rel_path = doc.lexically_relative (root).generic_string ();
  std::vector<std::pair<std::string, std::string>> parsed;
  size_t begin = 0;
  while (begin <= renames.size ()) {
    size_t end = renames.find ((char) 30, begin);
    std::string entry = renames.substr (
      begin, end == std::string::npos ? std::string::npos : end - begin);
    size_t separator = entry.find ((char) 31);
    if (separator != std::string::npos)
      parsed.emplace_back (entry.substr (0, separator),
                           entry.substr (separator + 1));
    if (end == std::string::npos) break;
    begin = end + 1;
  }
  size_t changed = 0;
  std::string error;
  if (!athena_vault_map_rewrite_at_root (root, rel_path, parsed, changed,
                                         error)) {
    log_error ("anchor structures: could not rewrite Vault map for " +
               compact_log_path (doc) + ": " + error);
    return 0;
  }
  return changed;
}

static bool
apply_anchor_candidate (const fs::path& root, const fs::path& doc,
                        VaultMaintenanceSummary& summary) {
    std::string result;
    try {
      result = tm_to_std (as_string (
        call ("vault-anchor-maintenance-file", object (
          url_system (std_to_tm (doc.string ()))))));
    }
    catch (...) {
      log_error ("anchor structures: Scheme failure for " + compact_log_path (doc));
      summary.anchor_failures++;
      return false;
    }

    AnchorMaintenanceFileResult parsed =
      parse_anchor_maintenance_result (result);
    if (!parsed.ok) {
      log_error ("anchor structures: failed for " + compact_log_path (doc) +
                 (parsed.message.empty () ? std::string () :
                  (": " + parsed.message)));
      summary.anchor_failures++;
      return false;
    }

    summary.anchor_enunciations_wrapped += parsed.wrapped;
    summary.anchor_headings_added += parsed.headings;
    summary.anchor_stale_structures_updated += parsed.updated;
    summary.anchor_dead_pairs_removed += parsed.removed;
    if (parsed.changed) {
      summary.anchor_files_changed++;
      size_t map_updates =
        rewrite_map_anchor_references (root, doc, parsed.renames);
      summary.anchor_map_references_updated += map_updates;
      log_info ("anchor structures: updated " + compact_log_path (doc) +
                " (wrapped " + std::to_string (parsed.wrapped) +
                ", headings " + std::to_string (parsed.headings) +
                ", updated " + std::to_string (parsed.updated) +
                  ", map references " + std::to_string (map_updates) +
                ", removed " + std::to_string (parsed.removed) +
                " dead pair(s))");
    }
    return true;
}

static void
log_anchor_summary (VaultMaintenanceSummary& summary) {
  log_info ("anchor structures: changed " +
            std::to_string (summary.anchor_files_changed) + " file(s), wrapped " +
            std::to_string (summary.anchor_enunciations_wrapped) +
            " enunciation(s), added " +
            std::to_string (summary.anchor_headings_added) +
            " heading anchor(s), updated " +
            std::to_string (summary.anchor_stale_structures_updated) +
            " stale anchor structure(s), rewrote " +
            std::to_string (summary.anchor_map_references_updated) +
            " map reference(s), removed " +
            std::to_string (summary.anchor_dead_pairs_removed) +
            " dead anchor pair(s)");
  if (summary.anchor_failures != 0)
    log_info ("anchor structures: " +
              std::to_string (summary.anchor_failures) + " file(s) failed");
}

static bool
anchor_structures_serial (const fs::path& root,
                          const std::vector<fs::path>& docs,
                          VaultMaintenanceSummary& summary) {
  for (size_t i=0; i<docs.size (); i++) {
    print_progress (i + 1, docs.size (), "Anchoring structures",
                    docs[i].filename ().string ());
    if (!apply_anchor_candidate (root, docs[i], summary)) {
      finish_progress ();
      return false;
    }
  }
  finish_progress ();
  return true;
}

#if defined(__unix__) || defined(__APPLE__)
static bool
write_worker_line (int fd, const std::string& line) {
  size_t offset = 0;
  while (offset < line.size ()) {
    ssize_t n = write (fd, line.data () + offset, line.size () - offset);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    offset += (size_t) n;
  }
  return true;
}

static std::string
pipe_safe (std::string s) {
  for (char& c: s)
    if (c == '\t' || c == '\n' || c == '\r') c = ' ';
  return s;
}

static int
anchor_reader_worker (const std::vector<fs::path>& docs, int jobs, int index,
                      int fd) {
  for (size_t i=(size_t) index; i<docs.size (); i += (size_t) jobs) {
    const fs::path& doc = docs[i];
    std::string path = doc.string ();
    try {
      std::string raw = tm_to_std (as_string (
        call ("vault-anchor-maintenance-check-file", object (
          url_system (std_to_tm (path))))));
      AnchorMaintenanceFileResult parsed =
        parse_anchor_maintenance_result (raw);
      if (!parsed.ok) {
        write_worker_line (
          fd, "E\t" + path + "\t" + pipe_safe (parsed.message) + "\n");
        return 1;
      }
      write_worker_line (fd, "F\t" + path + "\n");
      if (parsed.changed)
        write_worker_line (fd, "C\t" + path + "\n");
    }
    catch (...) {
      write_worker_line (fd, "E\t" + path + "\tScheme failure\n");
      return 1;
    }
  }
  return 0;
}

static void
silence_reader_stdio () {
  int dev_null = open ("/dev/null", O_WRONLY);
  if (dev_null < 0) return;
  dup2 (dev_null, STDOUT_FILENO);
  dup2 (dev_null, STDERR_FILENO);
  if (dev_null > STDERR_FILENO) close (dev_null);
}

static int
anchor_reader_jobs (size_t file_count, int requested) {
  if (file_count < 16) return 1;
  unsigned hw = std::max (1u, std::thread::hardware_concurrency ());
  unsigned jobs = requested < 1 ? hw : (unsigned) requested;
  jobs = std::min (jobs, (unsigned) file_count);
  return std::max (1u, jobs);
}

static bool
anchor_structures_parallel (const fs::path& root,
                            const std::vector<fs::path>& docs,
                            VaultMaintenanceSummary& summary) {
  int requested = summary.anchor_reader_processes;
  int jobs = anchor_reader_jobs (docs.size (), requested);
  unsigned hw = std::max (1u, std::thread::hardware_concurrency ());
  log_info ("anchor structures: parallelizability: files=" +
            std::to_string (docs.size ()) + ", preference=" +
            (requested < 1 ? std::string ("Unlimited") :
             std::to_string (requested)) +
            ", nproc=" + std::to_string (hw) +
            ", reader processes=" + std::to_string (jobs) +
            ", writer processes=1");
  if (jobs <= 1) {
    log_info ("anchor structures: using serial anchoring");
    return anchor_structures_serial (root, docs, summary);
  }

  log_info ("anchor structures: parallel read filter using " +
            std::to_string (jobs) + " worker process(es)");

  std::vector<pid_t> pids;
  std::vector<int> reads;
  std::vector<std::string> buffers;
  pids.reserve ((size_t) jobs);
  reads.reserve ((size_t) jobs);
  buffers.resize ((size_t) jobs);

  for (int i=0; i<jobs; i++) {
    int fds[2] = {-1, -1};
    if (pipe (fds) != 0) {
      log_error ("anchor structures: failed to create worker pipe");
      return anchor_structures_serial (root, docs, summary);
    }
    pid_t pid = fork ();
    if (pid == 0) {
      close (fds[0]);
      silence_reader_stdio ();
      int rc = anchor_reader_worker (docs, jobs, i, fds[1]);
      close (fds[1]);
      _exit (rc);
    }
    close (fds[1]);
    if (pid < 0) {
      close (fds[0]);
      log_error ("anchor structures: failed to fork worker");
      return anchor_structures_serial (root, docs, summary);
    }
    pids.push_back (pid);
    reads.push_back (fds[0]);
  }

  bool ok = true;
  int completed = 0;
  size_t scanned = 0;
  size_t candidates = 0;
  size_t written = 0;
  auto start = std::chrono::steady_clock::now ();

  auto stop_workers = [&] () {
    for (pid_t pid: pids)
      if (pid > 0) kill (pid, SIGTERM);
  };

  auto process_line = [&] (const std::string& line) {
    if (line.empty ()) return;
    std::vector<std::string> parts = split_tabs (line);
    if (parts.empty ()) return;
    if (parts[0] == "F") {
      scanned++;
      return;
    }
    if (parts[0] == "E" && parts.size () >= 2) {
      finish_progress ();
      summary.anchor_failures++;
      log_error ("anchor structures: failed for " + compact_log_path (parts[1]) +
                 (parts.size () >= 3 && !parts[2].empty () ?
                  (": " + parts[2]) : std::string ()));
      ok = false;
      stop_workers ();
      return;
    }
    if (parts[0] == "C" && parts.size () >= 2) {
      candidates++;
      fs::path doc (parts[1]);
      print_progress (scanned, docs.size (), "Writing anchor updates",
                      doc.filename ().string ());
      if (!apply_anchor_candidate (root, doc, summary)) {
        ok = false;
        stop_workers ();
      }
      else written++;
    }
  };
  auto drain_pending = [&] (std::string& pending, bool final) {
    while (true) {
      size_t nl = pending.find ('\n');
      if (nl == std::string::npos) break;
      std::string line = pending.substr (0, nl);
      pending.erase (0, nl + 1);
      process_line (line);
      if (!ok) return;
    }
    if (final && !pending.empty ()) {
      process_line (pending);
      pending.clear ();
    }
  };

  auto reap_workers = [&] (bool blocking) {
    for (size_t i=0; i<pids.size (); i++) {
      pid_t pid = pids[i];
      if (pid <= 0) continue;
      int status = 0;
      pid_t got = waitpid (pid, &status, blocking ? 0 : WNOHANG);
      if (got == 0) continue;
      if (got < 0) {
        if (errno == EINTR) continue;
        pids[i] = 0;
        completed++;
        ok = false;
        continue;
      }
      pids[i] = 0;
      completed++;
      if (!WIFEXITED (status) || WEXITSTATUS (status) != 0) ok = false;
    }
  };

  auto pipes_open = [&] () {
    for (int fd: reads)
      if (fd >= 0) return true;
    return false;
  };

  while ((completed < jobs || pipes_open ()) && ok) {
    std::vector<pollfd> pfds (reads.size ());
    for (size_t i=0; i<reads.size (); i++)
      pfds[i] = {reads[i], POLLIN | POLLHUP | POLLERR, 0};

    if (!pfds.empty ()) {
      int rc = poll (pfds.data (), pfds.size (), 250);
      if (rc > 0) {
        for (size_t i=0; i<pfds.size (); i++) {
          pollfd& pfd = pfds[i];
          if (pfd.fd < 0) continue;
          if (pfd.revents & POLLIN) {
            char buf[4096];
            ssize_t n = read (pfd.fd, buf, sizeof (buf));
            if (n > 0) {
              std::string& pending = buffers[i];
              pending.append (buf, buf + n);
              drain_pending (pending, false);
            }
          }
          if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) {
            std::string& pending = buffers[i];
            drain_pending (pending, true);
            close (pfd.fd);
            reads[i] = -1;
          }
          if (!ok) break;
        }
      }
    }

    reap_workers (false);

    auto now = std::chrono::steady_clock::now ();
    long long elapsed = std::chrono::duration_cast<std::chrono::seconds> (
      now - start).count ();
    if (scanned > docs.size ()) scanned = docs.size ();
    print_progress (scanned, docs.size (), "Filtering anchor candidates",
                    std::to_string (jobs - completed) +
                    " readers active, " + std::to_string (candidates) +
                    " candidate(s), " + std::to_string (written) +
                    " written, " + std::to_string (elapsed) + "s elapsed");
  }
  finish_progress ();

  for (int fd: reads)
    if (fd >= 0) close (fd);
  while (completed < jobs)
    reap_workers (true);

  return ok;
}
#endif

static bool
anchor_structures_in_vault (const fs::path& root,
                            VaultMaintenanceSummary& summary) {
  std::vector<fs::path> docs = scan_ath_documents (root);
  summary.anchor_files_scanned = docs.size ();
  log_info ("anchor structures: scanning " + std::to_string (docs.size ()) +
            " .ath files");

  bool ok = false;
#if defined(__unix__) || defined(__APPLE__)
  ok = anchor_structures_parallel (root, docs, summary);
#else
  ok = anchor_structures_serial (root, docs, summary);
#endif

  log_anchor_summary (summary);
  return ok;
}


VaultMaintenancePassResult
vault_maintenance_pass_anchor_enunciations (VaultMaintenanceContext& ctx) {
  if (anchor_structures_in_vault (ctx.root, ctx.summary))
    return VaultMaintenancePassResult::success ();
  return VaultMaintenancePassResult::failure ("structural anchoring failed");
}

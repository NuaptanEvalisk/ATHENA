/******************************************************************************
* MODULE     : vault_format_upgrade.cpp
* DESCRIPTION: Validate, stage and atomically publish an offline XML vault upgrade
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "vault_format_upgrade.hpp"
#include "document_file_codec.hpp"
#include "document_upgrade_file.hpp"
#include "vault_directory_lease.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"
#include "drd_std.hpp"
#include "unicode_text.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <future>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/fs.h>
#endif

namespace athena::document {
namespace fs= std::filesystem;
namespace {
struct descriptor {
  int value;
  explicit descriptor (int fd): value (fd) {
    if (fd < 0) throw std::system_error (errno, std::generic_category (), "Open upgrade file");
  }
  ~descriptor () { ::close (value); }
  descriptor (const descriptor&)= delete;
};
void require (bool ok, const std::string& message) {
  if (!ok) throw std::runtime_error (message);
}
void sync_fd (int fd) {
  if (::fsync (fd) != 0)
    throw std::system_error (errno, std::generic_category (), "Sync vault upgrade");
}
struct record {
  struct stat info {};
  std::string digest, link;
};
using inventory= std::map<fs::path,record>;
bool same_time (timespec a, timespec b) {
  return a.tv_sec == b.tv_sec && a.tv_nsec == b.tv_nsec;
}
bool same_revision (const struct stat& a, const struct stat& b) {
  return a.st_dev == b.st_dev && a.st_ino == b.st_ino &&
    a.st_mode == b.st_mode && a.st_uid == b.st_uid && a.st_gid == b.st_gid &&
    a.st_size == b.st_size && a.st_nlink == b.st_nlink &&
    same_time (a.st_mtim, b.st_mtim) && same_time (a.st_ctim, b.st_ctim);
}
record inspect (const fs::path& path, bool flush= false, bool hash_content= true,
                const std::atomic<bool>* stop= nullptr) {
  record r;
  if (::lstat (path.c_str (), &r.info) != 0)
    throw std::system_error (errno, std::generic_category (), path.string ());
  if (S_ISLNK (r.info.st_mode)) r.link= fs::read_symlink (path).native ();
  else if (S_ISREG (r.info.st_mode) && (flush || hash_content)) {
    descriptor fd (::open (path.c_str (), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    struct stat opened {};
    require (::fstat (fd.value, &opened) == 0 && same_revision (r.info, opened),
             "File changed while reading: " + path.string ());
    QCryptographicHash hash (QCryptographicHash::Sha256);
    char buffer[128 * 1024];
    while (hash_content) {
      if (stop && stop->load ()) throw std::runtime_error ("Inventory cancelled");
      ssize_t count= ::read (fd.value, buffer, sizeof buffer);
      if (count < 0 && errno == EINTR) continue;
      require (count >= 0, "Cannot read " + path.string ());
      if (count == 0) break;
      hash.addData (QByteArrayView (buffer, count));
    }
    if (flush) sync_fd (fd.value);
    require (::fstat (fd.value, &opened) == 0 && same_revision (r.info, opened),
             "File changed while hashing: " + path.string ());
    r.digest= hash.result ().toHex ().toStdString ();
  }
  else require (S_ISDIR (r.info.st_mode) || S_ISREG (r.info.st_mode),
                "Special file in vault: " + path.string ());
  return r;
}
void report (const vault_upgrade_progress& progress, const char* phase,
             std::size_t done, std::size_t total, const fs::path& path= {}) {
  if (progress) progress (phase, done, total, path.generic_string ());
}
inventory scan (const fs::path& root, const vault_upgrade_progress& progress,
                const char* phase, bool flush= false, bool hash_content= true) {
  inventory result;
  result.emplace (fs::path ("."), inspect (root));
  const auto device= result.begin ()->second.info.st_dev;
  for (const auto& entry: fs::recursive_directory_iterator (root)) {
    require (result.size () < 1000000, "Vault exceeds one million filesystem entries");
    auto relative= entry.path ().lexically_relative (root);
    text::require_utf8 (relative.native ());
    auto r= inspect (entry.path (), false, false);
    require (r.info.st_dev == device, "Nested mount in vault: " + relative.string ());
    result.emplace (relative, std::move (r));
    report (progress, phase, result.size () - 1, 0, relative);
  }
  // Bound parallel I/O; each worker owns its hash and writes a distinct record.
  // Progress callbacks (including cancellation) remain on the caller thread.
  std::vector<std::pair<fs::path, record*>> files;
  for (auto& [path, r]: result)
    if (S_ISREG (r.info.st_mode)) files.emplace_back (path, &r);
  std::atomic<std::size_t> next {0}, done {0};
  std::atomic<bool> stop {false};
  std::vector<std::future<void>> workers;
  try {
    const auto count= std::min<std::size_t> (files.size (),
      std::min (4u, std::max (1u, std::thread::hardware_concurrency ())));
    for (std::size_t i= 0; i < count; ++i)
      workers.push_back (std::async (std::launch::async, [&] {
        while (!stop.load ()) {
          const auto index= next.fetch_add (1);
          if (index >= files.size ()) break;
          const auto& [path, initial]= files[index];
          auto checked= inspect (root / path, flush, hash_content, &stop);
          require (same_revision (initial->info, checked.info),
                   "File changed during inventory: " + path.string ());
          *initial= std::move (checked);
          done.fetch_add (1);
        }
      }));
    for (auto& worker: workers) {
      while (worker.wait_for (std::chrono::milliseconds (100)) != std::future_status::ready)
        report (progress, phase, done.load (), files.size ());
      worker.get ();
    }
    report (progress, phase, done.load (), files.size ());
  }
  catch (...) {
    stop.store (true);
    for (auto& worker: workers) if (worker.valid ()) worker.wait ();
    throw;
  }
  if (flush) {
    // Flush children before their parents, including directory entries copied
    // by coreutils. File data alone is insufficient before the exchange.
    for (auto it= result.rbegin (); it != result.rend (); ++it)
      if (S_ISDIR (it->second.info.st_mode)) {
        descriptor fd (::open ((root / it->first).c_str (), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        sync_fd (fd.value);
      }
  }
  return result;
}
void compare (const inventory& expected, const inventory& actual, bool original) {
  require (expected.size () == actual.size (), "Vault entries changed during upgrade");
  for (const auto& [path, before]: expected) {
    auto it= actual.find (path);
    require (it != actual.end (), "Missing snapshot entry: " + path.string ());
    const auto& after= it->second;
    require (before.digest == after.digest && before.link == after.link &&
      before.info.st_mode == after.info.st_mode && before.info.st_uid == after.info.st_uid &&
      before.info.st_gid == after.info.st_gid &&
      (S_ISDIR (before.info.st_mode) || same_time (before.info.st_mtim, after.info.st_mtim)) &&
      (!original || same_revision (before.info, after.info)),
      "Snapshot mismatch or external modification: " + path.string ());
  }
}
bool is_document_file (const fs::path& path) {
  const auto first= *path.begin ();
  // Archives remain exact originals; they are not live vault documents.
  return first != ".backup" && first != ".git" && first != ".hg" &&
    first != ".svn" && path.extension () == ".ath";
}
std::string bytes (const fs::path& root, const fs::path& path) {
  filesystem::confined_root storage (root);
  return storage.open (path).read (codec_limits ().output_bytes);
}
std::string rag_storage_hash (const std::string& bytes) {
  // This is the existing RAG storage revision, not the logical SHA-256.
  std::uint64_t h= 1469598103934665603ULL;
  for (unsigned char c: bytes) { h ^= c; h *= 1099511628211ULL; }
  std::ostringstream out;
  out << std::hex << std::setw (16) << std::setfill ('0') << h;
  return out.str ();
}
struct document_record {
  fs::path path;
  std::string semantic;
  bool legacy;
};
legacy_import_limits limits () {
  legacy_import_limits out;
  out.codec.input_bytes= out.codec.output_bytes;
  return out;
}
void clone (const fs::path& source, const fs::path& target,
            const vault_upgrade_progress& progress) {
  const auto cp= QStandardPaths::findExecutable ("cp");
  require (!cp.isEmpty (), "GNU coreutils cp is required for metadata-preserving vault snapshots");
  QProcess process;
  process.setProcessChannelMode (QProcess::MergedChannels);
  process.start (cp, {"--archive", "--reflink=auto", "--", QString::fromStdString ((source / ".").string ()),
                     QString::fromStdString (target.string ())});
  require (process.waitForStarted (), "Cannot start vault snapshot copy");
  QByteArray diagnostics;
  try {
    while (!process.waitForFinished (200)) {
      diagnostics += process.readAll ();
      if (diagnostics.size () > 8192) diagnostics= diagnostics.right (8192);
      report (progress, "Copy snapshot", 0, 0);
    }
  }
  catch (...) { process.kill (); process.waitForFinished (-1); throw; }
  diagnostics += process.readAll ();
  require (process.exitStatus () == QProcess::NormalExit && process.exitCode () == 0,
           "Snapshot copy failed: " + diagnostics.right (8192).toStdString ());
}
volatile std::sig_atomic_t interrupted= 0;
void cancel_signal (int) { interrupted= 1; }
} // namespace

vault_upgrade_result upgrade_vault_format (const fs::path& requested,
                                           const vault_upgrade_progress& progress) {
#ifndef __linux__
  throw std::runtime_error ("Atomic vault format upgrade requires Linux renameat2(RENAME_EXCHANGE)");
#else
  const auto absolute= fs::absolute (requested).lexically_normal ();
  require (!fs::is_symlink (fs::symlink_status (absolute)), "Vault root must not be a symlink");
  const auto root= fs::canonical (absolute);
  text::require_utf8 (root.native ());
  require (root != root.root_path (), "Cannot upgrade filesystem root");
  filesystem::vault_directory_lease lease (root, true);
  descriptor parent (::open (root.parent_path ().c_str (), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  struct stat parent_info {}, root_info {};
  require (::fstat (parent.value, &parent_info) == 0 &&
    ::fstat (lease.descriptor (), &root_info) == 0 && parent_info.st_dev == root_info.st_dev,
    "Vault must not be a mount point; atomic sibling exchange requires the same filesystem");
  AthenaVaultfileInfo info;
  std::string error;
  require (athena_vaultfile_read (root, info, error), error);
  const auto original= scan (root, progress, "Inventory");
  std::vector<document_record> documents;
  std::vector<vault_upgrade_revision> revisions;
  vault_upgrade_result result;
  std::size_t total= 0;
  for (const auto& [path, r]: original) if (is_document_file (path)) ++total;
  for (const auto& [path, r]: original) {
    if (!is_document_file (path)) continue;
    require (S_ISREG (r.info.st_mode) && r.info.st_nlink == 1,
             "Document must be a regular, unlinked file: " + path.string ());
    const auto source= bytes (root, path);
    try {
      auto decoded= decode_document_bytes (source, root / path, limits ());
      const auto semantic= semantic_document_fingerprint (decoded.document);
      if (decoded.legacy ()) {
        require ((r.info.st_mode & 0222) != 0, "Read-only document: " + path.string ());
        ++result.converted;
      }
      else ++result.already_xml;
      documents.push_back ({path, semantic, decoded.legacy ()});
      revisions.push_back ({path.generic_string (), semantic, rag_storage_hash (source),
        static_cast<long long> (source.size ()),
        static_cast<long long> (fs::last_write_time (root / path).time_since_epoch ().count ())});
    }
    catch (const std::exception& e) {
      throw std::runtime_error ("Invalid document " + path.string () + ": " + e.what ());
    }
    report (progress, "Validate input", documents.size (), total, path);
  }
  if (result.converted == 0) return result;
  const auto workspace= root.parent_path () / ("." + root.filename ().string () +
    ".format-upgrade-" + QUuid::createUuid ().toString (QUuid::WithoutBraces).toStdString ());
  require (::mkdir (workspace.c_str (), 0700) == 0, "Cannot create private upgrade workspace");
  const auto staged= workspace / "vault";
  bool exchanged= false;
  try {
    fs::create_directory (staged);
    // Advertise the recovery location before copying or changing any bytes.
    report (progress, "Recovery directory", 0, 0, workspace);
    clone (root, staged, progress);
    filesystem::vault_directory_lease staged_lease (staged, true);
    compare (original, scan (staged, progress, "Verify snapshot"), false);
    prepare_vault_upgrade_indexes (staged, revisions, progress);
    QJsonArray manifest;
    std::size_t done= 0;
    filesystem::confined_root storage (staged);
    for (const auto& doc: documents) {
      report (progress, "Convert", done, documents.size (), doc.path);
      if (doc.legacy) {
        auto file= storage.open (doc.path);
        auto revision= file.stat ();
        auto source= file.read (limits ().codec.input_bytes);
        auto decoded= decode_document_bytes (source, staged / doc.path, limits ());
        require (semantic_document_fingerprint (decoded.document) == doc.semantic,
                 "Snapshot style context changed: " + doc.path.string ());
        const auto xml= write_xml (decoded.document);
        require (read_xml (xml, xml_kind::document, limits ().codec) == decoded.document,
                 "XML round-trip mismatch: " + doc.path.string ());
        auto replacement= storage.replace (doc.path, file, revision, xml);
        require (replacement.directory_synced, "Cannot sync converted document");
      }
      manifest.append (QJsonObject {{"path", QString::fromStdString (doc.path.generic_string ())},
        {"semantic_sha256", QString::fromStdString (doc.semantic)}, {"was_legacy", doc.legacy}});
      report (progress, "Convert", ++done, documents.size (), doc.path);
    }
    done= 0;
    for (const auto& doc: documents) {
      const auto xml= bytes (staged, doc.path);
      const auto decoded= read_xml (xml, xml_kind::document, limits ().codec);
      require (semantic_document_fingerprint (decoded) == doc.semantic,
               "Post-conversion semantic mismatch: " + doc.path.string ());
      report (progress, "Validate XML", ++done, documents.size (), doc.path);
    }
    scan (staged, progress, "Sync snapshot", true, false);
    QJsonObject receipt {{"version", 1}, {"source", QString::fromStdString (root.string ())},
      {"commit", "atomic-directory-exchange"}, {"documents", manifest}};
    filesystem::confined_root journal (workspace);
    const auto json= QJsonDocument (receipt).toJson (QJsonDocument::Indented);
    journal.preserve ("manifest.json", std::string_view (json.constData (), json.size ()));
    sync_fd (parent.value);
    // All original files, including databases and style resources, must still
    // match the snapshot. Offline exclusion remains required for old binaries
    // and unrelated writers that do not participate in our directory lease.
    report (progress, "Commit", 0, 1, root);
    compare (original, scan (root, progress, "Check external changes", true), true);
    descriptor workspace_fd (::open (workspace.c_str (), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    result.backup= staged;
    if (::syscall (SYS_renameat2, parent.value, root.filename ().c_str (),
                   AT_FDCWD, staged.c_str (), RENAME_EXCHANGE) != 0)
      throw std::system_error (errno, std::generic_category (), "Atomic vault directory exchange");
    exchanged= true;
    // No throwing callbacks, allocations or rollback after successful rename.
    const int first= ::fsync (workspace_fd.value);
    const int second= ::fsync (parent.value);
    result.durable= first == 0 && second == 0;
    return result;
  }
  catch (...) {
    if (!exchanged) {
      std::error_code ignored;
      fs::remove_all (workspace, ignored);
    }
    throw;
  }
#endif
}

int upgrade_vault_format_cli (const fs::path& root) {
  interrupted= 0;
  const auto old_int= std::signal (SIGINT, cancel_signal);
  const auto old_term= std::signal (SIGTERM, cancel_signal);
  int code= 1;
  try {
    init_std_drd ();
    std::cerr << "Offline vault upgrade. Close all users of this vault before continuing.\n";
    std::string previous;
    auto last= std::chrono::steady_clock::now ();
    const bool terminal= ::isatty (STDERR_FILENO);
    auto progress= [&] (const char* phase, std::size_t done, std::size_t total, const std::string& path) {
      if (interrupted) throw std::runtime_error ("Upgrade cancelled before commit");
      const auto now= std::chrono::steady_clock::now ();
      const bool complete= total != 0 && done == total;
      if (previous == phase && !complete &&
          now - last < std::chrono::milliseconds (terminal ? 200 : 2000)) return;
      if (terminal) std::cerr << '\r' << "\033[K";
      if (total) {
        const auto filled= 24 * done / total;
        std::cerr << '[' << std::string (filled, '=') << std::string (24 - filled, ' ') << "] ";
      }
      std::cerr << phase;
      if (done || total) std::cerr << " " << done;
      else std::cerr << " ...";
      if (total) std::cerr << '/' << total;
      if (previous != phase && !path.empty ()) std::cerr << " " << std::quoted (path);
      if (!terminal || complete) std::cerr << '\n';
      std::cerr << std::flush;
      previous= phase; last= now;
    };
    const auto result= upgrade_vault_format (root, progress);
    std::cerr << "\nConverted " << result.converted << "; already XML " << result.already_xml << ".\n";
    if (!result.backup.empty ()) std::cerr << "Original vault backup: " << result.backup << '\n';
    if (!result.durable) std::cerr << "COMMITTED, but directory durability could not be confirmed. Do not delete the backup.\n";
    code= result.durable ? 0 : 2;
  }
  catch (const std::exception& e) { std::cerr << "\nVault upgrade failed: " << e.what () << '\n'; }
  catch (const string& e) { std::cerr << "\nVault upgrade failed: " << std::string (e.data (), N(e)) << '\n'; }
  std::signal (SIGINT, old_int);
  std::signal (SIGTERM, old_term);
  return interrupted && code != 0 ? 130 : code;
}
} // namespace athena::document

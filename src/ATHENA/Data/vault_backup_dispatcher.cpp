/******************************************************************************
* MODULE     : vault_backup_dispatcher.cpp
* DESCRIPTION: One-way vault backup dispatch through rsync
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "ATHENA/Data/vault_backup_dispatcher.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"
#include "athena_platform.hpp"

#include <QDir>
#include <QFileInfo>
#if !ATHENA_PLATFORM_IPADOS
#include <QProcess>
#include <QStandardPaths>
#endif
#include <QTemporaryDir>
#include <sqlite3.h>
#include <fstream>
#include <memory>
#include <set>
#include <stdexcept>

namespace fs= std::filesystem;

namespace {

std::string
utf8 (const QString& text) {
  QByteArray bytes= text.toUtf8 ();
  return std::string (bytes.constData (), (size_t) bytes.size ());
}

QString
qstring (const std::string& text) {
  return QString::fromUtf8 (text.data (), (qsizetype) text.size ());
}

bool
is_remote_destination (const QString& destination) {
  if (destination.startsWith ("rsync://", Qt::CaseInsensitive)) return true;
  int colon= destination.indexOf (':');
  if (colon <= 0) return false;
  if (colon == 1 && destination[0].isLetter ()) return false;
  return !destination.left (colon).contains ('/');
}

bool
path_contains (const fs::path& outer, const fs::path& inner) {
  auto a= outer.begin ();
  auto b= inner.begin ();
  for (; a != outer.end () && b != inner.end (); ++a, ++b)
    if (*a != *b) return false;
  return a == outer.end ();
}

QString
expand_local_destination (QString destination) {
  if (destination == "~") destination= QDir::homePath ();
  else if (destination.startsWith ("~/"))
    destination= QDir::homePath () + destination.mid (1);
  return QDir::cleanPath (QFileInfo (destination).absoluteFilePath ());
}

std::string
filter_path (const fs::path& path) {
  std::string result= "/";
  for (char c: path.generic_string ()) {
    if (c == '\\' || c == '*' || c == '?' || c == '[') result += '\\';
    result += c;
  }
  return result;
}

void
snapshot_database (const fs::path& source, const fs::path& destination) {
  fs::create_directories (destination.parent_path ());
  std::ifstream input (source, std::ios::binary);
  char header[16]= {};
  input.read (header, sizeof (header));
  if (!input && !input.eof ())
    throw std::runtime_error ("Cannot read " + source.string ());
  if (std::string (header, 16) != std::string ("SQLite format 3\0", 16)) {
    // A user's .db file need not be SQLite.
    fs::copy_file (source, destination);
    return;
  }
  input.close ();
  using database= std::unique_ptr<sqlite3, decltype (&sqlite3_close)>;
  sqlite3* raw= nullptr;
  int rc= sqlite3_open_v2 (source.c_str (), &raw, SQLITE_OPEN_READONLY, nullptr);
  database from (raw, sqlite3_close);
  if (rc != SQLITE_OK) throw std::runtime_error (sqlite3_errmsg (raw));
  raw= nullptr;
  rc= sqlite3_open_v2 (destination.c_str (), &raw,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
  database to (raw, sqlite3_close);
  if (rc != SQLITE_OK) throw std::runtime_error (sqlite3_errmsg (raw));
  sqlite3_busy_timeout (from.get (), 5000);
  sqlite3_busy_timeout (to.get (), 5000);
  // Pin a read snapshot so continuous WAL commits cannot restart the copy.
  if (sqlite3_exec (from.get (), "BEGIN; SELECT count(*) FROM sqlite_schema",
                    nullptr, nullptr, nullptr) != SQLITE_OK)
    throw std::runtime_error (sqlite3_errmsg (from.get ()));
  sqlite3_backup* backup= sqlite3_backup_init (to.get (), "main", from.get (), "main");
  if (!backup) throw std::runtime_error (sqlite3_errmsg (to.get ()));
  int retries= 0;
  do {
    rc= sqlite3_backup_step (backup, 1024);
    if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED) {
      if (++retries > 50) break;
      sqlite3_sleep (100);
    }
  } while (rc == SQLITE_OK || rc == SQLITE_BUSY || rc == SQLITE_LOCKED);
  int finished= sqlite3_backup_finish (backup);
  if (rc != SQLITE_DONE || finished != SQLITE_OK)
    throw std::runtime_error (std::string ("SQLite snapshot failed: ") +
                             sqlite3_errstr (rc == SQLITE_DONE? finished: rc));
  if (sqlite3_exec (to.get (), "PRAGMA journal_mode=DELETE", nullptr, nullptr,
                    nullptr) != SQLITE_OK)
    throw std::runtime_error (sqlite3_errmsg (to.get ()));
  to.reset ();
  fs::permissions (destination, fs::status (source).permissions ());
}

bool
run_rsync (const std::string& program, const QStringList& arguments,
            std::string& error) {
#if ATHENA_PLATFORM_IPADOS
  (void) program;
  (void) arguments;
  error= "rsync backup dispatch is unavailable on iPadOS";
  return false;
#else
  QProcess process;
  process.setProcessChannelMode (QProcess::SeparateChannels);
  process.start (qstring (program), arguments);
  if (!process.waitForStarted ()) {
    error= "Could not start rsync: " + utf8 (process.errorString ());
    return false;
  }
  if (!process.waitForFinished (-1) ||
      process.exitStatus () != QProcess::NormalExit || process.exitCode () != 0) {
    QString detail= QString::fromUtf8 (process.readAllStandardError ()).trimmed ();
    error= "rsync backup dispatch failed";
    if (!detail.isEmpty ()) error += ": " + utf8 (detail);
    return false;
  }
  return true;
#endif
}

} // namespace

bool
athena_backup_dispatch_validate_destination (
  const fs::path& vault_root, const std::string& destination_text,
  std::string& normalized_destination, std::string& error) {
  QString destination= qstring (destination_text).trimmed ();
  if (destination.isEmpty ()) {
    error= "Backup destination cannot be empty";
    return false;
  }
  QString final_destination= destination;
  if (!is_remote_destination (destination)) {
    if (!QDir::isAbsolutePath (destination) && destination != "~" &&
        !destination.startsWith ("~/")) {
      error= "Local backup destination must be an absolute path or start "
             "with ~/";
      return false;
    }
    final_destination= expand_local_destination (destination);
    QFileInfo destination_info (final_destination);
    if (destination_info.exists () && !destination_info.isDir ()) {
      error= "Backup destination is not a directory: " +
             utf8 (final_destination);
      return false;
    }

    fs::path root= fs::absolute (vault_root).lexically_normal ();
    fs::path target= fs::path (utf8 (final_destination)).lexically_normal ();
    if (target == target.root_path () || path_contains (root, target) ||
        path_contains (target, root)) {
      error= "Backup destination must not equal, contain, or be inside the "
             "vault root";
      return false;
    }
    if (!final_destination.endsWith ('/')) final_destination += '/';
  }
  normalized_destination= utf8 (final_destination);
  return true;
}

bool
athena_backup_dispatch_prepare (
  const fs::path& vault_root, const std::string& destination_text,
  AthenaBackupDispatchCommand& command, std::string& error) {
#if ATHENA_PLATFORM_IPADOS
  (void) vault_root;
  (void) destination_text;
  (void) command;
  error= "rsync backup dispatch is unavailable on iPadOS";
  return false;
#else
  std::string normalized_destination;
  if (!athena_backup_dispatch_validate_destination (
        vault_root, destination_text, normalized_destination, error))
    return false;

  QString executable= QStandardPaths::findExecutable ("rsync");
  if (executable.isEmpty ()) {
    error= "rsync is required for vault backup dispatching but was not found";
    return false;
  }

  QString final_destination= qstring (normalized_destination);
  if (!is_remote_destination (final_destination) &&
      !QDir ().mkpath (final_destination)) {
    error= "Could not create backup destination: " +
           normalized_destination;
    return false;
  }

  QString source= QString::fromStdString (
    fs::absolute (vault_root).lexically_normal ().string ());
  if (!source.endsWith ('/')) source += '/';
  command.program= utf8 (executable);
  command.normalized_destination= normalized_destination;
  command.arguments= {
    "-a", "--delete-delay", "--delay-updates", "--protect-args",
    "--exclude=/.backup/", "--exclude=/.athena/rag-backup-*",
    utf8 (source), utf8 (final_destination)};
  return true;
#endif
}

bool
athena_backup_dispatch_run (
  const fs::path& vault_root, const std::string& destination,
  std::string& error) {
  AthenaBackupDispatchCommand command;
  if (!athena_backup_dispatch_prepare (
        vault_root, destination, command, error)) return false;

  try {
    QTemporaryDir temporary (QDir::tempPath () + "/athena-backup-XXXXXX");
    if (!temporary.isValid ())
      throw std::runtime_error ("Cannot create private database snapshot directory");
    fs::path staging= fs::path (utf8 (temporary.path ())) / "snapshot";
    fs::create_directory (staging);
    fs::path manifest_path= fs::path (utf8 (temporary.path ())) / "files";
    std::ofstream manifest (manifest_path, std::ios::binary);
    AthenaVaultfileInfo info;
    if (athena_vaultfile_present (vault_root) &&
        !athena_vaultfile_read (vault_root, info, error)) return false;
    std::set<fs::path> databases;
    for (const auto& name: {info.map_path, info.namespace_db_path,
         info.rag_index_path, info.artifacts_path, info.enunciations_path,
         info.bold_text_path, info.materials_db_path}) {
      fs::path relative= fs::path (name).lexically_normal ();
      if (relative.empty () || relative.is_absolute () || *relative.begin () == "..")
        throw std::runtime_error ("Database path escapes vault: " + name);
      databases.insert (relative);
    }
    for (fs::recursive_directory_iterator it (vault_root), end; it != end; ++it) {
      fs::path relative= it->path ().lexically_relative (vault_root);
      if (it->is_directory ()) {
        if (relative == ".backup" ||
            (relative.parent_path () == ".athena" &&
             relative.filename ().string ().rfind ("rag-backup-", 0) == 0))
          it.disable_recursion_pending ();
      }
      else if (it->path ().extension () == ".sqlite" ||
               it->path ().extension () == ".db") databases.insert (relative);
    }
    QStringList arguments;
    for (size_t i= 0; i + 2 < command.arguments.size (); ++i)
      arguments << qstring (command.arguments[i]);
    // Sender-only hiding also lets --delete remove old destination sidecars.
    for (const char* pattern: {"*.sqlite", "*.db", "*.sqlite-wal", "*.sqlite-shm",
                              "*.sqlite-journal", "*.sqlite-mj*",
                              "*.db-wal", "*.db-shm", "*.db-journal", "*.db-mj*"})
      arguments << qstring (std::string ("--filter=H ") + pattern);
    bool have_snapshots= false;
    for (const fs::path& relative: databases) {
      std::string pattern= filter_path (relative);
      for (const char* suffix: {"", "-wal", "-shm", "-journal", "-mj*"})
        arguments << qstring ("--filter=H " + pattern + suffix);
      fs::path source= vault_root / relative;
      if (!fs::exists (source)) continue;
      if (fs::is_symlink (fs::symlink_status (source)) ||
          !path_contains (fs::canonical (vault_root), fs::canonical (source)))
        throw std::runtime_error ("Database symlink is not a private vault file: " +
                                 relative.string ());
      snapshot_database (source, staging / relative);
      const std::string name= relative.generic_string ();
      manifest.write (name.data (), name.size ());
      manifest.put ('\0');
      arguments << qstring ("--filter=P " + pattern);
      have_snapshots= true;
    }
    manifest.close ();
    if (!manifest) throw std::runtime_error ("Could not write snapshot manifest");
    arguments << qstring (command.arguments[command.arguments.size () - 2])
              << qstring (command.normalized_destination);
    if (!run_rsync (command.program, arguments, error)) return false;
    if (have_snapshots && !run_rsync (command.program,
          {"-a", "--checksum", "--delay-updates", "--protect-args",
           "--no-implied-dirs", "--from0", qstring ("--files-from=" + manifest_path.string ()),
           qstring (staging.string () + '/'), qstring (command.normalized_destination)}, error))
      return false;
  }
  catch (const std::exception& ex) {
    error= std::string ("Database-safe backup failed: ") + ex.what ();
    return false;
  }
  return true;
}

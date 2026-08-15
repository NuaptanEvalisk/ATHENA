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

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

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
}

bool
athena_backup_dispatch_run (
  const fs::path& vault_root, const std::string& destination,
  std::string& error) {
  AthenaBackupDispatchCommand command;
  if (!athena_backup_dispatch_prepare (
        vault_root, destination, command, error)) return false;

  QStringList arguments;
  for (const std::string& argument: command.arguments)
    arguments << qstring (argument);
  QProcess process;
  process.setProcessChannelMode (QProcess::SeparateChannels);
  process.start (qstring (command.program), arguments);
  if (!process.waitForStarted ()) {
    error= "Could not start rsync: " + utf8 (process.errorString ());
    return false;
  }
  if (!process.waitForFinished (-1) ||
      process.exitStatus () != QProcess::NormalExit ||
      process.exitCode () != 0) {
    QString detail= QString::fromUtf8 (process.readAllStandardError ()).trimmed ();
    error= "rsync backup dispatch failed";
    if (!detail.isEmpty ()) error += ": " + utf8 (detail);
    return false;
  }
  return true;
}

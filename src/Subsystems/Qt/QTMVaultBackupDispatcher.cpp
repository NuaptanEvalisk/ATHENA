/******************************************************************************
* MODULE     : QTMVaultBackupDispatcher.cpp
* DESCRIPTION: Asynchronous vault backup dispatch scheduling
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include "QTMVaultBackupDispatcher.hpp"

#include "ATHENA/Data/vault_backup_dispatcher.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"
#include "QTMVaultInfoModel.hpp"
#include "tm_ostream.hpp"

#include <QApplication>
#include <QEvent>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QQueue>
#include <QSet>
#include <QTimer>

#include <filesystem>

namespace {

std::string
utf8 (const QString& text) {
  QByteArray bytes= text.toUtf8 ();
  return std::string (bytes.constData (), (size_t) bytes.size ());
}

struct BackupTask {
  QString root;
  QString destination;
  QString key;
};

class BackupDispatcherManager: public QObject {
public:
  explicit BackupDispatcherManager (QObject* parent): QObject (parent) {
    idleTimer.setSingleShot (true);
    idleTimer.setInterval (5 * 60 * 1000);
    connect (&idleTimer, &QTimer::timeout, this, [this] () {
      requestTrigger ("idle");
    });
    process.setProcessChannelMode (QProcess::SeparateChannels);
    connect (&process, qOverload<int,QProcess::ExitStatus> (&QProcess::finished),
             this, [this] (int exitCode, QProcess::ExitStatus exitStatus) {
      finishCurrent (exitCode, exitStatus);
    });
    connect (&process, &QProcess::errorOccurred, this,
             [this] (QProcess::ProcessError error) {
      if (error == QProcess::FailedToStart) {
        std_error << "backup dispatcher: could not start rsync: "
                  << process.errorString ().toStdString ().c_str () << "\n";
        if (active)
          QTimer::singleShot (0, this, [this] () {
            finishCurrent (-1, QProcess::CrashExit);
          });
      }
    });
    idleTimer.start ();
  }

  void noteActivity (const QEvent* event) {
    if (event == nullptr || !event->spontaneous ()) return;
    switch (event->type ()) {
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
    case QEvent::Wheel:
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::TabletPress:
    case QEvent::TabletMove:
    case QEvent::TabletRelease:
    case QEvent::NativeGesture:
      idleTimer.start ();
      break;
    default:
      break;
    }
  }

  void requestTrigger (const QString& trigger) {
    QString root= qtm_vault_root_path ();
    if (root.isEmpty ()) return;
    AthenaVaultfileInfo info;
    std::string error;
    if (!athena_vaultfile_read (
          std::filesystem::path (utf8 (root)), info, error)) {
      std_error << "backup dispatcher: could not read Vaultfile.json: "
                << error.c_str () << "\n";
      return;
    }
    for (const AthenaBackupDispatcher& dispatcher: info.backup_dispatchers) {
      if (QString::fromStdString (dispatcher.trigger) != trigger) continue;
      enqueue ({root, QString::fromStdString (dispatcher.destination),
                root + QChar ('\n') +
                QString::fromStdString (dispatcher.destination)});
    }
  }

  void requestRealtime (const QString& savedFile) {
    QString root= qtm_vault_root_path ();
    if (root.isEmpty () || savedFile.isEmpty ()) return;
    QString absoluteRoot= QDir::cleanPath (QFileInfo (root).absoluteFilePath ());
    QString absoluteFile= QDir::cleanPath (
      QFileInfo (savedFile).absoluteFilePath ());
    QString relative= QDir (absoluteRoot).relativeFilePath (absoluteFile);
    if (relative == ".." || relative.startsWith ("../") ||
        QDir::isAbsolutePath (relative)) return;
    requestTrigger ("realtime");
  }

private:
  void enqueue (const BackupTask& task) {
    if (active && current.key == task.key) {
      rerun.insert (task.key);
      return;
    }
    if (queuedKeys.contains (task.key)) return;
    queue.enqueue (task);
    queuedKeys.insert (task.key);
    startNext ();
  }

  void startNext () {
    if (active || queue.isEmpty ()) return;
    current= queue.dequeue ();
    queuedKeys.remove (current.key);

    AthenaBackupDispatchCommand command;
    std::string error;
    if (!athena_backup_dispatch_prepare (
          std::filesystem::path (utf8 (current.root)),
          utf8 (current.destination), command, error)) {
      std_error << "backup dispatcher: " << error.c_str () << "\n";
      current= {};
      QTimer::singleShot (0, this, [this] () { startNext (); });
      return;
    }
    QStringList arguments;
    for (const std::string& argument: command.arguments)
      arguments << QString::fromStdString (argument);
    active= true;
    athena_spdlog_info (
      "backup dispatcher: synchronizing vault to " +
      command.normalized_destination);
    process.start (QString::fromStdString (command.program), arguments);
  }

  void finishCurrent (int exitCode, QProcess::ExitStatus exitStatus) {
    if (!active) return;
    QString finishedKey= current.key;
    BackupTask finishedTask= current;
    if (exitStatus == QProcess::NormalExit && exitCode == 0)
      athena_spdlog_info (
        "backup dispatcher: synchronization completed");
    else {
      QString detail= QString::fromUtf8 (
        process.readAllStandardError ()).trimmed ();
      std_error << "backup dispatcher: synchronization failed";
      if (!detail.isEmpty ())
        std_error << ": " << detail.toStdString ().c_str ();
      std_error << "\n";
    }
    active= false;
    current= {};
    if (rerun.remove (finishedKey) > 0) enqueue (finishedTask);
    startNext ();
  }

  QTimer idleTimer;
  QProcess process;
  QQueue<BackupTask> queue;
  QSet<QString> queuedKeys;
  QSet<QString> rerun;
  BackupTask current;
  bool active= false;
};

BackupDispatcherManager*&
manager_storage () {
  static BackupDispatcherManager* instance= nullptr;
  return instance;
}

BackupDispatcherManager*
manager (bool create) {
  BackupDispatcherManager*& instance= manager_storage ();
  if (create && instance == nullptr && qApp != nullptr) {
    BackupDispatcherManager* created= new BackupDispatcherManager (nullptr);
    instance= created;
    created->setParent (qApp);
  }
  return instance;
}

} // namespace

void
qtm_vault_backup_dispatcher_initialize () {
  (void) manager (true);
}

void
qtm_vault_backup_dispatcher_note_activity (const QEvent* event) {
  BackupDispatcherManager* instance= manager (false);
  if (instance != nullptr) instance->noteActivity (event);
}

void
qtm_vault_backup_dispatch_realtime (const QString& saved_file) {
  BackupDispatcherManager* instance= manager (true);
  if (instance != nullptr) instance->requestRealtime (saved_file);
}

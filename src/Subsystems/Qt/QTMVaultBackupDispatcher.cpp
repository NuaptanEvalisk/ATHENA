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
#include <QThreadPool>
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
    worker.setMaxThreadCount (1);
    idleTimer.start ();
  }

  ~BackupDispatcherManager () override { worker.waitForDone (); }

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

    active= true;
    athena_spdlog_info (
      "backup dispatcher: synchronizing vault to " +
      utf8 (current.destination));
    const BackupTask task= current;
    worker.start ([this, task] {
      std::string error;
      const bool ok= athena_backup_dispatch_run (
        std::filesystem::path (utf8 (task.root)), utf8 (task.destination), error);
      QMetaObject::invokeMethod (this, [this, ok, error] {
        finishCurrent (ok, QString::fromStdString (error));
      }, Qt::QueuedConnection);
    });
  }

  void finishCurrent (bool ok, const QString& detail) {
    if (!active) return;
    QString finishedKey= current.key;
    BackupTask finishedTask= current;
    if (ok)
      athena_spdlog_info (
        "backup dispatcher: synchronization completed");
    else {
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
  QThreadPool worker;
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

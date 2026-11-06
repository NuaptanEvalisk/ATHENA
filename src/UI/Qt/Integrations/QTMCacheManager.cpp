/******************************************************************************
* MODULE     : QTMCacheManager.cpp
* DESCRIPTION: ADS cache manager with cancellable, resumable Vault prewarming
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/
#include "UI/Qt/Integrations/QTMCacheManager.hpp"

#include "App/Runtime/server.hpp"
#include "Editor/Commands/application_tools.hpp"
#include "UI/Qt/Shell/QTMMainTabWindow.hpp"
#include "UI/Qt/Toolkit/qt_utilities.hpp"
#include "athena_platform.hpp"
#include "config.h"
#include "vault.hpp"

#include <DockWidget.h>
#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QHash>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLocale>
#include <QLockFile>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <lmdb.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#if defined(Q_OS_UNIX)
#include <sys/resource.h>
#include <unistd.h>
#endif
#if defined(Q_OS_LINUX)
#include <linux/ioprio.h>
#include <sched.h>
#include <sys/syscall.h>
#endif

namespace {
namespace fs= std::filesystem;

constexpr const char* checkpoint_schema= "native-shaping-v1";

int maximum_workers () {
  int processors= std::max (1, QThread::idealThreadCount ());
#if defined(Q_OS_LINUX)
  // nproc uses the calling process's permitted CPUs, not all host CPUs.
  cpu_set_t allowed;
  if (::sched_getaffinity (0, sizeof (allowed), &allowed) == 0)
    processors= std::max (1, CPU_COUNT (&allowed));
#endif
  return std::max (1, processors - 1);
}

struct scan_document {
  QString relative;
  QString absolute;
  QString stamp;
  std::uint64_t rank= 0;
};

struct scan_result {
  std::vector<scan_document> documents;
  QHash<QString,QString> saved;
  QString error;
  int vault_count= 0;
};

QHash<QString,QString> read_checkpoints (const QString& file);

std::uint64_t stable_rank (const std::string& name) {
  std::uint64_t value= 14695981039346656037ULL;
  for (unsigned char byte: name) {
    value^= byte;
    value*= 1099511628211ULL;
  }
  return value;
}

QString current_stamp (const fs::path& path) {
  std::error_code ec;
  const auto bytes= fs::file_size (path, ec);
  if (ec) return {};
  const auto modified= fs::last_write_time (path, ec);
  if (ec) return {};
  return QString::number (bytes) + ":" +
         QString::number (modified.time_since_epoch ().count ()) + ":" +
         QString::fromLatin1 (ATHENA_REVISION);
}

fs::path to_path (const QString& name) {
  const auto utf8= name.toUtf8 ();
  return fs::u8path (std::string (utf8.constData (), utf8.size ()));
}

QString from_path (const fs::path& path) {
  const auto utf8= path.generic_u8string ();
  return QString::fromUtf8 (utf8.data (), int (utf8.size ()));
}

scan_result scan_vault (const fs::path& root, int percent,
                        const std::atomic<bool>& cancel,
                        const QString& journal) {
  scan_result result;
  std::error_code ec;
  fs::recursive_directory_iterator iter (
    root, fs::directory_options::skip_permission_denied, ec), end;
  if (ec) {
    result.error= QString::fromStdString (ec.message ());
    return result;
  }
  for (; iter != end && !cancel.load (std::memory_order_relaxed);
       iter.increment (ec)) {
    if (ec) {
      // The directory may change while ATHENA edits a different document.
      // Skip inaccessible entries rather than blocking the active editor.
      ec.clear ();
      continue;
    }
    const auto path= iter->path ();
    if (iter->is_symlink (ec)) {
      if (iter->is_directory (ec)) iter.disable_recursion_pending ();
      ec.clear ();
      continue;
    }
    if (iter->is_directory (ec)) {
      if (path.filename () == ".athena" || path.filename () == ".backup" ||
          path.filename () == ".git")
        iter.disable_recursion_pending ();
      continue;
    }
    if (ec || !iter->is_regular_file (ec) || path.extension () != ".ath") {
      ec.clear ();
      continue;
    }
    const auto relative= path.lexically_relative (root).generic_u8string ();
    auto stamp= current_stamp (path);
    if (stamp.isEmpty ()) continue;
    result.documents.push_back ({
      QString::fromUtf8 (relative.data (), int (relative.size ())),
      from_path (path), std::move (stamp), stable_rank (relative)});
  }
  if (cancel.load (std::memory_order_relaxed)) {
    result.documents.clear ();
    return result;
  }
  std::sort (result.documents.begin (), result.documents.end (),
    [] (const scan_document& a, const scan_document& b) {
      return a.rank == b.rank ? a.relative < b.relative : a.rank < b.rank;
    });
  result.vault_count= int (result.documents.size ());
  result.documents.resize (
    (result.documents.size () * std::size_t (percent) + 99) / 100);
  if (!cancel.load (std::memory_order_relaxed))
    result.saved= read_checkpoints (journal);
  return result;
}

QString cache_root () {
  const QString configured= qEnvironmentVariable ("ATHENA_HOME_PATH");
  const QString home= configured.isEmpty () ?
    QDir::homePath () + "/.ATHENA" : configured;
  return QDir (home).filePath ("system/cache");
}

std::optional<std::size_t> shaping_entry_count (const QString& directory) {
  // LMDB exposes the named database's entry count in its B-tree metadata.
  // This is a short read-only transaction, not a scan of the cache records.
  MDB_env* raw= nullptr;
  if (mdb_env_create (&raw) != MDB_SUCCESS) return {};
  std::unique_ptr<MDB_env, decltype (&mdb_env_close)> env (raw, mdb_env_close);
  if (mdb_env_set_maxdbs (env.get (), 4) != MDB_SUCCESS) return {};
  const auto utf8= directory.toUtf8 ();
  if (mdb_env_open (env.get (), utf8.constData (),
                    MDB_RDONLY | MDB_NOTLS, 0400) != MDB_SUCCESS) return {};
  MDB_txn* txn= nullptr;
  if (mdb_txn_begin (env.get (), nullptr, MDB_RDONLY, &txn) != MDB_SUCCESS)
    return {};
  MDB_dbi dbi= 0;
  MDB_stat statistics {};
  const bool ok= mdb_dbi_open (txn, "runs", 0, &dbi) == MDB_SUCCESS &&
                 mdb_stat (txn, dbi, &statistics) == MDB_SUCCESS;
  mdb_txn_abort (txn);
  if (!ok) return {};
  return statistics.ms_entries;
}

QString journal_for_vault (const QString& root) {
  const auto hash= QCryptographicHash::hash (
    root.toUtf8 (), QCryptographicHash::Sha256).toHex ();
  return cache_root () + "/native-shaping-v1/checkpoints/" +
         QString::fromLatin1 (hash) + ".jsonl";
}

QHash<QString,QString> read_checkpoints (const QString& file) {
  QHash<QString,QString> saved;
  QFile input (file);
  if (!input.open (QIODevice::ReadOnly)) return saved;
  while (!input.atEnd ()) {
    const auto json= QJsonDocument::fromJson (input.readLine ());
    if (!json.isObject ()) continue;
    const auto data= json.object ();
    if (data.value ("schema").toString () !=
        QString::fromLatin1 (checkpoint_schema)) continue;
    const auto path= data.value ("file").toString ();
    if (!path.isEmpty ()) saved.insert (path, data.value ("stamp").toString ());
  }
  return saved;
}

bool append_checkpoint (const QString& file, const scan_document& document) {
  QFile journal (file);
  if (!journal.open (QIODevice::WriteOnly | QIODevice::Append)) return false;
  QJsonObject data {
    {"schema", QString::fromLatin1 (checkpoint_schema)},
    {"file", document.relative}, {"stamp", document.stamp}};
  const auto line= QJsonDocument (data).toJson (QJsonDocument::Compact) + "\n";
  if (journal.write (line) != line.size () || !journal.flush ()) return false;
#if defined(Q_OS_UNIX)
  // A completed document must remain checked off after a process crash.
  if (::fsync (journal.handle ()) != 0) return false;
#endif
  return true;
}

class CacheManagerPane final : public QWidget {
public:
  CacheManagerPane () {
    setObjectName ("athena-cache-manager-content");
    auto* layout= new QVBoxLayout (this);

    auto* maintenance= new QGroupBox ("System cache");
    auto* maintenance_layout= new QVBoxLayout (maintenance);
    auto* actions= new QVBoxLayout;
    refresh_styles= new QPushButton ("Refresh style cache");
    clean= new QPushButton ("Clean cache");
    actions->addWidget (refresh_styles);
    actions->addWidget (clean);
    maintenance_layout->addLayout (actions);
    layout->addWidget (maintenance);

    auto* shaping= new QGroupBox ("Shaping cache · Vault prewarm");
    auto* shaping_layout= new QVBoxLayout (shaping);
    vault_label= new QLabel;
    vault_label->setWordWrap (true);
    shaping_layout->addWidget (vault_label);
    auto* controls= new QVBoxLayout;
    sample= new QSpinBox;
    sample->setRange (1, 100);
    sample->setSuffix ("%");
    sample->setValue (25);
    all= new QCheckBox ("Entire Vault (100%)");
    all->setChecked (true);
    sample->setEnabled (false);
    controls->addWidget (all);
    auto* sampling= new QHBoxLayout;
    sampling->addWidget (new QLabel ("Sample:"));
    sampling->addWidget (sample);
    sampling->addStretch ();
    controls->addLayout (sampling);
    auto* concurrency= new QHBoxLayout;
    concurrency->addWidget (new QLabel ("Workers:"));
    worker_count= new QSpinBox;
    const int limit= maximum_workers ();
    worker_count->setRange (1, limit);
    worker_count->setValue (std::min (4, limit));
    worker_count->setToolTip (
      QString ("At most %1 workers (nproc - 1).").arg (limit));
    concurrency->addWidget (worker_count);
    concurrency->addWidget (
      new QLabel (QString ("Maximum: %1").arg (limit)));
    concurrency->addStretch ();
    controls->addLayout (concurrency);
    low_priority= new QCheckBox ("Lower worker priority (CPU and I/O)");
    low_priority->setChecked (true);
    controls->addWidget (low_priority);
    shaping_layout->addLayout (controls);

    explanation= new QLabel (
      "Builds in independent ATHENA worker processes. You can continue editing; "
      "stopping preserves cached results and completed-file progress.");
    explanation->setWordWrap (true);
    shaping_layout->addWidget (explanation);

    progress= new QProgressBar;
    progress->setRange (0, 1);
    progress->setValue (0);
    progress->setFormat ("%v / %m documents");
    shaping_layout->addWidget (progress);
    status= new QLabel ("Ready");
    status->setWordWrap (true);
    shaping_layout->addWidget (status);

    auto* buttons= new QVBoxLayout;
    start= new QPushButton ("Build / Resume");
    stop= new QPushButton ("Stop");
    stop->setEnabled (false);
    buttons->addWidget (start);
    buttons->addWidget (stop);
    buttons->addStretch ();
    shaping_layout->addLayout (buttons);
    layout->addWidget (shaping);

    auto* storage= new QGroupBox ("Shaping cache storage");
    auto* storage_layout= new QVBoxLayout (storage);
    usage= new QLabel;
    usage->setWordWrap (true);
    storage_layout->addWidget (usage);
    auto* storage_actions= new QVBoxLayout;
    auto* open_directory= new QPushButton ("Open cache directory");
    auto* refresh_statistics= new QPushButton ("Refresh statistics");
    storage_actions->addWidget (open_directory);
    storage_actions->addWidget (refresh_statistics);
    storage_layout->addLayout (storage_actions);
    layout->addWidget (storage);
    log= new QPlainTextEdit;
    log->setReadOnly (true);
    log->setMaximumBlockCount (1000);
    layout->addWidget (log, 1);

    connect (all, &QCheckBox::toggled, this,
             [this] (bool checked) { sample->setEnabled (!checked); });
    connect (refresh_styles, &QPushButton::clicked, this, [this] {
      get_server ()->style_clear_cache ();
      log->appendPlainText ("Style cache refreshed.");
    });
    connect (clean, &QPushButton::clicked, this, [this] {
      tools_clean_cache ();
      refresh_status ();
    });
    connect (open_directory, &QPushButton::clicked, this, [this] {
      const QString directory= cache_root ();
      if (!QDir ().mkpath (directory) ||
          !QDesktopServices::openUrl (QUrl::fromLocalFile (directory)))
        QMessageBox::warning (
          this, "Cache manager", "Could not open the cache directory.");
    });
    connect (refresh_statistics, &QPushButton::clicked,
             this, [this] { refresh_status (); });
    connect (start, &QPushButton::clicked, this, [this] { begin (); });
    connect (stop, &QPushButton::clicked, this, [this] { cancel (); });
    refresh_status ();
  }

  ~CacheManagerPane () override {
    if (scan_cancel) scan_cancel->store (true);
    for (const auto& task: workers) {
      task->process->disconnect (this);
      if (task->process->state () != QProcess::NotRunning) {
        task->process->kill ();
        task->process->waitForFinished (1000);
      }
    }
  }

  void refresh_status () {
    if (!running) {
      const auto context= vault_capture_context ();
      vault_label->setText (context ?
        "Vault: " + from_path (context->root) :
        "No active Vault. Open a Vault to build its shaping cache.");
    }
    const auto root= cache_root () + "/native-shaping-v1";
    const auto body= QFileInfo (root + "/body/data.mdb");
    const auto math= QFileInfo (root + "/math/data.mdb");
    const auto size= [] (const QFileInfo& file) {
      return file.exists () ? file.size () : qint64 (0);
    };
    const auto describe= [] (const QFileInfo& file) {
      if (!file.exists ()) return QString ("not created");
      QString description= QLocale ().formattedDataSize (file.size ());
      const auto entries= shaping_entry_count (file.absolutePath ());
      description+= entries ?
        QString (" · %1 entries").arg (QLocale ().toString (qulonglong (*entries))) :
        QString (" · entries unavailable");
      return description;
    };
    usage->setText (
      "Total: " + QLocale ().formattedDataSize (size (body) + size (math)) +
      "\nBody: " + describe (body) + "\nMath: " + describe (math));
    start->setEnabled (!running && vault_active ());
  }

private:
  QPushButton *refresh_styles, *clean, *start, *stop;
  QCheckBox *all, *low_priority;
  QSpinBox *sample, *worker_count;
  QLabel *vault_label, *explanation, *status, *usage;
  QProgressBar* progress;
  QPlainTextEdit* log;
  struct worker_task {
    QProcess* process= nullptr;
    int index= 0;
    QString output;
    QByteArray output_log;
  };
  QHash<QProcess*,std::shared_ptr<worker_task>> workers;
  std::unique_ptr<QTemporaryDir> temporary;
  std::unique_ptr<QLockFile> vault_lock;
  std::shared_ptr<std::atomic<bool>> scan_cancel;
  std::vector<scan_document> documents;
  QString journal_path, current_vault;
  QHash<QString,QString> completed_stamps;
  int next_document= 0, worker_limit= 1;
  int processed= 0, completed= 0, failures= 0;
  bool running= false, cancel_requested= false;
  bool scan_pending= false;

  void set_running (bool value) {
    running= value;
    start->setEnabled (!value);
    stop->setEnabled (value);
    clean->setEnabled (!value);
    all->setEnabled (!value);
    sample->setEnabled (!value && !all->isChecked ());
    worker_count->setEnabled (!value);
    low_priority->setEnabled (!value);
  }

  void begin () {
    if (running) return;
    const auto context= vault_capture_context ();
    if (!context) {
      status->setText ("Load a Vault before building its shaping cache.");
      return;
    }
    current_vault= from_path (context->root.lexically_normal ());
    journal_path= journal_for_vault (current_vault);
    if (!QDir ().mkpath (QFileInfo (journal_path).absolutePath ())) {
      status->setText ("Cannot create cache checkpoint directory.");
      return;
    }
    // Permit one prewarm coordinator across ATHENA instances. That coordinator
    // can run multiple user-configured workers against the shared LMDB store.
    vault_lock= std::make_unique<QLockFile> (
      cache_root () + "/native-shaping-v1/prewarm.lock");
    if (!vault_lock->tryLock (0)) {
      vault_lock.reset ();
      status->setText ("A shaping cache build is already active in another ATHENA instance.");
      return;
    }
    set_running (true);
    cancel_requested= false;
    next_document= processed= completed= failures= 0;
    worker_limit= worker_count->value ();
    scan_pending= true;
    documents.clear ();
    progress->setRange (0, 0);
    status->setText ("Scanning Vault in background...");
    vault_label->setText ("Vault: " + current_vault);
    scan_cancel= std::make_shared<std::atomic<bool>> (false);
    auto result= std::make_shared<scan_result> ();
    const auto cancelled= scan_cancel;
    const auto root= context->root;
    const int percent= all->isChecked () ? 100 : sample->value ();
    const auto journal= journal_path;
    QThread* scanner= QThread::create ([result, cancelled, root, percent, journal] {
      *result= scan_vault (root, percent, *cancelled, journal);
    });
    connect (scanner, &QThread::finished, scanner, &QObject::deleteLater);
    connect (scanner, &QThread::finished, this, [this, cancelled, result] {
      scan_pending= false;
      if (!running) return;
      if (cancelled->load ()) { finish (false); return; }
      if (!result->error.isEmpty ()) {
        status->setText ("Vault scan failed: " + result->error);
        finish (true);
        return;
      }
      documents= std::move (result->documents);
      completed_stamps= std::move (result->saved);
      for (const auto& file: documents)
        if (completed_stamps.value (file.relative) == file.stamp) ++completed;
      processed= completed;
      progress->setRange (0, int (documents.size ()));
      progress->setValue (processed);
      log->appendPlainText (
        QString ("Selected %1 of %2 Vault documents; %3 already cached; %4 workers.")
        .arg (documents.size ()).arg (result->vault_count).arg (completed)
        .arg (worker_limit));
      temporary= std::make_unique<QTemporaryDir> (
        QDir::tempPath () + "/ATHENA-shaping-prewarm-XXXXXX");
      if (!temporary->isValid ()) {
        status->setText ("Could not create temporary output directory.");
        finish (true);
        return;
      }
      QTimer::singleShot (0, this, [this] { launch_available (); });
    });
    scanner->start (QThread::LowPriority);
  }

  void worker_finished (const std::shared_ptr<worker_task>& task,
                        int code, QProcess::ExitStatus outcome) {
    if (!running || !workers.contains (task->process)) return;
    task->output_log+= task->process->readAll ();
    if (task->output_log.size () > 8192)
      task->output_log.remove (0, task->output_log.size () - 8192);
    workers.remove (task->process);
    task->process->deleteLater ();
    const auto& document= documents[task->index];
    const bool good= outcome == QProcess::NormalExit && code == 0 &&
      QFileInfo::exists (task->output) &&
      QFileInfo (task->output).size () > 0 &&
      current_stamp (to_path (document.absolute)) == document.stamp;
    bool recorded= false;
    if (good) {
      if (append_checkpoint (journal_path, document)) {
        ++completed;
        completed_stamps.insert (document.relative, document.stamp);
        recorded= true;
      }
      else {
        ++failures;
        log->appendPlainText (
          "Could not save resume checkpoint for " + document.relative);
        cancel ();
      }
    }
    else if (!cancel_requested) {
      ++failures;
      log->appendPlainText (
        "Could not prewarm " + document.relative + ": " +
        QString::fromUtf8 (task->output_log.right (600)).trimmed ());
    }
    // Interrupted conversions remain pending for the next Build / Resume.
    if (!cancel_requested || recorded) ++processed;
    progress->setValue (processed);
    QFile::remove (task->output);
    if (cancel_requested) {
      if (workers.isEmpty () && !scan_pending) finish (false);
      return;
    }
    launch_available ();
  }

  void launch_worker (int index) {
    const auto& document= documents[index];
    auto task= std::make_shared<worker_task> ();
    task->index= index;
    task->output= temporary->path () + "/" + QString::number (index) + ".pdf";
    task->process= new QProcess (this);
    QProcess* process= task->process;
    process->setProcessChannelMode (QProcess::MergedChannels);
    process->setProgram (QCoreApplication::applicationFilePath ());
    process->setWorkingDirectory (current_vault);
    // Export reference preparation requires the source Vault's location
    // snapshot. A worker is an independent process, so it cannot inherit the
    // editor's already loaded Vault merely from its working directory.
    process->setArguments (
      {"-H", "-vault", current_vault, "-C", document.absolute, task->output});
    auto environment= QProcessEnvironment::systemEnvironment ();
    environment.insert ("ATHENA_SHAPING_BUILDER", "1");
    // A prewarm process must never connect to the interactive Qt display.
    // It still needs a QGui-capable application for SVG text/font rendering.
    environment.insert ("QT_QPA_PLATFORM", "offscreen");
    process->setProcessEnvironment (environment);
#if defined(Q_OS_UNIX)
    if (low_priority->isChecked ())
      process->setChildProcessModifier ([] {
        // This executes after fork; do not invoke Qt APIs here.
        ::setpriority (PRIO_PROCESS, 0, 19);
#if defined(Q_OS_LINUX)
        ::syscall (SYS_ioprio_set, IOPRIO_WHO_PROCESS, 0,
                   IOPRIO_PRIO_VALUE (IOPRIO_CLASS_IDLE, 0));
#endif
      });
#endif
    connect (process, &QProcess::readyRead, this, [task] {
      task->output_log+= task->process->readAll ();
      if (task->output_log.size () > 8192)
        task->output_log.remove (0, task->output_log.size () - 8192);
    });
    connect (process, &QProcess::errorOccurred, this,
      [this, task] (QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart ||
            !workers.contains (task->process)) return;
        log->appendPlainText ("Could not launch ATHENA worker: " +
                              task->process->errorString ());
        cancel ();
        worker_finished (task, -1, QProcess::CrashExit);
      });
    connect (process, qOverload<int,QProcess::ExitStatus> (&QProcess::finished),
      this, [this, task] (int code, QProcess::ExitStatus outcome) {
        worker_finished (task, code, outcome);
      });
    workers.insert (process, std::move (task));
    process->start ();
  }

  void launch_available () {
    if (!running || cancel_requested) return;
    while (workers.size () < worker_limit) {
      while (next_document < int (documents.size ()) &&
             completed_stamps.value (documents[next_document].relative) ==
               documents[next_document].stamp)
        ++next_document;
      if (next_document >= int (documents.size ())) break;
      launch_worker (next_document++);
    }
    if (workers.isEmpty () && next_document >= int (documents.size ())) {
      finish (false);
      return;
    }
    status->setText (
      QString ("Building · %1 / %2 processed · %3 active worker(s)")
        .arg (processed).arg (documents.size ()).arg (workers.size ()));
  }

  void cancel () {
    if (!running || cancel_requested) return;
    cancel_requested= true;
    status->setText ("Stopping workers... completed files will remain cached.");
    if (scan_cancel) scan_cancel->store (true);
    const auto active= workers.values ();
    for (const auto& task: active) {
      QProcess* process= task->process;
      if (process->state () == QProcess::NotRunning) continue;
      const qint64 pid= process->processId ();
      process->terminate ();
      QPointer<QProcess> guarded (process);
      QTimer::singleShot (2000, this, [guarded, pid] {
        if (guarded && guarded->state () != QProcess::NotRunning &&
            guarded->processId () == pid) guarded->kill ();
      });
    }
    if (workers.isEmpty () && !scan_pending) finish (false);
  }

  void finish (bool error) {
    if (!running) return;
    if (progress->minimum () == 0 && progress->maximum () == 0) {
      // A stopped or failed scan must not leave an indeterminate progress
      // animation running after its worker thread has exited.
      progress->setRange (0, 1);
      progress->setValue (0);
    }
    if (!error) {
      status->setText (
        (cancel_requested ? "Stopped" : "Finished") +
        QString (": %1 of %2 documents cached; %3 failure(s). "
                 "Build / Resume retains completed progress.")
          .arg (completed).arg (documents.size ()).arg (failures));
    }
    set_running (false);
    scan_cancel.reset ();
    temporary.reset ();
    vault_lock.reset ();
    refresh_status ();
  }
};

QPointer<CacheManagerPane> cache_pane;
QPointer<ads::CDockWidget> cache_dock;

} // namespace

void cache_manager_show () {
  if (qt_defer_to_main_thread (cache_manager_show)) return;
  auto* window= QTMMainTabWindow::topTabWindow ();
  if (!window || !window->dockManager ()) return;
  if (!cache_pane) {
    cache_pane= new CacheManagerPane;
    cache_pane->resize (500, 450);
  }
  else cache_pane->refresh_status ();
  if (!cache_dock) {
    cache_dock= new ads::CDockWidget ("Cache manager");
    cache_dock->setObjectName ("athena-cache-manager");
    cache_dock->setWidget (cache_pane, ads::CDockWidget::ForceNoScrollArea);
    cache_dock->setFeature (ads::CDockWidget::DockWidgetDeleteOnClose, false);
  }
  window->showAdsDockWidget (cache_dock, ads::RightDockWidgetArea);
  cache_dock->show ();
  cache_dock->raise ();
}

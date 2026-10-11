/******************************************************************************
* MODULE     : QTMContinuousRag.cpp
* DESCRIPTION: Continuous active-vault scanning and isolated NPU coordination
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMContinuousRag.hpp"
#include "QTMSystemPowerMonitor.hpp"

#include "rag_index.hpp"
#include "rag_embedding_contract.hpp"
#include "rag_realtime_generation.hpp"
#include "ATHENA/Data/node_location_cache.hpp"
#include "ATHENA/Data/background_workers.hpp"
#include "scheme.hpp"
#include "tm_ostream.hpp"
#include "vault.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QThreadPool>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

QString
pref (const char* key, const char* fallback= "") {
  string value= get_preference (string (key), string (fallback));
  return QString::fromUtf8 (as_charp (value), N(value));
}

bool
pref_on (const char* key) {
  return pref (key, "off") == QStringLiteral ("on");
}

std::string
std_string (const QString& value) {
  QByteArray utf8= value.toUtf8 ();
  return std::string (utf8.constData (), std::size_t (utf8.size ()));
}

QThreadPool&
rag_index_pool () {
  // Intentionally process-lifetime.  The editor must not wait for parsing on
  // shutdown; every task carries a QPointer plus an atomic generation guard.
  static QThreadPool* pool= [] {
    auto* result= new QThreadPool;
    result->setMaxThreadCount (1);
    result->setExpiryTimeout (-1);
    return result;
  } ();
  return *pool;
}

QString
worker_program () {
  return QDir (QCoreApplication::applicationDirPath ())
    .filePath (QStringLiteral ("athena-rag-npu-worker"));
}

QString
openvino_cache_dir () {
  QString home= QString::fromUtf8 (qgetenv ("ATHENA_HOME_PATH")).trimmed ();
  if (home.isEmpty ()) home= QDir::home ().filePath (QStringLiteral (".ATHENA"));
  return QDir (home).filePath (QStringLiteral ("system/rag-openvino-cache"));
}

QProcessEnvironment
npu_worker_environment () {
  QProcessEnvironment environment= QProcessEnvironment::systemEnvironment ();
  if (!environment.contains (QStringLiteral ("ZE_INTEL_NPU_LOGLEVEL")))
    environment.insert (QStringLiteral ("ZE_INTEL_NPU_LOGLEVEL"),
                        QStringLiteral ("ERROR"));
  const QString athena_path=
    QString::fromUtf8 (qgetenv ("ATHENA_PATH")).trimmed ();
  if (athena_path.isEmpty ()) return environment;
  const QString runtime=
    QDir (athena_path).filePath (QStringLiteral ("lib/openvino-npu"));
  if (!QFileInfo (runtime).isDir ()) return environment;
  QString library_path= environment.value (QStringLiteral ("LD_LIBRARY_PATH"));
  if (library_path.isEmpty ()) library_path= runtime;
  else library_path= runtime + QChar (':') + library_path;
  environment.insert (QStringLiteral ("LD_LIBRARY_PATH"), library_path);

  // Some distributions package the Intel NPU UMD but omit the matching
  // compiler-in-driver libraries it expects beside the UMD.  A complete
  // ATHENA-private support bundle is intentionally selected only for the
  // worker process; ATHENA.bin and the rest of the desktop keep using the
  // system Level Zero/OpenVINO stack unchanged.
  const QDir runtime_dir (runtime);
  const bool private_npu_stack=
    QFileInfo (runtime_dir.filePath ("libze_intel_npu.so.1")).exists () &&
    QFileInfo (runtime_dir.filePath (
      "libopenvino_intel_npu_compiler_loader.so")).exists () &&
    QFileInfo (runtime_dir.filePath (
      "libopenvino_intel_npu_compiler.so")).exists ();
  if (private_npu_stack)
    environment.insert (
      QStringLiteral ("ZE_ENABLE_ALT_DRIVERS"),
      QStringLiteral ("libze_intel_npu.so.1"));
  return environment;
}

struct RealtimeJob {
  QString key;
  QString absolute_path;
  QString vault_root;
  QString rel_path;
  QString storage_revision;
  std::uint64_t generation= 0;
  std::uint64_t saved_generation= 0;
  vault_context_handle context;
  std::atomic<bool> current {true};
  std::shared_ptr<athena::rag::RagPreparedDocument> prepared;
  bool awaiting_worker= false;
  bool committed= false;
};

class ContinuousRagManager: public QObject {
  struct Query {
    QPointer<QObject> owner;
    QtmRagQueryCallback callback;
    std::shared_ptr<bool> cancelled;
    QString text;
    std::uint64_t generation;
    bool admitted= false;
    bool sent= false;
    QMetaObject::Connection destroyed;
    QTimer* timeout= nullptr;
  };
public:
  explicit ContinuousRagManager (QObject* parent): QObject (parent), power_ (this) {
    process_.setParent (this);
    process_.setProcessChannelMode (QProcess::SeparateChannels);
    QObject::connect (&process_, &QProcess::readyReadStandardOutput,
                      this, [this] { read_stdout (); });
    QObject::connect (&process_, &QProcess::readyReadStandardError,
                      this, [this] { read_stderr (); });
    QObject::connect (
      &process_, qOverload<int,QProcess::ExitStatus> (&QProcess::finished),
      this, [this] (int code, QProcess::ExitStatus status) {
        read_stdout ();
        read_stderr (true);
        worker_ready_= false;
        stdout_buffer_.clear ();
        if (shutting_down_) return;
        if (expected_worker_stop_) {
          expected_worker_stop_= false;
          if (has_current_embedding_work ()) ensure_worker ();
          return;
        }
        fail_queries (QStringLiteral ("NPU worker exited: ") + QString::number (code));
        athena_spdlog_warning (
          "continuous RAG: NPU worker exited code=" + std::to_string (code) +
          (status == QProcess::CrashExit ? " (crash)" : ""));
        failure ("", "NPU worker exited: " + QString::number (code));
        for (auto& entry: latest_)
          if (is_current (entry.second) && entry.second->awaiting_worker) {
            entry.second->awaiting_worker= false;
            pending_dispatch_[entry.first]= entry.second;
          }
        if (has_current_embedding_work () && compute_allowed ()) {
          if (!restart_attempted_) {
            restart_attempted_= true;
            QTimer::singleShot (500, this, [this] {
              if (shutting_down_ || !compute_allowed () ||
                  !has_current_embedding_work ()) return;
              ensure_worker ();
            });
          }
          else {
            next_scan_= Clock::now () + std::chrono::seconds (60);
            std::vector<std::shared_ptr<RealtimeJob>> failed;
            failed.reserve (latest_.size ());
            for (const auto& entry: latest_)
              if (is_current (entry.second) &&
                  (entry.second->awaiting_worker ||
                   pending_dispatch_.count (entry.first) != 0))
                failed.push_back (entry.second);
            for (const auto& job: failed) finish_job (job);
          }
        }
      });
    QObject::connect (
      &process_, &QProcess::errorOccurred, this,
      [this] (QProcess::ProcessError error) {
        if (shutting_down_) return;
        fail_queries (process_.errorString ());
        if (error == QProcess::FailedToStart) {
          athena_spdlog_warning (
            "continuous RAG: could not start NPU worker: " +
            std_string (process_.errorString ()));
          failure ("", process_.errorString ());
          fail_pending ();
        }
      });
    QObject::connect (qApp, &QCoreApplication::aboutToQuit,
                      this, [this] { shutdown (); });
    QObject::connect (&power_, &QTMSystemPowerMonitor::stateChanged,
                      this, [this] { power_state_changed (); });
    QObject::connect (&poll_, &QTimer::timeout, this, [this] { poll (); });
    power_block_= power_block_reason ();
    poll_.start (1000);
  }

  QtmRagQueryCancel embed_query (QObject* owner, QString text, QtmRagQueryCallback callback) {
    auto cancelled= std::make_shared<bool> (false);
    const auto generation= ++next_generation_;
    const QString key= QStringLiteral ("interactive-query:") +
      QString::number (generation).rightJustified (20, QChar ('0'));
    Query query {owner, std::move (callback), cancelled, std::move (text), generation};
    query.timeout= new QTimer (this);
    query.timeout->setSingleShot (true);
    query.destroyed= QObject::connect (owner, &QObject::destroyed, this,
      [this, key, cancelled] { *cancelled= true; cancel_query (key); });
    QObject::connect (query.timeout, &QTimer::timeout, this, [this, key] {
      complete_query (key, {}, {}, QStringLiteral ("NPU query timed out"), true);
    });
    query.timeout->start (120000);
    queries_.emplace (key, std::move (query));
    QPointer<ContinuousRagManager> self (this);
    QTimer::singleShot (0, this, [this, key] {
      auto found= queries_.find (key);
      if (found == queries_.end ()) return;
      QString error= interactive_error ();
      if (found->second.text.trimmed ().isEmpty ()) error= QStringLiteral ("Query is empty");
      if (found->second.text.size () > 1024 * 1024) error= QStringLiteral ("Query is too large");
      if (queries_.size () > 64) error= QStringLiteral ("NPU query queue is full");
      if (!error.isEmpty ()) { complete_query (key, {}, {}, error); return; }
      found->second.admitted= true;
      ensure_worker ();
      dispatch_queries ();
    });
    return [self, key, cancelled] {
      Q_ASSERT (!self || QThread::currentThread () == self->thread ());
      *cancelled= true;
      if (self) self->cancel_query (key);
    };
  }

  void saved (const QString& saved_file, const QString& vault_root,
               const QString& storage_revision, std::uint64_t save_sequence) {
    (void) storage_revision;
    if (shutting_down_ || save_sequence == 0) return;
    const QFileInfo root_info (vault_root);
    const QFileInfo file_info (saved_file);
    const QString root= QDir::cleanPath (root_info.absoluteFilePath ());
    const QString file= QDir::cleanPath (file_info.absoluteFilePath ());
    QString rel= QDir (root).relativeFilePath (file);
    if (rel.isEmpty () || rel == QStringLiteral (".") ||
        rel.startsWith (QStringLiteral ("../")) || rel == QStringLiteral ("..") ||
        QFileInfo (rel).suffix () != QStringLiteral ("ath")) return;

    const QString key= root + QChar ('\n') + rel;
    auto found= latest_.find (key);
    if (found != latest_.end () &&
        found->second->saved_generation < save_sequence) {
      auto previous= found->second;
      previous->current.store (false, std::memory_order_release);
      send_cancel (previous);
      finish_job (previous);
    }
    // Never enqueue or prioritize this document: the sweep will encounter it
    // normally, including documents that have never been opened or saved here.
  }

  void shutdown () {
    if (shutting_down_) return;
    shutting_down_= true;
    fail_queries (QStringLiteral ("NPU worker is shutting down"));
    poll_.stop ();
    if (inventory_) inventory_->store (false, std::memory_order_release);
    for (auto& entry: latest_)
      entry.second->current.store (false, std::memory_order_release);
    latest_.clear ();
    pending_dispatch_.clear ();

    athena::background::publish (athena::background::worker::rag, {});

    if (process_.state () != QProcess::NotRunning) {
      QJsonObject message;
      message["op"]= QStringLiteral ("shutdown");
      write_worker (message);
      process_.kill ();
    }
  }

private:
  using Clock= std::chrono::steady_clock;

  QString interactive_error () const {
    if (shutting_down_) return QStringLiteral ("NPU worker is shutting down");
    if (pref ("rag realtime npu power policy", "ac-only").trimmed () == QStringLiteral ("paused"))
      return QStringLiteral ("NPU computation is paused by preference");
    const auto state= power_.state ();
    if (state.powerSaverKnown && state.powerSaver)
      return QStringLiteral ("NPU queries are unavailable in system power saver");
    if (configured_model ().isEmpty () || configured_tokenizer ().isEmpty ())
      return QStringLiteral ("NPU model or tokenizer is not configured");
    return {};
  }

  void complete_query (QString key, std::vector<float> vector,
      std::string space, QString error, bool cancel_worker= false) {
    auto found= queries_.find (key);
    if (found == queries_.end ()) return;
    Query query= std::move (found->second);
    queries_.erase (found);
    QObject::disconnect (query.destroyed);
    query.timeout->stop ();
    query.timeout->deleteLater ();
    if (cancel_worker && query.sent) {
      QJsonObject message;
      message["op"]= QStringLiteral ("cancel");
      message["key"]= key;
      message["interactive"]= true;
      message["generation"]= QString::number (query.generation);
      write_worker (message);
    }
    if (query.owner && !*query.cancelled && query.callback)
      QTimer::singleShot (0, query.owner,
        [callback= std::move (query.callback), cancelled= query.cancelled,
         vector= std::move (vector), space= std::move (space), error= std::move (error)] () mutable {
          if (!*cancelled) callback (std::move (vector), std::move (space), std::move (error));
        });
  }

  void cancel_query (const QString& key) { complete_query (key, {}, {}, {}, true); }

  void fail_queries (const QString& error) {
    while (!queries_.empty ()) complete_query (queries_.begin ()->first, {}, {}, error, true);
  }

  void dispatch_queries () {
    if (queries_.empty ()) return;
    const QString error= interactive_error ();
    if (!error.isEmpty ()) { fail_queries (error); return; }
    if (!worker_ready_ || expected_worker_stop_) return;
    if (worker_config_key_ != config_key ()) {
      stop_worker_for_reconfiguration ();
      return;
    }
    std::vector<QString> keys;
    for (const auto& entry: queries_) keys.push_back (entry.first);
    for (const auto& key: keys) {
      auto found= queries_.find (key);
      if (found == queries_.end ()) continue;
      auto& query= found->second;
      if (!query.admitted || query.sent) continue;
      QJsonObject message;
      message["op"]= QStringLiteral ("embed");
      message["key"]= key;
      message["generation"]= QString::number (query.generation);
      message["interactive"]= true;
      message["texts"]= QJsonArray {query.text};
      query.sent= true;
      write_worker (message);
    }
  }

  enum class PowerBlock {
    None,
    Preference,
    Battery,
    Unknown,
    Saver
  };

  PowerBlock power_block_reason () const {
    const QString policy= pref (
      "rag realtime npu power policy", "ac-only").trimmed ();
    if (policy == QStringLiteral ("paused")) return PowerBlock::Preference;
    const QTMSystemPowerMonitor::State state= power_.state ();
    if (state.powerSaverKnown && state.powerSaver) return PowerBlock::Saver;
    if (policy == QStringLiteral ("always")) return PowerBlock::None;
    if (state.supply == QTMSystemPowerMonitor::Supply::External)
      return PowerBlock::None;
    if (state.supply == QTMSystemPowerMonitor::Supply::Battery)
      return PowerBlock::Battery;
    return PowerBlock::Unknown;
  }

  bool compute_allowed () const {
    return power_block_ == PowerBlock::None;
  }

  QString power_pause_detail () const {
    switch (power_block_) {
    case PowerBlock::Preference: return QStringLiteral ("Paused by preference");
    case PowerBlock::Battery: return QStringLiteral ("Waiting for external power");
    case PowerBlock::Unknown: return QStringLiteral ("Power state unavailable");
    case PowerBlock::Saver: return QStringLiteral ("System power saver");
    case PowerBlock::None: break;
    }
    return {};
  }

  void resume_pending_dispatch () {
    if (!compute_allowed ()) return;
    std::vector<std::shared_ptr<RealtimeJob>> pending;
    pending.reserve (pending_dispatch_.size ());
    for (const auto& entry: pending_dispatch_)
      if (is_current (entry.second)) pending.push_back (entry.second);
    for (const auto& job: pending) dispatch (job);
    if (latest_.empty ()) advance ();
  }

  void power_state_changed () {
    if (shutting_down_) return;
    dispatch_queries ();
    const PowerBlock next= power_block_reason ();
    if (next == power_block_) return;
    power_block_= next;
    if (compute_allowed ()) resume_pending_dispatch ();
    report ();
  }

  void report () {
    namespace bg= athena::background;
    if (shutting_down_ || !context_) { bg::publish (bg::worker::rag, {}); return; }
    const bool busy= inventory_ || !latest_.empty () || next_file_ < files_.size ();
    const auto phase= !failures_.empty () ? bg::phase::error :
      !compute_allowed () ? bg::phase::paused :
      busy ? bg::phase::working : bg::phase::idle;
    std::string detail= !compute_allowed () ? std_string (power_pause_detail ()):
      inventory_ ? "Inventory" : "";
    if (compute_allowed () && !latest_.empty ())
      detail= std_string (latest_.begin ()->second->rel_path);
    std::string error;
    if (!failures_.empty ())
      error= std_string (failures_.begin ()->first) + ": " +
        std_string (failures_.begin ()->second);
    bg::publish (bg::worker::rag,
      {phase, completed_files_, total_files_, failures_.size (), std::move (detail), std::move (error)});
  }

  void failure (const QString& key, const QString& message) {
    failures_[key]= message;
    report ();
  }

  void fail_pending () {
    if (failures_.empty ()) failure ("", "NPU request failed or returned invalid data");
    next_scan_= Clock::now () + std::chrono::seconds (60);
    std::vector<std::shared_ptr<RealtimeJob>> jobs;
    for (const auto& entry: latest_) jobs.push_back (entry.second);
    for (const auto& job: jobs) finish_job (job);
  }

  void poll () {
    if (shutting_down_) return;
    dispatch_queries ();
    const PowerBlock next_power_block= power_block_reason ();
    const bool power_changed= next_power_block != power_block_;
    power_block_= next_power_block;
    auto context= vault_capture_context ();
    if (!pref_on ("rag realtime npu enabled")) context.reset ();
    QString configuration= config_key ();
    const bool configuration_changed= configuration != scan_configuration_;
    if (context != context_ || configuration_changed) {
      if (inventory_) inventory_->store (false, std::memory_order_release);
      inventory_.reset ();
      for (auto& entry: latest_) {
        send_cancel (entry.second);
        entry.second->current.store (false, std::memory_order_release);
      }
      latest_.clear ();
      pending_dispatch_.clear ();
      files_.clear ();
      next_file_= 0;
      completed_files_= total_files_= 0;
      failures_.clear ();
      context_= std::move (context);
      source_watch_.reset ();
      scanned_revision_= 0;
      needs_sweep_= true;
      scan_configuration_= configuration;
      next_scan_= Clock::now ();
      if (configuration_changed && !worker_config_key_.isEmpty () &&
          worker_config_key_ != configuration) stop_worker_for_reconfiguration ();
    }
    // The actor updates the registry before posting its GUI save notification.
    if (!latest_.empty () && !is_current (latest_.begin ()->second)) {
      auto job= latest_.begin ()->second;
      send_cancel (job);
      needs_sweep_= true;
      finish_job (job);
    }
    if (power_changed && compute_allowed ()) resume_pending_dispatch ();
    else advance ();
    report ();
  }

  void advance () {
    if (shutting_down_ || !context_ ||
        !vault_context_is_current (context_) || !latest_.empty () ||
        inventory_ || !compute_allowed () || Clock::now () < next_scan_) return;
    if (configured_model ().isEmpty () || configured_tokenizer ().isEmpty ()) {
      ensure_worker ();
      return;
    }
    if (next_file_ < files_.size ()) {
      fs::path file= files_[next_file_++];
      auto job= std::make_shared<RealtimeJob> ();
      job->context= context_;
      job->absolute_path= QString::fromStdString (file.string ());
      job->vault_root= QString::fromStdString (context_->root.string ());
      job->rel_path= QString::fromStdString (
        file.lexically_relative (context_->root).generic_string ());
      job->key= job->vault_root + QChar ('\n') + job->rel_path;
      job->generation= ++next_generation_;
      job->saved_generation= athena::rag::rag_saved_generation (file);
      latest_[job->key]= job;
      restart_attempted_= false;
      report ();
      prepare (job);
      return;
    }
    try {
      if (!source_watch_)
        source_watch_= std::make_shared<athena::background::source_watch> ();
      const auto revision= source_watch_->revision ();
      if (!needs_sweep_ && failures_.empty () && revision == scanned_revision_) return;
      scanned_revision_= revision;
      needs_sweep_= false;
    }
    catch (const std::exception& ex) {
      failure ("", QString::fromUtf8 (ex.what ()));
      next_scan_= Clock::now () + std::chrono::seconds (30);
      return;
    }
    auto active= std::make_shared<std::atomic<bool>> (true);
    inventory_= active;
    completed_files_= total_files_= 0;
    report ();
    auto context= context_;
    QPointer<ContinuousRagManager> self (this);
    auto watch= source_watch_;
    rag_index_pool ().start ([self, context, active, watch] {
      std::vector<fs::path> files;
      QString error;
      try {
        files= athena::rag::rag_document_files (context->root, [active, context] {
          return active->load (std::memory_order_acquire) &&
            vault_context_is_current (context);
        }, watch.get ());
      }
      catch (const std::exception& ex) { error= QString::fromUtf8 (ex.what ()); }
      if (!self || !active->load (std::memory_order_acquire)) return;
      QMetaObject::invokeMethod (self,
        [self, context, active, error, files=std::move (files)] () mutable {
          if (!self || self->inventory_ != active ||
              !vault_context_is_current (context)) return;
          self->inventory_.reset ();
          if (!error.isEmpty ()) {
            self->failure ("", error);
            self->next_scan_= Clock::now () + std::chrono::seconds (30);
            return;
          }
          self->files_= std::move (files);
          self->next_file_= 0;
          self->total_files_= self->files_.size ();
          // Errors for deleted files no longer describe outstanding work.
          for (auto it= self->failures_.begin (); it != self->failures_.end (); ) {
            if (!it->first.isEmpty () && !QFileInfo (it->first).exists ())
              it= self->failures_.erase (it);
            else ++it;
          }
          if (self->files_.empty ())
            self->next_scan_= Clock::now () + std::chrono::seconds (30);
          else self->advance ();
          self->report ();
        }, Qt::QueuedConnection);
    });
  }

  bool is_current (const std::shared_ptr<RealtimeJob>& job) const {
    if (!job || shutting_down_ ||
        !job->current.load (std::memory_order_acquire)) return false;
    if (!athena::rag::rag_saved_generation_is_current (
          fs::path (std_string (job->absolute_path)), job->saved_generation) ||
        !vault_context_is_current (job->context))
      return false;
    auto found= latest_.find (job->key);
    return found != latest_.end () && found->second == job;
  }

  void finish_job (const std::shared_ptr<RealtimeJob>& job) {
    if (!job) return;
    auto found= latest_.find (job->key);
    if (found == latest_.end () || found->second != job) return;
    pending_dispatch_.erase (job->key);
    job->current.store (false, std::memory_order_release);
    latest_.erase (found);
    completed_files_= next_file_;
    if (next_file_ >= files_.size ()) {
      files_.clear ();
      next_file_= 0;
      next_scan_= std::max (next_scan_, Clock::now () + std::chrono::seconds (30));
    }
    QTimer::singleShot (0, this, [this] { advance (); });
    report ();
  }

  QString configured_model () const {
    return pref ("rag npu openvino model", "").trimmed ();
  }

  QString configured_tokenizer () const {
    QString value= pref ("rag npu tokenizer gguf", "").trimmed ();
    if (value.isEmpty ()) value= pref ("rag embedding model", "").trimmed ();
    return value;
  }

  QString config_key () const {
    return configured_model () + QChar ('\0') + configured_tokenizer ();
  }

  void ensure_worker () {
    if (shutting_down_ || expected_worker_stop_ ||
        (!compute_allowed () && queries_.empty ())) return;
    const QString model= configured_model ();
    const QString tokenizer= configured_tokenizer ();
    if (model.isEmpty () || tokenizer.isEmpty ()) {
      failure ("", "OpenVINO model or tokenizer GGUF is not configured");
      fail_queries (QStringLiteral ("OpenVINO model or tokenizer GGUF is not configured"));
      if (!configuration_warning_shown_) {
        configuration_warning_shown_= true;
        athena_spdlog_warning (
          "continuous RAG: realtime NPU is enabled but its OpenVINO model or "
          "tokenizer GGUF is not configured");
      }
      return;
    }
    configuration_warning_shown_= false;

    const QString desired= config_key ();
    if (process_.state () != QProcess::NotRunning) {
      if (worker_config_key_ == desired) return;
      stop_worker_for_reconfiguration ();
      return;
    }

    const QString program= worker_program ();
    if (!QFileInfo (program).isExecutable ()) {
      failure ("", "NPU worker is not executable: " + program);
      fail_queries (QStringLiteral ("NPU worker is not executable: ") + program);
      athena_spdlog_warning (
        "continuous RAG: NPU worker is not executable: " + std_string (program));
      fail_pending ();
      return;
    }
    QString cache= openvino_cache_dir ();
    QDir ().mkpath (cache);
    process_.setProgram (program);
    process_.setArguments ({QStringLiteral ("--model"), model,
                            QStringLiteral ("--tokenizer-gguf"), tokenizer,
                            QStringLiteral ("--cache-dir"), cache,
                            QStringLiteral ("--max-tokens"),
                            QString::number (athena::rag::bge_m3_max_tokens)});
    process_.setProcessEnvironment (npu_worker_environment ());
    worker_config_key_= desired;
    worker_ready_= false;
    stdout_buffer_.clear ();
    stderr_buffer_.clear ();
    process_.start ();
  }

  void stop_worker_for_reconfiguration () {
    worker_ready_= false;
    fail_queries (QStringLiteral ("NPU worker configuration changed"));
    for (auto& entry: latest_)
      if (is_current (entry.second) && entry.second->awaiting_worker) {
        entry.second->awaiting_worker= false;
        pending_dispatch_[entry.first]= entry.second;
      }
    if (process_.state () == QProcess::NotRunning) return;
    expected_worker_stop_= true;
    QJsonObject message;
    message["op"]= QStringLiteral ("shutdown");
    write_worker (message);
    QTimer::singleShot (200, this, [this] {
      if (expected_worker_stop_ && process_.state () != QProcess::NotRunning) process_.kill ();
    });
  }

  void write_worker (const QJsonObject& object) {
    if (process_.state () == QProcess::NotRunning) return;
    QByteArray bytes= QJsonDocument (object).toJson (QJsonDocument::Compact);
    bytes.append ('\n');
    (void) process_.write (bytes);
  }

  void send_cancel (const std::shared_ptr<RealtimeJob>& job) {
    if (!job || process_.state () == QProcess::NotRunning) return;
    QJsonObject message;
    message["op"]= QStringLiteral ("cancel");
    message["key"]= job->key;
    message["generation"]= QString::number (job->generation);
    write_worker (message);
  }

  void prepare (std::shared_ptr<RealtimeJob> job) {
    QPointer<ContinuousRagManager> self (this);
    rag_index_pool ().start ([self, job=std::move (job)] () mutable {
      if (self == nullptr || !job->current.load (std::memory_order_acquire)) return;
      if (!vault_context_is_current (job->context)) return;
      athena::rag::RagConfig config;
      config.vault_root= fs::path (std_string (job->vault_root));
      config.db_path= fs::path (athena::rag::rag_default_db_path (config.vault_root));
      config.load_embedding_model= false;
      config.progress= false;
      athena::rag::RagIndex index;
      auto prepared= std::make_shared<athena::rag::RagPreparedDocument> ();
      const bool ok= index.open (config) &&
        index.prepare_document (
          std_string (job->rel_path), std_string (job->storage_revision),
          athena::rag::bge_m3_embedding_space_id, *prepared);
      const std::string error= index.status ().last_error;
      const bool superseded= index.status ().revision_superseded;
      if (!job->current.load (std::memory_order_acquire)) return;
      if (self == nullptr) return;
      QMetaObject::invokeMethod (
        self,
        [self, job=std::move (job), prepared=std::move (prepared), ok, superseded,
         error=QString::fromStdString (error)] () mutable {
          if (self != nullptr)
            self->prepared (std::move (job), std::move (prepared), ok, superseded, error);
        }, Qt::QueuedConnection);
    });
  }

  void prepared (std::shared_ptr<RealtimeJob> job,
                 std::shared_ptr<athena::rag::RagPreparedDocument> prepared,
                 bool ok, bool superseded, const QString& error) {
    if (!is_current (job) || superseded) {
      needs_sweep_= true;
      finish_job (job);
      return;
    }
    if (!ok) {
      failure (job->absolute_path, error);
      athena_spdlog_warning (
        "continuous RAG: prepare failed for " + std_string (job->rel_path) +
        (error.isEmpty () ? std::string () : ": " + std_string (error)));
      finish_job (job);
      return;
    }
    job->prepared= std::move (prepared);
    if (job->prepared->unchanged) {
      failures_.erase (job->absolute_path);
      finish_job (job);
      return;
    }
    if (job->prepared->metadata_only ||
        job->prepared->missing_embedding_indices.empty ()) {
      commit (job, {});
      return;
    }
    dispatch (job);
  }

  void dispatch (const std::shared_ptr<RealtimeJob>& job) {
    if (!is_current (job) || !job->prepared) return;
    dispatch_queries ();
    if (!compute_allowed ()) {
      pending_dispatch_[job->key]= job;
      job->awaiting_worker= false;
      report ();
      return;
    }
    if (!worker_ready_) {
      pending_dispatch_[job->key]= job;
      ensure_worker ();
      return;
    }
    QJsonArray texts;
    for (std::size_t index: job->prepared->missing_embedding_indices)
      texts.append (QString::fromStdString (job->prepared->chunks[index].chunk.text));
    QJsonObject message;
    message["op"]= QStringLiteral ("embed");
    message["key"]= job->key;
    message["generation"]= QString::number (job->generation);
    message["texts"]= texts;
    pending_dispatch_.erase (job->key);
    job->awaiting_worker= true;
    write_worker (message);
  }

  void commit (std::shared_ptr<RealtimeJob> job,
               std::vector<std::vector<float>> vectors) {
    if (!is_current (job) || !job->prepared) return;
    QPointer<ContinuousRagManager> self (this);
    rag_index_pool ().start (
      [self, job=std::move (job), vectors=std::move (vectors)] () mutable {
        if (self == nullptr || !job->current.load (std::memory_order_acquire)) return;
        athena::rag::RagConfig config;
        config.vault_root= fs::path (std_string (job->vault_root));
        config.db_path= fs::path (athena::rag::rag_default_db_path (config.vault_root));
        config.load_embedding_model= false;
        config.progress= false;
        athena::rag::RagIndex index;
        bool ok= index.open (config) && index.commit_document (
          *job->prepared, vectors,
          [job] {
            return job->current.load (std::memory_order_acquire) &&
              athena::rag::rag_saved_generation_is_current (
                fs::path (std_string (job->absolute_path)), job->saved_generation) &&
              vault_context_is_current (job->context);
          });
        std::string error= index.status ().last_error;
        const bool superseded= index.status ().revision_superseded;
        if (!job->current.load (std::memory_order_acquire)) return;
        if (self == nullptr) return;
        QMetaObject::invokeMethod (
          self, [self, job=std::move (job), ok, superseded,
                 error=QString::fromStdString (error)] {
            if (self != nullptr) self->committed (job, ok, superseded, error);
          }, Qt::QueuedConnection);
      });
  }

  void committed (const std::shared_ptr<RealtimeJob>& job, bool ok, bool superseded,
                  const QString& error) {
    if (!is_current (job) || superseded) {
      needs_sweep_= true;
      finish_job (job);
      return;
    }
    if (!ok) {
      failure (job->absolute_path, error);
      athena_spdlog_warning (
        "continuous RAG: commit failed for " +
        std_string (job->rel_path) +
        (error.isEmpty () ? std::string () : ": " + std_string (error)));
      finish_job (job);
      return;
    }
    job->committed= true;
    failures_.erase (job->absolute_path);
    failures_.erase ("");
    athena_spdlog_info (
      "continuous RAG: committed " + std_string (job->rel_path) +
      " job=" + std::to_string (job->generation));
    finish_job (job);
  }

  void read_stdout () {
    stdout_buffer_.append (process_.readAllStandardOutput ());
    while (true) {
      qsizetype newline= stdout_buffer_.indexOf ('\n');
      if (newline < 0) break;
      QByteArray line= stdout_buffer_.left (newline);
      stdout_buffer_.remove (0, newline + 1);
      if (line.trimmed ().isEmpty ()) continue;
      QJsonParseError parse;
      QJsonDocument doc= QJsonDocument::fromJson (line, &parse);
      if (parse.error != QJsonParseError::NoError || !doc.isObject ()) {
        fail_queries (QStringLiteral ("Invalid NPU worker response"));
        failure ("", "Invalid NPU worker response");
        athena_spdlog_warning ("continuous RAG: invalid NPU worker response");
        continue;
      }
      handle_worker_message (doc.object ());
    }
  }

  void read_stderr (bool flush= false) {
    stderr_buffer_.append (process_.readAllStandardError ());
    while (!stderr_buffer_.isEmpty ()) {
      qsizetype newline= stderr_buffer_.indexOf ('\n');
      if (newline < 0 && !flush) break;
      if (newline < 0) newline= stderr_buffer_.size ();
      QByteArray line= stderr_buffer_.left (newline).trimmed ();
      stderr_buffer_.remove (0, newline + 1);
      // stderr is a transport, not a severity. Typed library diagnostics use
      // the log protocol; unclassified driver output remains informational.
      if (!line.isEmpty ())
        athena_spdlog_info ("continuous RAG NPU stderr: " +
                           std::string (line.constData (), line.size ()));
    }
  }

  static std::uint64_t generation_value (const QJsonValue& value) {
    if (value.isString ()) return value.toString ().toULongLong ();
    return static_cast<std::uint64_t> (value.toDouble ());
  }

  void handle_worker_message (const QJsonObject& message) {
    const QString type= message.value ("type").toString ();
    if (type == QStringLiteral ("protocol-error")) {
      fail_queries (message.value ("error").toString ("NPU protocol error"));
      return;
    }
    if (type == QStringLiteral ("log")) {
      const QString level= message.value ("level").toString ();
      const std::string prefix= "continuous RAG NPU " +
        std_string (message.value ("source").toString ()) + ": ";
      for (const QString& line: message.value ("text").toString ().split ('\n')) {
        if (line.trimmed ().isEmpty ()) continue;
        const std::string text= prefix + std_string (line);
        if (level == QStringLiteral ("error")) athena_spdlog_error (text);
        else if (level == QStringLiteral ("warning")) athena_spdlog_warning (text);
        else if (level == QStringLiteral ("debug")) athena_spdlog_debug (text);
        else athena_spdlog_info (text);
      }
      return;
    }
    if (type == QStringLiteral ("fatal") &&
        message.value ("key").toString ().isEmpty ()) {
      fail_queries (message.value ("error").toString ("NPU worker startup failed"));
      failure ("", message.value ("error").toString ("NPU worker startup failed"));
      athena_spdlog_warning (
        "continuous RAG: NPU worker startup failed: " +
        std_string (message.value ("error").toString ("unknown error")));
      return;
    }
    if (type == QStringLiteral ("ready")) {
      if (message.value ("protocol").toString () !=
            QStringLiteral ("athena-rag-npu-v1") ||
          message.value ("space_id").toString () !=
            QString::fromLatin1 (athena::rag::bge_m3_embedding_space_id) ||
          message.value ("dimension").toInt () !=
            athena::rag::bge_m3_embedding_dimension ||
          message.value ("device").toString () != QStringLiteral ("NPU")) {
        athena_spdlog_warning (
          "continuous RAG: NPU worker embedding contract mismatch");
        stop_worker_for_reconfiguration ();
        fail_pending ();
        return;
      }
      worker_ready_= true;
      dispatch_queries ();
      failures_.erase ("");
      athena_spdlog_info (
        "continuous RAG: NPU worker ready (BGE-M3, 1024 dimensions)");
      std::vector<std::shared_ptr<RealtimeJob>> pending;
      for (const auto& entry: pending_dispatch_) pending.push_back (entry.second);
      for (const auto& job: pending) dispatch (job);
      return;
    }

    const QString key= message.value ("key").toString ();
    const std::uint64_t generation= generation_value (message.value ("generation"));
    auto query= queries_.find (key);
    if (query != queries_.end () && query->second.generation == generation) {
      if (type != QStringLiteral ("result")) {
        complete_query (key, {}, {}, message.value ("error").toString ("NPU query was dropped"));
        return;
      }
      const auto outer= message.value ("vectors").toArray ();
      const auto inner= outer.size () == 1 ? outer[0].toArray () : QJsonArray ();
      std::vector<float> vector;
      double norm= 0;
      bool valid= message.value ("space_id").toString () ==
          QString::fromLatin1 (athena::rag::bge_m3_embedding_space_id) &&
        message.value ("dimension").toInt () == athena::rag::bge_m3_embedding_dimension &&
        inner.size () == athena::rag::bge_m3_embedding_dimension;
      for (const auto& scalar: inner) {
        const float value= static_cast<float> (scalar.toDouble ());
        valid= valid && scalar.isDouble () && std::isfinite (value);
        norm += double (value) * double (value);
        vector.push_back (value);
      }
      if (!valid || !std::isfinite (norm) || norm <= 0)
        complete_query (key, {}, {}, QStringLiteral ("Invalid NPU query embedding"));
      else complete_query (key, std::move (vector), athena::rag::bge_m3_embedding_space_id, {});
      return;
    }
    auto found= latest_.find (key);
    if (found == latest_.end () || found->second->generation != generation ||
        !is_current (found->second)) return;
    std::shared_ptr<RealtimeJob> job= found->second;

    if (type == QStringLiteral ("dropped")) {
      finish_job (job);
      return;
    }
    if (type == QStringLiteral ("error") || type == QStringLiteral ("fatal")) {
      failure (job->absolute_path, job->rel_path + ": " + message.value ("error").toString ());
      athena_spdlog_warning (
        "continuous RAG: NPU worker error for " + std_string (job->rel_path) +
        ": " + std_string (message.value ("error").toString ()));
      // Fatal execution errors exit the worker. The finished handler retries
      // this prepared revision once in a fresh process, then backs off.
      if (type == QStringLiteral ("fatal")) return;
      job->awaiting_worker= false;
      finish_job (job);
      return;
    }
    if (type != QStringLiteral ("result")) return;
    job->awaiting_worker= false;
    if (message.value ("space_id").toString () !=
          QString::fromLatin1 (athena::rag::bge_m3_embedding_space_id) ||
        message.value ("dimension").toInt () !=
          athena::rag::bge_m3_embedding_dimension) {
      fail_pending ();
      return;
    }

    std::vector<std::vector<float>> vectors;
    const QJsonArray outer= message.value ("vectors").toArray ();
    vectors.reserve (std::size_t (outer.size ()));
    for (const QJsonValue& value: outer) {
      QJsonArray inner= value.toArray ();
      if (inner.size () != athena::rag::bge_m3_embedding_dimension) {
        fail_pending ();
        return;
      }
      std::vector<float> vector;
      vector.reserve (athena::rag::bge_m3_embedding_dimension);
      for (const QJsonValue& scalar: inner) {
        float number= static_cast<float> (scalar.toDouble ());
        if (!scalar.isDouble () || !std::isfinite (number)) {
          fail_pending ();
          return;
        }
        vector.push_back (number);
      }
      vectors.push_back (std::move (vector));
    }
    if (!job->prepared ||
        vectors.size () != job->prepared->missing_embedding_indices.size ()) {
      fail_pending ();
      return;
    }
    commit (std::move (job), std::move (vectors));
  }

  bool has_current_embedding_work () const {
    if (!queries_.empty ()) return true;
    for (const auto& entry: pending_dispatch_)
      if (entry.second->current.load (std::memory_order_acquire)) return true;
    for (const auto& entry: latest_)
      if (entry.second->current.load (std::memory_order_acquire) &&
          entry.second->awaiting_worker)
        return true;
    return false;
  }

  QTMSystemPowerMonitor power_;
  PowerBlock power_block_= PowerBlock::Unknown;
  QProcess process_;
  QByteArray stdout_buffer_;
  QByteArray stderr_buffer_;
  QString worker_config_key_;
  bool worker_ready_= false;
  bool shutting_down_= false;
  bool restart_attempted_= false;
  bool expected_worker_stop_= false;
  bool configuration_warning_shown_= false;
  QTimer poll_;
  vault_context_handle context_;
  QString scan_configuration_;
  std::shared_ptr<std::atomic<bool>> inventory_;
  std::shared_ptr<athena::background::source_watch> source_watch_;
  std::uint64_t scanned_revision_= 0;
  bool needs_sweep_= true;
  std::vector<fs::path> files_;
  std::size_t next_file_= 0;
  std::size_t completed_files_= 0, total_files_= 0;
  std::map<QString,QString> failures_;
  std::uint64_t next_generation_= 0;
  Clock::time_point next_scan_ {};
  std::unordered_map<QString,std::shared_ptr<RealtimeJob>> latest_;
  std::unordered_map<QString,std::shared_ptr<RealtimeJob>> pending_dispatch_;
  std::map<QString,Query> queries_;
};

ContinuousRagManager*
manager (bool create) {
  static QPointer<ContinuousRagManager> instance;
  if (instance == nullptr && create && qApp != nullptr)
    instance= new ContinuousRagManager (qApp);
  return instance;
}

} // namespace

QtmRagQueryCancel
qtm_rag_embed_query (QObject* owner, QString text, QtmRagQueryCallback callback) {
  Q_ASSERT (qApp && QThread::currentThread () == qApp->thread ());
  Q_ASSERT (owner && owner->thread () == qApp->thread ());
  if (!qApp || !owner || QThread::currentThread () != qApp->thread () ||
      owner->thread () != qApp->thread ()) return {};
  return manager (true)->embed_query (owner, std::move (text), std::move (callback));
}

void
qtm_continuous_rag_start () {
  (void) manager (true);
}

void
qtm_continuous_rag_saved (const QString& saved_file, const QString& vault_root,
                           const QString& storage_revision,
                           std::uint64_t save_sequence) {
  // Wake the continuous UUID sweep, but deliberately do not enqueue or
  // prioritize this file.  The worker still consumes the vault in sweep order.
  athena::node_location::persistent_index_wake ();
  ContinuousRagManager* instance= manager (false);
  if (instance != nullptr)
    instance->saved (saved_file, vault_root, storage_revision, save_sequence);
}

void
qtm_continuous_rag_shutdown () {
  ContinuousRagManager* instance= manager (false);
  if (instance != nullptr) instance->shutdown ();
}

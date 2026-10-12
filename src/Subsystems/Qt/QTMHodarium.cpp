/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "QTMHodarium.hpp"
#include "QTMMainTabWindow.hpp"
#include <DockWidget.h>
#include <QWizard>
#include <QWizardPage>
#include <QComboBox>
#include <QCheckBox>
#include <QTabWidget>
#include <QStackedWidget>
#include <QGridLayout>
#include <QToolButton>
#include <QMenu>
#include "ATHENA/Data/hodarium_worker.hpp"
#include "QTMSystemPowerMonitor.hpp"
#include "QTMATHENADiff.hpp"
#include "Data/Convert/Xml/document_file_codec.hpp"
#include "interop_document_source.hpp"
#include "node_metadata.hpp"
#include "ATHENA/Data/document_node_copy.hpp"
#include "ATHENA/Hodarium/profile_session.hpp"
#include "ATHENA/Hodarium/control_http.hpp"
#include "ATHENA/Hodarium/revisions.hpp"
#include "ATHENA/Hodarium/recovery.hpp"
#include "ATHENA/Hodarium/relay_credentials.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"
#include "ATHENA/Data/hodarium_inventory.hpp"
#include "ATHENA/Data/source_identity_repair.hpp"
#include "ATHENA/Data/hodarium_application.hpp"
#include "ATHENA/Data/hodarium_resources.hpp"
#include "ATHENA/Data/hodarium_logical_sync.hpp"
#include "ATHENA/Data/hodarium_logical_database.hpp"
#include "ATHENA/Data/hodarium_materials_database.hpp"
#include "ATHENA/Data/hodarium_rename_records.hpp"
#include "ATHENA/Data/document_history_store.hpp"
#include "buffer_name_catalog.hpp"
#include "confined_filesystem.hpp"
#include "qt_utilities.hpp"
#include "tm_ostream.hpp"
#include "new_buffer.hpp"
#include "new_window.hpp"
#include "tm_buffer.hpp"
#include "buffer_actor.hpp"
#include "file.hpp"
#include <QMessageBox>
#include <QApplication>
#include <QFileInfo>
#include <QPointer>
#include <QThread>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <nlohmann/json.hpp>
#include <QPushButton>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QDateTime>
#include <QSysInfo>
#include <QDesktopServices>
#include <QFileDialog>
#include <QSaveFile>
#include <QInputDialog>
#include <QSignalBlocker>
#include <QUuid>
#include <filesystem>
#include <map>
#include <mutex>
#include <algorithm>
#include <future>
#include <deque>
#include <set>
#include <QElapsedTimer>

namespace {
using namespace athena::hodarium;
std::mutex save_gate;
worker* save_worker= nullptr;
bool capture_enabled= false;
std::size_t queued_save_bytes= 0;
struct vault_panel_state {
  QString name, error;
  athena::background::progress progress;
  std::vector<source_inventory::identity_conflict> duplicates;
};
struct panel_state {
  rows profiles;
  std::map<std::pair<std::string, std::string>, vault_panel_state> vaults;
  std::map<std::pair<std::string, std::string>, std::string> devices;
};

class service: public QObject {
  Q_OBJECT
  QThread thread_;
  worker* worker_;
  QTMSystemPowerMonitor power_;
  bool stopping_= false;
  bool suspended_= false;
  using identity_notice= std::pair<vault_binding, source_inventory::identity_conflict>;
  std::deque<identity_notice> identity_notices_;
  std::set<std::string> reported_identities_;
  bool identity_dialog_= false;
  QTimer* progress_timer_= nullptr;
  bool progress_pending_= false;

  void show_identity_conflict () {
    if (identity_dialog_ || identity_notices_.empty () || stopping_ || suspended_) return;
    identity_dialog_= true;
    auto notice= std::move (identity_notices_.front ()); identity_notices_.pop_front ();
    auto* dialog= new QDialog (QApplication::activeWindow ());
    dialog->setAttribute (Qt::WA_DeleteOnClose);
    dialog->setWindowTitle (tr ("Duplicate document identity"));
    auto* layout= new QVBoxLayout (dialog);
    auto* description= new QLabel (tr ("These documents share the same identity. Synchronization of these documents is blocked."), dialog);
    description->setWordWrap (true); layout->addWidget (description);
    auto* files= new QListWidget (dialog);
    for (const auto& path: notice.second.paths) files->addItem (QString::fromStdString (path));
    layout->addWidget (files);
    auto* buttons= new QDialogButtonBox (dialog);
    auto* different= buttons->addButton (tr ("These are different documents"), QDialogButtonBox::AcceptRole);
    auto* ignore= buttons->addButton (tr ("Ignore"), QDialogButtonBox::RejectRole);
    ignore->setDefault (true); different->setAutoDefault (false);
    layout->addWidget (buttons); dialog->resize (760, 300);
    connect (ignore, &QPushButton::clicked, dialog, &QDialog::reject);
    connect (different, &QPushButton::clicked, dialog, [this, dialog, notice] {
      QStringList paths;
      for (const auto& path: notice.second.paths) paths << QString::fromStdString (path);
      bool accepted= false;
      auto retained= QInputDialog::getItem (dialog, tr ("Keep original identity"),
        tr ("Document that keeps its identity. Other documents receive new root identities; their contents and child identities are unchanged."),
        paths, 0, false, &accepted);
      if (!accepted) return;
      QPointer<service> receiver= this;
      for (const auto& path: notice.second.paths) {
        if (QString::fromStdString (path) == retained) continue;
        perform ([receiver, binding= notice.first, path, id= notice.second.object] (worker& w) {
          const auto bindings= w.settings->vaults (binding.group);
          if (std::none_of (bindings.begin (), bindings.end (), [&] (const vault_binding& b) {
                return b.root == binding.root && b.vault == binding.vault;
              })) throw std::runtime_error ("Vault binding changed before identity repair");
          athena::document_node::repair_source_identity (binding.root, path, id,
            [receiver, path] (bool live, std::string error) {
              if (!receiver) return;
              QMetaObject::invokeMethod (receiver, [receiver, path, live, error= std::move (error)] {
                if (!receiver || receiver->stopping_) return;
                if (!error.empty ()) {
                  std_error << "Document identity repair: " << string (path.c_str ()) << ": " << string (error.c_str ()) << LF;
                  QMessageBox::warning (QApplication::activeWindow (), tr ("Identity repair failed"),
                    QString::fromStdString (path + "\n" + error));
                }
                else if (live) QMessageBox::information (QApplication::activeWindow (), tr ("Document identity updated"),
                  tr ("%1\n\nThe open document has been updated. Save it to persist the new identity; realtime save follows your normal settings.")
                    .arg (QString::fromStdString (path)));
              }, Qt::QueuedConnection);
            });
        });
      }
      dialog->accept ();
    });
    connect (dialog, &QDialog::finished, this, [this] {
      identity_dialog_= false;
      QMetaObject::invokeMethod (this, [this] { show_identity_conflict (); }, Qt::QueuedConnection);
    });
    dialog->setWindowModality (Qt::ApplicationModal); dialog->open ();
  }
signals:
  void statusChanged (QString group, QString diagnostic);
public:
  explicit service (const std::filesystem::path& root): QObject (qApp),
    worker_ (new worker) {
    worker_->application_hooks= live_document_application_hooks ();
    worker_->apply_live_document= apply_open_document;
    worker_->live_source_present= [] (const std::filesystem::path& path) {
      return published_buffer_actor_id (path.generic_string ()) != 0;
    };
    worker_->with_source_publication= [] (const std::function<void ()>& action) {
      std::lock_guard<std::recursive_mutex> publication (document_publication_mutex ());
      action ();
    };
    worker_->capture_enabled_changed= [w= worker_] (bool enabled) {
      std::lock_guard<std::mutex> lock (save_gate);
      if (save_worker == w) capture_enabled= enabled;
    };
    worker_->moveToThread (&thread_);
    worker_->identity_conflict= [this] (vault_binding binding, source_inventory::identity_conflict conflict) {
      QMetaObject::invokeMethod (this, [this, binding= std::move (binding), conflict= std::move (conflict)] {
        if (stopping_) return;
        std::string key= binding.root.generic_string () + "\n" + conflict.object;
        for (const auto& path: conflict.paths) key+= "\n" + path;
        if (!reported_identities_.insert (key).second) return;
        identity_notices_.emplace_back (binding, conflict);
        show_identity_conflict ();
      }, Qt::QueuedConnection);
    };
    { std::lock_guard<std::mutex> lock (save_gate); save_worker= worker_; capture_enabled= false; }
    worker_->report= [this] (profile_status status) {
      if (status.phase == profile_phase::error && !status.diagnostic.empty ())
        worker_->background_errors[status.group]= status.diagnostic;
      else if (status.phase == profile_phase::authorized && status.diagnostic.empty ())
        worker_->background_errors.erase (status.group);
      QMetaObject::invokeMethod (this, [this, status= std::move (status)] {
        emit statusChanged (QString::fromStdString (status.group), QString::fromStdString (
          status.diagnostic.empty () ? status.discovery_diagnostic : status.diagnostic));
        // The shared indicator applies the transport outage grace period and
        // owns error-console/toast deduplication. Status updates also occur for
        // peer counts, so logging here would repeat an unchanged failure.
      }, Qt::QueuedConnection);
    };
    connect (&thread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect (&thread_, &QThread::started, worker_, [w= worker_, root] { w->start (root); });
    connect (qApp, &QCoreApplication::aboutToQuit, this, [this] { stop (); });
    connect (qApp, &QGuiApplication::applicationStateChanged, this,
      [this] (Qt::ApplicationState state) {
#if defined(Q_OS_IOS)
        suspended (state != Qt::ApplicationActive);
#else
        if (state == Qt::ApplicationSuspended) suspended (true);
        else if (state == Qt::ApplicationActive) suspended (false);
#endif
      });
    connect (&power_, &QTMSystemPowerMonitor::sleepChanged, this,
      [this] (bool asleep) { suspended (asleep); });
    connect (&power_, &QTMSystemPowerMonitor::stateChanged, this, [this] {
      const auto state= power_.state ();
      const bool constrained= state.powerSaver || state.supply != QTMSystemPowerMonitor::Supply::External;
      perform ([constrained] (worker& w) {
        w.energy_constrained= constrained;
        for (const auto& [group, session]: w.sessions) session->set_energy_constrained (constrained);
      });
    });
    worker_->energy_constrained= power_.state ().powerSaver ||
      power_.state ().supply != QTMSystemPowerMonitor::Supply::External;
    thread_.setObjectName ("Hodarium control");
    thread_.start ();
    progress_timer_= new QTimer (this);
    progress_timer_->setInterval (1000);
    connect (progress_timer_, &QTimer::timeout, this, [this] {
      if (stopping_ || progress_pending_) return;
      progress_pending_= true;
      QMetaObject::invokeMethod (worker_, [this] {
        athena::background::progress progress;
        try { progress= worker_->background_progress (); }
        catch (const std::exception& e) {
          progress.state= athena::background::phase::error;
          progress.caption= "Sync failed"; progress.error_detail= e.what ();
        }
        QMetaObject::invokeMethod (this, [this, progress= std::move (progress)] {
          progress_pending_= false;
          if (!stopping_) athena::background::publish (athena::background::worker::hodarium, progress);
        }, Qt::QueuedConnection);
      }, Qt::QueuedConnection);
    });
    progress_timer_->start ();
  }
  ~service () override {
    stop ();
    athena::background::publish (athena::background::worker::hodarium, {});
  }
  void review_identities () {
    reported_identities_.clear ();
    perform ([] (worker& w) {
      for (const auto& profile: w.settings->profiles ())
        for (const auto& binding: w.settings->vaults (profile.pin.group))
          if (binding.enabled) w.schedule_inventory (binding);
    });
  }
  void perform (std::function<void (worker&)> operation) {
    if (stopping_ || suspended_) return;
    QMetaObject::invokeMethod (worker_, [w= worker_, operation= std::move (operation)] {
      try { operation (*w); }
      catch (const std::exception& e) { w->report ({"", profile_phase::error, e.what (), "", 0}); }
    }, Qt::QueuedConnection);
  }
  void perform (QObject* context, std::function<void (worker&)> operation,
                std::function<void (QString)> completed) {
    if (stopping_ || suspended_) { completed (tr ("Hodarium is suspended")); return; }
    QPointer<QObject> receiver= context;
    QMetaObject::invokeMethod (worker_, [this, receiver, operation, completed] {
      QString error;
      try { operation (*worker_); }
      catch (const std::exception& e) {
        error= QString::fromUtf8 (e.what ());
        worker_->report ({"", profile_phase::error, e.what (), "", 0});
      }
      QMetaObject::invokeMethod (this, [receiver, completed, error] {
        if (receiver) completed (error);
      }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
  }
  void review_identity (const vault_binding& binding, const source_inventory::identity_conflict& conflict) {
    identity_notices_.emplace_back (binding, conflict);
    show_identity_conflict ();
  }
  void inspect_panel (QObject* context, std::function<void (panel_state)> completed) {
    if (stopping_) { completed ({}); return; }
    QPointer<QObject> receiver= context;
    QMetaObject::invokeMethod (worker_, [this, receiver, completed] {
      panel_state state;
      try {
        worker_->background_progress ();
        state.profiles= worker_->snapshot ();
        for (const auto& row: state.profiles) {
          const auto group= row.profile.pin.group;
          if (auto members= worker_->sessions.at (group)->membership ())
            for (const auto& member: members->members) state.devices[{group, member.id}]= member.name;
          for (const auto& binding: row.vaults) {
            auto& view= state.vaults[{group, binding.vault}];
            AthenaVaultfileInfo info;
            std::string error;
            view.name= athena_vaultfile_read (binding.root, info, error) ?
              QString::fromStdString (info.name) : QString::fromStdString (binding.root.filename ().u8string ());
            view.error= QString::fromStdString (error);
            if (auto progress= worker_->vault_progress.find ({group, binding.vault}); progress != worker_->vault_progress.end ())
              view.progress= progress->second;
            auto track= worker_->inventories.find ({group, binding.vault});
            if (track != worker_->inventories.end ()) {
              std::map<std::string, std::vector<std::string>> paths;
              for (const auto& source: track->second->cache)
                if (!source.second.object.empty ()) paths[source.second.object].push_back (source.first);
              for (auto& entry: paths) if (entry.second.size () > 1)
                view.duplicates.push_back ({entry.first, std::move (entry.second)});
            }
          }
        }
      }
      catch (const std::exception& e) { worker_->report ({"", profile_phase::error, e.what (), "", 0}); }
      QMetaObject::invokeMethod (this, [receiver, completed, state= std::move (state)] {
        if (receiver) completed (state);
      }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
  }
  void inspect (QObject* context, std::function<void (rows)> completed) {
    if (stopping_) { completed ({}); return; }
    QPointer<QObject> receiver= context;
    QMetaObject::invokeMethod (worker_, [this, receiver, completed= std::move (completed)] {
      rows state;
      try { state= worker_->snapshot (); }
      catch (const std::exception& e) { worker_->report ({"", profile_phase::error, e.what (), "", 0}); }
      QMetaObject::invokeMethod (this, [receiver, completed, state= std::move (state)] {
        if (receiver) completed (state);
      }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
  }
  void suspended (bool value) {
    if (stopping_ || suspended_ == value) return;
    suspended_= value;
    if (!value) show_identity_conflict ();
    if (value) cancel_device_key_operations ();
    QMetaObject::invokeMethod (worker_, [w= worker_, value] {
      w->suspended (value);
    }, Qt::QueuedConnection);
  }
  template<typename Result>
  void inspect_journal (QObject* context, const vault_binding& binding,
    std::function<Result (revision_store&)> query,
    std::function<void (Result, QString)> completed) {
    if (stopping_) { completed ({}, tr ("Hodarium is stopping")); return; }
    QPointer<QObject> receiver= context;
    QMetaObject::invokeMethod (worker_, [this, receiver, binding, query, completed] {
      Result result; QString error;
      try {
        if (!worker_->settings) throw std::runtime_error ("Hodarium settings unavailable");
        auto bindings= worker_->settings->vaults (binding.group);
        if (std::none_of (bindings.begin (), bindings.end (), [&] (const vault_binding& v) {
            return v.vault == binding.vault && v.root == binding.root;
          })) throw std::runtime_error ("Hodarium Vault binding changed");
        auto journal= worker_->journals.find (binding.group);
        if (journal == worker_->journals.end ()) throw std::runtime_error ("Hodarium journal unavailable");
        result= query (*journal->second);
      }
      catch (const std::exception& e) {
        error= QString::fromUtf8 (e.what ());
        worker_->report ({binding.group, profile_phase::error, e.what (), "", 0});
      }
      QMetaObject::invokeMethod (this, [receiver, completed, result= std::move (result), error] {
        if (receiver) completed (std::move (result), error);
      }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
  }
  void stop () {
    if (stopping_) return;
    stopping_= true;
    { std::lock_guard<std::mutex> lock (save_gate); save_worker= nullptr; }
    cancel_device_key_operations ();
    QMetaObject::invokeMethod (worker_, [w= worker_] { w->stop (); }, Qt::BlockingQueuedConnection);
    thread_.quit (); thread_.wait ();
  }
  template<typename Result>
  void inspect_proposals (QObject* context, const vault_binding& binding,
    std::function<Result (profile_session&)> query, std::function<void (Result, QString)> completed) {
    if (stopping_) { completed ({}, tr ("Hodarium is stopping")); return; }
    QPointer<QObject> receiver= context;
    QMetaObject::invokeMethod (worker_, [this, receiver, binding, query, completed] {
      Result result; QString error;
      try {
        auto found= worker_->sessions.find (binding.group);
        if (found == worker_->sessions.end ()) throw std::runtime_error ("Hodarium profile unavailable");
        result= query (*found->second);
      }
      catch (const std::exception& e) { error= QString::fromUtf8 (e.what ()); }
      QMetaObject::invokeMethod (this, [receiver, completed, result= std::move (result), error] {
        if (receiver) completed (std::move (result), error);
      }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
  }
  void prepare_resolution (QObject* context, const vault_binding& binding, revision result,
                          std::function<void (std::string, QString)> completed) {
    if (stopping_ || suspended_) { completed ({}, tr ("Hodarium is suspended")); return; }
    QPointer<QObject> receiver= context;
    QMetaObject::invokeMethod (worker_, [this, receiver, binding, result= std::move (result), completed] () mutable {
      QString error;
      std::string operation;
      try {
        if (!worker_->selected (binding)) throw std::runtime_error ("Hodarium Vault binding changed");
        auto session= worker_->sessions.find (binding.group);
        if (session == worker_->sessions.end ()) throw std::runtime_error ("Hodarium profile unavailable");
        if (resource_revision_format (result.format)) {
          revision_store journal (worker_->settings->revision_database_path (worker_->root, binding.group));
          validate_resource_revision (binding.root, result, material_resource_paths (binding.root, journal, binding.vault));
        }
        operation= session->second->prepare_resolution (std::move (result));
      }
      catch (const std::exception& e) { error= QString::fromUtf8 (e.what ()); }
      QMetaObject::invokeMethod (this, [receiver, completed, operation, error] {
        if (receiver) completed (operation, error);
      }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
  }
  void resolve_path (QObject* context, const vault_binding& binding, std::string id, std::string path,
                     std::function<void (QString)> completed) {
    if (stopping_ || suspended_) { completed (tr ("Hodarium is suspended")); return; }
    QPointer<QObject> receiver= context;
    QMetaObject::invokeMethod (worker_, [this, receiver, binding, id, path, completed] {
      QString error;
      try {
        if (!worker_->selected (binding)) throw std::runtime_error ("Hodarium Vault binding changed");
        auto& journal= *worker_->journals.at (binding.group);
        auto offered= journal.offer (id);
        if (!offered || offered->metadata.vault != binding.vault || offered->metadata.deleted ||
            offered->size > 512ULL*1024*1024 ||
            journal.heads (binding.vault, offered->metadata.object) != std::vector<std::string>{id})
          throw std::runtime_error ("Choose an object with one current surviving revision; merge concurrent revisions first");
        if (path == offered->metadata.relative_path || !athena::history::valid_relative_document_path (path))
          throw std::runtime_error ("Choose a different Vault-relative destination");
        std::string cursor;
        for (;;) {
          auto pending= journal.pending_applications (binding.vault, cursor, 64);
          if (pending.empty ()) break;
          for (const auto& intent: pending) {
            cursor= intent.operation;
            auto value= journal.offer (intent.revision_id);
            if (value && value->metadata.object == offered->metadata.object)
              throw std::runtime_error ("Finish the object's interrupted application before changing its path");
          }
        }
        auto result= *journal.get (id);
        result.id.clear (); result.parents= {id}; result.relative_path= path;
        result.origin_member= worker_->settings->find (binding.group)->member;
        if (resource_revision_format (result.format)) validate_resource_revision (binding.root, result,
          material_resource_paths (binding.root, journal, binding.vault));
        else if (result.format != "ath-xml-v2" || result.semantic_version != 3 ||
                 std::filesystem::path (path).extension () != ".ath")
          throw std::runtime_error ("Invalid physical document destination");
        journal.receive (seal_revision (std::move (result)));
      }
      catch (const std::exception& e) { error= QString::fromUtf8 (e.what ()); }
      QMetaObject::invokeMethod (this, [receiver, completed, error] { if (receiver) completed (error); }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
  }
  void inspect_recovery (QObject* context, std::string group,
    std::function<void (std::string, authority_pin, QString)> completed) {
    if (stopping_ || suspended_) { completed ({}, {}, tr ("Hodarium is suspended")); return; }
    QPointer<QObject> receiver= context;
    QMetaObject::invokeMethod (worker_, [this, receiver, group, completed] {
      auto finish= [this, receiver, completed] (std::string ticket, authority_pin pin, QString error) {
        QMetaObject::invokeMethod (this, [receiver, completed, ticket, pin, error] {
          if (receiver) completed (ticket, pin, error);
        }, Qt::QueuedConnection);
      };
      try {
        if (!worker_->settings || worker_->discovery) throw std::runtime_error ("Authority discovery is unavailable or busy");
        auto profile= worker_->settings->find (group);
        if (!profile) throw std::runtime_error ("Unknown Hodarium profile");
        worker_->discovery= std::make_unique<control_http> (profile->origin, nullptr);
        query_authority_recovery (*worker_->discovery, *profile,
          [this, group, finish] (control_result result, std::optional<recovery_candidate> candidate) {
            worker_->discovery.release ()->deleteLater ();
            if (result.failure != control_failure::none || !candidate) {
              finish ({}, {}, QString::fromStdString (result.diagnostic)); return;
            }
            auto ticket= QUuid::createUuid ().toString (QUuid::WithoutBraces).toStdString ();
            auto pin= candidate->proposed_pin ();
            worker_->recoveries.erase (group);
            worker_->recoveries.emplace (group, std::make_pair (ticket, std::move (*candidate)));
            finish (ticket, pin, {});
          });
      }
      catch (const std::exception& e) { finish ({}, {}, QString::fromUtf8 (e.what ())); }
    }, Qt::QueuedConnection);
  }
  void accept_recovery (const std::string& group, const std::string& ticket) {
    perform ([group, ticket] (worker& w) {
      auto candidate= w.recoveries.find (group);
      if (candidate == w.recoveries.end () || candidate->second.first != ticket)
        throw std::runtime_error ("Recovery approval no longer matches its inspected proof");
      auto session= w.sessions.find (group);
      if (session != w.sessions.end ()) { session->second->suspend (); w.sessions.erase (session); }
      try {
        w.settle_recovery (group);
        auto profile= w.settings->accept_recovery (std::move (candidate->second.second));
        w.reset_recovered_journal (group);
        w.recoveries.erase (candidate);
        w.add_session (profile); w.sessions.at (group)->join ();
      }
      catch (...) {
        w.recoveries.erase (group);
        if (!w.sessions.count (group)) w.add_session (*w.settings->find (group));
        throw;
      }
    });
  }
};
QPointer<service> active_service;
QString phase_name (profile_phase phase) {
  switch (phase) {
    case profile_phase::disabled: return QObject::tr ("Paused");
    case profile_phase::pending: return QObject::tr ("Awaiting admission");
    case profile_phase::validating: return QObject::tr ("Validating membership");
    case profile_phase::authorized: return QObject::tr ("Membership valid");
    case profile_phase::offline_valid: return QObject::tr ("Offline membership valid");
    case profile_phase::expired: return QObject::tr ("Membership expired");
    case profile_phase::denied: return QObject::tr ("Membership denied");
    case profile_phase::error: return QObject::tr ("Error");
    case profile_phase::suspended: return QObject::tr ("Suspended");
  }
  return {};
}
class conflicts_pane: public QWidget {
  QPointer<service> service_;
  vault_binding binding_;
  QTreeWidget* tree_;
  QPushButton* more_;
  QPushButton* refresh_;
  QPushButton* compare_;
  QPushButton* ancestor_;
  QPushButton* adopt_;
  QPushButton* edit_;
  QPushButton* duplicate_;
  QPushButton* export_;
  QPushButton* submit_;
  std::optional<revision> draft_;
  url draft_buffer_;
  QLabel* status_;
  QLabel* resolution_status_;
  std::string resolution_operation_;
  resolution_phase resolution_phase_= resolution_phase::pending;
  bool inspecting_resolution_= false;
  std::string cursor_;
  std::uint64_t generation_= 0;
  void refresh_resolution () {
    if (!service_ || resolution_operation_.empty () || inspecting_resolution_) return;
    inspecting_resolution_= true;
    const auto operation= resolution_operation_;
    service_->inspect_proposals<resolution_progress> (this, binding_,
      [operation] (profile_session& session) { return session.resolution_status (operation); },
      [this, operation] (resolution_progress progress, QString error) {
        inspecting_resolution_= false;
        if (operation != resolution_operation_) return;
        if (!error.isEmpty ()) { resolution_status_->setText (error); return; }
        QString message;
        switch (progress.phase) {
          case resolution_phase::pending:
            message= tr ("Merge submitted. Waiting for automatic server confirmation."); break;
          case resolution_phase::accepted:
            message= tr ("Server confirmed the merge. Waiting for protected local application."); break;
          case resolution_phase::applied:
            message= tr ("Merge confirmed and applied locally."); break;
          case resolution_phase::retained:
            message= tr ("Another merge was accepted. Your draft remains in Saved drafts."); break;
        }
        if (!progress.diagnostic.empty ()) message+= "\n" + QString::fromStdString (progress.diagnostic);
        resolution_status_->setText (message);
        if (progress.phase != resolution_phase_) {
          resolution_phase_= progress.phase;
          page (true);
        }
        if (progress.phase == resolution_phase::applied || progress.phase == resolution_phase::retained)
          resolution_operation_.clear ();
      });
  }
  void page (bool reset) {
    if (!service_) return;
    if (reset) { ++generation_; cursor_.clear (); tree_->clear (); }
    const auto generation= generation_;
    more_->setEnabled (false); refresh_->setEnabled (false); status_->clear ();
    service_->inspect_journal<std::vector<revision_conflict>> (this, binding_,
      [vault= binding_.vault, after= cursor_] (revision_store& journal) { return journal.conflicts (vault, after); },
      [this, generation] (std::vector<revision_conflict> entries, QString error) {
        if (generation != generation_) return;
        refresh_->setEnabled (true);
        if (!error.isEmpty ()) { status_->setText (error); return; }
        for (const auto& entry: entries) {
          auto* item= new QTreeWidgetItem (tree_, {QString::fromStdString (entry.representative_path),
            tr ("%1 branches").arg (entry.heads), QString::fromStdString (entry.object)});
          item->setData (0, Qt::UserRole, QString::fromStdString (entry.object));
          item->setChildIndicatorPolicy (QTreeWidgetItem::ShowIndicator);
          cursor_= entry.object;
        }
        more_->setEnabled (entries.size () == 64);
        if (tree_->topLevelItemCount () == 0) status_->setText (tr ("No concurrent revisions"));
      });
  }
  void expand (QTreeWidgetItem* item) {
    if (!service_ || item->parent () || item->data (0, Qt::UserRole+1).toBool ()) return;
    item->setData (0, Qt::UserRole+1, true);
    const auto object= item->data (0, Qt::UserRole).toString ().toStdString ();
    const auto generation= generation_;
    service_->inspect_journal<std::vector<revision>> (this, binding_,
      [vault= binding_.vault, object] (revision_store& journal) {
        auto heads= journal.heads (vault, object);
        if (heads.size () > 256) throw std::runtime_error ("Conflict has more than 256 branches");
        std::vector<revision> result;
        for (const auto& id: heads) {
          auto entry= journal.offer (id);
          if (entry) result.push_back (std::move (entry->metadata));
        }
        return result;
      }, [this, generation, item] (std::vector<revision> branches, QString error) {
        if (generation != generation_) return;
        if (!error.isEmpty ()) { item->setData (0, Qt::UserRole+1, false); status_->setText (error); return; }
        item->setText (1, tr ("%1 branches").arg (branches.size ()));
        for (const auto& branch: branches) {
          auto* child= new QTreeWidgetItem (item, {QString::fromStdString (branch.relative_path),
            branch.deleted ? tr ("Deleted") : logical_revision_format (branch.format) ? tr ("Logical record") :
            resource_revision_format (branch.format) ? tr ("Resource") : tr ("Document"), QString::fromStdString (branch.id)});
          child->setToolTip (1, tr ("Origin: %1").arg (QString::fromStdString (branch.origin_member)));
          child->setData (0, Qt::UserRole, QString::fromStdString (branch.id));
          child->setData (0, Qt::UserRole+1, branch.deleted);
          child->setData (0, Qt::UserRole+2, QString::fromStdString (branch.format));
        }
      });
  }
  void compare (bool ancestor= false) {
    auto selected= tree_->selectedItems ();
    if (!service_ || selected.size () != 2 || !selected[0]->parent () ||
        selected[0]->parent () != selected[1]->parent ()) return;
    const auto object= selected[0]->parent ()->data (0, Qt::UserRole).toString ().toStdString ();
    std::vector<std::string> ids;
    for (auto* item: selected) ids.push_back (item->data (0, Qt::UserRole).toString ().toStdString ());
    const auto generation= generation_;
    compare_->setEnabled (false);
    service_->inspect_journal<std::vector<revision>> (this, binding_,
      [vault= binding_.vault, object, ids, ancestor] (revision_store& journal) mutable {
        if (ancestor) {
          auto bases= journal.merge_bases (ids[0], ids[1]);
          if (bases.empty ()) throw std::runtime_error ("No common ancestor is available for these branches");
          if (bases.size () != 1) throw std::runtime_error ("These branches have multiple common ancestors; no unique three-way base exists");
          ids[1]= bases.front ();
        }
        std::vector<revision> result;
        for (const auto& id: ids) {
          auto value= journal.offer (id);
          if (!value || value->metadata.vault != vault || value->metadata.object != object ||
              value->metadata.deleted || value->metadata.format != "ath-xml-v2" || value->metadata.semantic_version != 3 ||
              value->size > athena::document::codec_limits ().input_bytes)
            throw std::invalid_argument ("Revision cannot be compared as a native document");
          result.push_back (std::move (*journal.get (id)));
        }
        return result;
      }, [this, generation] (std::vector<revision> revisions, QString error) {
        if (generation != generation_) return;
        compare_->setEnabled (true);
        if (!error.isEmpty ()) { status_->setText (error); return; }
        try {
          auto decode= [&] (const revision& value) {
            if (!athena::history::valid_relative_document_path (value.relative_path))
              throw std::invalid_argument ("Invalid Hodarium comparison source path");
            auto source= athena::document::read_xml_v2 (value.payload);
            auto diagnostic= interop_document_source_error (source);
            if (!diagnostic.empty ()) throw std::invalid_argument (diagnostic);
            std::string identity;
            for (int i= 0; i < N(source); ++i)
              if (is_compound (source[i], "body", 1)) identity= athena::node::id (source[i][0]);
            if (identity != value.object) throw std::invalid_argument ("Revision source identity mismatch");
            return source;
          };
          auto left= decode (revisions.at (0)), right= decode (revisions.at (1));
          auto source_url= [&] (const revision& value) {
            auto path= (binding_.root / std::filesystem::u8path (value.relative_path)).u8string ();
            return url_system (string (path.data (), path.size ()));
          };
          auto title= [&] (const revision& value) {
            auto text= value.relative_path + " @ " + value.id.substr (0, 12);
            return string (text.data (), text.size ());
          };
          athena_diff_show_snapshots (left, right, source_url (revisions[0]), source_url (revisions[1]),
                                     title (revisions[0]), title (revisions[1]));
        }
        catch (const std::exception& e) {
          status_->setText (QString::fromUtf8 (e.what ()));
          std_error << "Hodarium comparison: " << string (e.what ()) << LF;
        }
        catch (const string& error) {
          status_->setText (to_qstring (error)); std_error << "Hodarium comparison: " << error << LF;
        }
      });
  }
  tree source (const revision& r) {
    auto t= athena::document::read_xml_v2 (r.payload);
    auto diagnostic= interop_document_source_error (t);
    if (!diagnostic.empty ()) throw std::invalid_argument (diagnostic);
    std::string id;
    for (int i= 0; i < N(t); ++i)
      if (is_compound (t[i], "body", 1)) id= athena::node::id (t[i][0]);
    if (id != r.object) throw std::invalid_argument ("Conflict result changed its document identity");
    return t;
  }
  void publish_result (revision result) {
    if (!service_) return;
    if (!athena::history::valid_relative_document_path (result.relative_path)) {
      status_->setText (tr ("Invalid result path")); return;
    }
    if (resource_revision_format (result.format)) { /* Worker validates current Material authorization before CAS. */ }
    else if (logical_revision_format (result.format)) {
      if (result.semantic_version != 1) throw std::runtime_error ("Unsupported logical record version");
      if (!result.deleted) validate_logical_object ({result.object, result.format, result.payload});
    }
    else if (result.format != "ath-xml-v2" || result.semantic_version != 3)
      throw std::runtime_error ("Unsupported conflict result format");
    submit_->setEnabled (false);
    service_->prepare_resolution (this, binding_, std::move (result), [this] (std::string operation, QString error) {
      submit_->setEnabled (draft_.has_value ());
      if (!error.isEmpty ()) { status_->setText (error); return; }
      status_->clear ();
      resolution_operation_= std::move (operation);
      resolution_phase_= resolution_phase::pending;
      resolution_status_->setText (tr ("Merge submitted. Waiting for automatic server confirmation."));
      refresh_resolution ();
    });
  }
  void export_resource_copy (const revision& result, QWidget* parent) {
    if (result.deleted || !resource_revision_format (result.format))
      throw std::runtime_error ("Choose a surviving resource revision");
    if (!service_) return;
    QPointer<QWidget> owner= parent;
    service_->inspect_journal<bool> (this, binding_,
      [binding= binding_, result] (revision_store& journal) {
        validate_resource_revision (binding.root, result,
          material_resource_paths (binding.root, journal, binding.vault));
        return true;
      }, [this, owner, result] (bool valid, QString error) {
        if (!owner) return;
        if (!error.isEmpty () || !valid) { status_->setText (error); return; }
        try {
          auto filename= QFileDialog::getSaveFileName (owner, tr ("Export resource revision"),
            QString::fromStdString (std::filesystem::path (result.relative_path).filename ().string ()));
          if (filename.isEmpty ()) return;
          auto destination= std::filesystem::weakly_canonical (std::filesystem::u8path (filename.toUtf8 ().toStdString ()));
          auto relative= destination.lexically_relative (binding_.root);
          if (!relative.empty () && !relative.is_absolute () && *relative.begin () != "..")
            throw std::runtime_error ("Export conflict inspection copies outside the bound Vault");
          QSaveFile output (filename);
          if (!output.open (QIODevice::WriteOnly) ||
              output.write (result.payload.data (), result.payload.size ()) != qint64 (result.payload.size ()) || !output.commit ())
            throw std::runtime_error (output.errorString ().toStdString ());
        }
        catch (const std::exception& e) { status_->setText (QString::fromUtf8 (e.what ())); }
      });
  }
  void resolve (int mode) {
    const auto selected= tree_->selectedItems ();
    if (!service_ || selected.size () != 1 || !selected[0]->parent ()) return;
    const auto id= selected[0]->data (0, Qt::UserRole).toString ().toStdString ();
    const auto object= selected[0]->parent ()->data (0, Qt::UserRole).toString ().toStdString ();
    const auto generation= generation_;
    service_->inspect_journal<revision> (this, binding_,
      [vault= binding_.vault, id, object] (revision_store& journal) {
        auto heads= journal.heads (vault, object);
        if (heads.size () < 2 || heads.size () > 256 || std::find (heads.begin (), heads.end (), id) == heads.end ())
          throw std::runtime_error ("Conflict branches changed; refresh the list");
        auto value= journal.offer (id);
        if (!value || value->size > 512ULL*1024*1024)
          throw std::runtime_error ("Conflict revision is missing or exceeds its payload budget");
        auto result= *journal.get (id);
        result.parents= std::move (heads); result.id.clear (); return result;
      }, [this, generation, mode] (revision result, QString error) {
        if (generation != generation_) return;
        if (!error.isEmpty ()) { status_->setText (error); return; }
        try {
          if (mode == 3) {
            export_resource_copy (result, this);
          }
          else if (mode == 2) {
            if (result.deleted) throw std::runtime_error ("A deleted branch has no document to duplicate");
            auto document= athena::document_node::duplicate_source_nodes (source (result));
            auto name= url_scratch ("hodarium_copy_", ".ath");
            set_buffer_tree (name, document);
            auto master= (binding_.root / std::filesystem::u8path (result.relative_path)).u8string ();
            set_master_buffer (name, url_system (string (master.data (), master.size ())));
            new_buffer_in_new_window (name, document);
            status_->setText (tr ("Opened a separate copy with new identities. Save it under a new path; the original conflict remains."));
          }
          else if (mode == 1) {
            if (result.deleted) throw std::runtime_error ("Choose a surviving branch to edit a delete/modify conflict");
            if (logical_revision_format (result.format)) {
              if (derived_logical_revision_format (result.format))
                throw std::runtime_error ("Derived results are immutable; select an authoritative source instead");
              QDialog dialog (this); dialog.setWindowTitle (tr ("Merge logical record")); dialog.resize (850, 600);
              auto* layout= new QVBoxLayout (&dialog);
              auto* input= new QPlainTextEdit (&dialog);
              input->setPlainText (QString::fromStdString (nlohmann::json::parse (result.payload).dump (2)));
              layout->addWidget (input);
              auto* buttons= new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
              layout->addWidget (buttons);
              connect (buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
              connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
              if (dialog.exec () != QDialog::Accepted) return;
              result.payload= nlohmann::json::parse (input->toPlainText ().toStdString ()).dump ();
              publish_result (std::move (result)); return;
            }
            if (result.format != "ath-xml-v2") throw std::runtime_error ("Adopt a resource branch or edit the file with its native editor");
            auto document= source (result);
            if (draft_ && QMessageBox::question (this, tr ("Merge draft"), tr ("Start another merge draft? The previous editor buffer will remain open.")) != QMessageBox::Yes) return;
            draft_buffer_= url_scratch ("hodarium_merge_", ".ath");
            set_buffer_tree (draft_buffer_, document);
            auto master= (binding_.root / std::filesystem::u8path (result.relative_path)).u8string ();
            set_master_buffer (draft_buffer_, url_system (string (master.data (), master.size ())));
            new_buffer_in_new_window (draft_buffer_, document);
            set_title_buffer (draft_buffer_, from_qstring (tr ("Hodarium merge: %1").arg (QString::fromStdString (result.relative_path))));
            draft_= std::move (result); submit_->setEnabled (true);
            status_->setText (tr ("Edit the merge buffer, then submit its result here. Source documents remain unchanged."));
          }
          else {
            if (!result.deleted && result.format == "ath-xml-v2") (void) source (result);
            if (QMessageBox::question (this, tr ("Resolve conflict"), result.deleted ?
                tr ("Resolve all listed branches by deleting this document? File History protects existing local content.") :
                tr ("Resolve all listed branches using the selected revision and its path?")) != QMessageBox::Yes) return;
            publish_result (std::move (result));
          }
        }
        catch (const std::exception& e) { status_->setText (QString::fromUtf8 (e.what ())); }
        catch (const string& e) { status_->setText (to_qstring (e)); }
      });
  }
  void submit () {
    if (!draft_) return;
    try {
      auto document= get_buffer_tree (draft_buffer_);
      auto result= *draft_;
      result.payload= athena::document::write_xml_v2 (document);
      (void) source (result);
      bool accepted= false;
      auto path= QInputDialog::getText (this, tr ("Merge result"), tr ("Vault-relative result path"),
        QLineEdit::Normal, QString::fromStdString (result.relative_path), &accepted);
      if (!accepted) return;
      result.relative_path= path.toStdString ();
      publish_result (std::move (result));
    }
    catch (const std::exception& e) { status_->setText (QString::fromUtf8 (e.what ())); }
    catch (const string& e) { status_->setText (to_qstring (e)); }
  }
  void saved_drafts () {
    if (!service_) return;
    QDialog dialog (this); dialog.setWindowTitle (tr ("Saved merge drafts")); dialog.resize (780, 450);
    auto* layout= new QVBoxLayout (&dialog);
    auto* list= new QTreeWidget (&dialog); list->setColumnCount (2);
    list->setHeaderLabels ({tr ("Path"), tr ("Decision")});
    list->header ()->setSectionResizeMode (0, QHeaderView::Stretch); layout->addWidget (list);
    auto* message= new QLabel (&dialog); message->setWordWrap (true); layout->addWidget (message);
    auto* buttons= new QDialogButtonBox (QDialogButtonBox::Close, &dialog); layout->addWidget (buttons);
    auto* more= buttons->addButton (tr ("More"), QDialogButtonBox::ActionRole);
    auto* open= buttons->addButton (tr ("Open draft copy"), QDialogButtonBox::ActionRole); open->setEnabled (false);
    connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect (list, &QTreeWidget::itemSelectionChanged, &dialog, [list, open] {
      open->setEnabled (list->currentItem () != nullptr);
    });
    auto load= [this, &dialog, list, more, message] {
      more->setEnabled (false);
      const auto after= list->property ("cursor").toString ().toStdString ();
      service_->inspect_proposals<std::vector<conflict_proposal_summary>> (&dialog, binding_,
        [vault= binding_.vault, after] (profile_session& session) { return session.proposals (vault, after); },
        [list, more, message] (std::vector<conflict_proposal_summary> entries, QString error) {
          if (!error.isEmpty ()) { message->setText (error); return; }
          for (const auto& entry: entries) {
            const auto state= entry.state == conflict_proposal_state::pending ? tr ("Pending") :
              entry.state == conflict_proposal_state::accepted ? tr ("Accepted") : tr ("Retained after another decision");
            auto* item= new QTreeWidgetItem (list, {QString::fromStdString (entry.path), state});
            item->setData (0, Qt::UserRole, QString::fromStdString (entry.operation));
            list->setProperty ("cursor", QString::fromStdString (entry.operation));
          }
          more->setEnabled (entries.size () == 64);
        });
    };
    connect (more, &QPushButton::clicked, &dialog, load);
    connect (open, &QPushButton::clicked, &dialog, [this, &dialog, list, message] {
      if (!service_ || !list->currentItem ()) return;
      const auto operation= list->currentItem ()->data (0, Qt::UserRole).toString ().toStdString ();
      service_->inspect_proposals<std::optional<conflict_proposal>> (&dialog, binding_,
        [operation] (profile_session& session) { return session.proposal (operation); },
        [this, &dialog, message] (std::optional<conflict_proposal> value, QString error) {
          if (!error.isEmpty ()) { message->setText (error); return; }
          try {
            if (!value || value->resolution.vault != binding_.vault) throw std::runtime_error ("Merge draft is unavailable");
            const auto& result= value->resolution;
            if (result.deleted) { message->setText (tr ("This retained proposal deletes the object; it has no content payload.")); return; }
            if (result.format == "ath-xml-v2") {
              auto document= athena::document_node::duplicate_source_nodes (source (result));
              auto name= url_scratch ("hodarium_draft_", ".ath");
              set_buffer_tree (name, document);
              auto master= (binding_.root / std::filesystem::u8path (result.relative_path)).u8string ();
              set_master_buffer (name, url_system (string (master.data (), master.size ())));
              new_buffer_in_new_window (name, document); dialog.accept ();
            }
            else if (resource_revision_format (result.format)) export_resource_copy (result, &dialog);
            else {
              QDialog inspect (&dialog); inspect.setWindowTitle (tr ("Retained merge content")); inspect.resize (800, 550);
              auto* layout= new QVBoxLayout (&inspect); auto* text= new QPlainTextEdit (&inspect);
              text->setReadOnly (true);
              if (logical_revision_format (result.format))
                text->setPlainText (QString::fromStdString (nlohmann::json::parse (result.payload).dump (2)));
              else throw std::runtime_error ("Unsupported retained merge format");
              layout->addWidget (text); inspect.exec ();
            }
          }
          catch (const std::exception& e) { message->setText (QString::fromUtf8 (e.what ())); }
          catch (const string& e) { message->setText (to_qstring (e)); }
        });
    });
    load (); dialog.exec ();
  }
  void path_collisions () {
    if (!service_) return;
    QDialog dialog (this); dialog.setWindowTitle (tr ("Conflicting file paths")); dialog.resize (850, 450);
    auto* layout= new QVBoxLayout (&dialog);
    auto* list= new QTreeWidget (&dialog); list->setColumnCount (2);
    list->setHeaderLabels ({tr ("Path"), tr ("Object identity")});
    list->header ()->setSectionResizeMode (0, QHeaderView::Stretch); layout->addWidget (list);
    auto* message= new QLabel (&dialog); message->setWordWrap (true); layout->addWidget (message);
    auto* buttons= new QDialogButtonBox (QDialogButtonBox::Close, &dialog); layout->addWidget (buttons);
    auto* more= buttons->addButton (tr ("More"), QDialogButtonBox::ActionRole);
    auto* rename= buttons->addButton (tr ("Choose another path"), QDialogButtonBox::ActionRole); rename->setEnabled (false);
    connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect (list, &QTreeWidget::itemSelectionChanged, &dialog, [list, rename] {
      rename->setEnabled (list->currentItem () && list->currentItem ()->parent ());
    });
    auto load= [this, &dialog, list, more, message] {
      more->setEnabled (false);
      service_->inspect_journal<std::vector<revision_path_collision>> (&dialog, binding_,
        [vault= binding_.vault, after= list->property ("cursor").toString ().toStdString ()] (revision_store& journal) {
          return journal.path_collisions (vault, after);
        }, [list, more, message] (std::vector<revision_path_collision> entries, QString error) {
          if (!error.isEmpty ()) { message->setText (error); return; }
          for (const auto& entry: entries) {
            auto* parent= new QTreeWidgetItem (list, {QString::fromStdString (entry.path)});
            for (const auto& head: entry.heads) {
              auto* item= new QTreeWidgetItem (parent, {QString::fromStdString (head.relative_path), QString::fromStdString (head.object)});
              item->setData (0, Qt::UserRole, QString::fromStdString (head.id));
            }
            parent->setExpanded (true); list->setProperty ("cursor", QString::fromStdString (entry.path));
          }
          more->setEnabled (entries.size () == 64);
          if (!list->topLevelItemCount ()) message->setText (tr ("No file path collisions"));
        });
    };
    connect (more, &QPushButton::clicked, &dialog, load);
    connect (rename, &QPushButton::clicked, &dialog, [this, &dialog, list, message] {
      if (!service_ || !list->currentItem () || !list->currentItem ()->parent ()) return;
      auto* item= list->currentItem (); bool accepted= false;
      auto path= QInputDialog::getText (&dialog, tr ("Resolve file path"), tr ("New Vault-relative path"),
        QLineEdit::Normal, item->text (0), &accepted);
      if (!accepted) return;
      service_->resolve_path (&dialog, binding_, item->data (0, Qt::UserRole).toString ().toStdString (),
        path.toStdString (), [message] (QString error) {
          message->setText (error.isEmpty () ? tr ("Path revision recorded; protected application is pending.") : error);
        });
    });
    load (); dialog.exec ();
  }
public:
  conflicts_pane (service* controller, vault_binding binding, QWidget* parent):
    QWidget (parent), service_ (controller), binding_ (std::move (binding)) {
    auto* layout= new QVBoxLayout (this);
    tree_= new QTreeWidget (this); tree_->setColumnCount (3);
    tree_->setHeaderLabels ({tr ("Path"), tr ("State"), tr ("Identity")});
    tree_->header ()->setSectionResizeMode (0, QHeaderView::Stretch);
    tree_->header ()->setSectionResizeMode (1, QHeaderView::ResizeToContents);
    tree_->setEditTriggers (QAbstractItemView::NoEditTriggers);
    tree_->setSelectionMode (QAbstractItemView::ExtendedSelection);
    layout->addWidget (tree_);
    status_= new QLabel (this); status_->setWordWrap (true); layout->addWidget (status_);
    resolution_status_= new QLabel (this); resolution_status_->setWordWrap (true);
    layout->addWidget (resolution_status_);
    auto* buttons= new QDialogButtonBox (this);
    refresh_= buttons->addButton (tr ("Refresh"), QDialogButtonBox::ActionRole);
    auto* drafts= buttons->addButton (tr ("Saved drafts"), QDialogButtonBox::ActionRole);
    auto* paths= buttons->addButton (tr ("File paths"), QDialogButtonBox::ActionRole);
    compare_= buttons->addButton (tr ("Compare revisions"), QDialogButtonBox::ActionRole);
    compare_->setEnabled (false);
    auto* actions= new QGridLayout; layout->addLayout (actions);
    ancestor_= new QPushButton (tr ("Compare ancestor"), this);
    adopt_= new QPushButton (tr ("Adopt branch"), this);
    edit_= new QPushButton (tr ("Edit merge"), this);
    duplicate_= new QPushButton (tr ("Duplicate branch"), this);
    export_= new QPushButton (tr ("Export resource"), this);
    submit_= new QPushButton (tr ("Submit merge"), this);
    int position= 0;
    for (auto* button: {ancestor_, adopt_, edit_, duplicate_, export_, submit_}) {
      button->setEnabled (false); actions->addWidget (button, position / 3, position % 3); ++position;
    }
    more_= buttons->addButton (tr ("More"), QDialogButtonBox::ActionRole); layout->addWidget (buttons);
    connect (refresh_, &QPushButton::clicked, this, [this] { page (true); });
    connect (drafts, &QPushButton::clicked, this, [this] { saved_drafts (); });
    connect (paths, &QPushButton::clicked, this, [this] { path_collisions (); });
    connect (more_, &QPushButton::clicked, this, [this] { page (false); });
    connect (compare_, &QPushButton::clicked, this, [this] { compare (); });
    connect (ancestor_, &QPushButton::clicked, this, [this] { compare (true); });
    connect (adopt_, &QPushButton::clicked, this, [this] { resolve (0); });
    connect (edit_, &QPushButton::clicked, this, [this] { resolve (1); });
    connect (duplicate_, &QPushButton::clicked, this, [this] { resolve (2); });
    connect (export_, &QPushButton::clicked, this, [this] { resolve (3); });
    connect (submit_, &QPushButton::clicked, this, [this] { submit (); });
    connect (tree_, &QTreeWidget::itemSelectionChanged, this, [this] {
      auto selected= tree_->selectedItems ();
      auto native= [] (QTreeWidgetItem* item) { return item->data (0, Qt::UserRole+2).toString () == "ath-xml-v2"; };
      compare_->setEnabled (selected.size () == 2 && selected[0]->parent () &&
        selected[0]->parent () == selected[1]->parent () &&
        native (selected[0]) && native (selected[1]) &&
        !selected[0]->data (0, Qt::UserRole+1).toBool () && !selected[1]->data (0, Qt::UserRole+1).toBool ());
      ancestor_->setEnabled (compare_->isEnabled ());
      adopt_->setEnabled (selected.size () == 1 && selected[0]->parent ());
      const bool surviving= adopt_->isEnabled () && !selected[0]->data (0, Qt::UserRole+1).toBool ();
      const auto format= surviving ? selected[0]->data (0, Qt::UserRole+2).toString ().toStdString () : std::string ();
      edit_->setEnabled (surviving && (native (selected[0]) ||
        (logical_revision_format (format) && !derived_logical_revision_format (format))));
      duplicate_->setEnabled (surviving && native (selected[0]));
      export_->setEnabled (surviving && resource_revision_format (format));
    });
    connect (tree_, &QTreeWidget::itemExpanded, this, [this] (QTreeWidgetItem* item) { expand (item); });
    auto* timer= new QTimer (this);
    timer->setInterval (1000);
    connect (timer, &QTimer::timeout, this, [this] { refresh_resolution (); });
    timer->start ();
    page (true);
  }
};

class admission_page: public QWizardPage {
public:
  bool admitted= false;
  bool isComplete () const override { return admitted; }
  void setAdmitted (bool value) { if (admitted != value) { admitted= value; emit completeChanged (); } }
};

class manager: public QWidget {
  QPointer<service> service_;
  QTabWidget* tabs_;
  QVBoxLayout* connections_;
  QVBoxLayout* issues_;
  QStackedWidget* issues_stack_;
  QPointer<QWidget> conflict_page_;
  QPointer<QWidget> relay_editor_;
  std::string relay_group_;
  std::string selected_group_;
  std::optional<vault_binding> pending_conflict_;
  QListWidget* attention_;
  QPointer<conflicts_pane> conflict_view_;
  panel_state panel_;
  QTableWidget* table_;
  QLabel* diagnostic_;
  QLabel* decision_status_;
  QLabel* link_status_;
  QPushButton* resume_;
  QPushButton* pause_;
  QPushButton* admin_;
  QPushButton* bind_;
  QPushButton* unbind_;
  QPushButton* conflicts_;
  QPushButton* relays_;
  QAction* recover_;
  QLineEdit* vault_identity_;
  QTableWidget* vaults_;
  rows rows_;
  bool inspecting_= false;
  bool refresh_pending_= false;
  void add_vault (const std::string& group, QWidget* parent) {
    QDialog dialog (parent); dialog.setWindowTitle (tr ("Add Vault"));
    auto* layout= new QVBoxLayout (&dialog);
    auto* form= new QFormLayout; layout->addLayout (form);
    auto* directory= new QLineEdit (&dialog);
    auto* browse= new QPushButton (tr ("Choose directory…"), &dialog);
    form->addRow (tr ("Local directory"), directory); form->addRow ({}, browse);
    auto* mode= new QComboBox (&dialog);
    mode->addItems ({tr ("Sync an existing shared Vault"), tr ("Share a new Vault")});
    form->addRow (tr ("Vault"), mode);
    auto* identity= new QLineEdit (&dialog);
    form->addRow (tr ("Shared Vault UUID"), identity);
    connect (mode, qOverload<int> (&QComboBox::currentIndexChanged), &dialog, [identity] (int index) { identity->setEnabled (index == 0); });
    connect (browse, &QPushButton::clicked, &dialog, [&dialog, directory] {
      const auto path= QFileDialog::getExistingDirectory (&dialog, tr ("Choose local Vault"));
      if (!path.isEmpty ()) directory->setText (path);
    });
    auto* error= new QLabel (&dialog); error->setWordWrap (true); layout->addWidget (error);
    auto* buttons= new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button (QDialogButtonBox::Ok)->setText (tr ("Start syncing")); layout->addWidget (buttons);
    connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect (buttons, &QDialogButtonBox::accepted, &dialog, [this, &dialog, directory, identity, mode, buttons, error, group] {
      const auto path= directory->text ().trimmed ();
      auto id= mode->currentIndex () == 0 ? QUuid (identity->text ().trimmed ()) : QUuid::createUuid ();
      if (path.isEmpty () || id.isNull ()) { error->setText (tr ("Choose a local Vault and provide its shared UUID, or select Share a new Vault.")); return; }
      vault_binding binding{group, id.toString (QUuid::WithoutBraces).toStdString (), std::filesystem::u8path (path.toStdString ()), true};
      buttons->button (QDialogButtonBox::Ok)->setEnabled (false);
      service_->perform (&dialog, [binding] (worker& w) {
        AthenaVaultfileInfo info; std::string error;
        if (!athena_vaultfile_read (binding.root, info, error)) throw std::invalid_argument (error);
        w.settings->bind_vault (binding); w.update_capture (); w.configure_replication (binding.group);
        w.report (w.sessions.at (binding.group)->status ());
      }, [&dialog, error, buttons] (QString message) {
        if (message.isEmpty ()) dialog.accept ();
        else { error->setText (message); buttons->button (QDialogButtonBox::Ok)->setEnabled (true); }
      });
    });
    dialog.exec (); refresh ();
  }
  void refresh () {
    if (!service_) return;
    if (inspecting_) { refresh_pending_= true; return; }
    inspecting_= true;
    service_->inspect_panel (this, [this] (panel_state state) {
      inspecting_= false;
      std::string selected;
      int row= table_->currentRow ();
      if (row >= 0 && row < int (rows_.size ())) selected= rows_[row].profile.pin.group;
      panel_= std::move (state);
      rows_= panel_.profiles;
      QSignalBlocker table_block (table_);
      table_->setRowCount (int (rows_.size ()));
      for (int i= 0; i < int (rows_.size ()); ++i) {
        const auto& r= rows_[i];
        QStringList cells{r.profile.origin.toString (), QString::fromStdString (r.profile.name),
          phase_name (r.status.phase)};
        for (int j= 0; j < cells.size (); ++j) {
          auto* item= new QTableWidgetItem (cells[j]);
          item->setToolTip (QString::fromStdString (r.status.diagnostic));
          table_->setItem (i, j, item);
        }
        if (r.profile.pin.group == selected) table_->selectRow (i);
      }
      if (selected.empty () && !rows_.empty ()) table_->selectRow (0);
      selection ();
      if (pending_conflict_) {
        for (const auto& row: rows_) if (row.profile.pin.group == pending_conflict_->group) {
          auto binding= *pending_conflict_; pending_conflict_.reset (); show_conflicts (binding); break;
        }
      }
      if (refresh_pending_) { refresh_pending_= false; refresh (); }
    });
  }
  void selection () {
    int i= table_->currentRow ();
    bool valid= i >= 0 && i < int (rows_.size ());
    const auto group= valid ? rows_[i].profile.pin.group : std::string ();
    if (selected_group_ != group) {
      selected_group_= group;
      delete conflict_page_.data ();
    }
    if (relay_group_ != group) {
      relay_group_= group;
      delete relay_editor_.data ();
      if (valid) edit_relays ();
    }
    decision_status_->setText (valid ? QString::fromStdString (rows_[i].status.decision_diagnostic) : QString ());
    QStringList links;
    if (valid) for (const auto& route: rows_[i].status.routes) if (route.established)
      links << tr ("%1 via %2: %3 ms, %4 KiB/s").arg (QString::fromStdString (panel_.devices.count ({rows_[i].profile.pin.group, route.member}) ? panel_.devices.at ({rows_[i].profile.pin.group, route.member}) : route.member),
        QString::fromStdString (route.route)).arg (route.roundtrip_ms, 0, 'f', 1)
        .arg (route.transfer_bytes_per_second/1024.0, 0, 'f', 1);
    link_status_->setText (valid ? (links.empty () ? tr ("No online peers") : links.join ("\n")) : QString ());
    resume_->setEnabled (valid); admin_->setEnabled (valid);
    resume_->setText (valid && rows_[i].profile.member.empty () ? tr ("Continue setup") :
      valid && !rows_[i].profile.enabled ? tr ("Resume sync") : tr ("Retry"));
    relays_->setEnabled (valid); recover_->setEnabled (valid);
    pause_->setEnabled (valid && rows_[i].profile.enabled);
    attention_->clear ();
    if (valid) for (const auto& v: rows_[i].vaults) {
      const auto found= panel_.vaults.find ({v.group, v.vault});
      if (found == panel_.vaults.end ()) continue;
      const auto& view= found->second;
      if (view.progress.conflicts) {
        auto* item= new QListWidgetItem (tr ("%1: Resolve synchronization conflicts").arg (view.name), attention_);
        item->setData (Qt::UserRole, QString::fromStdString (v.vault));
      }
      if (!view.error.isEmpty ()) attention_->addItem (view.name + ": " + view.error);
      if (!view.progress.error_detail.empty ()) attention_->addItem (view.name + ": " + QString::fromStdString (view.progress.error_detail));
      for (const auto& duplicate: view.duplicates) {
        auto* item= new QListWidgetItem (tr ("Duplicate documents: %1").arg (QString::fromStdString (duplicate.paths.front ())), attention_);
        item->setData (Qt::UserRole, QString::fromStdString (v.vault));
        item->setData (Qt::UserRole + 1, QString::fromStdString (duplicate.object));
      }
    }
    bind_->setEnabled (valid && !rows_[i].profile.member.empty ());
    QSignalBlocker block (vaults_);
    QString selected;
    if (vaults_->currentRow () >= 0) selected= vaults_->item (vaults_->currentRow (), 1)->data (Qt::UserRole).toString ();
    vaults_->setRowCount (valid ? int (rows_[i].vaults.size ()) : 0);
    if (valid) for (int j= 0; j < int (rows_[i].vaults.size ()); ++j) {
      const auto& v= rows_[i].vaults[j];
      auto* enabled= new QTableWidgetItem;
      enabled->setFlags (Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
      enabled->setCheckState (v.enabled ? Qt::Checked : Qt::Unchecked);
      vaults_->setItem (j, 0, enabled);
      const auto detail= panel_.vaults.find ({v.group, v.vault});
      auto* title= new QTableWidgetItem (detail == panel_.vaults.end () ? QString::fromStdString (v.root.filename ().string ()) : detail->second.name);
      title->setData (Qt::UserRole, QString::fromStdString (v.vault));
      title->setToolTip (tr ("Vault UUID: %1").arg (QString::fromStdString (v.vault)));
      vaults_->setItem (j, 1, title);
      vaults_->setItem (j, 2, new QTableWidgetItem (QString::fromStdString (v.root.u8string ())));
      auto* code= new QTableWidgetItem;
      code->setFlags (Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
      code->setCheckState (v.allow_code_resources ? Qt::Checked : Qt::Unchecked);
      vaults_->setItem (j, 3, code);
      vaults_->setItem (j, 4, new QTableWidgetItem (!v.enabled ? tr ("Paused") : detail == panel_.vaults.end () ? QString () : QString::fromStdString (detail->second.progress.caption)));
      if (QString::fromStdString (v.vault) == selected) vaults_->selectRow (j);
    }
    unbind_->setEnabled (vaults_->currentRow () >= 0);
    conflicts_->setEnabled (vaults_->currentRow () >= 0);
    vault_identity_->setText (vaults_->currentRow () >= 0 ? vaults_->item (vaults_->currentRow (), 1)->data (Qt::UserRole).toString () : QString ());
  }
  void action (bool enable) {
    int i= table_->currentRow ();
    if (!service_ || i < 0 || i >= int (rows_.size ())) return;
    auto profile= rows_[i].profile;
    if (enable && profile.member.empty ()) { join (profile); return; }
    service_->perform ([profile, enable] (worker& w) {
      auto& session= *w.sessions.at (profile.pin.group);
      if (enable && profile.member.empty ()) session.join ();
      else session.set_enabled (enable);
    });
  }
  void edit_relays () {
    int i= table_->currentRow ();
    if (!service_ || i < 0 || i >= int (rows_.size ())) return;
    const auto row= rows_[i];
    delete relay_editor_.data ();
    auto* dialog= new QWidget (this); relay_editor_= dialog;
    connections_->addWidget (dialog);
    auto* layout= new QVBoxLayout (dialog);
    auto* list= new QListWidget (dialog); layout->addWidget (list);
    for (const auto& relay: row.relays) list->addItem (relay.origin.toString ());
    auto* form= new QFormLayout;
    auto* origin= new QLineEdit (dialog); origin->setPlaceholderText ("https://");
    auto* token= new QLineEdit (dialog); token->setEchoMode (QLineEdit::Password);
    token->setPlaceholderText (tr ("New access token"));
    form->addRow (tr ("Relay"), origin); form->addRow (tr ("Access token"), token); layout->addLayout (form);
    auto* buttons= new QDialogButtonBox (dialog);
    auto* save= buttons->addButton (tr ("Save Relay"), QDialogButtonBox::ActionRole);
    auto* remove= buttons->addButton (tr ("Remove"), QDialogButtonBox::ActionRole);
    layout->addWidget (buttons); remove->setEnabled (false); save->setEnabled (false);
    connect (token, &QLineEdit::textChanged, dialog, [save] (const QString& value) { save->setEnabled (!value.trimmed ().isEmpty ()); });
    connect (list, &QListWidget::currentRowChanged, dialog, [list, origin, remove] (int selected) {
      remove->setEnabled (selected >= 0);
      if (selected >= 0) origin->setText (list->item (selected)->text ());
    });
    connect (save, &QPushButton::clicked, dialog, [this, origin, token, group= row.profile.pin.group] {
      QUrl endpoint (origin->text ().trimmed ());
      auto credential= token->text ().toStdString (); token->clear ();
      service_->perform (this, [group, endpoint, credential= std::move (credential)] (worker& w) {
        const auto canonical= canonical_relay_origin (endpoint);
        const auto previous= w.settings->relays (group);
        auto handle= store_relay_token (endpoint, credential);
        try { w.settings->set_relay ({group, QUrl (QString::fromStdString (canonical)), handle}); }
        catch (...) { delete_relay_token (endpoint, handle); throw; }
        w.sessions.at (group)->configure_relays ();
        for (const auto& old: previous) {
          if (canonical_relay_origin (old.origin) != canonical || old.credential_handle == handle) continue;
          bool referenced= false;
          for (const auto& profile: w.settings->profiles ())
            for (const auto& other: w.settings->relays (profile.pin.group))
              referenced= referenced || other.credential_handle == old.credential_handle;
          if (!referenced) delete_relay_token (old.origin, old.credential_handle);
        }
        w.report (w.sessions.at (group)->status ());
      }, [this, group] (QString error) {
        diagnostic_->setText (error);
        if (error.isEmpty () && relay_group_ == group) relay_group_.clear ();
        refresh ();
      });
    });
    connect (remove, &QPushButton::clicked, dialog, [this, list, row, group= row.profile.pin.group] {
      const int selected= list->currentRow ();
      if (selected < 0) return;
      auto relay= row.relays[selected];
      service_->perform (this, [group, relay] (worker& w) {
        w.settings->remove_relay (group, relay.origin);
        w.sessions.at (group)->configure_relays ();
        bool referenced= false;
        for (const auto& profile: w.settings->profiles ())
          for (const auto& other: w.settings->relays (profile.pin.group))
            referenced= referenced || other.credential_handle == relay.credential_handle;
        if (!referenced) delete_relay_token (relay.origin, relay.credential_handle);
        w.report (w.sessions.at (group)->status ());
      }, [this, group] (QString error) {
        diagnostic_->setText (error);
        if (error.isEmpty () && relay_group_ == group) relay_group_.clear ();
        refresh ();
      });
    });
  }
  void recover () {
    int i= table_->currentRow ();
    if (!service_ || i < 0 || i >= int (rows_.size ())) return;
    const auto profile= rows_[i].profile;
    recover_->setEnabled (false);
    service_->inspect_recovery (this, profile.pin.group,
      [this, profile] (std::string ticket, authority_pin proposed, QString error) {
        recover_->setEnabled (true);
        if (!error.isEmpty ()) { diagnostic_->setText (error); return; }
        if (QMessageBox::question (this, tr ("Accept authority recovery"),
            tr ("Verified with your pinned recovery key.\n\nServer: %1\nNew generation: %2\nAuthority: %3\n\n"
                "Replace this device's membership trust and request admission again? Local documents and history are retained.")
              .arg (profile.origin.toString (), QString::fromStdString (proposed.generation),
                    QString::fromStdString (proposed.public_key))) != QMessageBox::Yes) return;
        if (service_) service_->accept_recovery (profile.pin.group, ticket);
      });
  }
  void join (std::optional<client_profile> pending= {}) {
    auto* wizard= new QWizard (this);
    wizard->setAttribute (Qt::WA_DeleteOnClose);
    wizard->setWindowTitle (tr ("Join ATHENA Hodarium"));
    auto* setup= new admission_page;
    setup->setTitle (tr ("Server and device"));
    auto* form= new QFormLayout (setup);
    auto* origin= new QLineEdit (pending ? pending->origin.toString () : "https://");
    auto* name= new QLineEdit (pending ? QString::fromStdString (pending->name) : QSysInfo::machineHostName ());
    form->addRow (tr ("Server"), origin); form->addRow (tr ("Device name"), name);
    auto validate= [setup, origin, name] {
      const QUrl url (origin->text ().trimmed (), QUrl::StrictMode);
      const auto bytes= name->text ().trimmed ().toUtf8 ();
      setup->setAdmitted (url.isValid () && url.scheme () == "https" && !url.host ().isEmpty () &&
        url.userInfo ().isEmpty () && !bytes.isEmpty () && bytes.size () <= 128);
    };
    connect (origin, &QLineEdit::textChanged, setup, validate);
    connect (name, &QLineEdit::textChanged, setup, validate); validate ();
    wizard->addPage (setup);
    auto* approval= new admission_page;
    approval->setTitle (tr ("Approve this device"));
    auto* layout= new QVBoxLayout (approval);
    auto* status= new QLabel; status->setWordWrap (true);
    status->setTextInteractionFlags (Qt::TextSelectableByMouse);
    layout->addWidget (status);
    auto* open= new QPushButton (tr ("Open management page")); layout->addWidget (open);
    auto* retry= new QPushButton (tr ("Retry admission")); layout->addWidget (retry);
    connect (open, &QPushButton::clicked, wizard, [origin] { QDesktopServices::openUrl (QUrl (origin->text ().trimmed ())); });
    wizard->addPage (approval);
    auto* done= new QWizardPage; done->setTitle (tr ("Device admitted"));
    auto* done_layout= new QVBoxLayout (done);
    auto* add= new QPushButton (tr ("Add Vault…")); done_layout->addWidget (add);
    auto admitted_group= std::make_shared<std::string> ();
    connect (add, &QPushButton::clicked, wizard, [this, wizard, admitted_group] { if (!admitted_group->empty ()) add_vault (*admitted_group, wizard); });
    wizard->addPage (done);
    auto requested= std::make_shared<bool> (pending.has_value ());
    connect (retry, &QPushButton::clicked, wizard, [this, wizard, origin, name, status] {
      if (!service_) return;
      service_->perform (wizard, [address= QUrl (origin->text ().trimmed ()), device= name->text ().trimmed ().toStdString ()] (worker& w) {
        for (const auto& row: w.snapshot ()) if (row.profile.origin.adjusted (QUrl::StripTrailingSlash) == address.adjusted (QUrl::StripTrailingSlash)) {
          w.sessions.at (row.profile.pin.group)->join (); return;
        }
        w.join (address, device);
      }, [status] (QString error) { if (!error.isEmpty ()) status->setText (error); });
    });
    connect (wizard, &QWizard::currentIdChanged, wizard, [this, wizard, origin, name, status, requested] (int page) {
      if (page == 0) { *requested= false; origin->setReadOnly (false); name->setReadOnly (false); }
      if (page != 1 || *requested || !service_) return;
      *requested= true;
      origin->setReadOnly (true); name->setReadOnly (true);
      status->setText (tr ("Requesting admission…"));
      service_->perform (wizard, [address= QUrl (origin->text ().trimmed ()), device= name->text ().trimmed ().toStdString ()] (worker& w) {
        for (const auto& row: w.snapshot ()) if (row.profile.origin.adjusted (QUrl::StripTrailingSlash) == address.adjusted (QUrl::StripTrailingSlash)) {
          if (row.profile.member.empty ()) w.sessions.at (row.profile.pin.group)->join ();
          return;
        }
        w.join (address, device);
      }, [status] (QString error) { if (!error.isEmpty ()) status->setText (error); });
    });
    connect (service_, &service::statusChanged, wizard, [status] (QString, QString message) {
      if (!message.isEmpty ()) status->setText (message);
    });
    auto* timer= new QTimer (wizard);
    auto busy= std::make_shared<bool> (false);
    connect (timer, &QTimer::timeout, wizard, [this, wizard, origin, approval, status, busy, admitted_group] {
      if (!service_ || *busy) return;
      *busy= true;
      service_->inspect (wizard, [this, origin, approval, status, busy, admitted_group] (rows values) {
        *busy= false;
        for (const auto& row: values) if (row.profile.origin.adjusted (QUrl::StripTrailingSlash) == QUrl (origin->text ().trimmed ()).adjusted (QUrl::StripTrailingSlash)) {
          const bool admitted= !row.profile.member.empty () &&
            (row.status.phase == profile_phase::authorized || row.status.phase == profile_phase::offline_valid);
          approval->setAdmitted (admitted);
          if (admitted) *admitted_group= row.profile.pin.group;
          const auto seconds= row.status.code_expires - QDateTime::currentSecsSinceEpoch ();
          status->setText (admitted ? tr ("Device admitted") : seconds > 0 ?
            tr ("Admission code: %1\nExpires in %2 seconds").arg (QString::fromStdString (row.status.code)).arg (seconds) :
            phase_name (row.status.phase) + "\n" + QString::fromStdString (row.status.diagnostic));
          if (admitted) refresh ();
          break;
        }
      });
    });
    timer->start (1000);
    if (pending) wizard->setStartId (1);
    wizard->open ();
  }
public:
  void show_conflicts (const vault_binding& binding) {
    auto profile= std::find_if (rows_.begin (), rows_.end (), [&binding] (const profile_row& row) {
      return row.profile.pin.group == binding.group;
    });
    if (profile == rows_.end ()) { pending_conflict_= binding; refresh (); return; }
    table_->selectRow (int (profile - rows_.begin ()));
    selection ();
    delete conflict_page_.data ();
    conflict_page_= new QWidget (this);
    auto* layout= new QVBoxLayout (conflict_page_);
    auto* back= new QPushButton (tr ("Back to issues"), conflict_page_);
    layout->addWidget (back, 0, Qt::AlignLeft);
    connect (back, &QPushButton::clicked, this, [this] { issues_stack_->setCurrentIndex (0); });
    conflict_view_= new conflicts_pane (service_, binding, conflict_page_);
    layout->addWidget (conflict_view_);
    issues_stack_->addWidget (conflict_page_);
    issues_stack_->setCurrentWidget (conflict_page_);
    tabs_->setCurrentIndex (2);
  }
  explicit manager (service* controller): service_ (controller) {
    auto* layout= new QVBoxLayout (this);
    auto* join_button= new QPushButton (tr ("Join Hodarium…"), this);
    layout->addWidget (join_button, 0, Qt::AlignLeft);
    table_= new QTableWidget (0, 3, this);
    table_->setHorizontalHeaderLabels ({tr ("Server"), tr ("This device"), tr ("Membership")});
    table_->horizontalHeader ()->setSectionResizeMode (QHeaderView::ResizeToContents);
    table_->horizontalHeader ()->setStretchLastSection (true);
    table_->setSelectionBehavior (QAbstractItemView::SelectRows);
    table_->setSelectionMode (QAbstractItemView::SingleSelection);
    table_->setEditTriggers (QAbstractItemView::NoEditTriggers);
    layout->addWidget (table_);
    table_->setMaximumHeight (120);
    tabs_= new QTabWidget (this); layout->addWidget (tabs_);
    auto* sync= new QWidget; auto* sync_layout= new QVBoxLayout (sync); tabs_->addTab (sync, tr ("Sync"));
    auto* connections= new QWidget; connections_= new QVBoxLayout (connections); tabs_->addTab (connections, tr ("Connections"));
    issues_stack_= new QStackedWidget (this); tabs_->addTab (issues_stack_, tr ("Needs attention"));
    auto* issues= new QWidget; issues_= new QVBoxLayout (issues); issues_stack_->addWidget (issues);
    attention_= new QListWidget (issues); issues_->addWidget (attention_);
    connect (attention_, &QListWidget::itemActivated, this, [this] (QListWidgetItem* item) {
      int i= table_->currentRow ();
      if (i < 0 || i >= int (rows_.size ())) return;
      for (const auto& binding: rows_[i].vaults) if (QString::fromStdString (binding.vault) == item->data (Qt::UserRole).toString ()) {
        if (item->data (Qt::UserRole + 1).toString ().isEmpty ()) { show_conflicts (binding); return; }
        const auto& state= panel_.vaults.at ({binding.group, binding.vault});
        for (const auto& duplicate: state.duplicates) if (QString::fromStdString (duplicate.object) == item->data (Qt::UserRole + 1).toString ()) service_->review_identity (binding, duplicate);
      }
    });
    vaults_= new QTableWidget (0, 5, this);
    vaults_->setHorizontalHeaderLabels ({tr ("Sync"), tr ("Vault"), tr ("Local directory"), tr ("Allow styles / sorters"), tr ("Status")});
    vaults_->horizontalHeader ()->setSectionResizeMode (QHeaderView::ResizeToContents);
    vaults_->horizontalHeader ()->setStretchLastSection (true);
    vaults_->setSelectionBehavior (QAbstractItemView::SelectRows);
    vaults_->setSelectionMode (QAbstractItemView::SingleSelection);
    vaults_->setEditTriggers (QAbstractItemView::NoEditTriggers);
    sync_layout->addWidget (vaults_);
    auto* vault_detail= new QFormLayout;
    vault_identity_= new QLineEdit (this); vault_identity_->setReadOnly (true);
    vault_detail->addRow (tr ("Vault UUID"), vault_identity_); sync_layout->addLayout (vault_detail);
    diagnostic_= new QLabel (this); diagnostic_->setWordWrap (true);
    diagnostic_->setTextInteractionFlags (Qt::TextSelectableByMouse);
    issues_->addWidget (diagnostic_);
    decision_status_= new QLabel (this); decision_status_->setWordWrap (true);
    decision_status_->setTextInteractionFlags (Qt::TextSelectableByMouse); issues_->addWidget (decision_status_);
    link_status_= new QLabel (this); link_status_->setWordWrap (true);
    link_status_->setTextInteractionFlags (Qt::TextSelectableByMouse); connections_->addWidget (link_status_);
    auto* membership_buttons= new QDialogButtonBox (this);
    layout->addWidget (membership_buttons);
    auto* buttons= new QDialogButtonBox (this);
    resume_= membership_buttons->addButton (tr ("Resume / retry"), QDialogButtonBox::ActionRole);
    pause_= membership_buttons->addButton (tr ("Pause"), QDialogButtonBox::ActionRole);
    admin_= membership_buttons->addButton (tr ("Open management page"), QDialogButtonBox::ActionRole);
    connections_->addWidget (new QLabel (tr ("Relays"), this));
    relays_= new QPushButton (tr ("Reload Relays"), this); connections_->addWidget (relays_, 0, Qt::AlignLeft);
    auto* advanced= new QToolButton (this); advanced->setText (tr ("Advanced"));
    advanced->setPopupMode (QToolButton::InstantPopup);
    auto* advanced_menu= new QMenu (advanced);
    recover_= advanced_menu->addAction (tr ("Recover server trust…"));
    advanced->setMenu (advanced_menu); membership_buttons->addButton (advanced, QDialogButtonBox::ActionRole);
    bind_= buttons->addButton (tr ("Add Vault…"), QDialogButtonBox::ActionRole);
    unbind_= buttons->addButton (tr ("Stop syncing this Vault"), QDialogButtonBox::ActionRole);
    conflicts_= buttons->addButton (tr ("Conflicts"), QDialogButtonBox::ActionRole);
    auto* identities= new QPushButton (tr ("Review duplicate documents"), this); issues_->addWidget (identities);
    connect (identities, &QPushButton::clicked, this, [this] {
      if (service_) service_->review_identities ();
    });
    sync_layout->addWidget (buttons);
    connect (table_, &QTableWidget::itemSelectionChanged, this, [this] { selection (); });
    connect (resume_, &QPushButton::clicked, this, [this] { action (true); });
    connect (pause_, &QPushButton::clicked, this, [this] { action (false); });
    connect (relays_, &QPushButton::clicked, this, [this] { edit_relays (); });
    connect (recover_, &QAction::triggered, this, [this] { recover (); });
    connect (bind_, &QPushButton::clicked, this, [this] {
      int i= table_->currentRow ();
      if (!service_ || i < 0 || i >= int (rows_.size ())) return;
      add_vault (rows_[i].profile.pin.group, this);
    });
    connect (vaults_, &QTableWidget::itemSelectionChanged, this, [this] {
      unbind_->setEnabled (vaults_->currentRow () >= 0);
      conflicts_->setEnabled (vaults_->currentRow () >= 0);
      vault_identity_->setText (vaults_->currentRow () >= 0 ? vaults_->item (vaults_->currentRow (), 1)->data (Qt::UserRole).toString () : QString ());
    });
    connect (conflicts_, &QPushButton::clicked, this, [this] {
      int i= table_->currentRow (), j= vaults_->currentRow ();
      if (!service_ || i < 0 || i >= int (rows_.size ()) || j < 0 || j >= int (rows_[i].vaults.size ())) return;
      show_conflicts (rows_[i].vaults[j]);
    });
    connect (vaults_, &QTableWidget::itemChanged, this, [this] (QTableWidgetItem* item) {
      int i= table_->currentRow (), j= item->row ();
      if (!service_ || (item->column () != 0 && item->column () != 3) || i < 0 || i >= int (rows_.size ()) ||
          j >= int (rows_[i].vaults.size ())) return;
      auto binding= rows_[i].vaults[j]; bool enabled= item->checkState () == Qt::Checked;
      service_->perform ([binding, enabled, code= item->column () == 3] (worker& w) {
        if (code) w.settings->set_vault_code_resources (binding.group, binding.vault, enabled);
        else w.settings->set_vault_enabled (binding.group, binding.vault, enabled);
        w.update_capture ();
        w.configure_replication (binding.group);
        w.report (w.sessions.at (binding.group)->status ());
      });
    });
    connect (unbind_, &QPushButton::clicked, this, [this] {
      int i= table_->currentRow (), j= vaults_->currentRow ();
      if (!service_ || i < 0 || i >= int (rows_.size ()) || j < 0 || j >= int (rows_[i].vaults.size ())) return;
      auto binding= rows_[i].vaults[j];
      if (QMessageBox::question (this, tr ("Stop syncing this Vault"), tr ("Stop synchronizing this local Vault? Its files and history will remain on this device.")) != QMessageBox::Yes) return;
      service_->perform ([binding] (worker& w) {
        w.settings->unbind_vault (binding.group, binding.vault);
        w.update_capture ();
        w.configure_replication (binding.group);
        w.report (w.sessions.at (binding.group)->status ());
      });
    });
    connect (admin_, &QPushButton::clicked, this, [this] {
      int i= table_->currentRow ();
      if (i >= 0 && i < int (rows_.size ())) QDesktopServices::openUrl (rows_[i].profile.origin);
    });
    connect (join_button, &QPushButton::clicked, this, [this] { join (); });
    connect (controller, &service::statusChanged, this, [this] (QString, QString diagnostic) {
      diagnostic_->setText (diagnostic);
      if (isVisible ()) refresh ();
    });
    auto* timer= new QTimer (this); timer->setInterval (2000);
    connect (timer, &QTimer::timeout, this, [this] {
      if (isVisible ()) refresh ();
    });
    timer->start (); selection (); refresh ();
  }
};
QPointer<manager> active_manager;
QPointer<ads::CDockWidget> hodarium_dock;
}

void qtm_hodarium_initialize () {
  if (active_service) return;
  const QString home= qEnvironmentVariable ("ATHENA_HOME_PATH");
  if (home.isEmpty ()) return;
  const QString root= home + "/system/hodarium";
  // No database creation, secret-store access or network activity before setup.
  if (!QFileInfo::exists (root + "/device.sqlite")) return;
  active_service= new service (std::filesystem::path (root.toUtf8 ().constData ()));
}
void qtm_hodarium_reload () {
  delete hodarium_dock.data ();
  delete active_service.data ();
  qtm_hodarium_initialize ();
}
void qtm_hodarium_show () {
  auto* window= QTMMainTabWindow::topTabWindow ();
  if (!window || !window->dockManager ()) return;
  if (!active_service) {
    const QString home= qEnvironmentVariable ("ATHENA_HOME_PATH");
    if (home.isEmpty ()) { std_error << "Hodarium: application storage is unavailable" << LF; return; }
    active_service= new service (std::filesystem::path ((home + "/system/hodarium").toUtf8 ().constData ()));
  }
  if (!hodarium_dock) {
    active_manager= new manager (active_service);
    hodarium_dock= new ads::CDockWidget ("ATHENA Hodarium");
    hodarium_dock->setObjectName ("athena-hodarium");
    hodarium_dock->setWidget (active_manager);
    hodarium_dock->setFeature (ads::CDockWidget::DockWidgetDeleteOnClose, false);
  }
  window->showAdsDockWidget (hodarium_dock, ads::BottomDockWidgetArea);
}

void qtm_hodarium_show_conflicts () {
  if (!active_service) { qtm_hodarium_show (); return; }
  QPointer<service> controller= active_service;
  controller->perform ([controller] (worker& w) {
    std::optional<vault_binding> target;
    for (const auto& row: w.snapshot ()) for (const auto& binding: row.vaults) {
      auto journal= w.journals.find (binding.group);
      if (!target && row.profile.enabled && binding.enabled && journal != w.journals.end () &&
          !journal->second->conflicts (binding.vault, {}, 1).empty ()) target= binding;
    }
    QMetaObject::invokeMethod (controller, [controller, target] {
      if (!controller) return;
      qtm_hodarium_show ();
      if (target && active_manager) active_manager->show_conflicts (*target);
    }, Qt::QueuedConnection);
  });
}

#include "QTMHodarium.moc"

void qtm_hodarium_saved (const std::filesystem::path& path, std::string object,
  std::optional<std::string> predecessor, std::shared_ptr<const std::string> bytes) {
  if (!bytes) return;
  std::lock_guard<std::mutex> lock (save_gate);
  if (!save_worker || !capture_enabled) return;
  constexpr std::size_t budget= 1024ULL*1024*1024;
  if (bytes->size () > budget - queued_save_bytes) {
    std_warning << "Hodarium: saved revision capture queue is full; document remains saved locally" << LF;
    return;
  }
  auto* owner= save_worker;
  auto size= bytes->size (); queued_save_bytes+= size;
  bool queued= QMetaObject::invokeMethod (owner,
    [owner, path, object= std::move (object), predecessor= std::move (predecessor), bytes= std::move (bytes), size] {
      { std::lock_guard<std::mutex> lock (save_gate); queued_save_bytes-= size; }
      try { owner->saved (path, object, predecessor, *bytes); }
      catch (const std::exception& e) { owner->report ({"", profile_phase::error, e.what (), "", 0}); }
    }, Qt::QueuedConnection);
  if (!queued) {
    queued_save_bytes-= size;
    std_warning << "Hodarium: could not queue saved revision capture; document remains saved locally" << LF;
  }
}

/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "QTMHodarium.hpp"
#include "QTMSystemPowerMonitor.hpp"
#include "ATHENA/Hodarium/profile_session.hpp"
#include "ATHENA/Hodarium/control_http.hpp"
#include "ATHENA/Hodarium/revisions.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"
#include "ATHENA/Data/hodarium_inventory.hpp"
#include "confined_filesystem.hpp"
#include "qt_utilities.hpp"
#include "tm_ostream.hpp"
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
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QDateTime>
#include <QSysInfo>
#include <QDesktopServices>
#include <QFileDialog>
#include <QInputDialog>
#include <QSignalBlocker>
#include <QUuid>
#include <filesystem>
#include <map>
#include <mutex>
#include <algorithm>
#include <future>
#include <deque>
#include <QElapsedTimer>

namespace {
using namespace athena::hodarium;
struct profile_row { client_profile profile; profile_status status; std::vector<vault_binding> vaults; };
using rows= std::vector<profile_row>;
class worker;
std::mutex save_gate;
worker* save_worker= nullptr;
bool capture_enabled= false;
std::size_t queued_save_bytes= 0;

class worker: public QObject {
public:
  std::unique_ptr<client_settings> settings;
  std::map<std::string, std::unique_ptr<profile_session>> sessions;
  std::function<void (profile_status)> report;
  std::filesystem::path root;
  std::unique_ptr<control_http> discovery;
  std::map<std::string, std::unique_ptr<revision_store>> journals;
  std::deque<vault_binding> scan_queue;
  std::optional<vault_binding> scanning;
  std::future<source_inventory> scan_future;
  source_inventory scanned;
  struct inventory_tracking {
    vault_binding binding;
    athena::background::source_watch watch;
    std::map<std::string, source_inventory::cached_source> cache;
    std::uint64_t observed= 0;
    std::chrono::steady_clock::time_point poll{}, retry= std::chrono::steady_clock::time_point::max ();
  };
  std::map<std::pair<std::string,std::string>, std::shared_ptr<inventory_tracking>> inventories;
  std::shared_ptr<inventory_tracking> active_inventory;
  std::size_t scan_position= 0;
  std::shared_ptr<std::atomic<bool>> scan_cancel= std::make_shared<std::atomic<bool>> (false);
  QTimer* scan_timer= nullptr;
  void schedule_inventory (const vault_binding& binding) {
    if (!binding.enabled) return;
    auto& track= inventories[{binding.group, binding.vault}];
    if (!track || track->binding.root != binding.root) {
      track= std::make_shared<inventory_tracking> (); track->binding= binding;
    }
    for (const auto& pending: scan_queue)
      if (pending.group == binding.group && pending.vault == binding.vault && pending.root == binding.root) return;
    scan_queue.push_back (binding);
  }
  bool selected (const vault_binding& binding) {
    for (const auto& current: settings->vaults (binding.group))
      if (current.vault == binding.vault && current.root == binding.root && current.enabled) return true;
    return false;
  }
  void inventory_step () {
    if (scan_future.valid ()) {
      if (scan_future.wait_for (std::chrono::seconds (0)) != std::future_status::ready) return;
      scanned= scan_future.get (); scan_position= 0;
      for (const auto& error: scanned.errors) report ({scanning->group, profile_phase::error, error, "", 0});
    }
    if (scanning) {
      if (!selected (*scanning)) { scanned= {}; scanning.reset (); }
      else {
        QElapsedTimer budget; budget.start ();
        while (scan_position < scanned.documents.size () && budget.elapsed () < 10) {
          const auto& source= scanned.documents[scan_position++];
          try {
            athena::filesystem::confined_root root (scanning->root);
            if (!athena::filesystem::same_revision (root.open (source.path).stat (), source.revision)) {
              schedule_inventory (*scanning); continue;
            }
            saved (scanning->root / source.path, source.object, {}, *source.bytes, true);
            scanned.cache.at (source.path).published= true;
          }
          catch (const std::exception& e) {
            report ({scanning->group, profile_phase::error, source.path + ": " + e.what (), "", 0});
          }
        }
        if (scan_position != scanned.documents.size ()) return;
        active_inventory->cache= std::move (scanned.cache);
        bool retry= !scanned.errors.empty ();
        for (const auto& [path, entry]: active_inventory->cache) retry= retry || !entry.published;
        active_inventory->retry= retry ? std::chrono::steady_clock::now () + std::chrono::seconds (30) :
          std::chrono::steady_clock::time_point::max ();
        scanned= {}; scanning.reset ();
      }
    }
    const auto now= std::chrono::steady_clock::now ();
    for (auto it= inventories.begin (); it != inventories.end ();) {
      auto track= it->second;
      if (now < track->poll) { ++it; continue; }
      if (!selected (track->binding)) { it= inventories.erase (it); continue; }
      if (now >= track->poll) {
#ifdef __linux__
        track->poll= now + std::chrono::seconds (2);
#else
        track->poll= now + std::chrono::seconds (30);
#endif
        if (track->watch.revision () != track->observed || now >= track->retry)
          schedule_inventory (track->binding);
      }
      ++it;
    }
    while (!scan_queue.empty ()) {
      auto binding= scan_queue.front (); scan_queue.pop_front ();
      if (!selected (binding)) continue;
      scanning= binding;
      active_inventory= inventories.at ({binding.group, binding.vault});
      active_inventory->observed= active_inventory->watch.revision ();
      scan_future= std::async (std::launch::async, [binding, track= active_inventory, cancelled= scan_cancel] {
        return inventory_sources (binding.root, *cancelled, track->cache, &track->watch);
      });
      break;
    }
  }
  void configure_replication (const std::string& group) {
    auto& journal= journals[group];
    if (!journal) journal= std::make_unique<revision_store> (root / ("revisions-" + group + ".sqlite"));
    std::vector<std::string> vaults;
    for (const auto& binding: settings->vaults (group)) if (binding.enabled) {
      vaults.push_back (binding.vault); schedule_inventory (binding);
    }
    sessions.at (group)->configure_revisions (*journal, std::move (vaults));
  }
  void update_capture () {
    bool enabled= false;
    for (const auto& profile: settings->profiles ())
      if (!profile.member.empty ()) for (const auto& binding: settings->vaults (profile.pin.group))
        enabled= enabled || binding.enabled;
    std::lock_guard<std::mutex> lock (save_gate);
    if (save_worker == this) capture_enabled= enabled;
  }

  void saved (const std::filesystem::path& path, const std::string& object,
    const std::optional<std::string>& predecessor, const std::string& bytes, bool inventory= false) {
    if (!settings) return;
    for (const auto& profile: settings->profiles ()) {
      if (profile.member.empty ()) continue;
      for (const auto& binding: settings->vaults (profile.pin.group)) {
        if (!binding.enabled) continue;
        auto relative= path.lexically_normal ().lexically_relative (binding.root);
        if (relative.empty () || relative.is_absolute () || *relative.begin () == "..") continue;
        if (std::any_of (relative.begin (), relative.end (), [] (const std::filesystem::path& part) {
          return part == ".athena" || part == ".backup" || part == ".git";
        })) continue;
        if (object.empty ()) throw std::invalid_argument ("Saved Hodarium document has no source UUID");
        auto& journal= journals[profile.pin.group];
        if (!journal) journal= std::make_unique<revision_store> (root / ("revisions-" + profile.pin.group + ".sqlite"));
        auto expected= journal->applied (binding.vault, object);
        if (expected) {
          auto previous= journal->offer (*expected);
          if (!previous->metadata.deleted && previous->metadata.relative_path != relative.generic_u8string ()) {
            athena::filesystem::confined_root confined (binding.root);
            bool still_present= true;
            try { (void) confined.open (previous->metadata.relative_path); }
            catch (const std::system_error& e) {
              if (e.code () != std::errc::no_such_file_or_directory) throw;
              still_present= false;
            }
            if (still_present)
              throw std::invalid_argument ("Hodarium source UUID occurs at two paths: " +
                previous->metadata.relative_path + " and " + relative.generic_u8string ());
          }
        }
        if (expected && !predecessor && !inventory)
          throw std::invalid_argument ("Saved Hodarium document has no predecessor fingerprint");
        revision snapshot;
        snapshot.vault= binding.vault; snapshot.object= object; snapshot.origin_member= profile.member;
        snapshot.format= "ath-xml-v2"; snapshot.semantic_version= 3;
        snapshot.relative_path= relative.generic_u8string (); snapshot.payload= bytes;
        // Identical imported replicas share a deterministic genesis revision.
        // Actual editor saves retain their member provenance.
        if (inventory && !expected) snapshot.origin_member= "saved-source-baseline-v1";
        if (!journal->capture_saved (std::move (snapshot), expected, predecessor))
          throw std::runtime_error ("Hodarium saved predecessor differs from the applied revision: " + relative.generic_u8string ());
        return;
      }
    }
  }

  void add_session (const client_profile& profile) {
    auto group= profile.pin.group;
    auto session= std::make_unique<profile_session> (*settings, profile,
      root / ("membership-" + group + ".sqlite"), report);
    auto* active= session.get ();
    sessions.emplace (group, std::move (session));
    configure_replication (group);
    active->start ();
  }
  rows snapshot () {
    rows result;
    if (settings) for (const auto& profile: settings->profiles ()) {
      auto session= sessions.find (profile.pin.group);
      if (session != sessions.end ()) result.push_back ({profile, session->second->status (), settings->vaults (profile.pin.group)});
    }
    return result;
  }
  void join (QUrl origin, std::string name) {
    if (!settings) throw std::runtime_error ("Hodarium settings unavailable");
    if (discovery) throw std::runtime_error ("Authority discovery already in progress");
    if (name.empty () || name.size () > 128)
      throw std::invalid_argument ("Device name must be 1-128 UTF-8 bytes");
    discovery= std::make_unique<control_http> (origin, nullptr);
    discover_authority (*discovery, [this, origin, name] (control_result result, authority_description info) {
      // Destroy the transport outside its reply callback.
      discovery.release ()->deleteLater ();
      try {
        if (result.failure != control_failure::none) throw std::runtime_error (result.diagnostic);
        if (!info.initialized) throw std::runtime_error ("Authority administrator setup is incomplete");
        if (settings->find (info.pin.group))
          throw std::runtime_error ("Hodarium already configured; resume its existing membership instead");
        client_profile profile{origin, info.pin, info.recovery_public_key,
          create_device_identity (), name, "", false};
        settings->add_pending (profile);
        add_session (profile);
        sessions.at (profile.pin.group)->join ();
      }
      catch (const std::exception& e) { report ({"", profile_phase::error, e.what (), "", 0}); }
    });
  }

  void start (const std::filesystem::path& root) {
    try {
      this->root= root;
      std::filesystem::create_directories (root);
      std::filesystem::permissions (root, std::filesystem::perms::owner_all);
      settings= std::make_unique<client_settings> (root / "device.sqlite");
      scan_timer= new QTimer (this);
      connect (scan_timer, &QTimer::timeout, this, [this] {
        try { inventory_step (); }
        catch (const std::exception& e) {
          if (active_inventory) active_inventory->retry= std::chrono::steady_clock::now () + std::chrono::seconds (30);
          scanned= {}; scanning.reset ();
          report ({"", profile_phase::error, e.what (), "", 0});
        }
      });
      scan_timer->start (50);
      for (auto& profile: settings->profiles ()) add_session (profile);
      update_capture ();
    }
    catch (const std::exception& e) {
      report ({"", profile_phase::error, e.what (), "", 0});
    }
  }
  void suspended (bool value) {
    if (value) discovery.reset ();
    for (const auto& entry: sessions) {
      try {
        if (value) entry.second->suspend ();
        else entry.second->resume ();
      }
      catch (const std::exception& e) {
        report ({entry.first, profile_phase::error, e.what (), "", 0});
      }
    }
  }
  void stop () {
    scan_cancel->store (true);
    if (scan_timer) scan_timer->stop ();
    if (scan_future.valid ()) scan_future.wait ();
    scanned= {}; scan_queue.clear (); scanning.reset ();
    active_inventory.reset (); inventories.clear ();
    discovery.reset (); sessions.clear (); journals.clear (); settings.reset ();
  }
};

class service: public QObject {
  QThread thread_;
  worker* worker_;
  QTMSystemPowerMonitor power_;
  bool stopping_= false;
  bool suspended_= false;
public:
  QPointer<QObject> observer;
  std::function<void (QString)> changed;
  explicit service (const std::filesystem::path& root): QObject (qApp),
    worker_ (new worker) {
    worker_->moveToThread (&thread_);
    { std::lock_guard<std::mutex> lock (save_gate); save_worker= worker_; capture_enabled= false; }
    worker_->report= [this] (profile_status status) {
      QMetaObject::invokeMethod (this, [this, status= std::move (status)] {
        if (observer && changed) changed (QString::fromStdString (
          status.diagnostic.empty () ? status.discovery_diagnostic : status.diagnostic));
        if (!stopping_ && !suspended_ && !status.discovery_diagnostic.empty ())
          std_warning << "Hodarium discovery: " << string (status.discovery_diagnostic.c_str ()) << LF;
        if (stopping_ || suspended_ || status.diagnostic.empty ()) return;
        if (status.phase == profile_phase::error || status.phase == profile_phase::denied)
          std_error << "Hodarium: " << string (status.diagnostic.c_str ()) << LF;
        else if (status.phase == profile_phase::expired || status.phase == profile_phase::offline_valid)
          std_warning << "Hodarium: " << string (status.diagnostic.c_str ()) << LF;
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
    thread_.setObjectName ("Hodarium control");
    thread_.start ();
  }
  ~service () override { stop (); }
  void perform (std::function<void (worker&)> operation) {
    if (stopping_ || suspended_) return;
    QMetaObject::invokeMethod (worker_, [w= worker_, operation= std::move (operation)] {
      try { operation (*w); }
      catch (const std::exception& e) { w->report ({"", profile_phase::error, e.what (), "", 0}); }
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
    if (value) cancel_device_key_operations ();
    QMetaObject::invokeMethod (worker_, [w= worker_, value] {
      w->suspended (value);
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
class manager: public QDialog {
  QPointer<service> service_;
  QTableWidget* table_;
  QLabel* diagnostic_;
  QPushButton* resume_;
  QPushButton* pause_;
  QPushButton* admin_;
  QPushButton* bind_;
  QPushButton* unbind_;
  QTableWidget* vaults_;
  rows rows_;
  bool inspecting_= false;
  bool refresh_pending_= false;
  void refresh () {
    if (!service_) return;
    if (inspecting_) { refresh_pending_= true; return; }
    inspecting_= true;
    service_->inspect (this, [this] (rows state) {
      inspecting_= false;
      std::string selected;
      int row= table_->currentRow ();
      if (row >= 0 && row < int (rows_.size ())) selected= rows_[row].profile.pin.group;
      rows_= std::move (state);
      table_->setRowCount (int (rows_.size ()));
      for (int i= 0; i < int (rows_.size ()); ++i) {
        const auto& r= rows_[i];
        QString code= QString::fromStdString (r.status.code);
        if (r.status.code_expires <= QDateTime::currentSecsSinceEpoch ()) code.clear ();
        QStringList cells{r.profile.origin.toString (), QString::fromStdString (r.profile.name),
          phase_name (r.status.phase), code};
        for (int j= 0; j < cells.size (); ++j) {
          auto* item= new QTableWidgetItem (cells[j]);
          item->setToolTip (QString::fromStdString (r.status.diagnostic));
          table_->setItem (i, j, item);
        }
        if (r.profile.pin.group == selected) table_->selectRow (i);
      }
      if (selected.empty () && !rows_.empty ()) table_->selectRow (0);
      selection ();
      if (refresh_pending_) { refresh_pending_= false; refresh (); }
    });
  }
  void selection () {
    int i= table_->currentRow ();
    bool valid= i >= 0 && i < int (rows_.size ());
    resume_->setEnabled (valid); admin_->setEnabled (valid);
    pause_->setEnabled (valid && rows_[i].profile.enabled);
    bind_->setEnabled (valid && !rows_[i].profile.member.empty ());
    QSignalBlocker block (vaults_);
    QString selected;
    if (vaults_->currentRow () >= 0) selected= vaults_->item (vaults_->currentRow (), 1)->text ();
    vaults_->setRowCount (valid ? int (rows_[i].vaults.size ()) : 0);
    if (valid) for (int j= 0; j < int (rows_[i].vaults.size ()); ++j) {
      const auto& v= rows_[i].vaults[j];
      auto* enabled= new QTableWidgetItem;
      enabled->setFlags (Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
      enabled->setCheckState (v.enabled ? Qt::Checked : Qt::Unchecked);
      vaults_->setItem (j, 0, enabled);
      vaults_->setItem (j, 1, new QTableWidgetItem (QString::fromStdString (v.vault)));
      vaults_->setItem (j, 2, new QTableWidgetItem (QString::fromStdString (v.root.u8string ())));
      if (QString::fromStdString (v.vault) == selected) vaults_->selectRow (j);
    }
    unbind_->setEnabled (vaults_->currentRow () >= 0);
  }
  void action (bool enable) {
    int i= table_->currentRow ();
    if (!service_ || i < 0 || i >= int (rows_.size ())) return;
    auto profile= rows_[i].profile;
    service_->perform ([profile, enable] (worker& w) {
      auto& session= *w.sessions.at (profile.pin.group);
      if (enable && profile.member.empty ()) session.join ();
      else session.set_enabled (enable);
    });
  }
public:
  explicit manager (service* controller): service_ (controller) {
    setAttribute (Qt::WA_DeleteOnClose);
    setWindowTitle (tr ("ATHENA Hodarium")); resize (850, 450);
    auto* layout= new QVBoxLayout (this);
    auto* form= new QFormLayout;
    auto* origin= new QLineEdit (this);
    origin->setPlaceholderText ("https://");
    auto* name= new QLineEdit (QSysInfo::machineHostName (), this);
    form->addRow (tr ("Server"), origin); form->addRow (tr ("Device name"), name);
    layout->addLayout (form);
    auto* join= new QPushButton (tr ("Request admission"), this);
    layout->addWidget (join, 0, Qt::AlignLeft);
    table_= new QTableWidget (0, 4, this);
    table_->setHorizontalHeaderLabels ({tr ("Server"), tr ("Device"), tr ("Membership"), tr ("Admission code")});
    table_->horizontalHeader ()->setSectionResizeMode (QHeaderView::ResizeToContents);
    table_->horizontalHeader ()->setStretchLastSection (true);
    table_->setSelectionBehavior (QAbstractItemView::SelectRows);
    table_->setSelectionMode (QAbstractItemView::SingleSelection);
    table_->setEditTriggers (QAbstractItemView::NoEditTriggers);
    layout->addWidget (table_);
    vaults_= new QTableWidget (0, 3, this);
    vaults_->setHorizontalHeaderLabels ({tr ("Selected"), tr ("Vault ID"), tr ("Local directory")});
    vaults_->horizontalHeader ()->setSectionResizeMode (QHeaderView::ResizeToContents);
    vaults_->horizontalHeader ()->setStretchLastSection (true);
    vaults_->setSelectionBehavior (QAbstractItemView::SelectRows);
    vaults_->setSelectionMode (QAbstractItemView::SingleSelection);
    vaults_->setEditTriggers (QAbstractItemView::NoEditTriggers);
    layout->addWidget (vaults_);
    diagnostic_= new QLabel (this); diagnostic_->setWordWrap (true);
    diagnostic_->setTextInteractionFlags (Qt::TextSelectableByMouse);
    layout->addWidget (diagnostic_);
    auto* buttons= new QDialogButtonBox (QDialogButtonBox::Close, this);
    resume_= buttons->addButton (tr ("Resume / retry"), QDialogButtonBox::ActionRole);
    pause_= buttons->addButton (tr ("Pause"), QDialogButtonBox::ActionRole);
    admin_= buttons->addButton (tr ("Administration"), QDialogButtonBox::ActionRole);
    bind_= buttons->addButton (tr ("Bind Vault"), QDialogButtonBox::ActionRole);
    unbind_= buttons->addButton (tr ("Unbind Vault"), QDialogButtonBox::ActionRole);
    layout->addWidget (buttons);
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    connect (table_, &QTableWidget::itemSelectionChanged, this, [this] { selection (); });
    connect (resume_, &QPushButton::clicked, this, [this] { action (true); });
    connect (pause_, &QPushButton::clicked, this, [this] { action (false); });
    connect (bind_, &QPushButton::clicked, this, [this] {
      int i= table_->currentRow ();
      if (!service_ || i < 0 || i >= int (rows_.size ())) return;
      auto group= rows_[i].profile.pin.group;
      auto directory= QFileDialog::getExistingDirectory (this, tr ("Bind local Vault"));
      if (directory.isEmpty ()) return;
      bool accepted= false;
      auto id= QInputDialog::getText (this, tr ("Vault identity"), tr ("Vault ID"),
        QLineEdit::Normal, QUuid::createUuid ().toString (QUuid::WithoutBraces), &accepted).trimmed ();
      if (!accepted) return;
      vault_binding binding{group, id.toStdString (), std::filesystem::u8path (directory.toUtf8 ().toStdString ()), true};
      service_->perform ([binding] (worker& w) {
        AthenaVaultfileInfo info; std::string error;
        if (!athena_vaultfile_read (binding.root, info, error)) throw std::invalid_argument (error);
        w.settings->bind_vault (binding);
        w.update_capture ();
        w.configure_replication (binding.group);
        w.report (w.sessions.at (binding.group)->status ());
      });
    });
    connect (vaults_, &QTableWidget::itemSelectionChanged, this, [this] {
      unbind_->setEnabled (vaults_->currentRow () >= 0);
    });
    connect (vaults_, &QTableWidget::itemChanged, this, [this] (QTableWidgetItem* item) {
      int i= table_->currentRow (), j= item->row ();
      if (!service_ || item->column () != 0 || i < 0 || i >= int (rows_.size ()) ||
          j >= int (rows_[i].vaults.size ())) return;
      auto binding= rows_[i].vaults[j]; bool enabled= item->checkState () == Qt::Checked;
      service_->perform ([binding, enabled] (worker& w) {
        w.settings->set_vault_enabled (binding.group, binding.vault, enabled);
        w.update_capture ();
        w.configure_replication (binding.group);
        w.report (w.sessions.at (binding.group)->status ());
      });
    });
    connect (unbind_, &QPushButton::clicked, this, [this] {
      int i= table_->currentRow (), j= vaults_->currentRow ();
      if (!service_ || i < 0 || i >= int (rows_.size ()) || j < 0 || j >= int (rows_[i].vaults.size ())) return;
      auto binding= rows_[i].vaults[j];
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
    connect (join, &QPushButton::clicked, this, [this, origin, name] {
      if (!service_) return;
      auto address= QUrl (origin->text ().trimmed (), QUrl::StrictMode);
      if (address.path () == "/") address.setPath ({});
      auto device= name->text ().trimmed ().toUtf8 ().toStdString ();
      diagnostic_->clear ();
      service_->perform ([address, device] (worker& w) { w.join (address, device); });
    });
    controller->observer= this;
    controller->changed= [this] (QString diagnostic) {
      if (!diagnostic.isEmpty ()) diagnostic_->setText (diagnostic);
      refresh ();
    };
    auto* timer= new QTimer (this); timer->setInterval (1000);
    connect (timer, &QTimer::timeout, this, [this] {
      for (int i= 0; i < int (rows_.size ()); ++i)
        if (rows_[i].status.code_expires <= QDateTime::currentSecsSinceEpoch ())
          table_->item (i, 3)->setText ({});
    });
    timer->start (); selection (); refresh ();
  }
};
QPointer<manager> active_manager;
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
  delete active_manager.data ();
  delete active_service.data ();
  qtm_hodarium_initialize ();
}
void qtm_hodarium_show () {
  if (active_manager) { active_manager->show (); active_manager->raise (); active_manager->activateWindow (); return; }
  if (!active_service) {
    const QString home= qEnvironmentVariable ("ATHENA_HOME_PATH");
    if (home.isEmpty ()) { std_error << "Hodarium: application storage is unavailable" << LF; return; }
    active_service= new service (std::filesystem::path ((home + "/system/hodarium").toUtf8 ().constData ()));
  }
  active_manager= new manager (active_service);
  active_manager->show ();
}

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

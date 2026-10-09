/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "QTMHodarium.hpp"
#include "QTMSystemPowerMonitor.hpp"
#include "QTMATHENADiff.hpp"
#include "Data/Convert/Xml/document_file_codec.hpp"
#include "interop_document_source.hpp"
#include "node_metadata.hpp"
#include "ATHENA/Hodarium/profile_session.hpp"
#include "ATHENA/Hodarium/control_http.hpp"
#include "ATHENA/Hodarium/revisions.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"
#include "ATHENA/Data/hodarium_inventory.hpp"
#include "ATHENA/Data/hodarium_application.hpp"
#include "ATHENA/Data/document_history_store.hpp"
#include "buffer_name_catalog.hpp"
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
#include <QTreeWidget>
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
#include <set>
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
    bool inventoried= false;
    std::optional<athena::filesystem::metadata> root_revision;
    std::string recovery_cursor, application_cursor, application_error;
    std::chrono::steady_clock::time_point next_application{};
    std::chrono::steady_clock::time_point poll{}, retry= std::chrono::steady_clock::time_point::max ();
  };
  std::map<std::pair<std::string,std::string>, std::shared_ptr<inventory_tracking>> inventories;
  std::shared_ptr<inventory_tracking> active_inventory;
  std::size_t scan_position= 0;
  std::string deletion_cursor;
  std::set<std::string> present_objects;
  bool deletion_started= false, deletion_deferred= false;
  std::unique_ptr<athena::history::document_history_store> deletion_history;
  std::shared_ptr<std::atomic<bool>> scan_cancel= std::make_shared<std::atomic<bool>> (false);
  QTimer* scan_timer= nullptr;
  struct application_result { bool completed= false; std::string error; };
  std::future<application_result> application_future;
  std::shared_ptr<std::atomic<bool>> application_current;
  std::shared_ptr<inventory_tracking> applying;
  bool paused= false;
  bool application_allowed (const vault_binding& binding) {
    if (paused || !selected (binding)) return false;
    auto session= sessions.find (binding.group);
    if (session == sessions.end ()) return false;
    auto phase= session->second->status ().phase;
    return phase == profile_phase::authorized || phase == profile_phase::offline_valid;
  }
  bool application_step () {
    if (application_future.valid ()) {
      if (!application_allowed (applying->binding)) application_current->store (false);
      if (application_future.wait_for (std::chrono::seconds (0)) != std::future_status::ready) return true;
      auto result= application_future.get ();
      applying->next_application= std::chrono::steady_clock::now () +
        std::chrono::seconds (result.completed ? 0 : 5);
      if (!result.error.empty () && result.error != applying->application_error)
        report ({applying->binding.group, profile_phase::error, result.error, "", 0});
      applying->application_error= result.error;
      // A partial rename can leave two paths. Recovery remains ahead of scans.
      if (result.completed) schedule_inventory (applying->binding);
      applying.reset (); application_current.reset ();
      return false;
    }
    if (scan_future.valid () || scanning || paused) return false;
    for (const auto& [key, track]: inventories) {
      if (!application_allowed (track->binding) ||
          std::chrono::steady_clock::now () < track->next_application) continue;
      auto& journal= *journals.at (track->binding.group);
      auto pending= journal.pending_applications (track->binding.vault, track->recovery_cursor, 1);
      std::string id;
      if (!pending.empty ()) {
        track->recovery_cursor= pending.front ().operation;
        id= pending.front ().revision_id;
      }
      else {
        track->recovery_cursor.clear ();
        if (!track->inventoried) continue;
        auto candidates= journal.application_candidates (track->binding.vault, track->application_cursor, 1);
        if (candidates.empty ()) {
          track->application_cursor.clear ();
          track->next_application= std::chrono::steady_clock::now () + std::chrono::seconds (2);
          continue;
        }
        track->application_cursor= candidates.front ().object; id= candidates.front ().id;
      }
      applying= track;
      application_current= std::make_shared<std::atomic<bool>> (true);
      const auto database= root / ("revisions-" + track->binding.group + ".sqlite");
      application_future= std::async (std::launch::async,
        [binding= track->binding, expected_root= track->root_revision, database, id, current= application_current] {
          application_result result;
          try {
            if (!current->load ()) return result;
            athena::filesystem::confined_root directory (binding.root);
            const auto actual= directory.open (".").stat ();
            if (expected_root && (actual.device != expected_root->device || actual.inode != expected_root->inode))
              throw std::runtime_error ("Hodarium application Vault root changed");
            revision_store journal (database);
            const auto target= journal.offer (id);
            const auto applied= journal.applied (binding.vault, target->metadata.object);
            if (applied && journal.offer (*applied)->metadata.relative_path != target->metadata.relative_path)
              throw std::runtime_error ("Hodarium rename is waiting for logical Vault record coordination");
            athena::history::document_history_store history;
            std::string error;
            if (!history.open (binding.root, error)) throw std::runtime_error (error);
            result.completed= apply_closed_document (binding.root, journal, history, id,
              [current] { return current->load (); });
            if (!result.completed && current->load ())
              result.completed= apply_open_document (binding.root, database, id,
                [current] { return current->load (); });
          }
          catch (const std::exception& e) { result.error= binding.root.string () + ": " + e.what (); }
          return result;
        });
      return true;
    }
    return false;
  }
  void schedule_inventory (const vault_binding& binding) {
    if (!binding.enabled) return;
    auto& track= inventories[{binding.group, binding.vault}];
    if (!track || track->binding.root != binding.root) {
      track= std::make_shared<inventory_tracking> (); track->binding= binding;
    }
    track->inventoried= false;
    for (const auto& pending: scan_queue)
      if (pending.group == binding.group && pending.vault == binding.vault && pending.root == binding.root) return;
    scan_queue.push_back (binding);
  }
  bool selected (const vault_binding& binding) {
    for (const auto& current: settings->vaults (binding.group))
      if (current.vault == binding.vault && current.root == binding.root && current.enabled) return true;
    return false;
  }
  bool missing_source (const revision& previous) {
    const auto absolute= (scanning->root / previous.relative_path).generic_string ();
    if (published_buffer_actor_id (absolute) != 0) return false;
    athena::filesystem::confined_root directory (scanning->root);
    const auto current_root= directory.open (".").stat ();
    if (!scanned.root_revision || current_root.device != scanned.root_revision->device ||
        current_root.inode != scanned.root_revision->inode)
      throw std::runtime_error ("Vault root changed after Hodarium inventory");
    try { (void) directory.open (previous.relative_path); }
    catch (const std::system_error& e) {
      if (e.code () != std::errc::no_such_file_or_directory) throw;
      // A missing/unmounted root is not evidence of document deletion.
      (void) directory.open (".");
      return true;
    }
    return false;
  }
  bool reconcile_deletions (QElapsedTimer& budget) {
    if (!deletion_started) {
      deletion_started= true;
      for (const auto& [path, entry]: scanned.cache) present_objects.insert (entry.object);
    }
#ifdef __linux__
    // Do not infer absence from an inventory overtaken by a rename/save.
    if (active_inventory->watch.revision () != active_inventory->observed) {
      deletion_deferred= true; schedule_inventory (*scanning); return true;
    }
#endif
    auto profile= settings->find (scanning->group);
    if (!profile || profile->member.empty ()) return true;
    auto& journal= *journals.at (scanning->group);
    while (budget.elapsed () < 10) {
      auto page= journal.applied_page (scanning->vault, deletion_cursor, 1);
      if (page.empty ()) return true;
      const auto& previous= page.front (); deletion_cursor= previous.object;
      if (previous.deleted || previous.format != "ath-xml-v2" || present_objects.count (previous.object)) continue;
      try {
        if (!missing_source (previous)) { deletion_deferred= true; continue; }
        if (!deletion_history) {
          auto history= std::make_unique<athena::history::document_history_store> ();
          std::string error;
          if (!history->open (scanning->root, error)) throw std::runtime_error (error);
          deletion_history= std::move (history);
        }
        auto old= journal.get (previous.id);
        std::int64_t version= 0; std::string error;
        if (!deletion_history->protect (previous.relative_path, old->payload,
            "hodarium-local-delete-" + previous.id, version, error)) throw std::runtime_error (error);
        // Serialize only the final check and journal publication with buffer
        // opening. History protection must not hold the GUI publication gate.
        std::lock_guard<std::recursive_mutex> publication (document_publication_mutex ());
#ifdef __linux__
        if (active_inventory->watch.revision () != active_inventory->observed) {
          deletion_deferred= true; schedule_inventory (*scanning); return true;
        }
#endif
        if (!missing_source (previous)) { deletion_deferred= true; continue; }
        revision removed= previous;
        removed.id.clear (); removed.parents.clear (); removed.payload.clear ();
        removed.deleted= true; removed.origin_member= profile->member;
        if (!journal.capture_saved (std::move (removed), previous.id)) deletion_deferred= true;
      }
      catch (const std::exception& e) {
        deletion_deferred= true;
        report ({scanning->group, profile_phase::error, previous.relative_path + ": " + e.what (), "", 0});
      }
    }
    return false;
  }
  void inventory_step () {
    if (application_step ()) return;
    if (scan_future.valid ()) {
      if (scan_future.wait_for (std::chrono::seconds (0)) != std::future_status::ready) return;
      scanned= scan_future.get (); scan_position= 0;
      deletion_cursor.clear (); present_objects.clear (); deletion_history.reset ();
      deletion_started= false; deletion_deferred= false;
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
        bool retry= !scanned.root_revision || !scanned.errors.empty ();
        for (const auto& [path, entry]: scanned.cache) retry= retry || !entry.published;
        if (!retry && !reconcile_deletions (budget)) return;
        retry= retry || deletion_deferred;
        active_inventory->inventoried= !retry;
        active_inventory->root_revision= scanned.root_revision;
        active_inventory->cache= std::move (scanned.cache);
        active_inventory->retry= retry ? std::chrono::steady_clock::now () + std::chrono::seconds (30) :
          std::chrono::steady_clock::time_point::max ();
        scanned= {}; scanning.reset (); deletion_history.reset ();
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
    if (application_current) application_current->store (false);
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
      root / ("membership-" + group + ".sqlite"), [this, group] (profile_status state) {
        if (applying && applying->binding.group == group &&
            state.phase != profile_phase::authorized && state.phase != profile_phase::offline_valid)
          application_current->store (false);
        report (std::move (state));
      });
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
    paused= value;
    if (value && application_current) application_current->store (false);
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
    if (application_current) application_current->store (false);
    if (application_future.valid ()) application_future.wait ();
    scan_cancel->store (true);
    if (scan_timer) scan_timer->stop ();
    if (scan_future.valid ()) scan_future.wait ();
    scanned= {}; scan_queue.clear (); scanning.reset (); deletion_history.reset ();
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
class conflicts_dialog: public QDialog {
  QPointer<service> service_;
  vault_binding binding_;
  QTreeWidget* tree_;
  QPushButton* more_;
  QPushButton* refresh_;
  QPushButton* compare_;
  QLabel* status_;
  std::string cursor_;
  std::uint64_t generation_= 0;
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
            branch.deleted ? tr ("Deleted") : tr ("Document"), QString::fromStdString (branch.id)});
          child->setToolTip (1, tr ("Origin: %1").arg (QString::fromStdString (branch.origin_member)));
          child->setData (0, Qt::UserRole, QString::fromStdString (branch.id));
          child->setData (0, Qt::UserRole+1, branch.deleted);
        }
      });
  }
  void compare () {
    auto selected= tree_->selectedItems ();
    if (!service_ || selected.size () != 2 || !selected[0]->parent () ||
        selected[0]->parent () != selected[1]->parent ()) return;
    const auto object= selected[0]->parent ()->data (0, Qt::UserRole).toString ().toStdString ();
    std::vector<std::string> ids;
    for (auto* item: selected) ids.push_back (item->data (0, Qt::UserRole).toString ().toStdString ());
    const auto generation= generation_;
    compare_->setEnabled (false);
    service_->inspect_journal<std::vector<revision>> (this, binding_,
      [vault= binding_.vault, object, ids] (revision_store& journal) {
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
public:
  conflicts_dialog (service* controller, vault_binding binding, QWidget* parent):
    QDialog (parent), service_ (controller), binding_ (std::move (binding)) {
    setAttribute (Qt::WA_DeleteOnClose); setWindowTitle (tr ("Hodarium conflicts")); resize (950, 500);
    auto* layout= new QVBoxLayout (this);
    tree_= new QTreeWidget (this); tree_->setColumnCount (3);
    tree_->setHeaderLabels ({tr ("Path"), tr ("State"), tr ("Identity")});
    tree_->header ()->setSectionResizeMode (0, QHeaderView::Stretch);
    tree_->header ()->setSectionResizeMode (1, QHeaderView::ResizeToContents);
    tree_->setEditTriggers (QAbstractItemView::NoEditTriggers);
    tree_->setSelectionMode (QAbstractItemView::ExtendedSelection);
    layout->addWidget (tree_);
    status_= new QLabel (this); status_->setWordWrap (true); layout->addWidget (status_);
    auto* buttons= new QDialogButtonBox (QDialogButtonBox::Close, this);
    refresh_= buttons->addButton (tr ("Refresh"), QDialogButtonBox::ActionRole);
    compare_= buttons->addButton (tr ("Compare revisions"), QDialogButtonBox::ActionRole);
    compare_->setEnabled (false);
    more_= buttons->addButton (tr ("More"), QDialogButtonBox::ActionRole); layout->addWidget (buttons);
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    connect (refresh_, &QPushButton::clicked, this, [this] { page (true); });
    connect (more_, &QPushButton::clicked, this, [this] { page (false); });
    connect (compare_, &QPushButton::clicked, this, [this] { compare (); });
    connect (tree_, &QTreeWidget::itemSelectionChanged, this, [this] {
      auto selected= tree_->selectedItems ();
      compare_->setEnabled (selected.size () == 2 && selected[0]->parent () &&
        selected[0]->parent () == selected[1]->parent () &&
        !selected[0]->data (0, Qt::UserRole+1).toBool () && !selected[1]->data (0, Qt::UserRole+1).toBool ());
    });
    connect (tree_, &QTreeWidget::itemExpanded, this, [this] (QTreeWidgetItem* item) { expand (item); });
    page (true);
  }
};

class manager: public QDialog {
  QPointer<service> service_;
  QTableWidget* table_;
  QLabel* diagnostic_;
  QPushButton* resume_;
  QPushButton* pause_;
  QPushButton* admin_;
  QPushButton* bind_;
  QPushButton* unbind_;
  QPushButton* conflicts_;
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
    conflicts_->setEnabled (vaults_->currentRow () >= 0);
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
    conflicts_= buttons->addButton (tr ("Conflicts"), QDialogButtonBox::ActionRole);
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
      conflicts_->setEnabled (vaults_->currentRow () >= 0);
    });
    connect (conflicts_, &QPushButton::clicked, this, [this] {
      int i= table_->currentRow (), j= vaults_->currentRow ();
      if (!service_ || i < 0 || i >= int (rows_.size ()) || j < 0 || j >= int (rows_[i].vaults.size ())) return;
      (new conflicts_dialog (service_, rows_[i].vaults[j], this))->show ();
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

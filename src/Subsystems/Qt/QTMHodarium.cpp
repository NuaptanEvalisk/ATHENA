/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "QTMHodarium.hpp"
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
resource_attachment_paths material_resource_paths (const std::filesystem::path& path,
    revision_store& journal, const std::string& vault) {
  resource_attachment_paths result;
  auto append= [&] (const logical_database_object& value) {
    if (value.format != "athena-material-attachment-v1") return;
    const auto object= decode_material_object (value);
    const auto& source= object.attachment.stored_path;
    if (!resource_path_kind (source, resource_attachment_paths{source}))
      throw std::invalid_argument ("Material attachment targets protected Vault data: " + source);
    result.insert (source);
  };
  athena::filesystem::confined_root root (path);
  AthenaVaultfileInfo info; std::string error;
  if (!athena_vaultfile_read (root.path (), info, error)) throw std::runtime_error (error);
  bool local= true;
  try { (void) root.open (info.materials_db_path); }
  catch (const std::system_error& e) {
    if (e.code () != std::errc::no_such_file_or_directory) throw;
    (void) root.open ("."); local= false;
  }
  if (local) for (const auto& value: export_material_objects (root.path ())) append (value);
  auto admitted= [&] (const revision& metadata) {
    if (metadata.deleted || metadata.format != "athena-material-attachment-v1" ||
        !journal.eligible (metadata.id) ||
        journal.heads (vault, metadata.object) != std::vector<std::string>{metadata.id}) return;
    auto value= journal.get (metadata.id);
    append ({value->object, value->format, value->payload});
  };
  for (bool applied: {true, false}) {
    std::string cursor;
    for (;;) {
      auto page= applied ? journal.applied_page (vault, cursor, 256) :
                          journal.application_candidates (vault, cursor, 256);
      if (page.empty ()) break;
      for (const auto& value: page) { cursor= value.object; admitted (value); }
    }
  }
  std::string cursor;
  for (;;) {
    auto page= journal.pending_applications (vault, cursor, 256);
    if (page.empty ()) break;
    for (const auto& intent: page) {
      cursor= intent.operation;
      if (auto value= journal.offer (intent.revision_id)) admitted (value->metadata);
    }
  }
  return result;
}
struct profile_row {
  client_profile profile; profile_status status; std::vector<vault_binding> vaults;
  std::vector<relay_binding> relays;
};
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
  std::map<std::string, std::pair<std::string, recovery_candidate>> recoveries;
  bool energy_constrained= false;
  std::map<std::string, std::unique_ptr<revision_store>> journals;
  std::map<std::string, std::vector<std::string>> replication_vaults;
  std::deque<vault_binding> scan_queue;
  std::optional<vault_binding> scanning;
  std::future<source_inventory> scan_future;
  source_inventory scanned;
  struct inventory_tracking {
    vault_binding binding;
    athena::background::source_watch watch;
    resource_attachment_paths declared_resources;
    std::set<std::string> declared_basenames;
    athena::background::source_watch resource_watch{[this] (const std::filesystem::path& name) {
      return resource_path_kind (name.generic_u8string ()).has_value () ||
        declared_basenames.count (name.filename ().generic_u8string ());
    }};
    std::map<std::string, athena::filesystem::metadata> resource_cache;
    std::uint64_t resource_observed= 0;
    std::string logical_stamp;
    std::string derived_stamp;
    derived_database_capture_state derived_state;
    std::chrono::steady_clock::time_point extra_poll{}, extra_retry{};
    std::map<std::string, source_inventory::cached_source> cache;
    std::uint64_t observed= 0;
    bool inventoried= false;
    bool recovery_baseline_pending= false;
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
  struct extra_result {
    std::map<std::string, athena::filesystem::metadata> resources;
    std::vector<std::string> errors;
    bool complete= true;
  };
  std::future<extra_result> extra_future;
  std::shared_ptr<inventory_tracking> extra_track;
  std::shared_ptr<std::atomic<bool>> extra_current;
  bool paused= false;
  bool application_allowed (const vault_binding& binding) {
    if (paused || !selected (binding)) return false;
    auto track= inventories.find ({binding.group, binding.vault});
    if (track != inventories.end () && track->second->recovery_baseline_pending) return false;
    auto session= sessions.find (binding.group);
    if (session == sessions.end ()) return false;
    auto phase= session->second->status ().phase;
    return phase == profile_phase::authorized || phase == profile_phase::offline_valid;
  }
  bool application_step () {
    if (extra_future.valid ()) return false;
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
      const auto database= settings->revision_database_path (root, track->binding.group);
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
            athena::history::document_history_store history;
            std::string error;
            if (!history.open (binding.root, error)) throw std::runtime_error (error);
            if (resource_revision_format (target->metadata.format))
              result.completed= apply_closed_resource (binding.root, journal, history, id,
                [current, binding] (resource_kind kind, const std::string&) {
                  return current->load () && (binding.allow_code_resources ||
                    (kind != resource_kind::style && kind != resource_kind::sorter));
                }, material_resource_paths (binding.root, journal, binding.vault));
            else if (logical_revision_format (target->metadata.format))
              result.completed= apply_logical_revision (binding.root, journal, history, id,
                [current] { return current->load (); });
            else {
              const auto records= make_document_rename_records (binding.root);
              result.completed= apply_closed_document (binding.root, journal, history, id,
                [current] { return current->load (); }, records);
              if (!result.completed && current->load ())
                result.completed= apply_open_document (binding.root, database, id,
                  [current] { return current->load (); }, records);
            }
          }
          catch (const std::exception& e) { result.error= binding.root.string () + ": " + e.what (); }
          return result;
        });
      return true;
    }
    return false;
  }
  static std::string logical_database_stamp (const std::filesystem::path& directory, bool derived= false) {
    AthenaVaultfileInfo info; std::string error;
    if (!athena_vaultfile_read (directory, info, error)) throw std::runtime_error (error);
    athena::filesystem::confined_root root (directory);
    std::string result;
    std::vector<std::string> paths= derived ?
      std::vector<std::string>{"Vaultfile.json", info.rag_index_path, info.artifacts_path, info.bold_text_path} :
      std::vector<std::string>{"Vaultfile.json", info.namespace_db_path,
                               info.artifact_title_filter_path, info.materials_db_path};
    for (const auto& path: paths) {
      for (const auto* suffix: {"", "-wal"}) {
        result+= path + suffix + ':';
        try {
          const auto stat= root.open (path + suffix).stat ();
          result+= std::to_string (stat.device) + ',' + std::to_string (stat.inode) + ',' +
            std::to_string (stat.size) + ',' + std::to_string (stat.modified.seconds) + ',' +
            std::to_string (stat.modified.nanoseconds) + ',' + std::to_string (stat.changed.seconds) + ',' +
            std::to_string (stat.changed.nanoseconds);
        }
        catch (const std::system_error& e) {
          if (e.code () != std::errc::no_such_file_or_directory) throw;
          result+= "absent";
        }
        result+= ';';
      }
    }
    return result;
  }
  bool extra_step () {
    if (extra_future.valid ()) {
      if (paused || !selected (extra_track->binding)) extra_current->store (false);
      if (extra_future.wait_for (std::chrono::seconds (0)) != std::future_status::ready) return true;
      auto result= extra_future.get ();
      extra_track->resource_cache= std::move (result.resources);
      if (result.complete && extra_track->inventoried && extra_track->recovery_baseline_pending) {
        extra_track->recovery_baseline_pending= false;
        configure_ready_replication (extra_track->binding.group);
      }
      for (const auto& error: result.errors)
        report ({extra_track->binding.group, profile_phase::error, error, "", 0});
      extra_track->extra_retry= result.complete ? std::chrono::steady_clock::time_point::max () :
        std::chrono::steady_clock::now () + std::chrono::seconds (30);
      extra_track.reset (); extra_current.reset ();
      return false;
    }
    if (paused || application_future.valid () || scan_future.valid () || scanning) return false;
    const auto now= std::chrono::steady_clock::now ();
    for (const auto& [key, track]: inventories) {
      if (!track->inventoried || now < track->extra_poll || !selected (track->binding)) continue;
      track->extra_poll= now + std::chrono::seconds (2);
      const auto observed= track->resource_watch.revision ();
      const auto stamp= logical_database_stamp (track->binding.root);
      const auto derived_stamp= logical_database_stamp (track->binding.root, true);
      const bool logical_changed= stamp != track->logical_stamp || now >= track->extra_retry;
      const bool resources_changed= logical_changed || observed != track->resource_observed || now >= track->extra_retry;
      const bool derived_changed= derived_stamp != track->derived_stamp || now >= track->extra_retry;
      if (!resources_changed && !logical_changed && !derived_changed) continue;
      auto profile= settings->find (track->binding.group);
      if (!profile || profile->member.empty ()) continue;
      track->resource_observed= observed;
      track->logical_stamp= stamp;
      track->derived_stamp= derived_stamp;
      extra_track= track; extra_current= std::make_shared<std::atomic<bool>> (true);
      const auto database= settings->revision_database_path (root, track->binding.group);
      extra_future= std::async (std::launch::async,
        [track, database, member= profile->member, current= extra_current, observed, cancelled= scan_cancel,
         resources_changed, logical_changed, derived_changed] {
          extra_result result;
          auto permitted= [current, track] {
            if (!current->load ()) return false;
            athena::filesystem::confined_root root (track->binding.root);
            const auto actual= root.open (".").stat ();
            return track->root_revision && actual.device == track->root_revision->device &&
              actual.inode == track->root_revision->inode;
          };
          try {
            if (!permitted ()) return extra_result{{}, {}, false};
            revision_store journal (database);
            if (logical_changed) {
              const auto logical= capture_logical_databases (track->binding.root, journal,
                track->binding.vault, member, permitted);
              result.complete= logical.complete;
            }
            if (derived_changed) {
              const auto derived= capture_derived_databases (track->binding.root, journal,
                track->binding.vault, member, track->derived_state, permitted);
              result.complete= result.complete && derived.complete;
            }
            if (!resources_changed) { result.resources= track->resource_cache; return result; }
            track->declared_resources= material_resource_paths (track->binding.root, journal, track->binding.vault);
            track->declared_basenames.clear ();
            for (const auto& name: track->declared_resources)
              track->declared_basenames.insert (std::filesystem::path (name).filename ().generic_string ());
            const auto& declared= track->declared_resources;
            auto inventory= inventory_resources (track->binding.root, *cancelled, 100000, &track->resource_watch, declared);
            result.errors= inventory.errors; result.complete= result.complete && inventory.complete;
            auto previous= applied_resources (journal, track->binding.vault, declared);
            std::set<std::string> present;
            for (const auto& file: inventory.files) {
              if (!permitted ()) { result.complete= false; break; }
              present.insert (file.path);
              try {
                auto old= previous.find (file.path);
                auto cached= track->resource_cache.find (file.path);
                if (old != previous.end () && cached != track->resource_cache.end () &&
                    athena::filesystem::same_revision (cached->second, file.observed)) {
                  result.resources.emplace (file.path, file.observed); continue;
                }
                auto captured= capture_resource (track->binding.root, journal, track->binding.vault, member,
                  file, old == previous.end () ? std::optional<revision> () : old->second, permitted, declared);
                if (captured) result.resources.emplace (file.path, file.observed);
                else result.complete= false;
              }
              catch (const std::exception& e) {
                result.complete= false; result.errors.push_back (file.path + ": " + e.what ());
              }
            }
            if (result.complete && track->resource_watch.revision () == observed) {
              for (const auto& [path, value]: previous) {
                if (present.count (path) || value.deleted) continue;
                if (!capture_resource_deletion (track->binding.root, journal, member, value, [&] {
                      return permitted () && track->resource_watch.revision () == observed;
                    }, declared)) result.complete= false;
              }
            }
          }
          catch (const std::exception& e) {
            result.complete= false; result.errors.push_back (track->binding.root.string () + ": " + e.what ());
          }
          return result;
        });
      return true;
    }
    return false;
  }
  void schedule_inventory (const vault_binding& binding) {
    if (!binding.enabled) return;
    auto& track= inventories[{binding.group, binding.vault}];
    if (!track || track->binding.root != binding.root || track->binding.allow_code_resources != binding.allow_code_resources) {
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
#if defined(__linux__) || defined(__APPLE__)
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
#if defined(__linux__) || defined(__APPLE__)
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
    if (extra_step ()) return;
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
#if defined(__linux__) || defined(__APPLE__)
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
  void configure_ready_replication (const std::string& group) {
    std::vector<std::string> vaults;
    for (const auto& binding: settings->vaults (group)) if (binding.enabled) {
      auto track= inventories.find ({group, binding.vault});
      if (track != inventories.end () && !track->second->recovery_baseline_pending)
        vaults.push_back (binding.vault);
    }
    auto previous= replication_vaults.find (group);
    if (previous != replication_vaults.end () && previous->second == vaults) return;
    sessions.at (group)->configure_revisions (*journals.at (group), vaults);
    replication_vaults[group]= std::move (vaults);
  }
  void configure_replication (const std::string& group) {
    if (application_current) application_current->store (false);
    auto& journal= journals[group];
    if (!journal) journal= std::make_unique<revision_store> (settings->revision_database_path (root, group));
    const auto previous_path= settings->previous_revision_database_path (root, group);
    std::unique_ptr<revision_store> previous;
    if (previous_path && std::filesystem::exists (*previous_path))
      previous= std::make_unique<revision_store> (*previous_path);
    const auto profile= settings->find (group);
    for (const auto& binding: settings->vaults (group)) if (binding.enabled) {
      schedule_inventory (binding);
      if (previous && profile && !profile->member.empty ()) {
        auto& track= inventories.at ({group, binding.vault});
        track->recovery_baseline_pending= true;
        track->extra_retry= {};
        std::string cursor;
        for (;;) {
          auto page= journal->recover_applied_deletions (*previous, binding.vault, profile->member, cursor, 64);
          if (page.empty ()) break;
          cursor= page.back ().object;
        }
      }
    }
    replication_vaults.erase (group);
    configure_ready_replication (group);
  }
  void settle_recovery (const std::string& group) {
    // Explicit recovery approval is a local repair operation. Stop producers
    // before reconciling durable old-generation intents; do not replay traffic.
    scan_cancel->store (true);
    if (application_current) application_current->store (false);
    if (extra_current) extra_current->store (false);
    if (application_future.valid ()) (void) application_future.get ();
    if (extra_future.valid ()) (void) extra_future.get ();
    if (scan_future.valid ()) (void) scan_future.get ();
    applying.reset (); application_current.reset (); extra_track.reset (); extra_current.reset ();
    scanning.reset (); active_inventory.reset (); scanned= {}; scan_position= 0;
    deletion_started= false; deletion_cursor.clear (); present_objects.clear (); deletion_history.reset ();
    scan_cancel= std::make_shared<std::atomic<bool>> (false);
    scan_queue.clear ();
    for (const auto& entry: inventories) schedule_inventory (entry.second->binding);
    auto found= journals.find (group);
    if (found == journals.end ()) return;
    auto& journal= *found->second;
    for (const auto& binding: settings->vaults (group)) {
      std::string after;
      for (;;) {
        auto pending= journal.pending_applications (binding.vault, after, 64);
        if (pending.empty ()) break;
        athena::history::document_history_store history;
        std::string error;
        if (!history.open (binding.root, error)) throw std::runtime_error (error);
        for (const auto& intent: pending) {
          after= intent.operation;
          auto target= journal.offer (intent.revision_id);
          if (!target) throw std::runtime_error ("Recovery intent has no revision");
          bool completed= false;
          if (resource_revision_format (target->metadata.format))
            completed= apply_closed_resource (binding.root, journal, history, intent.revision_id,
              [binding] (resource_kind kind, const std::string&) {
                return binding.allow_code_resources || (kind != resource_kind::style && kind != resource_kind::sorter);
              }, material_resource_paths (binding.root, journal, binding.vault));
          else if (logical_revision_format (target->metadata.format))
            completed= apply_logical_revision (binding.root, journal, history, intent.revision_id);
          else {
            auto records= make_document_rename_records (binding.root);
            completed= apply_closed_document (binding.root, journal, history, intent.revision_id, {}, records);
            if (!completed) completed= apply_open_document (binding.root, settings->revision_database_path (root, group),
              intent.revision_id, [] { return true; }, records);
          }
          if (!completed) throw std::runtime_error ("Recovery cannot abandon an unfinished application: " +
            target->metadata.relative_path + ". Close unsaved views or resolve the local filesystem change, then retry.");
        }
      }
    }
  }
  void reset_recovered_journal (const std::string& group) {
    journals.erase (group);
    replication_vaults.erase (group);
    for (auto it= inventories.begin (); it != inventories.end (); ) {
      if (it->first.first == group) it= inventories.erase (it); else ++it;
    }
    scan_queue.erase (std::remove_if (scan_queue.begin (), scan_queue.end (), [&] (const vault_binding& v) {
      return v.group == group;
    }), scan_queue.end ());
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
        if (!journal) journal= std::make_unique<revision_store> (settings->revision_database_path (root, profile.pin.group));
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
      settings->trust_database_path (root, group), [this, group] (profile_status state) {
        if (applying && applying->binding.group == group &&
            state.phase != profile_phase::authorized && state.phase != profile_phase::offline_valid)
          application_current->store (false);
        report (std::move (state));
      });
    auto* active= session.get ();
    active->set_energy_constrained (energy_constrained);
    sessions.emplace (group, std::move (session));
    configure_replication (group);
    active->start ();
  }
  rows snapshot () {
    rows result;
    if (settings) for (const auto& profile: settings->profiles ()) {
      auto session= sessions.find (profile.pin.group);
      if (session != sessions.end ()) result.push_back ({profile, session->second->status (),
        settings->vaults (profile.pin.group), settings->relays (profile.pin.group)});
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
    if (value && extra_current) extra_current->store (false);
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
    scan_cancel->store (true);
    if (extra_current) extra_current->store (false);
    if (extra_future.valid ()) extra_future.wait ();
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
                          std::function<void (QString)> completed) {
    if (stopping_ || suspended_) { completed (tr ("Hodarium is suspended")); return; }
    QPointer<QObject> receiver= context;
    QMetaObject::invokeMethod (worker_, [this, receiver, binding, result= std::move (result), completed] () mutable {
      QString error;
      try {
        if (!worker_->selected (binding)) throw std::runtime_error ("Hodarium Vault binding changed");
        auto session= worker_->sessions.find (binding.group);
        if (session == worker_->sessions.end ()) throw std::runtime_error ("Hodarium profile unavailable");
        if (resource_revision_format (result.format)) {
          revision_store journal (worker_->settings->revision_database_path (worker_->root, binding.group));
          validate_resource_revision (binding.root, result, material_resource_paths (binding.root, journal, binding.vault));
        }
        session->second->prepare_resolution (std::move (result));
      }
      catch (const std::exception& e) { error= QString::fromUtf8 (e.what ()); }
      QMetaObject::invokeMethod (this, [receiver, completed, error] { if (receiver) completed (error); }, Qt::QueuedConnection);
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
class conflicts_dialog: public QDialog {
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
    service_->prepare_resolution (this, binding_, std::move (result), [this] (QString error) {
      submit_->setEnabled (draft_.has_value ());
      if (!error.isEmpty ()) { status_->setText (error); return; }
      status_->setText (tr ("Merge draft saved. Waiting for the server decision and protected application."));
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
    auto* drafts= buttons->addButton (tr ("Saved drafts"), QDialogButtonBox::ActionRole);
    auto* paths= buttons->addButton (tr ("File paths"), QDialogButtonBox::ActionRole);
    compare_= buttons->addButton (tr ("Compare revisions"), QDialogButtonBox::ActionRole);
    compare_->setEnabled (false);
    auto* actions= new QDialogButtonBox (this); layout->addWidget (actions);
    ancestor_= actions->addButton (tr ("Compare ancestor"), QDialogButtonBox::ActionRole);
    adopt_= actions->addButton (tr ("Adopt branch"), QDialogButtonBox::ActionRole);
    edit_= actions->addButton (tr ("Edit merge"), QDialogButtonBox::ActionRole);
    duplicate_= actions->addButton (tr ("Duplicate branch"), QDialogButtonBox::ActionRole);
    export_= actions->addButton (tr ("Export resource"), QDialogButtonBox::ActionRole);
    submit_= actions->addButton (tr ("Submit merge"), QDialogButtonBox::ActionRole);
    for (auto* button: {ancestor_, adopt_, edit_, duplicate_, export_, submit_}) button->setEnabled (false);
    more_= buttons->addButton (tr ("More"), QDialogButtonBox::ActionRole); layout->addWidget (buttons);
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
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
    page (true);
  }
};

class manager: public QDialog {
  QPointer<service> service_;
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
  QPushButton* recover_;
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
    decision_status_->setText (valid ? QString::fromStdString (rows_[i].status.decision_diagnostic) : QString ());
    QStringList links;
    if (valid) for (const auto& route: rows_[i].status.routes) if (route.established)
      links << tr ("%1 via %2: %3 ms, %4 KiB/s").arg (QString::fromStdString (route.member.substr (0, 12)),
        QString::fromStdString (route.route)).arg (route.roundtrip_ms, 0, 'f', 1)
        .arg (route.transfer_bytes_per_second/1024.0, 0, 'f', 1);
    link_status_->setText (valid ? (links.empty () ? tr ("No online peers") : links.join ("\n")) : QString ());
    resume_->setEnabled (valid); admin_->setEnabled (valid);
    relays_->setEnabled (valid); recover_->setEnabled (valid);
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
      auto* code= new QTableWidgetItem;
      code->setFlags (Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
      code->setCheckState (v.allow_code_resources ? Qt::Checked : Qt::Unchecked);
      vaults_->setItem (j, 3, code);
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
  void edit_relays () {
    int i= table_->currentRow ();
    if (!service_ || i < 0 || i >= int (rows_.size ())) return;
    const auto row= rows_[i];
    QDialog dialog (this); dialog.setWindowTitle (tr ("Hodarium Relays")); dialog.resize (600, 300);
    auto* layout= new QVBoxLayout (&dialog);
    auto* list= new QListWidget (&dialog); layout->addWidget (list);
    for (const auto& relay: row.relays) list->addItem (relay.origin.toString ());
    auto* form= new QFormLayout;
    auto* origin= new QLineEdit (&dialog); origin->setPlaceholderText ("https://");
    auto* token= new QLineEdit (&dialog); token->setEchoMode (QLineEdit::Password);
    form->addRow (tr ("Relay"), origin); form->addRow (tr ("Access token"), token); layout->addLayout (form);
    auto* buttons= new QDialogButtonBox (QDialogButtonBox::Close, &dialog);
    auto* save= buttons->addButton (tr ("Add / replace"), QDialogButtonBox::ActionRole);
    auto* remove= buttons->addButton (tr ("Remove"), QDialogButtonBox::ActionRole);
    layout->addWidget (buttons); remove->setEnabled (false);
    connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect (list, &QListWidget::currentRowChanged, &dialog, [list, origin, remove] (int selected) {
      remove->setEnabled (selected >= 0);
      if (selected >= 0) origin->setText (list->item (selected)->text ());
    });
    connect (save, &QPushButton::clicked, &dialog, [&, group= row.profile.pin.group] {
      QUrl endpoint (origin->text ().trimmed ());
      auto credential= token->text ().toStdString (); token->clear ();
      service_->perform ([group, endpoint, credential= std::move (credential)] (worker& w) {
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
      });
      dialog.accept ();
    });
    connect (remove, &QPushButton::clicked, &dialog, [&, group= row.profile.pin.group] {
      const int selected= list->currentRow ();
      if (selected < 0) return;
      auto relay= row.relays[selected];
      service_->perform ([group, relay] (worker& w) {
        w.settings->remove_relay (group, relay.origin);
        w.sessions.at (group)->configure_relays ();
        bool referenced= false;
        for (const auto& profile: w.settings->profiles ())
          for (const auto& other: w.settings->relays (profile.pin.group))
            referenced= referenced || other.credential_handle == relay.credential_handle;
        if (!referenced) delete_relay_token (relay.origin, relay.credential_handle);
        w.report (w.sessions.at (group)->status ());
      });
      dialog.accept ();
    });
    dialog.exec ();
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
    vaults_= new QTableWidget (0, 4, this);
    vaults_->setHorizontalHeaderLabels ({tr ("Selected"), tr ("Vault ID"), tr ("Local directory"), tr ("Allow styles / sorters")});
    vaults_->horizontalHeader ()->setSectionResizeMode (QHeaderView::ResizeToContents);
    vaults_->horizontalHeader ()->setStretchLastSection (true);
    vaults_->setSelectionBehavior (QAbstractItemView::SelectRows);
    vaults_->setSelectionMode (QAbstractItemView::SingleSelection);
    vaults_->setEditTriggers (QAbstractItemView::NoEditTriggers);
    layout->addWidget (vaults_);
    diagnostic_= new QLabel (this); diagnostic_->setWordWrap (true);
    diagnostic_->setTextInteractionFlags (Qt::TextSelectableByMouse);
    layout->addWidget (diagnostic_);
    decision_status_= new QLabel (this); decision_status_->setWordWrap (true);
    decision_status_->setTextInteractionFlags (Qt::TextSelectableByMouse); layout->addWidget (decision_status_);
    link_status_= new QLabel (this); link_status_->setWordWrap (true);
    link_status_->setTextInteractionFlags (Qt::TextSelectableByMouse); layout->addWidget (link_status_);
    auto* membership_buttons= new QDialogButtonBox (this);
    layout->addWidget (membership_buttons);
    auto* buttons= new QDialogButtonBox (QDialogButtonBox::Close, this);
    resume_= membership_buttons->addButton (tr ("Resume / retry"), QDialogButtonBox::ActionRole);
    pause_= membership_buttons->addButton (tr ("Pause"), QDialogButtonBox::ActionRole);
    admin_= membership_buttons->addButton (tr ("Administration"), QDialogButtonBox::ActionRole);
    relays_= membership_buttons->addButton (tr ("Relays"), QDialogButtonBox::ActionRole);
    recover_= membership_buttons->addButton (tr ("Recover membership"), QDialogButtonBox::ActionRole);
    bind_= buttons->addButton (tr ("Bind Vault"), QDialogButtonBox::ActionRole);
    unbind_= buttons->addButton (tr ("Unbind Vault"), QDialogButtonBox::ActionRole);
    conflicts_= buttons->addButton (tr ("Conflicts"), QDialogButtonBox::ActionRole);
    layout->addWidget (buttons);
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    connect (table_, &QTableWidget::itemSelectionChanged, this, [this] { selection (); });
    connect (resume_, &QPushButton::clicked, this, [this] { action (true); });
    connect (pause_, &QPushButton::clicked, this, [this] { action (false); });
    connect (relays_, &QPushButton::clicked, this, [this] { edit_relays (); });
    connect (recover_, &QPushButton::clicked, this, [this] { recover (); });
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
    auto* timer= new QTimer (this); timer->setInterval (2000);
    connect (timer, &QTimer::timeout, this, [this] {
      for (int i= 0; i < int (rows_.size ()); ++i)
        if (rows_[i].status.code_expires <= QDateTime::currentSecsSinceEpoch ())
          table_->item (i, 3)->setText ({});
      refresh ();
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

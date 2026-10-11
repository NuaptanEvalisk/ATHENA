/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "hodarium_worker.hpp"
#include "vaultfile_json.hpp"
#include "hodarium_application.hpp"
#include "hodarium_logical_database.hpp"
#include "hodarium_materials_database.hpp"
#include "hodarium_rename_records.hpp"
#include "confined_filesystem.hpp"
#include <algorithm>
#include <stdexcept>
#include <system_error>
#include <QDateTime>

namespace athena::hodarium {
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

bool sync_worker::application_allowed (const vault_binding& binding) {
  if (paused || !selected (binding)) return false;
  auto track= inventories.find ({binding.group, binding.vault});
  if (track != inventories.end () && track->second->recovery_baseline_pending) return false;
  auto session= sessions.find (binding.group);
  if (session == sessions.end ()) return false;
  auto phase= session->second->status ().phase;
  return phase == profile_phase::authorized || phase == profile_phase::offline_valid;
}

bool sync_worker::application_step () {
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
      [binding= track->binding, expected_root= track->root_revision, database, id, current= application_current,
       hooks= application_hooks, apply_live= apply_live_document] {
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
              }, material_resource_paths (binding.root, journal, binding.vault), hooks);
          else if (logical_revision_format (target->metadata.format))
            result.completed= apply_logical_revision (binding.root, journal, history, id,
              [current] { return current->load (); });
          else {
            const auto records= make_document_rename_records (binding.root);
          result.completed= apply_closed_document (binding.root, journal, history, id,
            [current] { return current->load (); }, records, hooks);
          if (!result.completed && current->load () && apply_live)
            result.completed= apply_live (binding.root, database, id,
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

std::string sync_worker::logical_database_stamp (const std::filesystem::path& directory, bool derived) {
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

bool sync_worker::extra_step () {
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

void sync_worker::schedule_inventory (const vault_binding& binding) {
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

bool sync_worker::selected (const vault_binding& binding) {
  for (const auto& current: settings->vaults (binding.group))
    if (current.vault == binding.vault && current.root == binding.root && current.enabled) return true;
  return false;
}

bool sync_worker::missing_source (const revision& previous) {
  const auto absolute= (scanning->root / previous.relative_path).generic_string ();
  if (live_source_present && live_source_present (absolute)) return false;
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

bool sync_worker::reconcile_deletions (QElapsedTimer& budget) {
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
      // The desktop adapter serializes only this final check and publication
      // with buffer opening. History protection stays outside that gate.
      bool inventory_changed= false;
      auto publish= [&] {
#if defined(__linux__) || defined(__APPLE__)
        if (active_inventory->watch.revision () != active_inventory->observed) {
          deletion_deferred= true; schedule_inventory (*scanning);
          inventory_changed= true; return;
        }
#endif
        if (!missing_source (previous)) { deletion_deferred= true; return; }
        revision removed= previous;
        removed.id.clear (); removed.parents.clear (); removed.payload.clear ();
        removed.deleted= true; removed.origin_member= profile->member;
        if (!journal.capture_saved (std::move (removed), previous.id)) deletion_deferred= true;
      };
      if (with_source_publication) with_source_publication (publish);
      else publish ();
      if (inventory_changed) return true;
    }
    catch (const std::exception& e) {
      deletion_deferred= true;
      report ({scanning->group, profile_phase::error, previous.relative_path + ": " + e.what (), "", 0});
    }
  }
  return false;
}

void sync_worker::inventory_step () {
  if (extra_step ()) return;
  if (application_step ()) return;
  if (scan_future.valid ()) {
    if (scan_future.wait_for (std::chrono::seconds (0)) != std::future_status::ready) return;
    scanned= scan_future.get (); scan_position= 0;
    deletion_cursor.clear (); present_objects.clear (); deletion_history.reset ();
    deletion_started= false; deletion_deferred= false;
    for (const auto& error: scanned.errors) report ({scanning->group, profile_phase::error, error, "", 0});
    for (const auto& conflict: scanned.conflicts)
      if (identity_conflict) identity_conflict (*scanning, conflict);
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
      if (!retry) background_errors.erase (scanning->group);
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

void sync_worker::configure_ready_replication (const std::string& group) {
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

void sync_worker::configure_replication (const std::string& group) {
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

void sync_worker::settle_recovery (const std::string& group) {
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
            }, material_resource_paths (binding.root, journal, binding.vault), application_hooks);
        else if (logical_revision_format (target->metadata.format))
          completed= apply_logical_revision (binding.root, journal, history, intent.revision_id);
        else {
          auto records= make_document_rename_records (binding.root);
          completed= apply_closed_document (binding.root, journal, history, intent.revision_id, {}, records,
            application_hooks);
          if (!completed && apply_live_document)
            completed= apply_live_document (binding.root, settings->revision_database_path (root, group),
              intent.revision_id, [] { return true; }, records);
        }
        if (!completed) throw std::runtime_error ("Recovery cannot abandon an unfinished application: " +
          target->metadata.relative_path + ". Close unsaved views or resolve the local filesystem change, then retry.");
      }
    }
  }
}

void sync_worker::reset_recovered_journal (const std::string& group) {
  journals.erase (group);
  replication_vaults.erase (group);
  for (auto it= inventories.begin (); it != inventories.end (); ) {
    if (it->first.first == group) it= inventories.erase (it); else ++it;
  }
  scan_queue.erase (std::remove_if (scan_queue.begin (), scan_queue.end (), [&] (const vault_binding& v) {
    return v.group == group;
  }), scan_queue.end ());
}

void sync_worker::update_capture () {
  bool enabled= false;
  for (const auto& profile: settings->profiles ())
    if (!profile.member.empty ()) for (const auto& binding: settings->vaults (profile.pin.group))
      enabled= enabled || binding.enabled;
  if (capture_enabled_changed) capture_enabled_changed (enabled);
}

void sync_worker::saved (const std::filesystem::path& path, const std::string& object,
    const std::optional<std::string>& predecessor, const std::string& bytes, bool inventory) {
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

void sync_worker::add_session (const client_profile& profile) {
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

rows sync_worker::snapshot () {
  rows result;
  if (settings) for (const auto& profile: settings->profiles ()) {
    auto session= sessions.find (profile.pin.group);
    if (session != sessions.end ()) result.push_back ({profile, session->second->status (),
      settings->vaults (profile.pin.group), settings->relays (profile.pin.group)});
  }
  return result;
}

athena::background::progress sync_worker::background_progress () {
  using athena::background::phase;
  athena::background::progress result;
  vault_progress.clear ();
  if (!settings) {
    if (const auto error= background_errors.find (""); error != background_errors.end ()) {
      result.state= phase::error; result.caption= "Sync failed"; result.error_detail= error->second;
    }
    return result;
  }
  if (paused) { result.state= phase::paused; result.caption= "Suspended"; return result; }
  auto rank= [] (phase value) {
    switch (value) {
      case phase::error: return 5;
      case phase::waiting: return 4;
      case phase::working: return 3;
      case phase::idle: return 2;
      default: return 1;
    }
  };
  auto merge= [&] (athena::background::progress value) {
    std::string detail= result.detail;
    if (!detail.empty ()) detail+= "\n\n";
    detail+= value.detail;
    std::string error= result.error_detail;
    if (!value.error_detail.empty ()) {
      if (!error.empty ()) error+= "\n";
      error+= value.error_detail;
    }
    const bool conflicts= result.conflicts || value.conflicts;
    if (result.state == phase::inactive || rank (value.state) > rank (result.state)) result= value;
    result.detail= std::move (detail); result.error_detail= std::move (error);
    result.conflicts= conflicts;
  };
  for (const auto& row: snapshot ()) {
    const auto& status= row.status;
    const auto transport_error= !status.discovery_diagnostic.empty () ? status.discovery_diagnostic :
      status.phase == profile_phase::offline_valid ? status.diagnostic : std::string{};
    bool transport_failed= false;
    if (row.profile.enabled && !transport_error.empty ()) {
      auto since= background_transport_failures.emplace (row.profile.pin.group, std::chrono::steady_clock::now ());
      transport_failed= std::chrono::steady_clock::now () - since.first->second >= std::chrono::seconds (60);
    }
    else background_transport_failures.erase (row.profile.pin.group);
    if (std::none_of (row.vaults.begin (), row.vaults.end (), [] (const auto& b) { return b.enabled; })) {
      athena::background::progress value;
      value.state= phase::disconnected; value.caption= "No Vault selected";
      if (status.phase == profile_phase::pending) { value.state= phase::waiting; value.caption= "Awaiting admission"; }
      if (status.phase == profile_phase::validating) { value.state= phase::working; value.caption= "Validating membership"; }
      if (status.phase == profile_phase::error || status.phase == profile_phase::denied || status.phase == profile_phase::expired) {
        value.state= phase::error; value.caption= "Membership unavailable"; value.error_detail= status.diagnostic;
      }
      if (transport_failed) { value.state= phase::error; value.caption= "Connection failed"; value.error_detail= transport_error; }
      if (!row.profile.enabled && status.phase != profile_phase::pending) {
        value.state= phase::paused; value.caption= "Paused"; value.error_detail.clear ();
      }
      value.detail= row.profile.name + ": " + value.caption;
      merge (std::move (value));
    }
    for (const auto& binding: row.vaults) {
      if (!binding.enabled) continue;
      athena::background::progress value;
      value.state= phase::disconnected; value.caption= "No peers online";
      const auto group= binding.group;
      const auto traffic= sessions.at (group)->progress (binding.vault);
      auto track= inventories.find ({group, binding.vault});
      auto journal= journals.find (group);
      if (journal != journals.end () && track != inventories.end ()) {
        const auto token= journal->second->change_token ();
        auto& cached= *track->second;
        if (!cached.progress_token || *cached.progress_token != token) {
          cached.progress_pending= !journal->second->pending_applications (binding.vault, {}, 1).empty () ||
            !journal->second->application_candidates (binding.vault, {}, 1).empty ();
          cached.progress_conflicts= !journal->second->conflicts (binding.vault, {}, 1).empty ();
          cached.progress_token= token;
        }
      }
      const bool pending_application= track != inventories.end () && track->second->progress_pending;
      const bool conflict= track != inventories.end () && track->second->progress_conflicts;
      if (traffic.connected) {
        value.state= phase::idle; value.caption= "Up to date with connected peers";
      }
      if (traffic.pending && !traffic.connected) {
        value.state= phase::waiting; value.caption= "Waiting for peers; changes pending";
      }
      if (traffic.checking || status.phase == profile_phase::validating) {
        value.state= phase::working; value.caption= "Checking synchronization";
      }
      if (status.refreshing_membership) {
        value.state= phase::working; value.caption= "Refreshing membership";
      }
      if (traffic.sending.active || traffic.receiving.active) {
        value.state= phase::working; value.bytes= true;
        value.caption= traffic.sending.active && traffic.receiving.active ? "Syncing" :
          traffic.sending.active ? "Sending" : "Receiving";
        value.current= traffic.sending.current + traffic.receiving.current;
        value.total= traffic.sending.total + traffic.receiving.total;
      }
      if (pending_application) {
        value.state= phase::working; value.caption= "Applying changes";
        value.current= value.total= 0; value.bytes= false;
      }
      if (extra_track && extra_track->binding.group == group && extra_track->binding.vault == binding.vault) {
        value.state= phase::working; value.caption= "Indexing resources";
        value.current= value.total= 0; value.bytes= false;
      }
      if (track == inventories.end () || !track->second->inventoried ||
          (scanning && scanning->group == group && scanning->vault == binding.vault)) {
        value.state= phase::working; value.caption= "Scanning sources";
        value.current= value.total= 0; value.bytes= false;
        if (scanning && scanning->group == group && scanning->vault == binding.vault && !scan_future.valid ()) {
          value.current= scan_position; value.total= scanned.documents.size ();
        }
      }
      if (conflict) { value.state= phase::waiting; value.caption= "Conflicts need attention"; value.conflicts= true; }
      if (status.phase == profile_phase::pending) { value.state= phase::waiting; value.caption= "Awaiting admission"; }
      if (!status.diagnostic.empty () && (status.phase == profile_phase::error ||
          status.phase == profile_phase::denied || status.phase == profile_phase::expired))
        value.error_detail= status.diagnostic;
      if (transport_failed) value.error_detail= transport_error;
      const auto error= background_errors.find (group);
      if (error != background_errors.end () && value.error_detail != error->second) value.error_detail+=
        (value.error_detail.empty () ? "" : "\n") + error->second;
      if (track != inventories.end () && !track->second->application_error.empty ())
        value.error_detail+= (value.error_detail.empty () ? "" : "\n") + track->second->application_error;
      if (!value.error_detail.empty ()) { value.state= phase::error; value.caption= "Sync failed"; }
      if ((!row.profile.enabled && status.phase != profile_phase::pending) ||
          status.phase == profile_phase::disabled || status.phase == profile_phase::suspended) {
        value.state= phase::paused; value.caption= "Paused"; value.error_detail.clear ();
      }
      value.detail= binding.root.filename ().u8string () + " (" + binding.vault + "): " + value.caption;
      if (traffic.sending.active || traffic.receiving.active)
        value.detail+= "\nCurrent transfers (bytes): sending " + std::to_string (traffic.sending.current) + "/" +
          std::to_string (traffic.sending.total) + ", receiving " + std::to_string (traffic.receiving.current) + "/" +
          std::to_string (traffic.receiving.total);
      if (traffic.last_synchronized)
        value.detail+= "\nLast acknowledged exchange: " + QDateTime::fromSecsSinceEpoch (traffic.last_synchronized).toLocalTime ().toString (Qt::ISODate).toStdString ();
      const auto membership= sessions.at (group)->membership ();
      for (const auto& route: status.routes) if (route.established) {
        std::string name= route.member;
        if (membership) for (const auto& member: membership->members) if (member.id == route.member) name= member.name;
        value.detail+= "\n" + name + ": " + (route.route == "p2p" ? "P2P" : route.route);
      }
      if (!status.discovery_diagnostic.empty ()) value.detail+= "\n" + status.discovery_diagnostic;
      if (status.refreshing_membership) value.detail+= "\nRefreshing membership";
      vault_progress[{group, binding.vault}]= value;
      merge (std::move (value));
    }
  }
  if (const auto error= background_errors.find (""); error != background_errors.end ()) {
    result.state= phase::error; result.caption= "Sync failed"; result.error_detail= error->second;
  }
  return result;
}

void sync_worker::join (QUrl origin, std::string name) {
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

void sync_worker::start (const std::filesystem::path& root) {
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

void sync_worker::suspended (bool value) {
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

void sync_worker::stop () {
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

} // namespace athena::hodarium

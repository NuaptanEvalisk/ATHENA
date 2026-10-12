/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include "ATHENA/Hodarium/profile_session.hpp"
#include "ATHENA/Hodarium/control_http.hpp"
#include "ATHENA/Hodarium/recovery.hpp"
#include "hodarium_inventory.hpp"
#include "hodarium_application.hpp"
#include "hodarium_resources.hpp"
#include "hodarium_logical_sync.hpp"
#include "document_history_store.hpp"
#include <QObject>
#include <QElapsedTimer>
#include <atomic>
#include <chrono>
#include <deque>
#include <filesystem>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace athena::hodarium {
resource_attachment_paths material_resource_paths (const std::filesystem::path& path,
    revision_store& journal, const std::string& vault);
struct profile_row {
  client_profile profile; profile_status status; std::vector<vault_binding> vaults;
  std::vector<relay_binding> relays;
};
using rows= std::vector<profile_row>;

// Install report and configure callbacks before start; invoke methods on the
// QObject owner thread with a running Qt event loop.
// stop must complete before destroying the worker or its callback owners.
class sync_worker: public QObject {
public:
  std::unique_ptr<client_settings> settings;
  std::map<std::string, std::unique_ptr<profile_session>> sessions;
  std::function<void (profile_status)> report;
  std::function<void (vault_binding, source_inventory::identity_conflict)> identity_conflict;
  std::function<bool (const std::filesystem::path&)> live_source_present;
  std::function<void (bool)> capture_enabled_changed;
  // Desktop runs this synchronously under its buffer publication mutex.
  // With no live editor, an unset callback executes the action directly.
  std::function<void (const std::function<void ()>&)> with_source_publication;
  // Desktop installs live_document_application_hooks. Headless must select
  // exclusive_vault after obtaining exclusive ownership of its bound Vaults.
  document_application_hooks application_hooks;
  // Unset for headless owners: there is no live actor to apply to.
  std::function<bool (const std::filesystem::path&, const std::filesystem::path&,
    const std::string&, const std::function<bool ()>&,
    const document_rename_records&)> apply_live_document;
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
    std::optional<std::pair<std::int64_t,std::int64_t>> progress_token;
    bool progress_pending= false, progress_conflicts= false;
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
  bool application_allowed (const vault_binding& binding);
  bool application_step ();
  static std::string logical_database_stamp (const std::filesystem::path& directory, bool derived= false);
  bool extra_step ();
  void schedule_inventory (const vault_binding& binding);
  bool selected (const vault_binding& binding);
  bool missing_source (const revision& previous);
  bool reconcile_deletions (QElapsedTimer& budget);
  void inventory_step ();
  void configure_ready_replication (const std::string& group);
  void configure_replication (const std::string& group);
  void settle_recovery (const std::string& group);
  void reset_recovered_journal (const std::string& group);
  void update_capture ();

  void saved (const std::filesystem::path& path, const std::string& object,
    const std::optional<std::string>& predecessor, const std::string& bytes, bool inventory= false);

  void add_session (const client_profile& profile);
  rows snapshot ();
  athena::background::progress background_progress ();
  bool suppress_background_network_errors= false;
  std::map<std::string, control_result> background_errors;
  std::map<std::pair<std::string, std::string>, athena::background::progress> vault_progress;
  std::map<std::string, std::chrono::steady_clock::time_point> background_transport_failures;
  void join (QUrl origin, std::string name);

  void start (const std::filesystem::path& root);
  void suspended (bool value);
  void stop ();
};
using worker= sync_worker;
} // namespace athena::hodarium

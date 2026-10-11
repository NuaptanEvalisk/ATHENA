/******************************************************************************
* MODULE     : QTMDocumentHistory.cpp
* DESCRIPTION: Native document-history scheduling and UI integration
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include "QTMDocumentHistory.hpp"

#include "ATHENA/Data/document_history_store.hpp"
#include "ATHENA/Data/document_persistence.hpp"
#include "ATHENA/Data/new_buffer.hpp"
#include "ATHENA/Data/new_window.hpp"
#include "ATHENA/actor_transport.hpp"
#include "ATHENA/buffer_actor.hpp"
#include "ATHENA/tm_buffer.hpp"
#include "ATHENA/tm_window.hpp"
#include "Data/Convert/Xml/document_file_codec.hpp"
#include "QTMDocumentHistoryPane.hpp"
#include "editor.hpp"
#include "file.hpp"
#include "qt_utilities.hpp"
#include "scheme.hpp"
#include "scheme_execution_context.hpp"
#include "tm_ostream.hpp"
#include "vault.hpp"
#include "vaultfile_json.hpp"
#include "buffer_name_catalog.hpp"
#include "confined_filesystem.hpp"
#include "hodarium_logical_database.hpp"
#include "hodarium_logical_sync.hpp"
#include "hodarium_resources.hpp"
#include "namespace_ontology.hpp"

#include <QApplication>
#include <QDateTime>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <limits>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <utility>

namespace {
namespace fs= std::filesystem;

enum class HistoryTrigger: std::uint64_t {
  periodic= 1,
  manual= 2,
  before_restore= 3
};

const char*
trigger_name (HistoryTrigger trigger) {
  switch (trigger) {
  case HistoryTrigger::manual: return "Manual save";
  case HistoryTrigger::before_restore: return "Before restore";
  default: return "Periodic";
  }
}
std::string
std_text (string value) {
  return std::string (value.data (), static_cast<std::size_t> (N(value)));
}

std::optional<std::int64_t>
retention_seconds () {
  string value= get_preference ("document history preservation", "1 week");
  if (value == "Unlimited") return std::nullopt;
  if (value == "1 hour") return 60 * 60;
  if (value == "6 hours") return 6 * 60 * 60;
  if (value == "1 day") return 24 * 60 * 60;
  if (value == "3 days") return 3 * 24 * 60 * 60;
  if (value == "1 month") return 30LL * 24 * 60 * 60;
  return 7LL * 24 * 60 * 60;
}

int
periodic_interval_ms () {
  int seconds= as_int (get_preference ("document history interval", "600"));
  return seconds <= 0 ? 0 : std::max (seconds * 1000, 1000);
}

bool
manual_trigger_enabled () {
  return get_preference ("document history manual save", "on") == "on";
}

struct HistoryLocation {
  fs::path root;
  std::string relative;
};

std::optional<HistoryLocation>
history_location (url document) {
  auto context= vault_capture_context ();
  if (!context || is_none (document) || is_rooted_tmfs (document) ||
      is_rooted_web (document) || is_scratch (document))
    return std::nullopt;
  std::error_code ec;
  fs::path path= fs::weakly_canonical (
    fs::path (std_text (concretize (document))), ec);
  if (ec)
    path= fs::absolute (
      fs::path (std_text (concretize (document))), ec).lexically_normal ();
  fs::path root= fs::weakly_canonical (context->root, ec);
  if (ec) root= context->root.lexically_normal ();
  fs::path relative= path.lexically_relative (root);
  if (relative.empty () || relative.is_absolute ()) return std::nullopt;
  for (const auto& part: relative)
    if (part == "..") return std::nullopt;
  std::string encoded= relative.generic_string ();
  if (!athena::history::valid_relative_document_path (encoded))
    return std::nullopt;
  return HistoryLocation {root, std::move (encoded)};
}

athena_view_id
first_view (tm_buffer buffer) {
  return buffer == nullptr || N(buffer->vws) == 0 ? ATHENA_NO_VIEW :
    buffer->vws[0]->runtime_id;
}

tm_buffer
buffer_for_actor (athena_actor_id actor_id) {
  array<url> names= get_all_buffers ();
  for (int i=0; i<N(names); ++i) {
    tm_buffer buffer= concrete_buffer (names[i]);
    if (buffer != nullptr && buffer->actor != nullptr &&
        buffer->actor->id () == actor_id)
      return buffer;
  }
  return nullptr;
}

struct HistoryJob {
  fs::path root;
  std::string relative;
  std::string content;
  HistoryTrigger trigger= HistoryTrigger::periodic;
  std::optional<std::int64_t> retention;
  athena_actor_id actor_id= ATHENA_NO_ACTOR;
  std::uint64_t generation= 0;
};

class HistoryWriter {
public:
  HistoryWriter ():
    worker_ ([this] { run (); }) {}

  ~HistoryWriter () {
    {
      std::lock_guard<std::mutex> guard (mutex_);
      stop_= true;
    }
    condition_.notify_all ();
    if (worker_.joinable ()) worker_.join ();
  }

  void submit (HistoryJob job) {
    {
      std::lock_guard<std::mutex> guard (mutex_);
      queue_.push_back (std::move (job));
    }
    condition_.notify_one ();
  }

private:
  void run () {
    athena::history::document_history_store store;
    fs::path open_root;
    while (true) {
      HistoryJob job;
      {
        std::unique_lock<std::mutex> guard (mutex_);
        condition_.wait (guard, [this] { return stop_ || !queue_.empty (); });
        if (stop_ && queue_.empty ()) return;
        job= std::move (queue_.front ());
        queue_.pop_front ();
      }
      std::string error;
      bool success= true;
      if (open_root != job.root) {
        success= store.open (job.root, error);
        if (success) open_root= job.root;
      }
      bool inserted= false;
      std::int64_t version= 0;
      if (success)
        success= store.capture (
          job.relative, job.content, trigger_name (job.trigger), job.retention,
          inserted, version, error);

      const athena_actor_id actor_id= job.actor_id;
      const std::uint64_t generation= job.generation;
      qt_post_to_main_thread ([actor_id, generation, success, error] {
        tm_buffer buffer= buffer_for_actor (actor_id);
        if (buffer == nullptr) return;
        buffer->buf->history_snapshot_queued= false;
        if (success)
          buffer->buf->history_checkpoint_generation= std::max (
            buffer->buf->history_checkpoint_generation, generation);
        else
          std_warning << "Document history capture failed: "
                      << string (error.c_str ()) << LF;
      });
    }
  }

  std::mutex mutex_;
  std::condition_variable condition_;
  std::deque<HistoryJob> queue_;
  bool stop_= false;
  std::thread worker_;
};

struct PendingSnapshot {
  HistoryLocation location;
  HistoryTrigger trigger= HistoryTrigger::periodic;
  std::uint64_t generation= 0;
};

class DocumentHistoryManager: public QObject {
public:
  explicit DocumentHistoryManager (QObject* parent): QObject (parent) {
    periodic_.setSingleShot (false);
    periodic_.setInterval (1000);
    connect (&periodic_, &QTimer::timeout, this, [this] { periodicTick (); });
    last_periodic_ms_= QDateTime::currentMSecsSinceEpoch ();
    periodic_.start ();
  }

  void request (tm_buffer buffer, HistoryTrigger trigger) {
    if (buffer == nullptr || buffer->actor == nullptr) return;
    auto location= history_location (buffer->buf->name);
    if (!location) return;
    if (trigger == HistoryTrigger::periodic &&
        buffer->buf->history_generation <=
          buffer->buf->history_checkpoint_generation)
      return;

    const athena_actor_id actor_id= buffer->actor->id ();
    auto found= pending_.find (actor_id);
    if (found != pending_.end ()) {
      if (trigger == HistoryTrigger::manual ||
          trigger == HistoryTrigger::before_restore)
        found->second.trigger= trigger;
      return;
    }

    const std::uint64_t generation= buffer->buf->history_generation;
    pending_.emplace (
      actor_id, PendingSnapshot {*location, trigger, generation});
    buffer->buf->history_snapshot_queued= true;
    actor_command_ticket ticket= buffer->actor->try_submit (
      actor_command_kind::document_history_snapshot, first_view (buffer),
      ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER,
      0, generation, static_cast<std::uint64_t> (trigger));
    if (!ticket) {
      buffer->buf->history_snapshot_queued= false;
      pending_.erase (actor_id);
    }
  }

  void received (tm_buffer buffer, string bytes, std::uint64_t generation,
                 HistoryTrigger trigger, bool success) {
    if (buffer == nullptr || buffer->actor == nullptr) return;
    const athena_actor_id actor_id= buffer->actor->id ();
    auto found= pending_.find (actor_id);
    if (found == pending_.end ()) return;
    PendingSnapshot pending= std::move (found->second);
    pending_.erase (found);
    if (!success) {
      buffer->buf->history_snapshot_queued= false;
      return;
    }
    if (pending.trigger == HistoryTrigger::manual ||
        pending.trigger == HistoryTrigger::before_restore)
      trigger= pending.trigger;

    HistoryJob job;
    job.root= std::move (pending.location.root);
    job.relative= std::move (pending.location.relative);
    job.content= std_text (std::move (bytes));
    job.trigger= trigger;
    job.retention= retention_seconds ();
    job.actor_id= actor_id;
    job.generation= generation;
    writer_.submit (std::move (job));
  }

  bool captureSynchronously (tm_buffer buffer, HistoryTrigger trigger) {
    if (buffer == nullptr || buffer->actor == nullptr) return false;
    auto location= history_location (buffer->buf->name);
    if (!location) return false;
    actor_command_record result;
    if (!buffer->actor->invoke (
          actor_command_kind::document_history_snapshot, first_view (buffer),
          ATHENA_NO_BLOB, ATHENA_NO_BLOB, &result) ||
        result.argument[0] != 0 || result.payload0 == ATHENA_NO_BLOB)
      return false;

    string bytes= actor_text_registry::instance ().take (result.payload0);
    athena::history::document_history_store store;
    std::string error;
    if (!store.open (location->root, error)) return false;
    bool inserted= false;
    std::int64_t version= 0;
    bool ok= store.capture (
      location->relative, std_text (std::move (bytes)), trigger_name (trigger),
      retention_seconds (), inserted, version, error);
    if (ok)
      buffer->buf->history_checkpoint_generation=
        buffer->buf->history_generation;
    return ok;
  }

private:
  void periodicTick () {
    int interval= periodic_interval_ms ();
    if (interval <= 0) return;
    const qint64 now= QDateTime::currentMSecsSinceEpoch ();
    if (last_periodic_ms_ != 0 && now - last_periodic_ms_ < interval) return;
    last_periodic_ms_= now;
    array<url> names= get_all_buffers ();
    for (int i=0; i<N(names); ++i) {
      tm_buffer buffer= concrete_buffer (names[i]);
      if (buffer == nullptr || buffer->buf->history_snapshot_queued) continue;
      if (buffer->buf->history_generation <=
          buffer->buf->history_checkpoint_generation)
        continue;
      request (buffer, HistoryTrigger::periodic);
    }
  }

  QTimer periodic_;
  qint64 last_periodic_ms_= 0;
  HistoryWriter writer_;
  std::unordered_map<athena_actor_id, PendingSnapshot> pending_;
};

DocumentHistoryManager*&
manager_storage () {
  static DocumentHistoryManager* instance= nullptr;
  return instance;
}

DocumentHistoryManager*
manager (bool create) {
  auto*& instance= manager_storage ();
  if (create && instance == nullptr && qApp != nullptr)
    instance= new DocumentHistoryManager (qApp);
  return instance;
}

bool
decode_history_version (url document, std::int64_t version_id,
                        tree& decoded, std::string& error) {
  auto location= history_location (document);
  if (!location) {
    error= "Document is outside the active vault";
    return false;
  }
  athena::history::document_history_store store;
  if (!store.open (location->root, error)) return false;
  athena::history::version_entry version;
  if (!store.get (version_id, version, error)) return false;
  if (version.relative_path != location->relative) {
    error= "The history version belongs to a different source"; return false;
  }
  std::string bytes;
  if (!store.reconstruct (version_id, bytes, error)) return false;
  try {
    decoded= athena::document::decode_document_bytes (
      bytes, fs::path (std_text (concretize (document)))).document;
    return true;
  }
  catch (const std::exception& e) {
    error= e.what ();
    return false;
  }
}

} // namespace

void
qtm_document_history_initialize () {
  (void) manager (true);
}

bool
qtm_document_history_manual_save_requested () {
  if (!manual_trigger_enabled ()) return true;
  const SchemeExecutionContext* context= current_scheme_execution_context ();
  if (context != nullptr && context->editor != nullptr)
    return context->editor->publish_ui (
      actor_command_kind::ui_document_history_manual_request);
  if (context == nullptr) {
    tm_buffer buffer= concrete_buffer (get_current_buffer_safe ());
    if (auto* instance= manager (true))
      instance->request (buffer, HistoryTrigger::manual);
    return true;
  }
  return false;
}

void
qtm_document_history_manual_request (tm_buffer buffer) {
  if (!manual_trigger_enabled ()) return;
  if (auto* instance= manager (true))
    instance->request (buffer, HistoryTrigger::manual);
}

void
qtm_document_history_snapshot_received (
    tm_buffer buffer, string bytes, std::uint64_t generation,
    std::uint64_t trigger_code, bool success) {
  HistoryTrigger trigger= trigger_code == static_cast<std::uint64_t> (
    HistoryTrigger::manual) ? HistoryTrigger::manual : HistoryTrigger::periodic;
  if (auto* instance= manager (true))
    instance->received (buffer, std::move (bytes), generation, trigger, success);
}

void
qtm_document_history_show (url document) {
  document_history_pane_show_document (document);
}

bool
qtm_document_history_open_version (url document, std::int64_t version_id) {
  tree decoded;
  std::string error;
  if (!decode_history_version (document, version_id, decoded, error)) {
    std_warning << "Could not open document history version: "
                << string (error.c_str ()) << LF;
    return false;
  }

  int seed= static_cast<int> (
    version_id % static_cast<std::int64_t> (std::numeric_limits<int>::max ()));
  url history_name= url_scratch ("history_", ".ath", std::max (seed, 1));
  new_buffer_in_new_window (history_name, decoded);
  tm_buffer buffer= concrete_buffer (history_name);
  if (buffer == nullptr) return false;
  buffer->buf->read_only= true;
  publish_buffer_realtime_save_paused (buffer, true);
  (void) buffer->actor->invoke (
    actor_command_kind::set_buffer_read_only, first_view (buffer),
    ATHENA_NO_BLOB, ATHENA_NO_BLOB, nullptr, SCHEME_CAPABILITY_BUFFER, 1);
  (void) buffer->actor->invoke (
    actor_command_kind::set_realtime_save_paused, first_view (buffer),
    ATHENA_NO_BLOB, ATHENA_NO_BLOB, nullptr, SCHEME_CAPABILITY_BUFFER, 1);
  string title= as_string (tail (document));
  title << " @ history " << as_string (version_id);
  set_title_buffer (history_name, title);
  return true;
}

bool
qtm_document_history_restore_version (url document, std::int64_t version_id) {
  tree decoded;
  std::string error;
  if (!decode_history_version (document, version_id, decoded, error)) {
    std_warning << "Could not restore document history version: "
                << string (error.c_str ()) << LF;
    return false;
  }

  tm_buffer buffer= concrete_buffer (document);
  if (buffer == nullptr) {
    if (buffer_load (document)) return false;
    buffer= concrete_buffer (document);
  }
  if (buffer == nullptr) return false;
  if (auto* instance= manager (true))
    if (!instance->captureSynchronously (
          buffer, HistoryTrigger::before_restore))
      return false;
  set_buffer_tree (document, decoded);
  pretend_buffer_modified (document);
  (void) new_buffer_in_new_window (document, tree (DOCUMENT));
  if (athena_realtime_save_active (document) &&
      !athena_flush_realtime_buffer (document))
    return false;
  return true;
}

bool
qtm_document_history_restore_protected (const fs::path& vault_root,
                                         std::int64_t version_id,
                                         std::string* diagnostic) {
  using namespace athena;
  if (diagnostic) diagnostic->clear ();
  try {
    const auto context= vault_capture_context ();
    if (!context || context->root != vault_root)
      throw std::runtime_error ("The recovery Vault is no longer active");
    history::document_history_store history;
    history::version_entry version;
    std::string bytes, error;
    if (!history.open (vault_root, error) || !history.get (version_id, version, error) ||
        !history.reconstruct (version_id, bytes, error)) throw std::runtime_error (error);
    if (!version.protected_snapshot) throw std::runtime_error ("Not a protected recovery snapshot");
    const auto current= [context] { return vault_context_is_current (context); };
    const auto operation= "history-restore-" + QUuid::createUuid ().toString (QUuid::WithoutBraces).toStdString ();
    std::optional<history::logical_snapshot_identity> identity;
    if (!history.protected_metadata (version_id, identity, error)) throw std::runtime_error (error);
    const bool legacy_logical_path= version.relative_path.rfind ("logical/", 0) == 0 ||
      version.relative_path.rfind ("Hodarium logical records/", 0) == 0;
    if (!identity && legacy_logical_path)
      throw std::runtime_error ("This old logical snapshot has no typed identity. Export a copy; automatic logical restoration is unavailable.");
    if (identity) {
      hodarium::logical_database_object object {identity->object_key, identity->format, bytes};
      if (!hodarium::logical_revision_format (object.format) ||
          hodarium::derived_logical_revision_format (object.format))
        throw std::runtime_error ("This logical history format is not an authoritative recoverable record");
      hodarium::validate_logical_object (object);
      hodarium::logical_database_change change {object.key, {}, bytes};
      const bool rejection= object.key == "artifact-rejections";
      const bool material= hodarium::material_logical_format (object.format);
      if (rejection) {
        if (auto current= hodarium::export_artifact_rejections (vault_root))
          change.expected= current->payload;
      }
      else {
        const auto records= material ? hodarium::export_material_objects (vault_root) :
          hodarium::export_namespace_objects (context->namespace_db, vault_root);
        for (const auto& existing: records)
          if (existing.key == object.key) change.expected= existing.payload;
      }
      if (change.expected == change.replacement) return true;
      if (change.expected) {
        std::int64_t protected_id;
        if (!history.protect_logical (version.relative_path, *change.expected, operation,
              object.format, object.key, protected_id, error))
          throw std::runtime_error (error);
      }
      const auto result= rejection ? hodarium::apply_artifact_rejections (vault_root, change, current) :
        material ? hodarium::apply_material_object (vault_root, change, current) :
        hodarium::apply_namespace_objects (context->namespace_db, vault_root, {change}, current);
      if (result != hodarium::logical_apply_result::applied && result != hodarium::logical_apply_result::unchanged)
        throw std::runtime_error (result == hodarium::logical_apply_result::dependency_missing ?
          "Restore the referenced logical dependencies first" : "Logical record changed while restoring; refresh and retry");
      if (current ()) athena_namespace_ontology_invalidate (true);
      return true;
    }
    filesystem::confined_root directory (vault_root);
    const auto destination= directory.path () / version.relative_path;
    if (!history::valid_relative_document_path (version.relative_path))
      throw std::runtime_error ("Protected history contains an invalid destination");
    if (destination.extension () == ".ath") {
      if (fs::exists (destination) || published_buffer_actor_id (destination.generic_string ()))
        return qtm_document_history_restore_version (
          url_system (string (destination.string ().c_str ())), version_id);
      (void) document::decode_document_bytes (bytes, destination);
    }
    else if (!hodarium::resource_path_kind (version.relative_path))
      throw std::runtime_error ("This history payload can only be recovered with Export copy");
    std::optional<filesystem::entry> existing;
    try { existing= directory.open (version.relative_path); }
    catch (const std::system_error& e) {
      if (e.code () != std::errc::no_such_file_or_directory) throw;
    }
    std::optional<filesystem::metadata> observed;
    if (existing) {
      observed= existing->stat ();
      const auto before= existing->read (std::numeric_limits<int>::max ());
      if (!filesystem::same_revision (*observed, existing->stat ()))
        throw std::runtime_error ("Recovery destination changed while reading");
      if (before == bytes) return true;
      std::int64_t protected_id;
      if (!history.protect (version.relative_path, before, operation, protected_id, error))
        throw std::runtime_error (error);
    }
    std::unique_lock<std::recursive_mutex> publication (document_publication_mutex ());
    const auto gate= [&] {
      if (!current ()) throw std::runtime_error ("Recovery Vault changed");
      if (!published_file_buffers (destination.generic_string ()).empty ())
        throw std::runtime_error ("Close the open resource before restoring it");
    };
    gate ();
    directory.ensure_parents (version.relative_path, gate);
    const auto written= existing ? directory.replace (version.relative_path, *existing, *observed, bytes, gate) :
      directory.create (version.relative_path, bytes, gate);
    if (!written.directory_synced) throw std::runtime_error ("Restored file was written but directory synchronization failed");
    publication.unlock ();
    if (current ()) athena_namespace_ontology_invalidate (true);
    return true;
  }
  catch (const std::exception& e) {
    if (diagnostic) *diagnostic= e.what ();
    std_warning << "Could not restore protected File History: " << string (e.what ()) << LF;
    return false;
  }
}

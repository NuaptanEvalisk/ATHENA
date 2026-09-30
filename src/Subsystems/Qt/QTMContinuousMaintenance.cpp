/******************************************************************************
* MODULE     : QTMContinuousMaintenance.cpp
* DESCRIPTION: Revision-driven enunciation maintenance on disk and BufferActors
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "QTMContinuousMaintenance.hpp"
#include "ATHENA/Data/background_workers.hpp"
#include "ATHENA/Data/enunciation_model.hpp"
#include "ATHENA/Data/node_location_cache.hpp"
#include "ATHENA/Data/node_reference.hpp"
#include "Data/Convert/Xml/athena_document_xml.hpp"
#include "Data/Convert/Xml/document_upgrade_file.hpp"
#include "buffer_actor.hpp"
#include "buffer_name_catalog.hpp"
#include "buffer_state.hpp"
#include "editor.hpp"
#include "native_interfaces.hpp"
#include "scheme.hpp"
#include "node_metadata.hpp"
#include "vault.hpp"
#include "boot.hpp"
#include <QApplication>
#include <QTimer>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace {
namespace bg= athena::background;
namespace fs= athena::filesystem;
namespace xml= athena::document;
namespace en= athena::enunciation;

// Passes consume owner-local trees. No Scheme, editor, or tree object crosses
// the worker/actor boundary; preferences are captured as native values.
bool contains_legacy_enunciation (const tree& source) {
  if (is_atomic (source) || is_func (source, MACRO) || is_func (source, XMACRO) ||
      is_func (source, QUOTE) || is_func (source, QUASI) ||
      is_func (source, QUASIQUOTE)) return false;
  const string tag= as_string (L(source));
  if (en::standard_registry ().legacy (std::string (tag.data (), N(tag)))) return true;
  for (int i=0; i<N(source); ++i)
    if (contains_legacy_enunciation (source[i])) return true;
  return false;
}
en::conversion_result enunciation_pass (const tree& source, bool number_solutions) {
  if (!contains_legacy_enunciation (source)) return {source, 0, {}};
  en::conversion_options options;
  options.numbering_preferences["number solutions"]= number_solutions;
  return en::convert_detached_source (source, options);
}

void check_conversion (const en::conversion_result& result) {
  if (!result.diagnostics.empty ())
    throw std::runtime_error (result.diagnostics.front ().tag + ": " +
                              result.diagnostics.front ().detail);
}

// Only the newly introduced body wrapper can lack its source identity in an
// otherwise identity-complete v2 file. Existing IDs/bindings are never renewed.
void identify_new_bodies (tree& value) {
  if (is_atomic (value)) return;
  if (is_func (value, MACRO) || is_func (value, XMACRO) ||
      is_func (value, QUOTE) || is_func (value, QUASI) ||
      is_func (value, QUASIQUOTE)) return;
  if (en::is_canonical (value) && athena::node::id (value[0]).empty ()) {
    athena::node::metadata header;
    if (auto* old= athena::node::get (value[0])) header= *old;
    header.id= athena::node::new_id ();
    athena::node::set (value[0], header);
  }
  for (int i=0; i<N(value); ++i) identify_new_bodies (value[i]);
}

// Scheduling/lifecycle contention is unfinished work, not a conversion error.
struct MaintenanceDeferred { std::string reason; };

struct Reply {
  std::mutex lock;
  std::condition_variable ready;
  bool done= false;
  std::string error;
  std::string deferred;
};

class Maintenance final: public QObject {
  QTimer timer;
  std::mutex lock;
  std::condition_variable wake;
  vault_context_handle context;
  bool number_solutions= false;
  std::shared_ptr<std::atomic<bool>> stopping= std::make_shared<std::atomic<bool>> (false);
  std::thread worker;

  bool current (const vault_context_handle& vault) const {
    return !stopping->load () && vault_context_is_current (vault);
  }

  void live (const vault_context_handle& vault, const std::string& name,
             std::uint64_t actor, std::uint64_t view, bool numbered) {
    auto reply= std::make_shared<Reply> ();
    auto stop= stopping;
    const auto callback= actor_continuation_registry::instance ().store (
      [reply, stop, vault, name, numbered] {
        std::string error;
        std::string deferred;
        try {
          if (!stop->load () && vault_context_is_current (vault)) {
            const auto* execution= current_scheme_execution_context ();
            if (!execution || !execution->actor || !execution->editor)
              throw MaintenanceDeferred {"Source view is no longer available"};
            auto* owner= execution->actor;
            auto* state= owner->current_state ();
            const string actual= as_string (owner->current_buffer_url ());
            if (std::string (actual.data (), N(actual)) != name)
              throw MaintenanceDeferred {"Source buffer was renamed"};
            if (!state->node_identities)
              throw std::runtime_error ("Source is not in node-model mode");
            // One actor thread owns this checkpoint, including its undo history.
            static thread_local std::string incarnation;
            static thread_local std::string storage_revision;
            static thread_local std::uint64_t checked= ~std::uint64_t (0);
            const auto generation= state->node_identities->generation ();
            const auto revision= state->storage ? state->storage->source_sha256 () : "";
            if (incarnation != vault->incarnation || checked != generation || storage_revision != revision) {
              tree body= execution->editor->the_buffer ();
              auto result= enunciation_pass (body, numbered);
              check_conversion (result);
              if (result.converted) {
                if (state->read_only) throw std::runtime_error ("Source is read-only");
                if (state->node_identities->pending () || execution->editor->get_input_mode () != 0)
                  throw MaintenanceDeferred {"Waiting for the current input transaction"};
                execution->editor->archive_state ();
                execution->editor->start_editing ();
                try { tree_set_diff (body, result.source); }
                catch (...) { execution->editor->cancel_editing (); throw; }
                execution->editor->end_editing ();
              }
              incarnation= vault->incarnation;
              storage_revision= revision;
              checked= state->node_identities->generation ();
            }
          }
        }
        catch (const MaintenanceDeferred& e) { deferred= e.reason; }
        catch (const std::exception& e) { error= e.what (); }
        catch (const string& e) { error.assign (e.data (), N(e)); }
        catch (...) { error= "Live enunciation maintenance failed"; }
        { std::lock_guard<std::mutex> guard (reply->lock);
          reply->error= std::move (error);
          reply->deferred= std::move (deferred); reply->done= true; }
        reply->ready.notify_one ();
      });
    if (!buffer_actor::try_submit_to (actor, actor_command_kind::run_native_continuation,
          view, ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER, callback)) {
      actor_continuation_registry::instance ().discard (callback);
      throw MaintenanceDeferred {
        "Nonblocking actor submission unavailable (actor lifecycle or queue contention)"};
    }
    std::unique_lock<std::mutex> guard (reply->lock);
    while (!reply->done && current (vault))
      reply->ready.wait_for (guard, std::chrono::milliseconds (100));
    if (!reply->done) throw MaintenanceDeferred {"Vault changed or maintenance stopped"};
    if (!reply->error.empty ()) throw std::runtime_error (reply->error);
    if (!reply->deferred.empty ()) throw MaintenanceDeferred {reply->deferred};
  }

  void run () {
    vault_context_handle previous;
    std::map<std::string,fs::metadata> checked;
    std::string reported_error;
    std::size_t previous_errors= 0;
    using Clock= std::chrono::steady_clock;
    struct Retry {
      Clock::time_point since= Clock::now (), reported= since;
      std::size_t attempts= 0;
    };
    std::map<std::string, Retry> retries;
    for (;;) {
      vault_context_handle vault;
      bool numbered;
      { std::unique_lock<std::mutex> guard (lock);
        if (stopping->load ()) return;
        vault= context; numbered= number_solutions; }
      if (vault != previous) {
        checked.clear (); previous= vault; previous_errors= 0; reported_error.clear ();
        retries.clear ();
      }
      if (!vault) bg::publish (bg::worker::maintenance, {});
      else try {
        bg::publish (bg::worker::maintenance,
          {bg::phase::working, 0, 0, previous_errors, "Inventory\n" + reported_error});
        auto files= bg::inventory (vault->root, stopping.get ());
        const auto buffers= published_buffer_metadata ();
        std::size_t count= 0, errors= 0, deferred= 0;
        std::string last_error, last_deferred;
        std::set<std::string> pending;
        auto retry= [&] (const std::string& file, const std::string& reason) {
          ++deferred;
          pending.insert (file);
          auto& state= retries[file];
          ++state.attempts;
          const auto now= Clock::now ();
          const auto seconds= std::chrono::duration_cast<std::chrono::seconds> (
            now - state.since).count ();
          last_deferred= file + ": " + reason + " (" +
            std::to_string (seconds) + "s, attempts=" + std::to_string (state.attempts) + ")";
          if (now - state.reported >= std::chrono::seconds (60)) {
            athena_spdlog_info ("continuous maintenance: retry pending: " + last_deferred);
            state.reported= now;
          }
        };
        std::set<std::string> present;
        for (const auto& file: files) {
          if (!current (vault)) break;
          present.insert (file.path);
          bg::publish (bg::worker::maintenance,
            {bg::phase::working, count, files.size () + buffers.size (),
             std::max (errors, previous_errors), "Enunciations: " + file.path});
          try {
            const auto absolute= (vault->root / file.path).string ();
            if (published_buffer_source (absolute).first != ATHENA_NO_ACTOR) {
              checked.erase (file.path);
              ++count; continue;
            }
            auto known= checked.find (file.path);
            if (known != checked.end () && fs::same_revision (known->second, file.revision)) {
              ++count; continue;
            }
            fs::confined_root root (vault->root);
            auto entry= root.open (file.path);
            const auto revision= entry.stat ();
            const auto bytes= entry.read (xml::codec_limits ().input_bytes);
            auto result= enunciation_pass (xml::read_xml_v2 (bytes), numbered);
            check_conversion (result);
            if (result.converted) {
              identify_new_bodies (result.source);
              const auto output= xml::write_xml_v2 (result.source);
              if (xml::read_xml_v2 (output) != result.source)
                throw std::runtime_error ("Enunciation conversion failed XML roundtrip");
              if (!current (vault) || published_buffer_source (absolute).first != ATHENA_NO_ACTOR) {
                ++count; continue;
              }
              root.preserve (std::filesystem::path (".athena/maintenance/enunciations") /
                (xml::storage_bytes_fingerprint (bytes) + ".ath"), bytes);
              std::unique_lock<std::recursive_mutex> publication (document_publication_mutex (), std::defer_lock);
              if (!current (vault) || published_buffer_source (absolute).first != ATHENA_NO_ACTOR) {
                ++count; continue;
              }
              auto replaced= root.replace (file.path, entry, revision, output, [&] {
                if (!publication.try_lock () || !current (vault) ||
                    published_buffer_source (absolute).first != ATHENA_NO_ACTOR)
                  throw MaintenanceDeferred {"File publication busy, file opened or vault changed"};
              });
              if (!replaced.directory_synced)
                throw std::runtime_error ("Converted file published but directory fsync failed");
              checked[file.path]= replaced.file.stat ();
              athena::node_location::persistent_index_wake ();
              athena::node_reference::source_changed ();
              athena_spdlog_info ("continuous maintenance: converted " + file.path);
            }
            else checked[file.path]= revision;
          }
          catch (const MaintenanceDeferred& e) { retry (file.path, e.reason); }
          catch (const std::exception& e) {
            ++errors; last_error= file.path + ": " + e.what ();
          }
          catch (const string& e) {
            ++errors; last_error= file.path + ": " + std::string (e.data (), N(e));
          }
          ++count;
        }
        for (auto it= checked.begin (); it != checked.end (); )
          if (!present.count (it->first)) it= checked.erase (it); else ++it;
        for (const auto& buffer: buffers) {
          if (!current (vault)) break;
          ++count;
          const auto relative= std::filesystem::path (buffer.first).lexically_relative (vault->root);
          if (relative.empty () || relative.is_absolute () || *relative.begin () == ".." ||
              relative.extension () != ".ath") continue;
          bool excluded= false;
          for (const auto& part: relative)
            if (part == ".athena" || part == ".backup" || part == ".git") excluded= true;
          if (excluded || !buffer.second.actor_id) continue;
          if (!buffer.second.source_view) {
            retry (buffer.first, "Waiting for source view");
            continue;
          }
          bg::publish (bg::worker::maintenance,
            {bg::phase::working, count-1, files.size () + buffers.size (),
             std::max (errors, previous_errors), "Enunciations: " + buffer.first});
          try {
            live (vault, buffer.first, buffer.second.actor_id, buffer.second.source_view, numbered);
          }
          catch (const MaintenanceDeferred& e) { retry (buffer.first, e.reason); }
          catch (const std::exception& e) { ++errors; last_error= buffer.first + ": " + e.what (); }
        }
        if (current (vault)) {
          for (auto it= retries.begin (); it != retries.end (); )
            if (!pending.count (it->first)) it= retries.erase (it); else ++it;
          if (!last_error.empty () && last_error != reported_error)
            athena_spdlog_warning ("continuous maintenance: " + last_error);
          reported_error= last_error;
          previous_errors= errors;
          std::string detail= last_error;
          if (deferred) {
            if (!detail.empty ()) detail += "\n";
            detail += "Pending retries: " + std::to_string (deferred) + "\n" + last_deferred;
          }
          bg::publish (bg::worker::maintenance,
            {deferred ? bg::phase::working : errors ? bg::phase::error : bg::phase::idle,
             count-deferred, count, errors, detail});
        }
      }
      catch (const std::exception& e) {
        bg::publish (bg::worker::maintenance, {bg::phase::error, 0, 0, 1, e.what ()});
      }
      catch (...) {
        bg::publish (bg::worker::maintenance,
          {bg::phase::error, 0, 0, 1, "Maintenance inventory failed"});
      }
      std::unique_lock<std::mutex> guard (lock);
      wake.wait_for (guard, std::chrono::seconds (3), [&] {
        return stopping->load () || context != vault;
      });
      if (stopping->load ()) return;
    }
  }

  void stop () {
    stopping->store (true); wake.notify_all ();
    if (worker.joinable ()) worker.join ();
    bg::publish (bg::worker::maintenance, {});
  }
public:
  explicit Maintenance (QObject* parent): QObject (parent) {
    connect (&timer, &QTimer::timeout, this, [this] {
      auto next= vault_capture_context ();
      if (next && next->node_model_version < 1) next.reset ();
      const bool numbered= get_preference ("number solutions") == "on";
      std::lock_guard<std::mutex> guard (lock);
      if (context != next) { context= std::move (next); wake.notify_one (); }
      number_solutions= numbered;
    });
    connect (qApp, &QCoreApplication::aboutToQuit, this, [this] { stop (); });
    timer.start (1000);
    worker= std::thread ([this] { run (); });
  }
  ~Maintenance () override { stop (); }
};
}

void qtm_continuous_maintenance_start () {
  if (headless_mode || !qApp) return;
  static Maintenance* service= new Maintenance (qApp);
  (void) service;
}

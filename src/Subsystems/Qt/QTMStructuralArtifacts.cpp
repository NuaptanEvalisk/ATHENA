/******************************************************************************
* MODULE     : QTMStructuralArtifacts.cpp
* DESCRIPTION: Coalesced live matching and revision-driven structural extraction
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "QTMStructuralArtifacts.hpp"
#include "ATHENA/Data/artifact_live_cache.hpp"
#include "ATHENA/Data/artifact_radioactive_links.hpp"
#include "ATHENA/Data/background_workers.hpp"
#include "buffer_actor.hpp"
#include "buffer_name_catalog.hpp"
#include "buffer_state.hpp"
#include "editor.hpp"
#include "native_interfaces.hpp"
#include "scheme.hpp"
#include "boot.hpp"
#include <QApplication>
#include <QTimer>
#include <condition_variable>
#include <thread>
#include <set>

namespace {
namespace bg= athena::background;
namespace ar= athena::artifact;

class StructuralArtifacts final: public QObject {
  QTimer timer;
  std::shared_ptr<std::atomic<bool>> stopping= std::make_shared<std::atomic<bool>> (false);
  std::mutex lock;
  std::condition_variable wake;
  std::thread matcher, disk;
  std::string shutdown_error;
  std::map<std::uint64_t, std::shared_ptr<std::atomic<bool>>> scheduled;

  void wait (int milliseconds) {
    std::unique_lock<std::mutex> guard (lock);
    wake.wait_for (guard, std::chrono::milliseconds (milliseconds), [&] { return stopping->load (); });
  }

  void update_buffers () {
    const auto buffers= published_buffer_metadata ();
    std::set<std::uint64_t> live;
    for (const auto& item: buffers) {
      const auto actor= item.second.actor_id, view= item.second.source_view;
      if (!actor || !view) continue;
      live.insert (actor);
      auto& pending= scheduled[actor];
      if (!pending) pending= std::make_shared<std::atomic<bool>> (false);
      if (pending->exchange (true)) continue;
      const auto busy= pending, stop= stopping;
      const auto callback= actor_continuation_registry::instance ().store ([busy, stop] {
        struct Reset { std::shared_ptr<std::atomic<bool>> value; ~Reset () { value->store (false); } } reset {busy};
        if (stop->load ()) return;
        const auto* execution= current_scheme_execution_context ();
        if (!execution || !execution->actor || !execution->editor) return;
        auto* state= execution->actor->current_state ();
        auto* editor= execution->editor;
        try {
          if (state->node_identities && !state->artifacts.ready (*state) &&
              !state->node_identities->pending () && editor->get_input_mode () == 0) {
            tree body= editor->the_buffer ();
            editor->start_editing ();
            try { state->artifacts.update (*state, body, {}); }
            catch (...) { editor->cancel_editing (); state->artifacts.reset (); throw; }
            editor->end_editing ();
          }
          editor->typeset_refresh_radioactive_links ();
        }
        catch (const std::exception& e) {
          athena_spdlog_warning (std::string ("structural artifacts: live extraction: ") + e.what ());
        }
        catch (const string& e) {
          athena_spdlog_warning ("structural artifacts: live extraction: " + std::string (e.data (), N(e)));
        }
      });
      if (!buffer_actor::try_submit_to (actor, actor_command_kind::run_native_continuation,
            view, ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER, callback)) {
        actor_continuation_registry::instance ().discard (callback); pending->store (false);
      }
    }
    for (auto i= scheduled.begin (); i != scheduled.end (); )
      if (!live.count (i->first)) { ar::close (i->first); i= scheduled.erase (i); }
      else ++i;
  }

  void matching_loop () {
    std::string reported;
    while (!stopping->load ()) {
      try {
        athena_artifact_radioactive_refresh ();
        ar::publish_pending (); reported.clear ();
      }
      catch (const std::exception& e) {
        if (reported != e.what ()) athena_spdlog_warning (
          std::string ("structural artifacts: matching index: ") + e.what ());
        reported= e.what ();
      }
      catch (...) { athena_spdlog_warning ("structural artifacts: matching index failed"); }
      wait (250);
    }
  }

  bool build (const vault_context_handle& vault, const std::filesystem::path& file,
              const std::string& saved= {}, std::string* failure= nullptr) {
    AthenaArtifactsBuildOptions options;
    options.structural_only= true;
    options.closed_sources_only= saved.empty ();
    options.saved_sha256= saved;
    AthenaArtifactsBuildResult result;
    std::string error;
    const bool ok= athena_artifacts_build (vault->root, {file}, false,
      [&] (const auto&) {
        return !saved.empty () || (!stopping->load () && vault_context_is_current (vault));
      }, result, error, options);
    if (failure) *failure= error;
    return ok;
  }

  void disk_loop () {
    vault_context_handle previous;
    std::map<std::string, athena::filesystem::metadata> checked;
    auto next_inventory= std::chrono::steady_clock::now ();
    std::vector<bg::disk_file> files;
    std::set<std::string> inventory_paths;
    bool have_inventory= false;
    std::size_t current= 0, deferred= 0;
    std::map<std::string, std::string> failures;
    bool save_failed= false;
    std::map<std::string, ar::saved_revision> saves;
    auto retry_after= std::chrono::steady_clock::now ();
    for (;;) {
      try {
        // Drain successful saves even during orderly shutdown. Discarded edits
        // never enter this queue; the disk bytes, not a live tree, are consumed.
        for (auto& job: ar::take_saved ()) saves[job.file.string ()]= std::move (job);
        if (stopping->load () || std::chrono::steady_clock::now () >= retry_after) {
          shutdown_error.clear ();
          save_failed= false;
          for (auto i= saves.begin (); i != saves.end (); ) {
            const auto& job= i->second;
            std::string error;
            const bool ok= build (job.vault, job.file, job.sha256, &error);
            if (ok || error == "Deferred: saved artifact revision was superseded") {
              if (ok && job.vault == previous)
                failures.erase (job.file.lexically_relative (job.vault->root).generic_string ());
              i= saves.erase (i);
            }
            else {
              const bool failed= error.find ("Deferred:") == std::string::npos;
              if (failed || !save_failed) shutdown_error= job.file.string () + ": " + error;
              save_failed= save_failed || failed;
              ++i;
            }
          }
          retry_after= std::chrono::steady_clock::now () + std::chrono::seconds (2);
        }
        if (stopping->load ()) {
          // Keep failed jobs available if the user cancels exit and retries.
          for (const auto& item: saves) ar::saved (item.second.file, item.second.sha256);
          return;
        }
        const auto vault= vault_capture_context ();
        if (vault != previous) {
          previous= vault; checked.clear (); files.clear (); current= deferred= 0;
          failures.clear ();
          inventory_paths.clear (); have_inventory= false;
          next_inventory= std::chrono::steady_clock::now ();
        }
        if (!vault || vault->node_model_version < 1) {
          bg::publish (bg::worker::artifacts, {}); wait (500); continue;
        }
        const auto now= std::chrono::steady_clock::now ();
        if (current == files.size () && now >= next_inventory) {
          files= bg::inventory (vault->root, stopping.get ()); current= deferred= 0;
          next_inventory= now + std::chrono::seconds (5);
          if (stopping->load () || !vault_context_is_current (vault)) continue;
          std::set<std::string> paths;
          for (const auto& item: files) paths.insert (item.path);
          if (!have_inventory || paths != inventory_paths) {
            std::string error;
            if (!athena_artifacts_prune_missing (vault->root, error)) throw std::runtime_error (error);
            inventory_paths= std::move (paths); have_inventory= true;
            for (auto i= checked.begin (); i != checked.end (); )
              if (!inventory_paths.count (i->first)) i= checked.erase (i);
              else ++i;
            for (auto i= failures.begin (); i != failures.end (); )
              if (!inventory_paths.count (i->first)) i= failures.erase (i);
              else ++i;
          }
        }
        if (current < files.size ()) {
          const auto& file= files[current++];
          const auto path= vault->root / file.path;
          const auto known= checked.find (file.path);
          if ((known == checked.end () || !athena::filesystem::same_revision (known->second, file.revision)) &&
              published_buffer_source (path.string ()).first == ATHENA_NO_ACTOR) {
            bg::publish (bg::worker::artifacts,
              {bg::phase::working, current-1, files.size (), failures.size () + save_failed, file.path,
               save_failed ? shutdown_error : failures.empty () ? "" : failures.begin ()->second});
            std::string error;
            if (build (vault, path, {}, &error)) {
              athena::filesystem::confined_root root (vault->root);
              checked[file.path]= root.open (file.path).stat ();
              failures.erase (file.path);
            }
            else if (error.find ("Deferred:") != std::string::npos) ++deferred;
            else if (error != "Artifact build cancelled") {
              failures[file.path]= file.path + ": " + error;
              bg::publish (bg::worker::artifacts,
                {bg::phase::working, current, files.size (), failures.size (), file.path,
                 failures[file.path]});
            }
          }
          continue;
        }
        bg::publish (bg::worker::artifacts,
          {!failures.empty () || save_failed ? bg::phase::error :
             (deferred || !saves.empty ()) ? bg::phase::working : bg::phase::idle,
           current-deferred, files.size (), failures.size () + save_failed,
           !saves.empty () ? shutdown_error : deferred ? "Waiting for source publication" : "",
           save_failed ? shutdown_error : failures.empty () ? "" : failures.begin ()->second});
      }
      catch (const std::exception& e) {
        bg::publish (bg::worker::artifacts, {bg::phase::error, 0, 0, 1, e.what ()});
      }
      catch (...) {
        bg::publish (bg::worker::artifacts, {bg::phase::error, 0, 0, 1, "Structural extraction failed"});
      }
      wait (250);
    }
  }

  void stop () {
    timer.stop (); stopping->store (true); wake.notify_all ();
    if (matcher.joinable ()) matcher.join ();
    if (disk.joinable ()) disk.join ();
    bg::publish (bg::worker::artifacts, {});
  }
public:
  bool flush () {
    stop ();
    if (shutdown_error.empty ()) return true;
    athena_spdlog_warning ("structural artifacts: exit flush failed: " + shutdown_error);
    stopping->store (false); timer.start (500);
    matcher= std::thread ([this] { matching_loop (); });
    disk= std::thread ([this] { disk_loop (); });
    return false;
  }
  explicit StructuralArtifacts (QObject* parent): QObject (parent) {
    connect (&timer, &QTimer::timeout, this, [this] { update_buffers (); });
    connect (qApp, &QCoreApplication::aboutToQuit, this, [this] { stop (); });
    timer.start (500);
    matcher= std::thread ([this] { matching_loop (); });
    disk= std::thread ([this] { disk_loop (); });
  }
  ~StructuralArtifacts () override { stop (); }
};
StructuralArtifacts* service= nullptr;
}

void qtm_structural_artifacts_start () {
  if (headless_mode || !qApp) return;
  if (!service) service= new StructuralArtifacts (qApp);
}

bool qtm_structural_artifacts_flush () {
  return !service || service->flush ();
}

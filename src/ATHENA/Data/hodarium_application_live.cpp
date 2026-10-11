/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "hodarium_application.hpp"
#include "hodarium_resources.hpp"
#include "buffer_name_catalog.hpp"
#include "buffer_actor.hpp"
#include "buffer_state.hpp"
#include "Data/Convert/Xml/document_upgrade_file.hpp"
#include "artifacts.hpp"
#include <stdexcept>
#include <condition_variable>
#include <chrono>

namespace athena::hodarium {
document_application_hooks live_document_application_hooks () {
  document_application_hooks hooks;
  hooks.publication_mutex= &document_publication_mutex ();
  hooks.published_owner= published_buffer_actor_id;
  hooks.resource_view_open= resource_view_open;
  hooks.rename_artifacts= [] (const document_path_change& change, std::string& error) {
    return athena_artifacts_apply_path_rename (
      change.root, change.old_path, change.new_path, false, error);
  };
  return hooks;
}

bool apply_closed_document (const std::filesystem::path& root,
    revision_store& journal, history::document_history_store& history,
    const std::string& revision_id, const std::function<bool()>& permitted,
    const document_rename_records& records) {
  return apply_closed_document (root, journal, history, revision_id, permitted, records,
                                live_document_application_hooks ());
}

bool apply_open_document (const std::filesystem::path& root,
    const std::filesystem::path& database, const std::string& revision_id,
    const std::function<bool()>& permitted, const document_rename_records& records) {
  if (permitted && !permitted ()) return false;
  revision_store journal (database);
  auto target= journal.offer (revision_id);
  if (!target) return false;
  const auto prior_id= journal.applied (target->metadata.vault, target->metadata.object);
  const auto prior= prior_id ? journal.offer (*prior_id) : std::optional<revision_offer> ();
  const auto name= (root / (prior ? prior->metadata.relative_path : target->metadata.relative_path)).generic_string ();
  const auto owner= published_buffer_source (name);
  if (!owner.first) return false;
  struct reply {
    std::mutex mutex;
    std::condition_variable ready;
    bool started= false, cancelled= false, done= false, applied= false;
    std::exception_ptr error;
  };
  auto response= std::make_shared<reply> ();
  const auto continuation= actor_continuation_registry::instance ().store (
    [root, database, revision_id, name, owner, permitted, records, response] {
      {
        std::lock_guard<std::mutex> lock (response->mutex);
        if (response->cancelled) return;
        response->started= true;
      }
      bool applied= false; std::exception_ptr error;
      try {
        const auto* execution= current_scheme_execution_context ();
        if (execution && execution->actor && execution->actor->id () == owner.first &&
            (!permitted || permitted ())) {
          auto* actor= execution->actor;
          const auto actual= as_string (actor->current_buffer_url ());
          auto* state= actor->current_state ();
          if (std::string (actual.data (), N(actual)) == name && state->storage) {
            revision_store journal (database);
            auto target= journal.get (revision_id);
            if (!target) throw std::runtime_error ("Hodarium live revision disappeared");
            detail::validate_document_application_source (*target);
            const auto operation= "hodarium-apply-" + revision_id;
            auto intent= journal.application (operation);
            auto base= intent ? intent->expected_revision : journal.applied (target->vault, target->object);
            auto previous= base ? journal.offer (*base) : std::optional<revision_offer> ();
            if (previous) {
              const auto target_hash= journal.payload_fingerprint (revision_id);
              const auto expected= journal.payload_fingerprint (*base);
              const auto captured= state->storage->source_sha256 ();
              if (!intent && *base == revision_id) applied= captured == target_hash;
              else if (captured == expected || (intent && captured == target_hash)) {
                history::document_history_store history;
                std::string message;
                if (!history.open (root, message)) throw std::runtime_error (message);
                if (target->deleted || previous->metadata.relative_path != target->relative_path) {
                  applied= actor->retire_saved_document (captured, [&] {
                    if (!detail::apply_document_on_disk (root, journal, history, revision_id,
                          permitted, records, live_document_application_hooks (), owner.first)) return false;
                    journal.finish_apply (operation, target->deleted ?
                      std::nullopt : std::optional<std::string> (target_hash));
                    return true;
                  });
                }
                else {
                  auto source= document::read_xml_v2 (target->payload);
                  applied= actor->apply_saved_document (captured, target_hash, std::move (source), [&] {
                    if (!detail::apply_document_on_disk (root, journal, history, revision_id, permitted, records, live_document_application_hooks (), owner.first))
                      return std::unique_ptr<document::document_file> ();
                    return std::make_unique<document::document_file> (
                      document::document_file::capture (root / target->relative_path, root));
                  });
                  if (applied) journal.finish_apply (operation, target_hash);
                }
              }
            }
          }
        }
      }
      catch (const std::exception&) { error= std::current_exception (); }
      catch (const string& message) {
        error= std::make_exception_ptr (std::runtime_error (std::string (message.data (), N(message))));
      }
      catch (...) { error= std::make_exception_ptr (std::runtime_error ("Hodarium live application failed")); }
      {
        std::lock_guard<std::mutex> lock (response->mutex);
        response->applied= applied; response->error= error; response->done= true;
      }
      response->ready.notify_one ();
    });
  if (!buffer_actor::try_submit_to (owner.first, actor_command_kind::run_native_continuation,
      owner.second, ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER, continuation)) {
    actor_continuation_registry::instance ().discard (continuation); return false;
  }
  const auto deadline= std::chrono::steady_clock::now () + std::chrono::seconds (5);
  std::unique_lock<std::mutex> lock (response->mutex);
  while (!response->done) {
    if (!response->started && ((permitted && !permitted ()) || std::chrono::steady_clock::now () >= deadline)) {
      response->cancelled= true; return false;
    }
    response->ready.wait_for (lock, std::chrono::milliseconds (100));
  }
  if (response->error) std::rethrow_exception (response->error);
  return response->applied;
}
}

/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "hodarium_application.hpp"
#include "confined_filesystem.hpp"
#include "buffer_name_catalog.hpp"
#include "buffer_actor.hpp"
#include "buffer_state.hpp"
#include "Data/Convert/Xml/document_upgrade_file.hpp"
#include "interop_document_source.hpp"
#include "node_metadata.hpp"
#include <stdexcept>
#include <condition_variable>
#include <chrono>

namespace athena::hodarium {
namespace {
struct observed_file {
  filesystem::entry file;
  filesystem::metadata revision;
  std::string bytes, hash;
};
std::optional<observed_file> observe (filesystem::confined_root& root,
                                    const std::string& path) {
  std::optional<filesystem::entry> entry;
  try { entry= root.open (path); }
  catch (const std::system_error& e) {
    if (e.code () != std::errc::no_such_file_or_directory) throw;
    (void) root.open (".");
    return {};
  }
  const auto before= entry->stat ();
  auto bytes= entry->read (document::codec_limits ().input_bytes);
  if (!filesystem::same_revision (before, entry->stat ()))
    throw std::runtime_error ("Hodarium application source changed while reading");
  auto hash= document::storage_bytes_fingerprint (bytes);
  return observed_file{*entry, before, std::move (bytes), std::move (hash)};
}
void validate_path (const std::string& path) {
  if (!history::valid_relative_document_path (path) ||
      std::filesystem::path (path).extension () != ".ath")
    throw std::invalid_argument ("Invalid Hodarium document application path");
  for (const auto& part: std::filesystem::path (path))
    if (part == ".athena" || part == ".backup" || part == ".git")
      throw std::invalid_argument ("Hodarium cannot replace internal Vault data");
}
void validate_source (const revision& target) {
  if (target.format != "ath-xml-v2" || target.semantic_version != 3)
    throw std::invalid_argument ("Unsupported Hodarium document model");
  validate_path (target.relative_path);
  if (target.deleted) return;
  auto source= document::read_xml_v2 (target.payload);
  auto error= interop_document_source_error (source);
  if (!error.empty ()) throw std::invalid_argument (error);
  std::string id;
  for (int i= 0; i < N(source); ++i)
    if (is_compound (source[i], "body", 1)) id= node::id (source[i][0]);
  if (!node::valid_id (id) || id != target.object)
    throw std::invalid_argument ("Hodarium payload differs from its source identity");
}
struct live_source {};
}

static bool apply_document_on_disk (const std::filesystem::path& path,
    revision_store& journal, history::document_history_store& history,
    const std::string& revision_id, const std::function<bool()>& permitted,
    std::uint64_t owning_actor= 0) {
  if (permitted && !permitted ()) return false;
  auto target= journal.get (revision_id);
  if (!target) throw std::invalid_argument ("Unknown Hodarium application revision");
  validate_source (*target);
  filesystem::confined_root root (path);
  if (history.vault_root () != root.path ())
    throw std::invalid_argument ("Hodarium history belongs to another Vault root");
  const auto operation= "hodarium-apply-" + target->id;
  auto intent= journal.application (operation);
  if (intent && intent->completed) return true;
  auto expected= intent ? intent->expected_revision : journal.applied (target->vault, target->object);
  if (!intent && expected == target->id) return true;
  auto previous= expected ? journal.offer (*expected) : std::optional<revision_offer> ();
  const auto source_path= previous ? previous->metadata.relative_path : target->relative_path;
  validate_path (source_path);
  if (target->deleted && source_path != target->relative_path)
    throw std::invalid_argument ("Hodarium tombstone changes the source path");
  const auto ensure_closed= [&] {
    if (permitted && !permitted ()) throw live_source {};
    for (const auto& file: {source_path, target->relative_path}) {
      const auto actor= published_buffer_actor_id ((root.path () / file).generic_string ());
      if (actor != 0 && actor != owning_actor) throw live_source {};
    }
  };
  bool prepared_here= false, published= false;
  try {
    ensure_closed ();
    if (!intent) {
      if (journal.heads (target->vault, target->object) != std::vector<std::string>{target->id})
        throw std::runtime_error ("Hodarium object requires conflict resolution");
      if (expected && journal.compare (*expected, target->id) != ancestry::ancestor)
        throw std::runtime_error ("Hodarium target does not advance local history");
      auto source= observe (root, source_path);
      const bool existing= previous && !previous->metadata.deleted;
      if (bool (source) != existing || (source && source->hash != journal.payload_fingerprint (*expected)))
        throw std::runtime_error ("Hodarium source differs from its applied revision");
      apply_intent prepared;
      prepared.operation= operation; prepared.revision_id= target->id;
      prepared.expected_revision= expected; prepared.source_path= source_path;
      if (source) {
        prepared.source_fingerprint= source->hash;
        std::string error;
        if (!history.protect (source_path, source->bytes, operation, prepared.protected_history_version, error))
          throw std::runtime_error (error);
      }
      prepared_here= journal.prepare_apply (prepared); intent= prepared;
    }
    if (journal.applied (target->vault, target->object) != intent->expected_revision)
      throw std::runtime_error ("Hodarium application base changed");
    if (intent->source_fingerprint) {
      std::string protected_bytes, error;
      if (!history.reconstruct (intent->protected_history_version, protected_bytes, error))
        throw std::runtime_error (error);
      if (document::storage_bytes_fingerprint (protected_bytes) != *intent->source_fingerprint)
        throw std::runtime_error ("Hodarium protected preimage does not match application intent");
    }
    std::unique_lock<std::recursive_mutex> publication (document_publication_mutex (), std::defer_lock);
    const auto gate= [&] {
      if (!publication.owns_lock () && !publication.try_lock ()) throw live_source {};
      ensure_closed ();
    };
    auto source= observe (root, source_path);
    const auto target_hash= journal.payload_fingerprint (target->id);
    const bool renamed= source_path != target->relative_path;
    auto renamed_destination= renamed ? observe (root, target->relative_path) : std::optional<observed_file> ();
    const auto& destination= renamed ? renamed_destination : source;
    const bool installed= !target->deleted && destination && destination->hash == target_hash;
    if (source && !(installed && !renamed) &&
        (!intent->source_fingerprint || source->hash != *intent->source_fingerprint))
      throw std::runtime_error ("Hodarium preimage changed during application");
    if (!source && intent->source_fingerprint && !target->deleted && !installed)
      throw std::runtime_error ("Hodarium preimage disappeared before installation");
    if (!target->deleted && !installed) {
      if (renamed && destination) throw std::runtime_error ("Hodarium rename destination is occupied");
      const auto written= source && !renamed ?
        root.replace (source_path, source->file, source->revision, target->payload, gate) :
        root.create (target->relative_path, target->payload, gate);
      published= true;
      if (!written.directory_synced) throw std::runtime_error ("Hodarium document directory sync failed");
      publication.unlock ();
    }
    if ((target->deleted || renamed) && source) {
      const auto durable= root.remove (source_path, source->file, source->revision, gate);
      published= true;
      if (!durable)
        throw std::runtime_error ("Hodarium deletion directory sync failed");
      publication.unlock ();
    }
    gate ();
    if (!renamed || intent->source_fingerprint) root.sync_parent (source_path);
    if (renamed) root.sync_parent (target->relative_path);
    auto final= observe (root, target->relative_path);
    if (renamed && observe (root, source_path))
      throw std::runtime_error ("Hodarium rename source reappeared");
    const auto fingerprint= final ? std::optional<std::string> (final->hash) : std::nullopt;
    if (target->deleted ? bool (fingerprint) : (!fingerprint || *fingerprint != target_hash))
      throw std::runtime_error ("Hodarium installed file differs from its target revision");
    if (!owning_actor) journal.finish_apply (operation, fingerprint);
    return true;
  }
  catch (const live_source&) {
    if (prepared_here && !published) journal.abandon_unpublished_apply (operation);
    return false;
  }
}

bool apply_closed_document (const std::filesystem::path& root,
    revision_store& journal, history::document_history_store& history,
    const std::string& revision_id, const std::function<bool()>& permitted) {
  return apply_document_on_disk (root, journal, history, revision_id, permitted);
}

bool apply_open_document (const std::filesystem::path& root,
    const std::filesystem::path& database, const std::string& revision_id,
    const std::function<bool()>& permitted) {
  if (permitted && !permitted ()) return false;
  revision_store journal (database);
  auto target= journal.offer (revision_id);
  if (!target || target->metadata.deleted) return false;
  const auto name= (root / target->metadata.relative_path).generic_string ();
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
    [root, database, revision_id, name, owner, permitted, response] {
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
            validate_source (*target);
            const auto operation= "hodarium-apply-" + revision_id;
            auto intent= journal.application (operation);
            auto base= intent ? intent->expected_revision : journal.applied (target->vault, target->object);
            auto previous= base ? journal.offer (*base) : std::optional<revision_offer> ();
            if (previous && !target->deleted && previous->metadata.relative_path == target->relative_path) {
              const auto target_hash= journal.payload_fingerprint (revision_id);
              const auto expected= journal.payload_fingerprint (*base);
              const auto captured= state->storage->source_sha256 ();
              if (!intent && *base == revision_id) applied= captured == target_hash;
              else if (captured == expected || (intent && captured == target_hash)) {
                auto source= document::read_xml_v2 (target->payload);
                history::document_history_store history;
                std::string message;
                if (!history.open (root, message)) throw std::runtime_error (message);
                applied= actor->apply_saved_document (captured, target_hash, std::move (source), [&] {
                  if (!apply_document_on_disk (root, journal, history, revision_id, permitted, owner.first))
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

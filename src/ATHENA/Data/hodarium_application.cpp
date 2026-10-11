/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "hodarium_application.hpp"
#include "confined_filesystem.hpp"
#include "Data/Convert/Xml/document_upgrade_file.hpp"
#include "interop_document_source.hpp"
#include "node_metadata.hpp"
#include "hodarium_local_notifications.hpp"
#include <stdexcept>

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
}

void detail::validate_document_application_source (const revision& target) {
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
namespace {
struct live_source {};
}

bool detail::apply_document_on_disk (const std::filesystem::path& path,
    revision_store& journal, history::document_history_store& history,
    const std::string& revision_id, const std::function<bool()>& permitted,
    const document_rename_records& records, const document_application_hooks& hooks,
    std::uint64_t owning_actor) {
  if (!hooks.publication_mutex ||
      (hooks.mode == document_application_hooks::ownership::live_catalog && !hooks.published_owner) ||
      (hooks.mode == document_application_hooks::ownership::exclusive_vault && owning_actor))
    throw std::invalid_argument ("Incomplete Hodarium application ownership hooks");
  if (permitted && !permitted ()) return false;
  auto target= journal.get (revision_id);
  if (!target) throw std::invalid_argument ("Unknown Hodarium application revision");
  validate_document_application_source (*target);
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
  const bool renamed= source_path != target->relative_path;
  validate_path (source_path);
  if (target->deleted && renamed)
    throw std::invalid_argument ("Hodarium tombstone changes the source path");
  if (renamed && (!records.prepare || !records.commit))
    throw std::invalid_argument ("Hodarium rename requires a durable logical-record coordinator");
  if (renamed && !hooks.rename_artifacts)
    throw std::invalid_argument ("Hodarium rename requires a durable artifact-path adapter");
  const document_path_change path_change {
    root.path (), operation, target->object, source_path, target->relative_path};
  const auto ensure_closed= [&] {
    if (permitted && !permitted ()) throw live_source {};
    if (hooks.mode == document_application_hooks::ownership::exclusive_vault) return;
    for (const auto& file: {source_path, target->relative_path}) {
      const auto actor= hooks.published_owner ((root.path () / file).generic_string ());
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
    if (renamed) records.prepare (path_change);
    if (intent->source_fingerprint) {
      std::string protected_bytes, error;
      if (!history.reconstruct (intent->protected_history_version, protected_bytes, error))
        throw std::runtime_error (error);
      if (document::storage_bytes_fingerprint (protected_bytes) != *intent->source_fingerprint)
        throw std::runtime_error ("Hodarium protected preimage does not match application intent");
    }
    std::unique_lock<std::recursive_mutex> publication (*hooks.publication_mutex, std::defer_lock);
    const auto gate= [&] {
      if (!publication.owns_lock () && !publication.try_lock ()) throw live_source {};
      ensure_closed ();
    };
    auto source= observe (root, source_path);
    const auto target_hash= journal.payload_fingerprint (target->id);
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
      root.ensure_parents (target->relative_path, gate);
      const auto written= source && !renamed ?
        root.replace (source_path, source->file, source->revision, target->payload, gate) :
        root.create (target->relative_path, target->payload, gate);
      published= true;
      if (!written.directory_synced) throw std::runtime_error ("Hodarium document directory sync failed");
    }
    if ((target->deleted || renamed) && source) {
      const auto durable= root.remove (source_path, source->file, source->revision, gate);
      published= true;
      if (!durable)
        throw std::runtime_error ("Hodarium deletion directory sync failed");
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
    if (renamed) {
      records.commit (path_change);
      std::string error;
      if (!hooks.rename_artifacts (path_change, error) ||
          !history.rename_path (source_path, target->relative_path, false, error))
        throw std::runtime_error (error);
    }
    if (renamed || target->deleted) notify_local_change (root.path (), true);
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
    const std::string& revision_id, const std::function<bool()>& permitted,
    const document_rename_records& records, const document_application_hooks& hooks) {
  return detail::apply_document_on_disk (root, journal, history, revision_id, permitted, records, hooks);
}
}

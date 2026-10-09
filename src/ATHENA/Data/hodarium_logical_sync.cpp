/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "hodarium_logical_sync.hpp"
#include "hodarium_logical_database.hpp"
#include "hodarium_artifact_database.hpp"
#include "vaultfile_json.hpp"
#include "vault.hpp"
#include "namespace_ontology.hpp"
#include "confined_filesystem.hpp"
#include "Data/Convert/Xml/document_file_codec.hpp"

#include <QCryptographicHash>
#include <set>
#include <stdexcept>

namespace athena::hodarium {
namespace {
bool allowed (const std::function<bool()>& gate) { return !gate || gate (); }
std::string fingerprint (const std::string& payload) {
  return QCryptographicHash::hash (QByteArray::fromStdString (payload),
    QCryptographicHash::Sha256).toHex ().toStdString ();
}
std::string history_path (const std::string& key) {
  return "Hodarium logical records/" + fingerprint (key) + ".json";
}
struct vault_databases {
  filesystem::confined_root root;
  AthenaVaultfileInfo info;
  explicit vault_databases (const std::filesystem::path& path): root (path) {
    std::string error;
    if (!athena_vaultfile_read (root.path (), info, error)) throw std::runtime_error (error);
  }
  std::filesystem::path path (const std::string& relative, bool required) {
    auto value= std::filesystem::path (relative);
    if (value.empty () || value.is_absolute ()) throw std::invalid_argument ("Nonrelative logical database path");
    for (const auto& part: value)
      if (part == "." || part == "..") throw std::invalid_argument ("Logical database path escapes Vault");
    try { (void) root.open (value); }
    catch (const std::system_error& e) {
      if (required || e.code () != std::errc::no_such_file_or_directory) throw;
      (void) root.open ("."); return {};
    }
    return root.path () / value;
  }
};
bool namespace_format (const std::string& format) {
  return format == "athena-namespace-v1" || format == "athena-namespace-relation-v1";
}
std::optional<std::string> observe (vault_databases& files, const revision& target) {
  if (material_logical_format (target.format)) {
    if (files.path (files.info.materials_db_path,false).empty ()) return {};
    for (const auto& object: export_material_objects (files.root.path ()))
      if (object.key==target.object) {
        if (object.format!=target.format) throw std::invalid_argument ("Logical Material format changed");
        return object.payload;
      }
    return {};
  }
  if (namespace_format (target.format)) {
    for (const auto& object: export_namespace_objects (
           files.path (files.info.namespace_db_path, true), files.root.path ()))
      if (object.key == target.object) {
        if (object.format != target.format) throw std::invalid_argument ("Logical object format changed");
        return object.payload;
      }
    return {};
  }
  if (target.format == "athena-artifact-rejections-v1") {
    if (target.object != "artifact-rejections") throw std::invalid_argument ("Invalid rejection object identity");
    return export_artifact_rejections (files.root.path ()).payload;
  }
  throw std::invalid_argument ("Unsupported authoritative logical object");
}
void protect (history::document_history_store& history, const std::string& path,
              const std::string& bytes, const std::string& operation,
              const std::string& format, const std::string& object,
              std::int64_t& version) {
  std::string error;
  if (!history.protect_logical (path, bytes, operation, format, object, version, error))
    throw std::runtime_error (error);
}
revision make_revision (const logical_database_object& object, const std::string& vault,
                        const std::string& member, bool initial) {
  revision result;
  result.vault= vault; result.object= object.key;
  result.format= object.format; result.semantic_version= 1;
  result.origin_member= initial ? "logical-source-baseline-v1" : member;
  result.relative_path= history_path (object.key); result.payload= object.payload;
  return result;
}
}

bool derived_logical_revision_format (const std::string& format) {
  return format == "athena-rag-vector-v1" || format == "athena-artifact-range-v1";
}
bool logical_revision_format (const std::string& format) {
  return namespace_format (format) || format == "athena-artifact-rejections-v1" ||
    material_logical_format (format) || derived_logical_revision_format (format);
}

logical_sync_result capture_logical_databases (
  const std::filesystem::path& root, revision_store& journal,
  const std::string& vault, const std::string& member, const std::function<bool()>& gate) {
  logical_sync_result result;
  if (!allowed (gate)) { result.complete= false; return result; }
  vault_databases files (root);
  auto authoritative= export_namespace_objects (
    files.path (files.info.namespace_db_path, true), files.root.path ());
  authoritative.push_back (export_artifact_rejections (files.root.path ()));
  const bool materials_present= !files.path (files.info.materials_db_path,false).empty ();
  if (materials_present) {
    auto materials= export_material_objects (files.root.path ());
    authoritative.insert (authoritative.end (),materials.begin (),materials.end ());
  }
  std::set<std::string> present, pending;
  std::string cursor;
  for (;;) {
    auto page= journal.pending_applications (vault, cursor, 256);
    if (page.empty ()) break;
    for (const auto& intent: page) {
      cursor= intent.operation;
      if (auto target= journal.offer (intent.revision_id)) pending.insert (target->metadata.object);
    }
  }
  history::document_history_store history;
  auto ensure_history= [&] {
    if (!history.vault_root ().empty ()) return;
    std::string error;
    if (!history.open (files.root.path (), error)) throw std::runtime_error (error);
  };
  for (const auto& object: authoritative) {
    present.insert (object.key);
    if (!allowed (gate)) { result.complete= false; return result; }
    if (pending.count (object.key)) { ++result.deferred; continue; }
    auto expected= journal.applied (vault, object.key);
    if (expected) {
      auto prior= journal.get (*expected);
      if (!prior) throw std::runtime_error ("Logical applied revision is missing");
      if (!prior->deleted && prior->format == object.format && prior->payload == object.payload) continue;
      if (!prior->deleted) {
        ensure_history (); std::int64_t version;
        protect (history, prior->relative_path, prior->payload,
          "hodarium-logical-local-" + prior->id + "-" + fingerprint (object.payload),
          prior->format, prior->object, version);
      }
    }
    if (!allowed (gate)) { result.complete= false; return result; }
    if (journal.capture_saved (make_revision (object, vault, member, !expected), expected)) ++result.captured;
    else ++result.deferred;
  }
  cursor.clear ();
  for (;;) {
    if (!allowed (gate)) { result.complete= false; return result; }
    auto page= journal.applied_page (vault, cursor, 64);
    if (page.empty ()) break;
    for (const auto& prior_metadata: page) {
      cursor= prior_metadata.object;
      if (!(namespace_format (prior_metadata.format) || material_logical_format (prior_metadata.format)) || prior_metadata.deleted ||
          present.count (prior_metadata.object) || pending.count (prior_metadata.object)) continue;
      // An absent store is not evidence that all bibliographic objects were deleted.
      if (material_logical_format (prior_metadata.format) && !materials_present) {
        ++result.deferred; result.complete= false; continue;
      }
      auto prior= journal.get (prior_metadata.id);
      if (!prior) throw std::runtime_error ("Logical deletion preimage is missing");
      if (material_logical_format (prior_metadata.format))
        (void) files.path (files.info.materials_db_path,true);
      // Recheck disappearance against a complete fresh logical snapshot, not a
      // missing/corrupt database or a partially traversed inventory.
      if (observe (files, *prior)) { ++result.deferred; continue; }
      ensure_history (); std::int64_t version;
      protect (history, prior->relative_path, prior->payload,
        "hodarium-logical-delete-" + prior->id, prior->format, prior->object, version);
      if (!allowed (gate)) { result.complete= false; return result; }
      auto removed= *prior;
      removed.id.clear (); removed.parents.clear (); removed.payload.clear ();
      removed.deleted= true; removed.origin_member= member;
      if (journal.capture_saved (std::move (removed), prior->id)) ++result.removed;
      else ++result.deferred;
    }
  }
  return result;
}

logical_sync_result capture_derived_databases (
  const std::filesystem::path& root, revision_store& journal,
  const std::string& vault, const std::string& member,
  derived_database_capture_state& state, const std::function<bool()>& gate) {
  logical_sync_result result;
  if (!allowed(gate)) { result.complete=false; return result; }
  vault_databases files(root);
  const auto capture_derived= [&] (logical_database_object object) {
    // Nondeterministic model backends may produce numerically different valid
    // results. Each result is immutable; neither replaces the other's revision.
    object.key+= "/" + fingerprint (object.payload);
    if (journal.applied (vault, object.key)) return true;
    if (!journal.capture_saved (make_revision (object, vault, member, true), {})) return false;
    ++result.derived; return true;
  };
  auto rag= files.path (files.info.rag_index_path, false);
  if (!rag.empty ()) {
    const auto identity= files.root.open (files.info.rag_index_path).stat ();
    const auto key= rag.string ()+":"+std::to_string(identity.device)+":"+std::to_string(identity.inode);
    if (state.embedding_database_identity!=key) {
      initialize_embedding_changes(rag);
      state.embedding_database_identity=key; state.embedding_sequence=0;
    }
    for (;;) {
      if (!allowed (gate)) { result.complete= false; return result; }
      auto page= export_embedding_changes (rag,state.embedding_sequence,64);
      for (auto& object: page.objects)
        if (!allowed(gate) || !capture_derived (std::move(object))) {
          result.complete=false; return result;
        }
      state.embedding_sequence=page.through;
      if (!page.more) break;
    }
  }
  else {state.embedding_database_identity.clear(); state.embedding_sequence=0;}
  std::string artifact_stamp;
  for (const auto& db: {files.info.artifacts_path,files.info.bold_text_path})
    for (const auto& suffix: {std::string(),std::string("-wal")}) {
      const auto relative=db+suffix;
      try {
        const auto observed=files.root.open(relative).stat();
        artifact_stamp+=relative+":"+std::to_string(observed.device)+":"+std::to_string(observed.inode)+":"+
          std::to_string(observed.size)+":"+std::to_string(observed.modified.seconds)+":"+
          std::to_string(observed.modified.nanoseconds)+":"+std::to_string(observed.changed.seconds)+":"+
          std::to_string(observed.changed.nanoseconds)+";";
      }
      catch (const std::system_error& e) {
        if (e.code()!=std::errc::no_such_file_or_directory) throw;
        artifact_stamp+=relative+":missing;";
      }
    }
  if (artifact_stamp!=state.artifact_database_stamp && !files.path (files.info.artifacts_path, false).empty ()) {
    std::string cursor;
    std::set<std::string> present;
    for (;;) {
      if (!allowed (gate)) { result.complete= false; return result; }
      std::vector<HodariumArtifactRangeResult> page;
      std::string error;
      if (!athena_artifacts_export_range_results(files.root.path(),cursor,64,page,error))
        throw std::runtime_error(error);
      if (page.empty ()) break;
      for (const auto& record: page) {
        cursor=record.artifact_uuid; present.insert(cursor);
        // Compare the small raw row first; unchanged rows never enter JSON,
        // Base64, payload hashing or the revision journal.
        std::string raw;
        for (const auto* field: {&record.source_uuid,&record.source_role,&record.input_hash,
                                &record.structure_hash,&record.content_hash})
          raw+=std::to_string(field->size())+":"+*field;
        for (int offset: record.offsets) raw+=":"+std::to_string(offset);
        auto old=state.range_records.find(cursor);
        if (old!=state.range_records.end() && old->second==raw) continue;
        if (!allowed(gate) || !capture_derived(encode_artifact_range_result(record))) {
          result.complete=false; return result;
        }
        state.range_records[cursor]=std::move(raw);
      }
    }
    for (auto it=state.range_records.begin();it!=state.range_records.end();)
      if (!present.count(it->first)) it=state.range_records.erase(it); else ++it;
    state.artifact_database_stamp=std::move(artifact_stamp);
  }
  return result;
}

bool apply_logical_revision (const std::filesystem::path& root, revision_store& journal,
  history::document_history_store& history, const std::string& id,
  const std::function<bool()>& gate) {
  if (!allowed (gate)) return false;
  auto target= journal.get (id);
  if (!target || !logical_revision_format (target->format) || target->semantic_version != 1 ||
      target->relative_path != history_path (target->object))
    throw std::invalid_argument ("Invalid Hodarium logical revision model or path");
  if (!journal.eligible (id)) return false;
  vault_databases files (root);
  if (history.vault_root () != files.root.path ()) throw std::invalid_argument ("Logical history belongs to another Vault");
  if (derived_logical_revision_format (target->format)) {
    if (target->deleted || !target->parents.empty ())
      throw std::invalid_argument ("Derived Hodarium result must be immutable");
    const auto suffix= "/" + fingerprint (target->payload);
    if (target->object.size () <= suffix.size () ||
        target->object.compare (target->object.size ()-suffix.size (), suffix.size (), suffix) != 0)
      throw std::invalid_argument ("Derived Hodarium result fingerprint mismatch");
    if (journal.applied (target->vault, target->object) == id) return true;
    logical_database_object object {target->object.substr (0, target->object.size ()-suffix.size ()),
      target->format, target->payload};
    if (!allowed (gate)) return false;
    logical_apply_result applied;
    if (target->format == "athena-rag-vector-v1") {
      auto path= files.path (files.info.rag_index_path, false);
      if (path.empty ()) return false;
      applied= apply_embedding_object (path, object, gate);
    }
    else {
      if (files.path (files.info.artifacts_path, false).empty ()) return false;
      applied= apply_artifact_object (files.root.path (), object, gate);
    }
    if (applied == logical_apply_result::dependency_missing || applied == logical_apply_result::stale) return false;
    // This records consumption of the immutable result, not replacement of a
    // document snapshot. Its verified original bytes remain in the journal.
    journal.record_applied (id, {});
    return true;
  }
  if (target->deleted && target->format == "athena-artifact-rejections-v1")
    throw std::invalid_argument ("Use an explicit empty rejection list, not a tombstone");
  if (!target->deleted) validate_logical_object ({target->object, target->format, target->payload});
  const auto operation= "hodarium-logical-apply-" + id;
  auto intent= journal.application (operation);
  if (intent && intent->completed) return true;
  auto expected= intent ? intent->expected_revision : journal.applied (target->vault, target->object);
  if (!intent && expected == id) return true;
  auto previous= expected ? journal.get (*expected) : std::optional<revision> ();
  std::optional<std::string> preimage;
  if (previous && !previous->deleted) preimage= previous->payload;
  const std::optional<std::string> replacement= target->deleted ? std::nullopt :
    std::optional<std::string> (target->payload);
  auto current= observe (files, *target);
  if (!intent) {
    if (journal.heads (target->vault, target->object) != std::vector<std::string> {id} ||
        current != preimage) return false;
    apply_intent prepared;
    prepared.operation= operation; prepared.revision_id= id;
    prepared.expected_revision= expected; prepared.source_path= target->relative_path;
    if (preimage) {
      prepared.source_fingerprint= fingerprint (*preimage);
      protect (history, prepared.source_path, *preimage, operation,
        previous->format, previous->object, prepared.protected_history_version);
    }
    if (!allowed (gate)) return false;
    journal.prepare_apply (prepared); intent= prepared;
  }
  if (journal.applied (target->vault, target->object) != intent->expected_revision)
    throw std::runtime_error ("Logical application base changed");
  if (intent->source_fingerprint) {
    std::string protected_bytes, error;
    if (!history.reconstruct (intent->protected_history_version, protected_bytes, error))
      throw std::runtime_error (error);
    if (fingerprint (protected_bytes) != *intent->source_fingerprint)
      throw std::runtime_error ("Logical protected preimage differs from intent");
    if (!previous || previous->deleted || previous->payload!=protected_bytes ||
        previous->object!=target->object)
      throw std::runtime_error ("Logical history has no matching prior revision identity");
    // Older interrupted intents may have protected bytes but no typed binding.
    // Register only from the verified prior revision, never from JSON shape.
    if (!history.set_protected_metadata (intent->protected_history_version,
          previous->format, previous->object, error)) throw std::runtime_error (error);
  }
  if (current != replacement) {
    if (current != preimage || !allowed (gate)) return false;
    logical_database_change change {target->object, preimage, replacement};
    const auto applied= namespace_format (target->format) ? apply_namespace_objects (
      files.path (files.info.namespace_db_path, true), files.root.path (), {change}, gate) :
      material_logical_format (target->format) ? apply_material_object (files.root.path (),change,gate) :
      apply_artifact_rejections (files.root.path (), change, gate);
    if (applied == logical_apply_result::stale || applied == logical_apply_result::dependency_missing) {
      journal.abandon_unpublished_apply (operation);
      return false;
    }
  }
  auto observed= observe (files, *target);
  if (observed != replacement) return false;
  journal.finish_apply (operation, observed ? std::optional<std::string> (fingerprint (*observed)) : std::nullopt);
  if (namespace_format (target->format)) {
    const auto context= vault_capture_context ();
    if (context && context->root == files.root.path ()) athena_namespace_ontology_invalidate (false);
  }
  return true;
}
} // namespace athena::hodarium

/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include "ATHENA/Hodarium/revisions.hpp"
#include "document_history_store.hpp"
#include <functional>
#include <map>
#include <cstdint>

namespace athena::hodarium {
struct logical_sync_result {
  std::size_t captured= 0, removed= 0, derived= 0, deferred= 0;
  bool complete= true;
};

bool logical_revision_format (const std::string& format);
bool derived_logical_revision_format (const std::string& format);

// Keep one instance per bound Vault, exclusively owned by the serialized worker.
// Cursor advances only after journal capture succeeds; losing this cache is safe.
struct derived_database_capture_state {
  std::string embedding_database_identity;
  std::int64_t embedding_sequence= 0;
  std::string artifact_database_stamp;
  std::map<std::string,std::string> range_records;
};
logical_sync_result capture_derived_databases (
  const std::filesystem::path& root, revision_store& journal,
  const std::string& vault, const std::string& member,
  derived_database_capture_state& state,
  const std::function<bool()>& permitted= {});

// Authoritative records ONLY. Off-GUI, with the same journal owner as file capture. Does not inspect editor
// trees or switch active Vaults. Every authoritative source is enumerated to
// completion before absence can produce a tombstone. Gate false cancels without
// manufacturing deletion. Missing/corrupt namespace DB is an error, not empty.
logical_sync_result capture_logical_databases (
  const std::filesystem::path& root, revision_store& journal,
  const std::string& vault, const std::string& member,
  const std::function<bool()>& permitted= {});

// True means durably consumed/applied; false is cancellation, a missing logical
// dependency or a changed local preimage. No GUI/actor calls. Authoritative
// replacements protect their preimage before creating the durable apply intent.
// Cache results never replace authoritative records and are retained immutably
// in the revision journal even when an equivalent local result already exists.
bool apply_logical_revision (
  const std::filesystem::path& root, revision_store& journal,
  history::document_history_store& history, const std::string& revision_id,
  const std::function<bool()>& permitted= {});
} // namespace athena::hodarium

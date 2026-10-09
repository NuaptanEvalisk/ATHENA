/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include "ATHENA/Hodarium/revisions.hpp"
#include "document_history_store.hpp"
#include <functional>

namespace athena::hodarium {
// Owner-thread operation on an explicitly selected local Vault. Validates native
// source payloads and resumes a durable intent before advancing applied state.
// False defers to the live BufferActor; no live document is mutated here.
bool apply_closed_document (const std::filesystem::path& root,
  revision_store& journal, history::document_history_store& history,
  const std::string& revision_id,
  const std::function<bool()>& permitted= {});
// Background caller: dispatch a content update to its owning actor, using only
// native immutable request data across threads. Dirty or busy buffers defer.
bool apply_open_document (const std::filesystem::path& root,
  const std::filesystem::path& journal_path, const std::string& revision_id,
  const std::function<bool()>& permitted);
}

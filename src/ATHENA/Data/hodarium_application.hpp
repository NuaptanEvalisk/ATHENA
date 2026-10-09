/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include "ATHENA/Hodarium/revisions.hpp"
#include "document_history_store.hpp"
#include <functional>

namespace athena::hodarium {
struct document_path_change {
  std::filesystem::path root;
  std::string operation, object, old_path, new_path;
};
// Both callbacks are required for a path change. prepare durably records the
// logical preimages and expected values without mutating them. commit performs
// their conditional update. Each is idempotent by operation, including after a
// process restart; neither callback may await a BufferActor or access GUI state.
// The document intent remains pending until commit and all derived lineage
// updates succeed. Existing safe-rename artifact/history machinery is reused.
struct document_rename_records {
  std::function<void(const document_path_change&)> prepare;
  std::function<void(const document_path_change&)> commit;
};
// Owner-thread operation on an explicitly selected local Vault. Validates native
// source payloads and resumes a durable intent before advancing applied state.
// False defers to the live BufferActor; no live document is mutated here.
bool apply_closed_document (const std::filesystem::path& root,
  revision_store& journal, history::document_history_store& history,
  const std::string& revision_id,
  const std::function<bool()>& permitted= {},
  const document_rename_records& records= {});
// Background caller: dispatch to the owning actor, using only native immutable
// request data across threads. Dirty or busy buffers defer. A remote deletion or
// move retires the clean source buffer through the GUI close path; a move never
// renames over another open buffer. The destination remains available to reopen.
bool apply_open_document (const std::filesystem::path& root,
  const std::filesystem::path& journal_path, const std::string& revision_id,
  const std::function<bool()>& permitted,
  const document_rename_records& records= {});
}

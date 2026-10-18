/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once

#include "ATHENA/Hodarium/revisions.hpp"
#include "confined_filesystem.hpp"
#include "document_history_store.hpp"
#include "hodarium_application_hooks.hpp"
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <set>

namespace athena::background { class source_watch; }

namespace athena::hodarium {
enum class resource_kind { avd, attachment, style, sorter };
// Supplied by the logical Materials adapter, not by arbitrary peer filenames.
// Incoming attachment metadata may authorize its bytes before metadata apply
// (which itself waits for the verified bytes). These must be eligible logical
// revisions/current local records; a receipt-only unresolved branch is no grant.
using resource_attachment_paths= std::set<std::string>;
struct resource_file {
  std::string path;
  resource_kind kind;
  filesystem::metadata observed;
};
struct resource_inventory {
  std::vector<resource_file> files;
  std::vector<std::string> errors;
  bool complete= false;
};

// Explicit source-file allowlist, not an "everything except databases" scan.
// Internal/hidden trees, secrets, host configuration and unknown formats do not
// participate. Entries are descriptor-confined; no symlink is followed.
std::optional<resource_kind> resource_path_kind (const std::string& path,
  const resource_attachment_paths& declared= {});
bool resource_revision_format (const std::string& format);
// Metadata-only half of the live resource application contract. Does not read
// or mutate the Vault, journal, or resource payload. Declared custom material
// attachments must be supplied explicitly: unknown extensions fail closed.
resource_kind validate_resource_revision_metadata (
  const revision& value, const resource_attachment_paths& declared= {});
resource_inventory inventory_resources (const std::filesystem::path& root,
  const std::atomic<bool>& cancelled, std::size_t maximum_entries= 100000,
  background::source_watch* watch= nullptr, const resource_attachment_paths& declared= {});

// Build once per inventory, rather than scanning the journal for every file.
// This is a disposable view of synchronized revision identities, not a second
// manifest with independent authority. Duplicate occupied paths are errors.
using resource_bindings= std::map<std::string, revision>;
resource_bindings applied_resources (revision_store&, const std::string& vault,
  const resource_attachment_paths& declared= {});

// Initial imports derive a UUID from the Vault identity and canonical relative
// path, NEVER from bytes or the device. Identical independently imported replicas
// share a genesis revision; differing bytes become branches of the same object.
// Recreating a retired/moved initial path receives a fresh UUID. This function
// computes only the initial import ID, not the current identity of a path.
std::string initial_resource_identity (const std::string& vault, const std::string& path,
  const resource_attachment_paths& declared= {});

// Caller serializes local capture/application. On subsequent passes
// pass the exact applied binding, and update the local bindings after success.
// An explicit move supplies the OLD applied binding and NEW resource_file;
// the old path must be absent. Scanning alone never infers a move by content.
std::optional<std::string> capture_resource (const std::filesystem::path& root,
  revision_store&, const std::string& vault, const std::string& member,
  const resource_file&, const std::optional<revision>& previous= {},
  const std::function<bool()>& permitted= {}, const resource_attachment_paths& declared= {});

// Call only after a complete inventory and explicit deletion authorization.
// Missing files in an incomplete/cancelled scan must never become tombstones.
std::optional<std::string> capture_resource_deletion (
  const std::filesystem::path& root, revision_store&,
  const std::string& member, const revision& previous,
  const std::function<bool()>& permitted, const resource_attachment_paths& declared= {});

// AVD projection uses the native JSON v1 definition, removes disposable cache,
// and rebases the Vault directory to a portable relative path. It never follows
// or exports a descriptor selecting another Vault. Blobs remain byte-for-byte.
std::string resource_payload (const std::filesystem::path& root,
  const std::string& relative_path, std::string_view bytes,
  const resource_attachment_paths& declared= {});
// Pure validation for branch adoption BEFORE submitting an authority decision.
// Requires exact canonical wire bytes; does not install, execute or assign IDs.
void validate_resource_revision (const std::filesystem::path& root, const revision&,
  const resource_attachment_paths& declared= {});

// Desktop-only leases, implemented by the live adapter.
// Held from before asynchronous descriptor loading until the AVD pane dies.
// Publication shares the filesystem commit gate; no GUI widget is read from a
// worker. Multiple views can hold the same resource independently.
std::shared_ptr<void> hold_resource_view (const std::filesystem::path& absolute_path);
bool resource_view_open (const std::filesystem::path& absolute_path);

// Returns false when authorization or live-source ownership requires deferral.
// The gate is mandatory: for styles/sorters it must additionally enforce the
// receiver's execution policy; receiving code is not execution permission.
// File History protects original RAW bytes (including local AVD caches) before
// the durable journal intent and any mutation. Retry the same revision after
// interruption; a mismatching preimage/destination never gets overwritten.
// Creates missing physical parent directories through confined descriptors;
// it does not infer or modify logical namespace membership.
bool apply_closed_resource (const std::filesystem::path& root,
  revision_store&, history::document_history_store&,
  const std::string& revision_id,
  const std::function<bool(resource_kind, const std::string&)>& permitted,
  const resource_attachment_paths& declared= {});
// Shared entry point. Exclusive Vault ownership uses the caller's publication
// gate; desktop additionally requires both owner and active-view lookups.
// The overload above is a backward-compatible desktop-only wrapper.
bool apply_closed_resource (const std::filesystem::path& root,
  revision_store&, history::document_history_store&,
  const std::string& revision_id,
  const std::function<bool(resource_kind, const std::string&)>& permitted,
  const resource_attachment_paths& declared, const document_application_hooks& hooks);
}

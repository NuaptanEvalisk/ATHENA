/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>

namespace athena::hodarium {
struct document_path_change;
// Hooks are per owner, not process-global. exclusive_vault requires the caller
// to retain exclusive Vault ownership throughout the operation; it is not a
// substitute for obtaining that ownership. All calls are synchronous.
struct document_application_hooks {
  enum class ownership { live_catalog, exclusive_vault };
  ownership mode= ownership::live_catalog;
  // Must outlive every copied hook bundle and outstanding application. Desktop
  // uses the catalog gate; headless uses its exclusively owned Vault's gate.
  std::recursive_mutex* publication_mutex= nullptr;
  std::function<std::uint64_t(const std::string&)> published_owner;
  // Required for resource application in live_catalog mode. Uses the same
  // publication mutex as resource-view lease acquisition and release.
  std::function<bool(const std::filesystem::path&)> resource_view_open;
  // Required on path changes, including replay. Preserve artifact identity and
  // report durability failures; this is not an optional cache notification.
  std::function<bool(const document_path_change&, std::string&)> rename_artifacts;
};

// Desktop only. The returned hooks use the live buffer publication gate.
document_application_hooks live_document_application_hooks ();
}

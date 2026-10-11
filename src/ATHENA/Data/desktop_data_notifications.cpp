/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "desktop_data_notifications.hpp"
#include "hodarium_local_notifications.hpp"
#include "artifact_radioactive_links.hpp"
#include "namespace_ontology.hpp"
#include "vault.hpp"
#include "vault_node_location.hpp"

#include <mutex>

void athena_install_desktop_data_notifications () {
  static std::once_flag installed;
  std::call_once (installed, [] {
    athena::hodarium::set_local_change_observer (
      [] (const std::filesystem::path& root, bool structure_changed) {
        // Preserve global artifact-index invalidation even without an active
        // matching vault. The invalidator synchronizes its own shared caches;
        // actor-owned structural caches must not be touched from this callback.
        athena_artifact_radioactive_invalidate ();
        auto context= vault_capture_context ();
        if (context && context->root == root) {
          if (structure_changed)
            if (auto locator= athena::node_location::for_vault (context))
              locator->clear_cache ();
          athena_namespace_ontology_invalidate (structure_changed);
        }
      });
  });
}

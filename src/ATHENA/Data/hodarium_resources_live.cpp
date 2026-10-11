/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "hodarium_resources.hpp"
#include "buffer_name_catalog.hpp"

namespace athena::hodarium {
namespace {
std::map<std::string, std::size_t>& resource_views () {
  static auto* views= new std::map<std::string, std::size_t>;
  return *views;
}
}

std::shared_ptr<void> hold_resource_view (const std::filesystem::path& path) {
  std::lock_guard<std::recursive_mutex> lock (document_publication_mutex ());
  const auto canonical= std::filesystem::canonical (path).generic_string ();
  auto token= std::make_shared<std::string> (canonical);
  ++resource_views ()[canonical];
  return std::shared_ptr<void> (token.get (), [token] (void*) {
    std::lock_guard<std::recursive_mutex> release (document_publication_mutex ());
    auto it= resource_views ().find (*token);
    if (--it->second == 0) resource_views ().erase (it);
  });
}
bool resource_view_open (const std::filesystem::path& path) {
  std::lock_guard<std::recursive_mutex> lock (document_publication_mutex ());
  return resource_views ().count (path.lexically_normal ().generic_string ()) != 0;
}

bool apply_closed_resource (const std::filesystem::path& root,
    revision_store& journal, history::document_history_store& history,
    const std::string& revision_id,
    const std::function<bool(resource_kind, const std::string&)>& permitted,
    const resource_attachment_paths& declared) {
  document_application_hooks hooks;
  hooks.publication_mutex= &document_publication_mutex ();
  hooks.published_owner= published_buffer_actor_id;
  hooks.resource_view_open= resource_view_open;
  return apply_closed_resource (root, journal, history, revision_id, permitted, declared, hooks);
}
}

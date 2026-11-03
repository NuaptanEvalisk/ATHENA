// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "Editor/Commands/global_transformation_frontend.hpp"
#include "Interop/Adapters/interop_document_codec.hpp"
#include "native_interfaces.hpp"
#include "qt_utilities.hpp"
#include "convert.hpp"
#include "boot.hpp"
#include <QApplication>
#include <QMessageBox>
#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <set>

namespace {
using athena::interop::value;
using athena::interop::operation_result;
struct proposal {
  std::string id;
  vault_context_handle vault;
  value parameters;
  std::size_t bytes;
};
struct outcome {
  std::string vault;
  value result;
};
std::mutex proposals_mutex;
std::deque<proposal> proposals;
std::map<std::string, outcome> outcomes;
std::deque<std::string> completed;
std::size_t proposal_bytes= 0;
std::atomic<std::uint64_t> next_proposal {1};
bool reviewing= false; // Qt thread only

void review_next ();

void finish (const proposal& request, value result) {
  bool more;
  {
    std::lock_guard<std::mutex> lock (proposals_mutex);
    outcomes[request.id].result= std::move (result);
    proposal_bytes-= request.bytes;
    completed.push_back (request.id);
    while (completed.size () > 32) {
      outcomes.erase (completed.front ());
      completed.pop_front ();
    }
    more= !proposals.empty ();
  }
  reviewing= false;
  if (more) qt_post_to_main_thread (review_next);
}

void review_next () {
  if (reviewing) return;
  proposal request;
  {
    std::lock_guard<std::mutex> lock (proposals_mutex);
    if (proposals.empty ()) return;
    request= std::move (proposals.front ());
    proposals.pop_front ();
    outcomes[request.id].result= {{"status", "reviewing"}};
  }
  reviewing= true;
  try {
    if (!vault_context_is_current (request.vault))
      throw std::runtime_error ("The proposed transformation belongs to a closed Vault");
    std::map<std::string, const value*> replacements;
    std::set<std::string> visited;
    for (const auto& item: request.parameters.at ("documents"))
      replacements.emplace (item.at ("path").get<std::string> (), &item);
    auto transform= [&] (const std::string& relative, const tree& input,
                         tree& output, std::string& error) {
      auto found= replacements.find (relative);
      if (found == replacements.end ()) { output= input; return true; }
      visited.insert (relative);
      const auto& item= *found->second;
      const tree before= athena::interop::document_node_from_value_v3 (item.at ("before"));
      if (before != input) {
        error= "Document changed before transformation review: " + relative;
        return false;
      }
      output= athena::interop::document_node_from_value_v3 (item.at ("after"));
      return true;
    };
    auto validate= [&] (std::string& error) {
      if (!vault_context_is_current (request.vault)) {
        error= "Vault changed during transformation review";
        return false;
      }
      if (visited.size () != replacements.size ()) {
        error= "The proposal includes missing or excluded Vault documents";
        return false;
      }
      return true;
    };
    const auto name= request.parameters.at ("name").get<std::string> ();
    const tree result= athena_global_transformation_run (
      transform, utf8_to_cork (string (name.data (), name.size ())), validate);
    auto text= [] (tree node) {
      const string utf8= cork_to_utf8 (as_string (node));
      return std::string (utf8.data (), N(utf8));
    };
    value files= value::array ();
    for (int i= 0; i < N(result[5]); ++i) files.push_back (text (result[5][i]));
    finish (request, {{"status", text (result[0])}, {"message", text (result[3])},
                     {"backup", text (result[4])}, {"documents", std::move (files)}});
  }
  catch (const std::exception& error) {
    finish (request, {{"status", "error"}, {"message", error.what ()}});
    QMessageBox::warning (qApp->activeWindow (), "Global transformation",
                          QString::fromUtf8 (error.what ()));
  }
  catch (...) {
    finish (request, {{"status", "error"}, {"message", "Native transformation failed"}});
    QMessageBox::warning (qApp->activeWindow (), "Global transformation",
                          "Native transformation failed; the proposal was not completed.");
  }
}
} // namespace

namespace athena::interop {
operation_result propose_global_transformation (
    const vault_context_handle& vault, const value& parameters) {
  if (!vault_context_is_current (vault)) return {"STALE", "Vault has closed"};
  if (!qApp || headless_mode) return {"UNAVAILABLE", "Transformation review requires the interactive editor"};
  if (!parameters.contains ("name") || !parameters["name"].is_string () ||
      parameters["name"].get<std::string> ().empty () ||
      !parameters.contains ("documents") || !parameters["documents"].is_array () ||
      parameters["documents"].empty ())
    return {"INVALID_ARGUMENT", "Expected name and nonempty documents array"};
  std::set<std::string> paths;
  for (const auto& item: parameters["documents"]) {
    if (!item.is_object () || !item.contains ("path") || !item["path"].is_string () ||
        !item.contains ("before") || !item.contains ("after"))
      return {"INVALID_ARGUMENT", "Each document requires path, before and after"};
    const auto name= item["path"].get<std::string> ();
    const auto path= std::filesystem::u8path (name);
    if (name.empty () || path.is_absolute () || path.extension () != ".ath" ||
        path.lexically_normal ().generic_u8string () != name ||
        *path.begin () == ".." || !paths.insert (name).second)
      return {"INVALID_ARGUMENT", "Document paths must be unique normalized Vault-relative .ath paths"};
  }
  const auto bytes= parameters.dump ().size ();
  const auto id= std::to_string (next_proposal.fetch_add (1));
  {
    std::lock_guard<std::mutex> lock (proposals_mutex);
    if (proposals.size () >= 8 || bytes > 64 * 1024 * 1024 - proposal_bytes)
      return {"BUSY", "Transformation review queue is full"};
    proposal_bytes+= bytes;
    proposals.push_back ({id, vault, parameters, bytes});
    outcomes.emplace (id, outcome {vault->incarnation, {{"status", "queued"}}});
  }
  qt_post_to_main_thread (review_next);
  return {"OK", {{"id", id}, {"status", "queued"}}};
}

operation_result global_transformation_status (
    const vault_context_handle& vault, const std::string& id) {
  if (!vault_context_is_current (vault)) return {"STALE", "Vault has closed"};
  std::lock_guard<std::mutex> lock (proposals_mutex);
  auto found= outcomes.find (id);
  if (found == outcomes.end () || found->second.vault != vault->incarnation)
    return {"NOT_FOUND", "Unknown transformation proposal"};
  return {"OK", found->second.result};
}
} // namespace athena::interop

void run_global_transformation () {
  if (qt_defer_to_main_thread (run_global_transformation)) return;
  if (reviewing) return;
  bool available;
  {
    std::lock_guard<std::mutex> lock (proposals_mutex);
    available= !proposals.empty ();
  }
  if (available) review_next ();
  else QMessageBox::information (qApp->activeWindow (), "Global transformation",
    "No transformation is awaiting review. External tools can submit a Vault "
    "transformation through AUDMAP. Proposed changes are reviewed here before "
    "they are applied, with backups retained in the Vault.");
}

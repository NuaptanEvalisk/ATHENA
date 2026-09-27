/******************************************************************************
* MODULE     : vault_node_location.cpp
* DESCRIPTION: Actor-owned identity inventories and revision-checked native node reads
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "vault_node_location.hpp"
#include "node_reference_export.hpp"
#include "System/Boot/boot.hpp"
#include "buffer_actor.hpp"
#include "editor.hpp"
#include "buffer_name_catalog.hpp"
#include "file.hpp"
#include "Data/Convert/Xml/athena_document_xml.hpp"
#include <QCoreApplication>
#include <QThread>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <stdexcept>

namespace athena::node_location {
namespace {
std::string text (const string& s) { return {s.data (), std::size_t (N(s))}; }
struct endpoint {
  std::string url, file;
  std::uint64_t actor, view;
  bool operator == (const endpoint& other) const {
    return url == other.url && file == other.file && actor == other.actor && view == other.view;
  }
};
using endpoints= std::map<std::uint64_t, endpoint>;

void check_context (const vault_context_handle& vault) {
  if (!vault || !vault_context_is_current (vault))
    throw std::runtime_error ("Node location vault has closed or changed");
}
void require_background () {
  if (QCoreApplication::instance () &&
      QThread::currentThread () == QCoreApplication::instance ()->thread ())
    throw std::logic_error ("Node location waits are forbidden on the GUI thread");
  if (const auto* context= current_scheme_execution_context ())
    if (context->actor) throw std::logic_error ("Node location waits are background-only");
}

endpoints capture_endpoints (const vault_context_handle& vault) {
  check_context (vault);
  endpoints out;
  for (const auto& entry: published_buffer_metadata ()) {
    if (!entry.second.actor_id) continue;
    url name (entry.first.c_str ());
    std::string relative;
    if (is_rooted (name, "default") || is_rooted (name, "file")) {
      auto file= std::filesystem::weakly_canonical (
        std::filesystem::path (text (as_system_string (name))));
      auto within= file.lexically_relative (vault->root);
      if (within.empty () || within.is_absolute () || *within.begin () == ".." ||
          within.extension () != ".ath") continue;
      bool internal= false;
      for (const auto& part: within)
        if (part == ".athena" || part == ".backup" || part == ".git") internal= true;
      if (internal) continue;
      relative= within.generic_string ();
    }
    else if (!is_scratch (name)) continue;
    endpoint found {entry.first, relative, entry.second.actor_id, entry.second.source_view};
    if (!out.emplace (found.actor, found).second)
      throw std::runtime_error ("Actor has ambiguous published source names");
  }
  return out;
}

template<class T, class F>
T on_actor (const vault_context_handle& vault, const endpoint& source,
            const std::atomic<bool>& cancelled, F action) {
  require_background ();
  struct response {
    std::mutex lock;
    std::condition_variable ready;
    bool done= false;
    T value;
    std::string error;
  };
  auto answer= std::make_shared<response> ();
  auto abandoned= std::make_shared<std::atomic<bool>> (false);
  auto continuation= actor_continuation_registry::instance ().store (
    [vault, source, action, answer, abandoned] {
      if (*abandoned) return;
      T value;
      std::string error;
      try {
        check_context (vault);
        auto* owner= current_scheme_execution_context ()->actor;
        if (!owner || text (as_string (owner->current_buffer_url ())) != source.url)
          throw std::runtime_error ("Node source was renamed or replaced");
        value= action (*owner, source.view);
      }
      catch (const std::exception& e) { error= e.what (); }
      catch (const string& e) { error= text (e); }
      catch (...) { error= "Failed to capture actor-owned node source"; }
      {
        std::lock_guard<std::mutex> guard (answer->lock);
        answer->value= std::move (value);
        answer->error= std::move (error);
        answer->done= true;
      }
      answer->ready.notify_all ();
    });
  // A saturated actor mailbox must not trap the locator worker or vault close.
  const auto submitted= buffer_actor::try_submit_to (source.actor,
    actor_command_kind::run_native_continuation, source.view,
    ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER, continuation);
  if (!submitted) {
    actor_continuation_registry::instance ().discard (continuation);
    throw std::runtime_error ("Source actor is busy or closed; retry node resolution");
  }
  const auto deadline= std::chrono::steady_clock::now () + std::chrono::seconds (10);
  std::unique_lock<std::mutex> guard (answer->lock);
  while (!answer->done && !cancelled && std::chrono::steady_clock::now () < deadline)
    answer->ready.wait_for (guard, std::chrono::milliseconds (50));
  if (!answer->done || cancelled) {
    *abandoned= true;
    actor_continuation_registry::instance ().discard (continuation);
    throw std::runtime_error (cancelled ? "Node source query cancelled" : "Source actor query timed out");
  }
  if (!answer->error.empty ()) throw std::runtime_error (answer->error);
  return std::move (answer->value);
}

std::vector<live_source> capture_live (const vault_context_handle& vault,
                                      const std::atomic<bool>& cancelled) {
  const auto before= capture_endpoints (vault);
  std::vector<live_source> out;
  for (const auto& pair: before) {
    if (cancelled) throw std::runtime_error ("Node source inventory cancelled");
    const auto& source= pair.second;
    live_source next {source.file, source.actor, 0, {}, {}};
    try {
      auto captured= on_actor<std::pair<std::uint64_t, census>> (vault, source, cancelled,
        [] (buffer_actor& owner, std::uint64_t view) {
          return std::make_pair (current_scheme_execution_context ()->command_id,
                                 collect (owner.current_source (view)));
        });
      next.capture= captured.first;
      next.nodes= std::move (captured.second);
    }
    catch (const std::exception& e) { next.error= e.what (); }
    out.push_back (std::move (next));
  }
  if (before != capture_endpoints (vault))
    throw std::runtime_error ("Open source membership changed during node inventory; retry resolution");
  return out;
}
} // namespace

std::shared_ptr<service> for_vault (vault_context_handle vault) {
  check_context (vault);
  static std::mutex lock;
  static std::map<std::string, std::weak_ptr<service>> services;
  std::lock_guard<std::mutex> guard (lock);
  for (auto i= services.begin (); i != services.end (); )
    if (i->second.expired ()) i= services.erase (i); else ++i;
  auto& current= services[vault->incarnation];
  auto result= current.lock ();
  if (!result) {
    result= std::make_shared<service> (vault->root,
      [vault] (const std::atomic<bool>& stop) { return capture_live (vault, stop); },
      [vault] (const item& target, const std::atomic<bool>& stop) { return read_online (vault, target, stop); });
    current= result;
  }
  return result;
}

content_payload read_live (vault_context_handle vault, const item& target,
                       const std::atomic<bool>& cancelled) {
  require_background ();
  if (target.state != status::resolved || target.candidates.size () != 1 ||
      !target.candidates[0].actor)
    throw std::invalid_argument ("Target is not a uniquely resolved live node");
  const auto location= target.candidates[0];
  const auto sources= capture_endpoints (vault);
  const auto found= sources.find (location.actor);
  if (found == sources.end () || found->second.file != location.file)
    throw std::runtime_error ("Live node source has closed or moved");
  return on_actor<content_payload> (vault, found->second, cancelled,
    [target] (buffer_actor& owner, std::uint64_t view) {
      return capture_content (owner.current_source (view), target,
                              text (as_string (owner.current_buffer_url ())));
    });
}

content_payload read_online (vault_context_handle vault, const item& target,
                         const std::atomic<bool>& cancelled) {
  require_background ();
  check_context (vault);
  if (target.state != status::resolved || target.candidates.size () != 1)
    throw std::invalid_argument ("Target is not a uniquely resolved online node");
  if (target.candidates[0].actor) return read_live (std::move (vault), target, cancelled);
  auto check_saved= [&] {
    if (cancelled) throw std::runtime_error ("Node source read cancelled");
    for (const auto& pair: capture_endpoints (vault))
      if (pair.second.file == target.candidates[0].file)
        throw std::runtime_error ("Saved node is now actor-owned; retry node resolution");
  };
  check_saved ();
  auto source= read_disk_content (vault->root, target);
  check_saved ();
  return source;
}
} // namespace athena::node_location

namespace athena::node_reference {
prepared_snapshot prepare_headless_export (std::uint64_t actor, std::uint64_t view) {
  const auto* context= current_scheme_execution_context ();
  if (!is_headless () || (context && context->actor))
    throw std::logic_error ("Export preparation waits require the headless global owner");
  struct capture {
    std::vector<selection> seeds;
    std::string url, revision, error;
    bool done= false;
  };
  auto source= std::make_shared<capture> ();
  auto continuation= actor_continuation_registry::instance ().store ([source, view] {
    try {
      auto* owner= current_scheme_execution_context ()->actor;
      if (!owner) throw std::runtime_error ("Export source has no owner");
      const tree& doc= owner->current_source (view);
      source->seeds= export_selections (doc);
      source->revision= export_source_revision (doc);
      const string name= as_string (owner->current_buffer_url ());
      source->url.assign (name.data (), N(name));
      source->done= true;
    }
    catch (const std::exception& e) { source->error= e.what (); }
    catch (const string& e) { source->error.assign (e.data (), N(e)); }
    catch (...) { source->error= "Could not capture export source"; }
  });
  if (!buffer_actor::invoke_on (actor, actor_command_kind::run_native_continuation,
        view, ATHENA_NO_BLOB, ATHENA_NO_BLOB, nullptr,
        SCHEME_CAPABILITY_BUFFER, continuation)) {
    actor_continuation_registry::instance ().discard (continuation);
    throw std::runtime_error ("Export source actor is unavailable");
  }
  if (!source->done)
    throw std::runtime_error (source->error.empty () ? "Export source capture failed" : source->error);

  auto vault= vault_capture_context ();
  std::shared_ptr<node_location::service> locator;
  std::set<selection> requested (source->seeds.begin (), source->seeds.end ());
  const auto deadline= std::chrono::steady_clock::now () + std::chrono::minutes (5);
  for (unsigned round= 0; round < 32; ++round) {
    prepared_references result;
    if (!requested.empty ()) {
      if (!locator) locator= node_location::for_vault (vault);
      struct response {
        std::mutex lock;
        std::condition_variable ready;
        prepared_snapshot value;
      };
      auto answer= std::make_shared<response> ();
      export_preparation preparation (locator, {requested.begin (), requested.end ()},
        [answer] (prepared_snapshot value) {
          {
            std::lock_guard<std::mutex> guard (answer->lock);
            answer->value= std::move (value);
          }
          answer->ready.notify_all ();
        });
      std::unique_lock<std::mutex> guard (answer->lock);
      if (!answer->ready.wait_until (guard, deadline, [&] { return bool (answer->value); })) {
        guard.unlock ();
        preparation.cancel ();
        throw std::runtime_error ("Headless export reference preparation timed out");
      }
      result= *answer->value;
      guard.unlock ();
      if (!vault_context_is_current (vault))
        throw std::runtime_error ("Export vault changed during preparation");
      if (result.cancelled || !result.error.empty ())
        throw std::runtime_error (result.error.empty () ? "Export preparation cancelled" : result.error);
    }
    result.origin_actor= actor; result.origin_view= view;
    result.origin_url= source->url;
    result.origin_revision= source->revision;
    auto frozen= std::make_shared<const prepared_references> (std::move (result));
    struct probe_result {
      std::vector<selection> missing;
      std::string error;
      bool done= false;
    };
    auto probe= std::make_shared<probe_result> ();
    auto inspect= actor_continuation_registry::instance ().store ([frozen, probe] {
      try {
        const auto* context= current_scheme_execution_context ();
        if (!context || !context->editor) throw std::runtime_error ("Export view has closed");
        export_reference_scope references (frozen);
        context->editor->probe_print_references ();
        probe->missing= references.missing ();
        probe->done= true;
      }
      catch (const std::exception& e) { probe->error= e.what (); }
      catch (const string& e) { probe->error.assign (e.data (), N(e)); }
      catch (...) { probe->error= "Export reference layout failed"; }
    });
    if (!buffer_actor::invoke_on (actor, actor_command_kind::run_native_continuation,
          view, ATHENA_NO_BLOB, ATHENA_NO_BLOB, nullptr, SCHEME_CAPABILITY_BUFFER, inspect)) {
      actor_continuation_registry::instance ().discard (inspect);
      throw std::runtime_error ("Export source actor is unavailable");
    }
    if (!probe->done) throw std::runtime_error (probe->error);
    if (probe->missing.empty ()) return frozen;
    const auto before= requested.size ();
    requested.insert (probe->missing.begin (), probe->missing.end ());
    if (requested.size () == before || requested.size () > preparation_limits {}.selections)
      throw std::runtime_error ("Export reference discovery exceeded its budget or made no progress");
  }
  throw std::runtime_error ("Export reference discovery did not converge");
}

void verify_export_origin () {
  auto snapshot= current_export_references ();
  if (!snapshot || !snapshot->origin_actor) return;
  const auto* context= current_scheme_execution_context ();
  if (!context || !context->actor)
    throw std::logic_error ("Export source verification requires its actor");
  if (context->actor_id != snapshot->origin_actor) return;
  const string name= as_string (context->actor->current_buffer_url ());
  if (std::string (name.data (), N(name)) != snapshot->origin_url ||
      export_source_revision (context->actor->current_source (snapshot->origin_view)) !=
        snapshot->origin_revision)
    throw std::runtime_error ("Source changed while preparing export; retry the export");
}
} // namespace athena::node_reference

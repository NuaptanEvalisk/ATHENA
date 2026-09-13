/******************************************************************************
* MODULE     : resolution.cpp
* DESCRIPTION: Fixed-worker resolution scheduling, branch merging and tree pruning
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "resolution.hpp"

#include <boost/asio/post.hpp>
#include <boost/asio/thread_pool.hpp>
#include <algorithm>
#include <set>
#include <stdexcept>
#include <thread>
#ifdef __linux__
#include <pthread.h>
#endif

namespace athena::interop {

struct resolution_workers::impl {
  const std::size_t count;
  const std::size_t limit;
  std::atomic<std::size_t> queued {0};
  boost::asio::thread_pool pool;
  impl (std::size_t count, std::size_t limit): count (count), limit (limit),
    pool (count) {}
};

resolution_workers::resolution_workers (std::size_t count, std::size_t limit) {
  if (!count || !limit) throw std::invalid_argument ("Worker and queue limits must be positive");
  implementation = std::make_unique<impl> (count, limit);
}

resolution_workers::~resolution_workers () {
  // join drains admitted tasks, including continuations posted by running tasks.
  implementation->pool.join ();
}

std::size_t resolution_workers::size () const { return implementation->count; }

bool resolution_workers::post (std::function<void ()> task) {
  auto& p = *implementation;
  auto queued = p.queued.load (std::memory_order_relaxed);
  do {
    if (queued >= p.limit) return false;
  } while (!p.queued.compare_exchange_weak (queued, queued + 1,
                                          std::memory_order_relaxed));
  try {
    boost::asio::post (p.pool, [task = std::move (task), &p] {
      p.queued.fetch_sub (1, std::memory_order_relaxed);
#ifdef __linux__
      thread_local bool named = false;
      if (!named) { pthread_setname_np (pthread_self (), "ResolutionWork"); named = true; }
#endif
      task ();
    });
  }
  catch (...) { p.queued.fetch_sub (1, std::memory_order_relaxed); throw; }
  return true;
}

void resolution_output::publish (binding accessor, std::size_t offset,
                             continuation state) {
  branches.push_back ({std::move (accessor), {{offset, std::move (state)}}});
}

struct resolution_ticket::impl: std::enable_shared_from_this<impl> {
  resolution_workers& workers;
  const std::shared_ptr<const resolver_registry> registry;
  const selection selectors;
  const std::size_t node_limit;
  completion finished;
  std::atomic<bool> stopped {false};
  std::mutex mutex;
  std::size_t pending = 0;
  std::size_t dispatched = 0;
  std::vector<std::shared_ptr<const occurrence>> nodes;
  std::set<handle> endpoints;
  std::vector<truncation> truncated;
  std::string fault;
  bool cancelled = false;
  bool delivered = false;

  impl (resolution_workers& w, std::shared_ptr<const resolver_registry> r,
        selection s, completion f, std::size_t limit):
    workers (w), registry (std::move (r)), selectors (std::move (s)),
    node_limit (limit), finished (std::move (f)) {}

  void fail (std::string error) {
    if (fault.empty ()) fault = std::move (error);
    stopped.store (true, std::memory_order_release);
  }

  // Caller holds the ticket mutex. post never executes a task inline, so a
  // one-worker executor cannot re-enter the coordinator or wait for a child.
  void enqueue (std::shared_ptr<const occurrence> parent, std::size_t offset,
                continuation state) {
    if (offset > selectors.size ()) throw std::logic_error ("Resolver consumed past selection end");
    if (offset == selectors.size ()) {
      if (parent) endpoints.insert (parent->id);
      return;
    }
    if (++dispatched > node_limit * 8) throw std::runtime_error ("Resolution dispatch capacity exceeded");
    auto self = shared_from_this ();
    ++pending;
    try {
      if (workers.post ([self, parent = std::move (parent), offset,
                         state = std::move (state)] {
            self->run (parent, offset, state);
          })) return;
      fail ("Resolution worker queue capacity exceeded");
    }
    catch (const std::exception& e) { fail (e.what ()); }
    --pending;
  }

  void run (const std::shared_ptr<const occurrence>& parent, std::size_t offset,
            const continuation& state) {
    resolution_output output;
    std::string error;
    try {
      if (!stopped.load (std::memory_order_acquire)) {
        const resolution_request request {selectors, offset, parent, state, stopped};
        for (const auto& r: *registry) {
          if (stopped.load (std::memory_order_acquire)) break;
          resolution_output local;
          const auto outcome = r->resolve (request, local);
          if ((!local.branches.empty () || !local.redispatch.empty ()) && outcome != resolver_outcome::resolved)
            throw std::logic_error ("Non-resolving resolver emitted bindings");
          for (auto& branch: local.branches) output.branches.push_back (std::move (branch));
          for (auto& next: local.redispatch) output.redispatch.push_back (std::move (next));
          for (auto& reason: local.truncated) output.truncated.push_back (std::move (reason));
        }
      }
    }
    catch (const std::exception& e) { error = e.what (); }
    catch (...) { error = "Unhandled resolver failure"; }
    {
      std::lock_guard<std::mutex> lock (mutex);
      // Even a late failure must win over normal truncation in another branch.
      if (!error.empty ()) fail (std::move (error));
      if (!stopped.load (std::memory_order_acquire)) {
        try {
          for (auto& next: output.redispatch) {
            enqueue (parent, next.offset, std::move (next.state));
            if (stopped.load (std::memory_order_acquire)) break;
          }
          for (auto& reason: output.truncated) {
            if (std::none_of (truncated.begin (), truncated.end (),
                [&] (const truncation& t) { return t.reason == reason.reason && t.message == reason.message; }))
              truncated.push_back (std::move (reason));
          }
          for (auto& branch: output.branches) {
            if (!branch.accessor || branch.continuations.empty ())
              throw std::logic_error ("Resolver emitted an invalid binding");
            if (nodes.size () >= node_limit) throw std::runtime_error ("Resolution node capacity exceeded");
            auto node = std::make_shared<const occurrence> (
              occurrence {nodes.size () + 1, std::move (branch.accessor), parent});
            nodes.push_back (node);
            for (auto& next: branch.continuations) {
              enqueue (node, next.offset, std::move (next.state));
              if (stopped.load (std::memory_order_acquire)) break;
            }
            if (stopped.load (std::memory_order_acquire)) break;
          }
        }
        catch (const std::exception& e) { fail (e.what ()); }
      }
      --pending;
    }
    complete_if_idle ();
  }

  void complete_if_idle () {
    completion callback;
    resolution_result result;
    {
      std::lock_guard<std::mutex> lock (mutex);
      if (pending || delivered) return;
      delivered = true;
      callback = std::move (finished);
      if (cancelled) result.state = resolution_result::status::cancelled;
      else if (!fault.empty ()) {
        result.state = resolution_result::status::fault;
        result.error = std::move (fault);
      }
      else {
        // Preserve occurrences, not resource identities. A diamond has two
        // different handles and lineages even when both bind the same resource.
        std::set<handle> productive;
        for (handle end: endpoints) {
          for (auto n = nodes.at (end - 1); n; n = n->parent)
            if (!productive.insert (n->id).second) break;
        }
        for (const auto& n: nodes)
          if (productive.count (n->id)) result.tree.push_back (n);
        auto leaf_ids = productive;
        for (const auto& n: result.tree)
          if (n->parent) leaf_ids.erase (n->parent->id);
        result.leaves.assign (leaf_ids.begin (), leaf_ids.end ());
        result.truncated = std::move (truncated);
      }
      nodes.clear ();
      endpoints.clear ();
    }
    if (callback) callback (std::move (result));
  }
};

resolution_ticket::resolution_ticket (resolution_workers& workers,
    std::shared_ptr<const resolver_registry> registry, selection selectors,
    completion finished, std::size_t node_limit) {
  if (!registry || selectors.empty () || !finished || !node_limit)
    throw std::invalid_argument ("Invalid resolution ticket configuration");
  implementation = std::make_shared<impl> (workers, std::move (registry),
    std::move (selectors), std::move (finished), node_limit);
  {
    std::lock_guard<std::mutex> lock (implementation->mutex);
    implementation->enqueue ({}, 0, {});
  }
  implementation->complete_if_idle ();
}

void resolution_ticket::cancel () const {
  std::lock_guard<std::mutex> lock (implementation->mutex);
  if (implementation->delivered) return;
  implementation->cancelled = true;
  implementation->stopped.store (true, std::memory_order_release);
}
} // namespace athena::interop

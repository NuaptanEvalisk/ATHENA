/******************************************************************************
* MODULE     : traversal.hpp
* DESCRIPTION: Domain-neutral candidate matching and bounded cross-resolver traversal
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once

#include "resolution.hpp"
#include <chrono>

namespace athena::interop {
struct traversal_budget {
  const traversal_limits limits;
  const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now ();
  std::atomic<std::uint64_t> matches {0};
  explicit traversal_budget (traversal_limits limits): limits (std::move (limits)) {}
};

// Candidate continuations belong to one domain. Descent continuations can be
// restricted to that domain (??), or offered to every accepting resolver (???).
struct traversal final: continuation_state {
  enum class phase { candidate, descend };
  const std::shared_ptr<traversal_budget> budget;
  const std::uint64_t depth;
  const std::string domain;
  const phase step;
  const bool default_match;
  traversal (std::shared_ptr<traversal_budget> budget, std::uint64_t depth,
             std::string domain, phase step = phase::candidate, bool default_match = false):
    budget (std::move (budget)), depth (depth), domain (std::move (domain)),
    step (step), default_match (default_match) {}
};

bool traversal_stopped (const resolution_request&, const traversal_budget&, resolution_output&);
void publish_candidate (resolution_output&, binding, std::size_t offset,
                        const std::shared_ptr<traversal_budget>&, std::uint64_t depth,
                        const std::string& domain, bool default_match = false);
// Called only by the owner of the candidate's domain.
resolver_outcome visit_candidate (const resolution_request&, const traversal&,
                                  resolution_output&, bool descend = true);
} // namespace athena::interop

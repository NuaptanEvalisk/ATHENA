/******************************************************************************
* MODULE     : traversal.cpp
* DESCRIPTION: Shared predicate budgets and explicit traversal continuations
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "traversal.hpp"

namespace athena::interop {
bool traversal_stopped (const resolution_request& req, const traversal_budget& budget,
                        resolution_output& out) {
  if (req.stopped.load ()) return true;
  if (budget.limits.max_duration_ms && std::chrono::steady_clock::now () - budget.started >=
      std::chrono::milliseconds (*budget.limits.max_duration_ms)) {
    out.truncated.push_back ({truncation_kind::max_duration, "Maximum traversal duration reached"});
    return true;
  }
  if (budget.limits.max_matches && budget.matches.load () >= *budget.limits.max_matches) {
    out.truncated.push_back ({truncation_kind::max_matches, "Maximum traversal matches reached"});
    return true;
  }
  return false;
}

void publish_candidate (resolution_output& out, binding accessor, std::size_t offset,
    const std::shared_ptr<traversal_budget>& budget, std::uint64_t depth,
    const std::string& domain, bool default_match) {
  if (budget->limits.max_depth && depth > *budget->limits.max_depth) return;
  out.publish (std::move (accessor), offset,
    std::make_shared<traversal> (budget, depth, domain, traversal::phase::candidate, default_match));
}

resolver_outcome visit_candidate (const resolution_request& req, const traversal& state,
                                  resolution_output& out, bool descend) {
  if (traversal_stopped (req, *state.budget, out)) return resolver_outcome::miss;
  const auto& s = req.selectors.at (req.offset);
  const auto properties = req.basepoint->accessor->properties ();
  const bool matches = s.type == selector::kind::default_resource ? state.default_match :
    s.type == selector::kind::name ? properties.contains ("name") && properties.at ("name") == s.name :
    s.filter.matches (properties);
  if (matches) {
    const auto index = state.budget->matches.fetch_add (1);
    if (state.budget->limits.max_matches && index >= *state.budget->limits.max_matches) {
      out.truncated.push_back ({truncation_kind::max_matches, "Maximum traversal matches reached"});
      return resolver_outcome::miss;
    }
    out.redispatch.push_back ({req.offset + 1, {}});
  }
  if (descend && (s.type == selector::kind::recursive || s.type == selector::kind::scoped)) {
    if (!state.budget->limits.max_depth || state.depth < *state.budget->limits.max_depth)
      out.redispatch.push_back ({req.offset, std::make_shared<traversal> (state.budget,
        state.depth, s.type == selector::kind::recursive ? "" : state.domain,
        traversal::phase::descend)});
  }
  return resolver_outcome::resolved;
}
} // namespace athena::interop

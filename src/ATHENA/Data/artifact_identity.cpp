/******************************************************************************
* MODULE     : artifact_identity.cpp
* DESCRIPTION: Conservative cross-build artifact identity association
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "ATHENA/Data/artifact_identity.hpp"

#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/maximum_weighted_matching.hpp>

#include <algorithm>
#include <functional>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <utility>

namespace {

using Observation= AthenaArtifactIdentityObservation;
using Decision= AthenaArtifactIdentityDecision;

std::string identity_key (std::initializer_list<std::string> parts) {
  std::string key;
  for (const std::string& part: parts) {
    key += std::to_string (part.size ());
    key.push_back (':');
    key += part;
  }
  return key;
}

bool compatible (const Observation& old_value,
                 const Observation& new_value) {
  return old_value.origin == new_value.origin &&
         old_value.type == new_value.type;
}

using KeyFunction= std::function<std::string(const Observation&)>;
using ValidFunction= std::function<bool(const Observation&)>;

void unique_pass (
  const char* evidence,
  const std::vector<Observation>& old_values,
  const std::vector<Observation>& new_values,
  const KeyFunction& key_for, const ValidFunction& valid,
  std::vector<bool>& old_matched, std::vector<bool>& new_matched,
  std::vector<Decision>& decisions) {
  std::map<std::string,std::vector<int>> old_by_key;
  std::map<std::string,std::vector<int>> new_by_key;
  for (size_t i=0; i<old_values.size (); i++)
    if (valid (old_values[i])) old_by_key[key_for (old_values[i])].push_back ((int) i);
  for (size_t i=0; i<new_values.size (); i++)
    if (valid (new_values[i])) new_by_key[key_for (new_values[i])].push_back ((int) i);

  for (const auto& item: old_by_key) {
    auto found= new_by_key.find (item.first);
    if (item.second.size () != 1 || found == new_by_key.end () ||
        found->second.size () != 1)
      continue;
    int old_index= item.second.front ();
    int new_index= found->second.front ();
    if (old_matched[(size_t) old_index] || new_matched[(size_t) new_index] ||
        !compatible (old_values[(size_t) old_index],
                     new_values[(size_t) new_index]))
      continue;
    old_matched[(size_t) old_index]= true;
    new_matched[(size_t) new_index]= true;
    Decision& decision= decisions[(size_t) new_index];
    decision.kind= AthenaArtifactIdentityDecisionKind::Matched;
    decision.old_index= old_index;
    decision.evidence= evidence;
  }
}

void exact_duplicate_group_pass (
  const std::vector<Observation>& old_values,
  const std::vector<Observation>& new_values,
  std::vector<bool>& old_matched, std::vector<bool>& new_matched,
  std::vector<Decision>& decisions) {
  auto key_for= [] (const Observation& value) {
    return identity_key ({value.origin, value.type, value.anchor, value.focus,
                          value.host, value.before, value.after, value.display});
  };
  std::map<std::string,std::vector<int>> old_by_key;
  std::map<std::string,std::vector<int>> new_by_key;
  for (size_t i=0; i<old_values.size (); i++)
    if (!old_matched[i] && !old_values[i].focus.empty ())
      old_by_key[key_for (old_values[i])].push_back ((int) i);
  for (size_t i=0; i<new_values.size (); i++)
    if (!new_matched[i] && !new_values[i].focus.empty ())
      new_by_key[key_for (new_values[i])].push_back ((int) i);

  for (auto& item: old_by_key) {
    auto found= new_by_key.find (item.first);
    if (item.second.size () < 2 || found == new_by_key.end () ||
        item.second.size () != found->second.size ())
      continue;
    auto by_document_order= [] (int left, int right,
                                 const std::vector<Observation>& values) {
      if (values[(size_t) left].document_order !=
          values[(size_t) right].document_order)
        return values[(size_t) left].document_order <
               values[(size_t) right].document_order;
      return left < right;
    };
    std::sort (item.second.begin (), item.second.end (),
               [&] (int left, int right) {
                 return by_document_order (left, right, old_values);
               });
    std::sort (found->second.begin (), found->second.end (),
               [&] (int left, int right) {
                 return by_document_order (left, right, new_values);
               });
    for (size_t i=0; i<item.second.size (); i++) {
      int old_index= item.second[i];
      int new_index= found->second[i];
      old_matched[(size_t) old_index]= true;
      new_matched[(size_t) new_index]= true;
      Decision& decision= decisions[(size_t) new_index];
      decision.kind= AthenaArtifactIdentityDecisionKind::Matched;
      decision.old_index= old_index;
      decision.evidence= "exact-duplicate-group-order";
    }
  }
}

long long pair_score (const Observation& old_value,
                      const Observation& new_value) {
  if (!compatible (old_value, new_value)) return 0;
  long long score= 0;
  if (!old_value.focus.empty () && old_value.focus == new_value.focus)
    score += 100;
  if (!old_value.host.empty () && old_value.host == new_value.host)
    score += 80;
  if (!old_value.before.empty () && old_value.before == new_value.before)
    score += 25;
  if (!old_value.after.empty () && old_value.after == new_value.after)
    score += 25;
  if (!old_value.display.empty () && old_value.display == new_value.display)
    score += 10;
  return score;
}

bool has_context_evidence (const Observation& old_value,
                           const Observation& new_value) {
  return (!old_value.host.empty () && old_value.host == new_value.host) ||
         (!old_value.before.empty () && old_value.before == new_value.before) ||
         (!old_value.after.empty () && old_value.after == new_value.after);
}

struct CandidateEdge {
  int old_index= -1;
  int new_index= -1;
  long long score= 0;
};

struct MatchingSolution {
  long long weight= 0;
  std::vector<std::pair<int,int>> pairs;
};

MatchingSolution solve_component (
  const std::vector<int>& old_indices, const std::vector<int>& new_indices,
  const std::vector<CandidateEdge>& edges, int excluded_old= -1,
  int excluded_new= -1) {
  using Graph= boost::adjacency_list<
    boost::vecS, boost::vecS, boost::undirectedS, boost::no_property,
    boost::property<boost::edge_weight_t,long long>>;
  Graph graph (old_indices.size () + new_indices.size ());
  std::map<int,int> old_vertex;
  std::map<int,int> new_vertex;
  for (size_t i=0; i<old_indices.size (); i++)
    old_vertex[old_indices[i]]= (int) i;
  for (size_t i=0; i<new_indices.size (); i++)
    new_vertex[new_indices[i]]= (int) old_indices.size () + (int) i;
  for (const CandidateEdge& edge: edges) {
    if (edge.old_index == excluded_old && edge.new_index == excluded_new)
      continue;
    boost::add_edge ((size_t) old_vertex[edge.old_index],
                     (size_t) new_vertex[edge.new_index], edge.score, graph);
  }

  using Vertex= boost::graph_traits<Graph>::vertex_descriptor;
  std::vector<Vertex> mate (boost::num_vertices (graph));
  auto mate_map= boost::make_iterator_property_map (
    mate.begin (), boost::get (boost::vertex_index, graph));
  boost::maximum_weighted_matching (graph, mate_map);

  MatchingSolution solution;
  solution.weight= boost::matching_weight_sum (graph, mate_map);
  Vertex null_vertex= boost::graph_traits<Graph>::null_vertex ();
  for (size_t i=0; i<old_indices.size (); i++) {
    Vertex partner= mate[i];
    if (partner == null_vertex || partner < old_indices.size ()) continue;
    size_t new_local= (size_t) partner - old_indices.size ();
    if (new_local < new_indices.size ())
      solution.pairs.push_back ({old_indices[i], new_indices[new_local]});
  }
  return solution;
}

long long best_alternative_for_old (int old_index, int selected_new,
                                    const std::vector<CandidateEdge>& edges) {
  long long best= 0;
  for (const CandidateEdge& edge: edges)
    if (edge.old_index == old_index && edge.new_index != selected_new)
      best= std::max (best, edge.score);
  return best;
}

long long best_alternative_for_new (int new_index, int selected_old,
                                    const std::vector<CandidateEdge>& edges) {
  long long best= 0;
  for (const CandidateEdge& edge: edges)
    if (edge.new_index == new_index && edge.old_index != selected_old)
      best= std::max (best, edge.score);
  return best;
}

long long edge_score (int old_index, int new_index,
                      const std::vector<CandidateEdge>& edges) {
  for (const CandidateEdge& edge: edges)
    if (edge.old_index == old_index && edge.new_index == new_index)
      return edge.score;
  return 0;
}

void resolve_ambiguous_components (
  const std::vector<Observation>& old_values,
  const std::vector<Observation>& new_values,
  std::vector<bool>& old_matched, std::vector<bool>& new_matched,
  std::vector<Decision>& decisions) {
  std::vector<CandidateEdge> all_edges;
  std::map<int,std::vector<int>> old_to_edges;
  std::map<int,std::vector<int>> new_to_edges;
  for (size_t old_index=0; old_index<old_values.size (); old_index++) {
    if (old_matched[old_index]) continue;
    for (size_t new_index=0; new_index<new_values.size (); new_index++) {
      if (new_matched[new_index]) continue;
      long long score= pair_score (old_values[old_index], new_values[new_index]);
      // Focus equality is necessary but not sufficient after a collision.  In
      // particular, resolving one duplicate by a strong key must not cause a
      // deleted old duplicate to inherit an unrelated newly inserted one.
      if (score < 100 ||
          !has_context_evidence (old_values[old_index], new_values[new_index]))
        continue;
      int edge_index= (int) all_edges.size ();
      all_edges.push_back ({(int) old_index, (int) new_index, score});
      old_to_edges[(int) old_index].push_back (edge_index);
      new_to_edges[(int) new_index].push_back (edge_index);
    }
  }

  std::set<int> visited_old;
  std::set<int> visited_new;
  for (const auto& start: old_to_edges) {
    if (visited_old.count (start.first)) continue;
    std::vector<int> component_old;
    std::vector<int> component_new;
    std::vector<CandidateEdge> component_edges;
    std::queue<std::pair<bool,int>> pending;
    pending.push ({true, start.first});
    visited_old.insert (start.first);
    while (!pending.empty ()) {
      auto current= pending.front ();
      pending.pop ();
      if (current.first) {
        component_old.push_back (current.second);
        for (int edge_index: old_to_edges[current.second]) {
          int next= all_edges[(size_t) edge_index].new_index;
          if (visited_new.insert (next).second) pending.push ({false, next});
        }
      }
      else {
        component_new.push_back (current.second);
        for (int edge_index: new_to_edges[current.second]) {
          int next= all_edges[(size_t) edge_index].old_index;
          if (visited_old.insert (next).second) pending.push ({true, next});
        }
      }
    }
    std::set<int> old_set (component_old.begin (), component_old.end ());
    std::set<int> new_set (component_new.begin (), component_new.end ());
    for (const CandidateEdge& edge: all_edges)
      if (old_set.count (edge.old_index) && new_set.count (edge.new_index))
        component_edges.push_back (edge);

    // Large collision groups carry too little distinguishing evidence and are
    // deliberately rejected instead of paying cubic matching cost.
    if (component_old.size () + component_new.size () > 64) {
      for (int new_index: component_new) {
        decisions[(size_t) new_index].kind=
          AthenaArtifactIdentityDecisionKind::Ambiguous;
        decisions[(size_t) new_index].evidence= "collision-group-too-large";
      }
      continue;
    }

    MatchingSolution best=
      solve_component (component_old, component_new, component_edges);
    for (const auto& pair: best.pairs) {
      int old_index= pair.first;
      int new_index= pair.second;
      long long score= edge_score (old_index, new_index, component_edges);
      long long old_margin=
        score - best_alternative_for_old (old_index, new_index,
                                           component_edges);
      long long new_margin=
        score - best_alternative_for_new (new_index, old_index,
                                           component_edges);
      MatchingSolution without= solve_component (
        component_old, component_new, component_edges, old_index, new_index);
      long long global_delta= best.weight - without.weight;
      Decision& decision= decisions[(size_t) new_index];
      decision.score= score;
      decision.old_margin= old_margin;
      decision.new_margin= new_margin;
      decision.global_delta= global_delta;
      if (score >= 100 && old_margin >= 25 && new_margin >= 25 &&
          global_delta >= 25) {
        decision.kind= AthenaArtifactIdentityDecisionKind::Matched;
        decision.old_index= old_index;
        decision.evidence= "context-weighted-unique-assignment";
        old_matched[(size_t) old_index]= true;
        new_matched[(size_t) new_index]= true;
      }
      else {
        decision.kind= AthenaArtifactIdentityDecisionKind::Ambiguous;
        decision.old_index= old_index;
        decision.evidence= "assignment-insufficient-margin";
      }
    }
    for (int new_index: component_new)
      if (!new_matched[(size_t) new_index] &&
          decisions[(size_t) new_index].kind ==
            AthenaArtifactIdentityDecisionKind::New) {
        decisions[(size_t) new_index].kind=
          AthenaArtifactIdentityDecisionKind::Ambiguous;
        decisions[(size_t) new_index].evidence= "unresolved-collision";
      }
  }
}

} // namespace

AthenaArtifactIdentityResult
athena_artifact_associate_identities (
  const std::vector<AthenaArtifactIdentityObservation>& old_observations,
  const std::vector<AthenaArtifactIdentityObservation>& new_observations) {
  AthenaArtifactIdentityResult result;
  result.decisions.resize (new_observations.size ());
  std::vector<bool> old_matched (old_observations.size (), false);
  std::vector<bool> new_matched (new_observations.size (), false);

  unique_pass (
    "explicit-anchor", old_observations, new_observations,
    [] (const Observation& value) {
      return identity_key ({value.origin, value.type, value.anchor});
    },
    [] (const Observation& value) { return !value.anchor.empty (); },
    old_matched, new_matched, result.decisions);
  unique_pass (
    "exact-focus-host-neighbors", old_observations, new_observations,
    [] (const Observation& value) {
      return identity_key ({value.origin, value.type, value.focus, value.host,
                            value.before, value.after});
    },
    [] (const Observation& value) {
      return !value.focus.empty () && !value.host.empty ();
    }, old_matched, new_matched, result.decisions);
  unique_pass (
    "exact-focus-host", old_observations, new_observations,
    [] (const Observation& value) {
      return identity_key ({value.origin, value.type, value.focus, value.host});
    },
    [] (const Observation& value) {
      return !value.focus.empty () && !value.host.empty ();
    }, old_matched, new_matched, result.decisions);
  unique_pass (
    "exact-focus-neighbors", old_observations, new_observations,
    [] (const Observation& value) {
      return identity_key ({value.origin, value.type, value.focus,
                            value.before, value.after});
    },
    [] (const Observation& value) {
      return !value.focus.empty () &&
             (!value.before.empty () || !value.after.empty ());
    }, old_matched, new_matched, result.decisions);
  unique_pass (
    "unique-focus", old_observations, new_observations,
    [] (const Observation& value) {
      return identity_key ({value.origin, value.type, value.focus});
    },
    [] (const Observation& value) { return !value.focus.empty (); },
    old_matched, new_matched, result.decisions);

  // Multiple observations can be semantically and contextually identical.
  // When the complete multisets are unchanged, document order is the only
  // observable identity and gives a deterministic bijection.  Unequal groups
  // deliberately fall through to conservative ambiguity handling.
  exact_duplicate_group_pass (old_observations, new_observations,
                              old_matched, new_matched, result.decisions);

  resolve_ambiguous_components (old_observations, new_observations,
                                old_matched, new_matched, result.decisions);
  for (size_t i=0; i<old_matched.size (); i++)
    if (!old_matched[i]) result.deleted_old_indices.push_back ((int) i);
  return result;
}

const char*
athena_artifact_identity_decision_name (
  AthenaArtifactIdentityDecisionKind kind) {
  switch (kind) {
  case AthenaArtifactIdentityDecisionKind::Matched: return "matched";
  case AthenaArtifactIdentityDecisionKind::Ambiguous: return "ambiguous";
  case AthenaArtifactIdentityDecisionKind::New: return "new";
  }
  return "new";
}

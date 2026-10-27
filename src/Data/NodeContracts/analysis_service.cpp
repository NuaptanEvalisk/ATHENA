// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "analysis_service.hpp"
#include "ATHENA/Interop/resolution.hpp"
#include <immer/algorithm.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/strong_components.hpp>
#include <mutex>
#include <set>
#include <stdexcept>
#include <utility>
#include <deque>
#include <unordered_set>

namespace athena::contracts {

bool same_analysis_resolution (const analysis_resolution* a, const analysis_resolution* b) {
  if (a == b) return true;
  if (!a) return !b->binding && !b->value && !b->declaration;
  if (!b) return !a->binding && !a->value && !a->declaration;
  return a->binding == b->binding && a->value == b->value &&
    (a->declaration == b->declaration ||
     (a->declaration && b->declaration && *a->declaration == *b->declaration));
}

namespace {

interop::resolution_workers& workers () {
  static interop::resolution_workers executor (2, 32);
  return executor;
}

bool same_context (const analysis_context_ptr& a, const analysis_context_ptr& b) {
  return a && b && a->names == b->names && a->symbols == b->symbols;
}

analysis_contract_input detach_contract (const std::string& name, const node_contract& info) {
  analysis_contract_input result;
  result.name= name;
  result.layout= info.layout;
  result.type= info.type;
  result.variable_type= info.variable_type;
  result.with_like= info.with_like;
  result.type_origin= info.type_origin;
  result.layout_origin= info.layout_origin;
  result.variable_type_origin= info.variable_type_origin;
  result.with_origin= info.with_origin;
  for (int i=0; i<int(info.fields.size ()); ++i) {
    const auto& child= info.fields[i];
    const tree name= info.attribute ("name-" + std::to_string (i));
    result.fields.push_back ({child.type, child.accessible, child.type_origin,
      child.accessible_origin, child.environment_origin, child.environment,
      is_atomic (name) ? std::string (name->label.data (), N(name->label)) : std::string {}});
  }
  return result;
}

node_contract project_inference (const node_contract* declared,
                                 const analysis_contract_result& item) {
  const auto assign= [] (auto& value, auto& origin, const auto& next, auto source) {
    if (!accepts (origin, source)) return false;
    value= next;
    origin= source;
    return true;
  };
  node_contract value= declared ? *declared : node_contract {};
  if (assign (value.layout, value.layout_origin, item.layout, item.layout_origin) &&
      int(value.fields.size ()) != value.layout.fields ())
    value.fields.assign (value.layout.fields (), field_contract {});
  assign (value.type, value.type_origin, item.type, item.type_origin);
  assign (value.variable_type, value.variable_type_origin,
          item.variable_type, item.variable_type_origin);
  assign (value.with_like, value.with_origin, item.with_like, item.with_origin);
  for (int i=0; i<int(item.fields.size ()); ++i) {
    const auto& child= item.fields[i];
    if (i < int(value.fields.size ())) {
      auto& field= value.fields[i];
      assign (field.type, field.type_origin, child.type, child.type_origin);
      assign (field.accessible, field.accessible_origin, child.access, child.access_origin);
      assign (field.environment, field.environment_origin,
              child.environment, child.environment_origin);
      field.analysis= child.analysis;
    }
    if (!child.name.empty () && value.layout_origin != property_authority::builtin &&
        !value.attributes.count ("name-" + std::to_string (i)))
      value.attributes.insert_or_assign ("name-" + std::to_string (i),
        node::frozen_tree (tree (string (child.name.data (), child.name.size ()))));
  }
  return value;
}

macro_analysis_input difference (const analysis_context_ptr& before,
                                 const analysis_context_ptr& after,
                                 std::set<std::string>& changed) {
  macro_analysis_input delta;
  const analysis_symbols empty_symbols;
  const analysis_resolutions empty_names;
  const auto symbol= [&] (const auto& value) {
    delta.symbols.insert_or_assign (value.first, value.second);
  };
  immer::diff (before ? *before->symbols : empty_symbols, *after->symbols,
    immer::make_differ (symbol, [] (const auto&) {},
      [&] (const auto&, const auto& value) { symbol (value); }));
  const auto update= [&] (const std::string& name, const analysis_resolution* previous,
                         const std::shared_ptr<const analysis_resolution>& value) {
    changed.insert (name);
    if (!previous || previous->declaration != value->declaration) {
      if (value->declaration) delta.contracts.push_back (detach_contract (name, *value->declaration));
      else if (previous && previous->declaration) {
        analysis_contract_input empty;
        empty.name= name;
        empty.type= TYPE_REGULAR;
        empty.variable_type= VAR_MACRO;
        delta.contracts.push_back (std::move (empty));
      }
    }
    if (value->value) {
      if (!previous || !(previous->value == value->value))
        delta.changed.push_back ({name, *value->value});
    }
    else if (previous && previous->value) delta.removed.push_back (name);
  };
  const auto remove= [&] (const auto& entry) {
      const auto& [name, value]= entry;
      changed.insert (name);
      if (value->value) delta.removed.push_back (name);
      if (value->declaration) {
        analysis_contract_input empty;
        empty.name= name;
        empty.type= TYPE_REGULAR;
        empty.variable_type= VAR_MACRO;
        delta.contracts.push_back (std::move (empty));
      }
  };
  immer::diff (before ? *before->names : empty_names, *after->names,
    immer::make_differ (
      [&] (const auto& value) { update (value.first, nullptr, value.second); },
      remove,
      [&] (const auto& old, const auto& value) {
        update (value.first, old.second.get (), value.second);
      }));
  return delta;
}

analysis_dependency_index dependency_index (const analysis_facts& facts) {
  using graph= boost::adjacency_list<boost::vecS, boost::vecS, boost::directedS>;
  graph dependencies (facts.size ());
  std::map<std::string, std::size_t> vertices;
  std::size_t next= 0;
  for (const auto& [name, fact]: facts) vertices.emplace (name, next++);
  for (const auto& [name, fact]: facts)
    for (const auto& read: fact->dependencies) {
      const auto target= vertices.find (read);
      if (target != vertices.end ()) boost::add_edge (vertices.at (name), target->second, dependencies);
    }
  std::vector<int> groups (facts.size ());
  const auto count= boost::strong_components (dependencies,
    boost::make_iterator_property_map (groups.begin (), boost::get (boost::vertex_index, dependencies)));
  analysis_dependency_index result;
  result.components.resize (count);
  std::vector<std::set<std::string>> reads (count);
  std::vector<std::set<std::size_t>> prerequisites (count);
  for (const auto& [name, fact]: facts) {
    const auto group= groups[vertices.at (name)];
    result.components[group].members.push_back (name);
    reads[group].insert (name);
    for (const auto& read: fact->dependencies) {
      reads[group].insert (read);
      const auto target= vertices.find (read);
      if (target != vertices.end () && groups[target->second] != group)
        prerequisites[group].insert (groups[target->second]);
    }
  }
  for (int i=0; i<count; ++i) {
    for (const auto& name: reads[i]) result.readers[name].push_back (i);
    for (auto prerequisite: prerequisites[i])
      result.components[prerequisite].dependants.push_back (i);
  }
  return result;
}

struct detached_engine {
  macro_analysis_core core;
  analysis_context_ptr context;
  analysis_facts facts;
  contract_entries projections;

  analysis_proposal_ptr evaluate (analysis_context_ptr next, const std::atomic<bool>& cancelled) {
    std::set<std::string> changed;
    for (auto& result: core.apply (difference (context, next, changed), &cancelled)) {
      changed.insert (result.name);
      const auto source= next->names->find (result.name);
      if (!source || !(*source)->value) facts= std::move (facts).erase (result.name);
      else {
        const auto name= result.name;
        facts= std::move (facts).set (
          name, std::make_shared<const analysis_contract_result> (std::move (result)));
      }
    }
    for (const auto& name: changed) {
      if (cancelled.load (std::memory_order_relaxed))
        throw std::runtime_error ("Macro analysis cancelled");
      const auto label= as_tree_label (string (name.data (), name.size ()));
      const auto fact= facts.find (name);
      if (!fact) { projections= std::move (projections).erase (label); continue; }
      const auto source= next->names->find (name);
      auto value= project_inference ((*source)->declaration.get (), **fact);
      const auto previous= projections.find (label);
      if (previous && **previous == value) continue;
      projections= std::move (projections).set (label,
        std::make_shared<const node_contract> (std::move (value)));
    }
    context= std::move (next);
    const auto evidence= std::make_shared<const analysis_evidence> (
      analysis_evidence {context, dependency_index (facts)});
    inference_origins provenance;
    for (const auto& [label, value]: projections)
      provenance= std::move (provenance).set (label, evidence);
    inference_receipts receipts;
    if (!projections.empty ()) receipts= std::move (receipts).set (evidence, projections.size ());
    return std::make_shared<const analysis_proposal> (
      analysis_proposal {evidence, facts, projections, std::move (provenance), std::move (receipts), {}});
  }
};

} // namespace

std::vector<std::size_t> reject_analysis (
    const analysis_evidence& proposal, const analysis_resolutions& current) {
  std::vector<std::size_t> rejected;
  std::unordered_set<std::size_t> seen;
  const auto mark= [&] (std::size_t index) {
    if (seen.insert (index).second) rejected.push_back (index);
  };
  const auto changed= [&] (const std::string& name,
                           const analysis_resolution* before, const analysis_resolution* after) {
    const auto readers= proposal.dependencies.readers.find (name);
    if (readers == proposal.dependencies.readers.end () || same_analysis_resolution (before, after)) return;
    for (auto index: readers->second) mark (index);
  };
  immer::diff (*proposal.context->names, current, immer::make_differ (
    [&] (const auto& entry) { changed (entry.first, nullptr, entry.second.get ()); },
    [&] (const auto& entry) { changed (entry.first, entry.second.get (), nullptr); },
    [&] (const auto& before, const auto& after) {
      changed (after.first, before.second.get (), after.second.get ());
    }));
  for (std::size_t i=0; i<rejected.size (); ++i)
    for (auto dependant: proposal.dependencies.components[rejected[i]].dependants)
      mark (dependant);
  return rejected;
}

struct analysis_service::impl {
  struct scheduler {
    std::mutex mutex;
    std::deque<std::weak_ptr<impl>> waiting;
  };
  static scheduler& dispatch () {
    static scheduler queue;
    return queue;
  }
  std::atomic<bool> cancelled {false};
  bool busy= false, queued= false;
  analysis_context_ptr requested;
  std::function<void ()> notify;
  analysis_proposal_ptr completed;
  std::vector<analysis_proposal_ptr> retained;
  // Accessed by one admitted worker. Offline preparation has its own engine.
  std::unique_ptr<detached_engine> engine= std::make_unique<detached_engine> ();

  void retain (const analysis_proposal_ptr& result) {
    if (retained.size () == 8) retained.erase (retained.begin ());
    retained.push_back (result);
  }

  analysis_proposal_ptr evaluate (analysis_context_ptr context) {
    try { return engine->evaluate (context, cancelled); }
    catch (const std::exception& error) {
      engine= std::make_unique<detached_engine> ();
      return std::make_shared<const analysis_proposal> (
        analysis_proposal {std::make_shared<const analysis_evidence> (
          analysis_evidence {std::move (context), {}}), {}, {}, {}, {}, error.what ()});
    }
    catch (...) {
      engine= std::make_unique<detached_engine> ();
      return std::make_shared<const analysis_proposal> (
        analysis_proposal {std::make_shared<const analysis_evidence> (
          analysis_evidence {std::move (context), {}}), {}, {}, {}, {}, "Unhandled macro analysis failure"});
    }
  }

  static void enqueue (const std::shared_ptr<impl>& owner) {
    if (owner->busy || owner->queued || !owner->requested) return;
    owner->queued= true;
    dispatch ().waiting.push_back (owner);
  }

  // Caller holds the scheduler mutex. Waiting demand is one weak entry per
  // owner, not an unbounded list of captured edits; execution remains bounded.
  static void pump () {
    auto& waiting= dispatch ().waiting;
    while (!waiting.empty ()) {
      const auto owner= waiting.front ().lock ();
      if (!owner || owner->cancelled.load (std::memory_order_relaxed) || !owner->requested) {
        if (owner) owner->queued= false;
        waiting.pop_front ();
        continue;
      }
      const auto context= owner->requested;
      owner->busy= true;
      if (!workers ().post ([owner, context] {
            const auto result= owner->cancelled.load (std::memory_order_relaxed) ?
              nullptr : owner->evaluate (context);
            std::function<void ()> wake;
            {
              std::lock_guard<std::mutex> lock (dispatch ().mutex);
              owner->busy= false;
              if (!owner->cancelled.load (std::memory_order_relaxed)) {
                owner->completed= result;
                if (same_context (owner->requested, context)) owner->requested.reset ();
                owner->retain (result);
                wake= owner->notify;
                enqueue (owner);
              }
              pump ();
            }
            if (wake && !owner->cancelled.load (std::memory_order_relaxed)) wake ();
          })) {
        owner->busy= false;
        return;
      }
      owner->queued= false;
      waiting.pop_front ();
    }
  }
};

analysis_service::analysis_service (): state (std::make_shared<impl> ()) {}
analysis_service::~analysis_service () { state->cancelled.store (true, std::memory_order_relaxed); }

bool analysis_service::request (analysis_context_ptr context, std::function<void ()> notify) {
  const auto owner= state;
  std::lock_guard<std::mutex> lock (impl::dispatch ().mutex);
  owner->requested= context;
  owner->notify= std::move (notify);
  for (const auto& ready: owner->retained)
    if (same_context (ready->evidence->context, context)) {
      owner->completed= ready;
      owner->requested.reset ();
      return true;
    }
  impl::enqueue (owner);
  impl::pump ();
  return owner->busy;
}

analysis_proposal_ptr analysis_service::poll () {
  std::lock_guard<std::mutex> lock (impl::dispatch ().mutex);
  return std::exchange (state->completed, {});
}

bool analysis_service::pending () const {
  std::lock_guard<std::mutex> lock (impl::dispatch ().mutex);
  return state->busy || bool (state->requested);
}

analysis_proposal_ptr analysis_service::run (analysis_context_ptr context) {
  {
    std::lock_guard<std::mutex> lock (impl::dispatch ().mutex);
    for (const auto& ready: state->retained)
      if (same_context (ready->evidence->context, context)) return ready;
  }
  detached_engine offline;
  const auto result= offline.evaluate (std::move (context), state->cancelled);
  // Cold preparation and later interactive requests use the same immutable
  // result. Never borrow or wait for the engine currently used by a worker.
  std::lock_guard<std::mutex> lock (impl::dispatch ().mutex);
  state->retain (result);
  return result;
}

} // namespace athena::contracts

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

namespace athena::contracts {

bool same_analysis_receipt (const analysis_resolution* a, const analysis_resolution* b) {
  if (a == b) return true;
  if (!a) return !b->binding && !b->value && !b->declaration;
  if (!b) return !a->binding && !a->value && !a->declaration;
  return a->binding == b->binding && a->declaration == b->declaration &&
    bool (a->value) == bool (b->value) &&
    (!a->value || a->value->shares_storage (*b->value));
}

namespace {

void check_cancelled (const std::atomic<bool>& cancelled) {
  if (cancelled.load (std::memory_order_relaxed))
    throw std::runtime_error ("Macro analysis cancelled");
}

// Boost owns SCC traversal. Its component map is read/written for each vertex
// and edge, including while draining a large strongly connected component.
template<typename Map>
class cancellable_component_map:
  public boost::put_get_helper<typename boost::property_traits<Map>::reference,
                               cancellable_component_map<Map>> {
  Map values;
  const std::atomic<bool>& cancelled;
public:
  using key_type= typename boost::property_traits<Map>::key_type;
  using value_type= typename boost::property_traits<Map>::value_type;
  using reference= typename boost::property_traits<Map>::reference;
  using category= typename boost::property_traits<Map>::category;

  cancellable_component_map (Map values, const std::atomic<bool>& cancelled):
    values (values), cancelled (cancelled) {}
  reference operator[] (key_type key) const {
    check_cancelled (cancelled);
    return values[key];
  }
};

std::atomic<bool> executor_stopping {false};

struct analysis_executor {
  interop::resolution_workers pool {2, 32};
  ~analysis_executor () { executor_stopping.store (true, std::memory_order_relaxed); }
};

interop::resolution_workers& workers () {
  static analysis_executor executor;
  return executor.pool;
}

bool same_context (const analysis_context_ptr& a, const analysis_context_ptr& b) {
  return a && b && a->names == b->names && a->symbols == b->symbols &&
    a->inputs.same_snapshot (b->inputs) && a->scope_inputs == b->scope_inputs;
}

analysis_contract_input detach_contract (const std::string& name, const node_contract& info,
                                         const std::atomic<bool>& cancelled) {
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
    check_cancelled (cancelled);
    const auto& child= info.fields[i];
    const tree name= info.attribute ("name-" + std::to_string (i));
    result.fields.push_back ({child.type, child.accessible, child.type_origin,
      child.accessible_origin, child.environment_origin, child.environment,
      is_atomic (name) ? std::string (name->label.data (), N(name->label)) : std::string {}});
  }
  return result;
}

node_contract project_inference (const node_contract* declared,
                                 const analysis_contract_result& item,
                                 const std::atomic<bool>& cancelled) {
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
    check_cancelled (cancelled);
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
                                 std::set<std::string>& changed,
                                 const std::atomic<bool>& cancelled) {
  check_cancelled (cancelled);
  macro_analysis_input delta;
  const analysis_symbols empty_symbols;
  const analysis_resolutions empty_names;
  const auto symbol= [&] (const auto& value) {
    check_cancelled (cancelled);
    delta.symbols.insert_or_assign (value.first, value.second);
  };
  immer::diff (before ? *before->symbols : empty_symbols, *after->symbols,
    immer::make_differ (symbol, [] (const auto&) {},
      [&] (const auto&, const auto& value) { symbol (value); }));
  const auto update= [&] (const std::string& name, const analysis_resolution* previous,
                         const std::shared_ptr<const analysis_resolution>& value) {
    check_cancelled (cancelled);
    changed.insert (name);
    if (!previous || previous->declaration != value->declaration) {
      if (value->declaration)
        delta.contracts.push_back (detach_contract (name, *value->declaration, cancelled));
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
      check_cancelled (cancelled);
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

analysis_dependency_index dependency_index (const analysis_facts& facts,
                                             const std::atomic<bool>& cancelled) {
  using graph= boost::adjacency_list<boost::vecS, boost::vecS, boost::directedS>;
  graph dependencies (facts.size ());
  std::map<std::string, std::size_t> vertices;
  std::size_t next= 0;
  for (const auto& [name, fact]: facts) {
    check_cancelled (cancelled);
    vertices.emplace (name, next++);
  }
  for (const auto& [name, fact]: facts) {
    check_cancelled (cancelled);
    for (const auto& read: fact->dependencies) {
      check_cancelled (cancelled);
      const auto target= vertices.find (read);
      if (target != vertices.end ()) boost::add_edge (vertices.at (name), target->second, dependencies);
    }
  }
  std::vector<int> groups (facts.size ());
  const auto count= boost::strong_components (dependencies,
    cancellable_component_map (
      boost::make_iterator_property_map (groups.begin (), boost::get (boost::vertex_index, dependencies)),
      cancelled));
  analysis_dependency_index result;
  result.components.resize (count);
  std::vector<std::set<std::string>> reads (count);
  std::vector<std::set<std::size_t>> prerequisites (count);
  for (const auto& [name, fact]: facts) {
    check_cancelled (cancelled);
    const auto group= groups[vertices.at (name)];
    result.owners.emplace (as_tree_label (string (name.data (), name.size ())), group);
    reads[group].insert (name);
    for (const auto& read: fact->dependencies) {
      check_cancelled (cancelled);
      reads[group].insert (read);
      const auto target= vertices.find (read);
      if (target != vertices.end () && groups[target->second] != group)
        prerequisites[group].insert (groups[target->second]);
    }
  }
  for (int i=0; i<count; ++i) {
    check_cancelled (cancelled);
    result.components[i].reads.assign (reads[i].begin (), reads[i].end ());
    result.components[i].prerequisites.assign (prerequisites[i].begin (), prerequisites[i].end ());
  }
  return result;
}

struct detached_engine {
  macro_analysis_core core;
  analysis_context_ptr context;
  analysis_facts facts;
  contract_entries projections;
  struct batch {
    enum class phase { analysis, facts, projections, dependencies, provenance };
    phase current= phase::analysis;
    analysis_context_ptr next;
    std::set<std::string> changed;
    std::vector<analysis_contract_result> results;
    std::size_t offset= 0;
    std::set<std::string>::const_iterator projection;
    contract_entries::const_iterator origin;
    analysis_evidence_ptr evidence;
    inference_origins provenance;
  };
  std::unique_ptr<batch> active;

  analysis_proposal_ptr advance (analysis_context_ptr next,
      std::chrono::steady_clock::time_point deadline, const std::atomic<bool>& cancelled) {
    if (!active) {
      auto pending= std::make_unique<batch> ();
      pending->next= std::move (next);
      core.begin (difference (context, pending->next, pending->changed, cancelled));
      active= std::move (pending);
      if (std::chrono::steady_clock::now () >= deadline) return {};
    }
    auto& work= *active;
    using phase= batch::phase;
    do {
      check_cancelled (cancelled);
      switch (work.current) {
      case phase::analysis: {
        auto results= core.resume (deadline, &cancelled);
        if (!results) return {};
        work.results= std::move (*results);
        work.current= phase::facts;
        break;
      }
      case phase::facts: {
        if (work.offset == work.results.size ()) {
          work.projection= work.changed.begin ();
          work.current= phase::projections;
          break;
        }
        auto& result= work.results[work.offset++];
        work.changed.insert (result.name);
        const auto name= result.name;
        const auto source= work.next->names->find (name);
        if (!source || !(*source)->value) facts= std::move (facts).erase (name);
        else facts= std::move (facts).set (
            name, std::make_shared<const analysis_contract_result> (std::move (result)));
        break;
      }
      case phase::projections: {
        if (work.projection == work.changed.end ()) {
          work.current= phase::dependencies;
          break;
        }
        const auto& name= *work.projection++;
        const auto label= as_tree_label (string (name.data (), name.size ()));
        const auto fact= facts.find (name);
        if (!fact) { projections= std::move (projections).erase (label); break; }
        const auto source= work.next->names->find (name);
        auto value= project_inference ((*source)->declaration.get (), **fact, cancelled);
        const auto previous= projections.find (label);
        if (!previous || !(**previous == value))
          projections= std::move (projections).set (label,
            std::make_shared<const node_contract> (std::move (value)));
        break;
      }
      case phase::dependencies:
        work.evidence= std::make_shared<const analysis_evidence> (
          analysis_evidence {work.next, dependency_index (facts, cancelled)});
        work.origin= projections.begin ();
        work.current= phase::provenance;
        break;
      case phase::provenance:
        if (work.origin != projections.end ()) {
          work.provenance= std::move (work.provenance).set (work.origin->first, work.evidence);
          ++work.origin;
        }
        else {
          inference_receipts receipts;
          if (!projections.empty ())
            receipts= std::move (receipts).set (work.evidence, projections.size ());
          auto result= std::make_shared<const analysis_proposal> (analysis_proposal {
            work.evidence, facts, projections, std::move (work.provenance), std::move (receipts), {}});
          context= std::move (work.next);
          active.reset ();
          return result;
        }
        break;
      }
    } while (std::chrono::steady_clock::now () < deadline);
    return {};
  }

  analysis_proposal_ptr evaluate (analysis_context_ptr next, const std::atomic<bool>& cancelled) {
    return advance (std::move (next), std::chrono::steady_clock::time_point::max (), cancelled);
  }
};

} // namespace

struct analysis_service::impl {
  struct scheduler {
    std::mutex mutex;
    std::deque<std::shared_ptr<impl>> waiting;
    std::deque<std::shared_ptr<const void>> retired;
    bool reclaiming= false;
  };
  static scheduler& dispatch () {
    static scheduler queue;
    return queue;
  }
  std::atomic<bool> cancelled {false};
  bool busy= false, queued= false;
  analysis_context_ptr requested, running;
  std::function<void ()> notify;
  analysis_proposal_ptr completed;
  std::vector<analysis_proposal_ptr> retained;
  // Accessed by one admitted worker. Offline preparation has its own engine.
  std::unique_ptr<detached_engine> engine= std::make_unique<detached_engine> ();

  analysis_proposal_ptr retain (const analysis_proposal_ptr& result) {
    analysis_proposal_ptr retired;
    if (retained.size () == 8) {
      retired= std::move (retained.front ());
      retained.erase (retained.begin ());
    }
    retained.push_back (result);
    return retired;
  }

  analysis_proposal_ptr advance (analysis_context_ptr context) {
    try {
      return engine->advance (context,
        std::chrono::steady_clock::now () + std::chrono::milliseconds (4), cancelled);
    }
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
    if (owner->busy || owner->queued || (!owner->requested && !owner->running)) return;
    owner->queued= true;
    dispatch ().waiting.push_back (owner);
  }

  static void pump_releases () {
    auto& queue= dispatch ();
    if (!queue.retired.empty () && !queue.reclaiming) {
      queue.reclaiming= true;
      if (!workers ().post ([] {
            const auto deadline= std::chrono::steady_clock::now () + std::chrono::milliseconds (4);
            do {
              std::shared_ptr<const void> retired;
              {
                std::lock_guard<std::mutex> lock (dispatch ().mutex);
                auto& pending= dispatch ().retired;
                if (pending.empty ()) break;
                retired= std::move (pending.front ());
                pending.pop_front ();
              }
              // Destructors may cancel nested services and enqueue their state.
              retired.reset ();
            } while (std::chrono::steady_clock::now () < deadline);
            std::lock_guard<std::mutex> lock (dispatch ().mutex);
            dispatch ().reclaiming= false;
            pump ();
          })) queue.reclaiming= false;
    }
  }

  // Caller holds the scheduler mutex. Waiting demand is one owned entry per
  // owner, not an unbounded list of captured edits; execution remains bounded.
  static void pump () {
    auto& queue= dispatch ();
    pump_releases ();
    auto& waiting= queue.waiting;
    while (!waiting.empty ()) {
      const auto& owner= waiting.front ();
      if (owner->cancelled.load (std::memory_order_relaxed) ||
          (!owner->requested && !owner->running)) {
        owner->queued= false;
        queue.retired.push_back (std::move (waiting.front ()));
        waiting.pop_front ();
        continue;
      }
      const auto context= owner->running ? owner->running : owner->requested;
      owner->busy= true;
      if (!workers ().post ([owner, context] {
            const auto result= owner->cancelled.load (std::memory_order_relaxed) ?
              nullptr : owner->advance (context);
            std::function<void ()> wake;
            // Releasing a large detached graph must not hold the global
            // scheduler mutex that editor-side request/poll also acquire.
            analysis_proposal_ptr retired;
            {
              std::lock_guard<std::mutex> lock (dispatch ().mutex);
              owner->busy= false;
              if (!owner->cancelled.load (std::memory_order_relaxed)) {
                if (result) {
                  owner->running.reset ();
                  retired= owner->retain (result);
                  // Superseded work is useful to the cache/engine, not to the
                  // current editor. Only its exact requested context may wake
                  // the owner and become an admission candidate.
                  if (same_context (owner->requested, context)) {
                    owner->completed= result;
                    owner->requested.reset ();
                    wake= owner->notify;
                  }
                }
                // An unfinished slice rejoins the FIFO behind waiting owners.
                enqueue (owner);
              }
              pump ();
            }
            if (wake && !owner->cancelled.load (std::memory_order_relaxed)) wake ();
          })) {
        owner->busy= false;
        return;
      }
      owner->running= context;
      owner->queued= false;
      waiting.pop_front ();
    }
    pump_releases ();
  }
};

analysis_service::analysis_service (): state (std::make_shared<impl> ()) {}
analysis_service::~analysis_service () {
  cancel ();
  release_on_worker (std::move (state));
}

void analysis_service::cancel () {
  state->cancelled.store (true, std::memory_order_relaxed);
}

void analysis_service::release_on_worker (std::shared_ptr<const void> retired) {
  // Static owners can outlive the executor. Shutdown no longer has an editing
  // latency requirement and must not submit work to a destroyed thread pool.
  if (!retired || executor_stopping.load (std::memory_order_relaxed)) return;
  std::lock_guard<std::mutex> lock (impl::dispatch ().mutex);
  impl::dispatch ().retired.push_back (std::move (retired));
  impl::pump ();
}

bool analysis_service::request (analysis_context_ptr context, std::function<void ()> notify) {
  const auto owner= state;
  analysis_context_ptr retired_request;
  analysis_proposal_ptr retired_completion;
  std::function<void ()> retired_notify;
  bool admitted= false;
  {
    std::lock_guard<std::mutex> lock (impl::dispatch ().mutex);
    retired_request= std::exchange (owner->requested, context);
    retired_notify= std::exchange (owner->notify, std::move (notify));
    retired_completion= std::exchange (owner->completed, {});
    for (const auto& ready: owner->retained)
      if (same_context (ready->evidence->context, context)) {
        owner->completed= ready;
        owner->requested.reset ();
        admitted= true;
        break;
      }
    if (!admitted) {
      impl::enqueue (owner);
      impl::pump ();
      admitted= owner->busy;
    }
  }
  release_on_worker (std::move (retired_request));
  release_on_worker (std::move (retired_completion));
  return admitted;
}

analysis_proposal_ptr analysis_service::poll () {
  std::lock_guard<std::mutex> lock (impl::dispatch ().mutex);
  return std::exchange (state->completed, {});
}

bool analysis_service::pending () const {
  std::lock_guard<std::mutex> lock (impl::dispatch ().mutex);
  return bool (state->requested);
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
  analysis_proposal_ptr retired;
  {
    std::lock_guard<std::mutex> lock (impl::dispatch ().mutex);
    retired= state->retain (result);
  }
  return result;
}

} // namespace athena::contracts

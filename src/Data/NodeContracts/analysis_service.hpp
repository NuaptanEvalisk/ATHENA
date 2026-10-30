// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once

#include "macro_analysis_core.hpp"
#include "contract_scope.hpp"
#include "evaluation_inputs.hpp"
#include <functional>
#include <optional>
#include <set>

namespace athena::contracts {

// Resolution at one prepared context. Binding identity and expression equality
// are deliberately separate: a new assignment can change declaration attachment.
struct analysis_resolution {
  definition_binding binding;
  std::optional<node::frozen_tree> value;
  std::shared_ptr<const node_contract> declaration;
};
// Owner-side admission uses immutable input identity, never deep value equality.
// A new receipt may conservatively retire equal values; detached analysis decides
// whether their semantic results can be reused.
bool same_analysis_receipt (const analysis_resolution*, const analysis_resolution*);
using analysis_resolutions= immer::map<std::string, std::shared_ptr<const analysis_resolution>>;
using analysis_symbols= immer::map<tree_label, std::string>;

struct analysis_context {
  std::shared_ptr<const analysis_resolutions> names;
  std::shared_ptr<const analysis_symbols> symbols;
  evaluation_input_snapshot inputs;
  contract_input_identity scope_inputs;
};
using analysis_context_ptr= std::shared_ptr<const analysis_context>;
using analysis_facts= immer::map<std::string, std::shared_ptr<const analysis_contract_result>>;

// Immutable native inputs only. Live evaluator values are read later by their
// owner, after this plan has been matched against the current demand.
struct analysis_capture_request {
  contract_scope scope;
  evaluation_input_snapshot inputs;
  analysis_context_ptr baseline;
  std::optional<contract_scope> baseline_scope;
};
using analysis_capture_request_ptr= std::shared_ptr<const analysis_capture_request>;
struct analysis_capture_item { std::string name; bool value_changed; };
struct analysis_capture_plan {
  analysis_capture_request_ptr source;
  std::vector<analysis_capture_item> items;
  std::string error;
};
using analysis_capture_plan_ptr= std::shared_ptr<const analysis_capture_plan>;

struct analysis_component {
  std::vector<std::string> reads;
  std::vector<std::size_t> prerequisites;
};
struct analysis_dependency_index {
  std::vector<analysis_component> components;
  std::map<tree_label, std::size_t> owners;
};
struct analysis_evidence {
  analysis_context_ptr context;
  analysis_dependency_index dependencies;
};
struct analysis_proposal {
  analysis_evidence_ptr evidence;
  analysis_facts facts;
  contract_entries projections;
  inference_origins provenance;
  inference_receipts receipts;
  std::string error;
};
using analysis_proposal_ptr= std::shared_ptr<const analysis_proposal>;

// One detached engine per execution owner; one resumable batch in flight. The latest
// demand is coalesced while work runs. Only an exact-demand result is published
// or notifies the owner; superseded work is retained for later cache hits.
// Worker slices rejoin the FIFO without exposing partial results. A slice can
// overrun during one recursive definition analysis or dependency-index pass.
// poll() never waits. notify must carry only
// a detached wake-up/actor address, not an editor or evaluator pointer.
class analysis_service {
  struct impl;
  std::shared_ptr<impl> state;
public:
  analysis_service ();
  ~analysis_service ();
  analysis_service (const analysis_service&)= delete;
  analysis_service& operator= (const analysis_service&)= delete;
  void cancel ();
  // Transfer worker-safe state only, never an editor/evaluator or raw tree.
  // Cancellation is separate so closing an owner takes effect immediately.
  static void release_on_worker (std::shared_ptr<const void>);
  void request_plan (analysis_capture_request_ptr, std::function<void ()> notify);
  analysis_capture_plan_ptr poll_plan ();
  analysis_capture_plan_ptr plan_now (analysis_capture_request_ptr);
  bool request (analysis_context_ptr, std::function<void ()> notify);
  analysis_proposal_ptr poll ();
  // Unsatisfied current demand, not superseded work still finishing in flight.
  bool pending () const;
  // Explicit offline preparation uses an independent engine, never waits on
  // or borrows a running background engine.
  analysis_proposal_ptr run (analysis_context_ptr);
};

} // namespace athena::contracts

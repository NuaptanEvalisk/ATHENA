// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "macro_analysis.hpp"
#include "analysis_service.hpp"
#include "tm_ostream.hpp"
#include <chrono>
#include <stdexcept>
#include <utility>

namespace athena::contracts {
namespace {
std::string bytes (const string& value) { return std::string (value.data (), N(value)); }
string native (const std::string& value) { return string (value.data (), value.size ()); }
}

struct macro_analysis::impl {
  analysis_service service;
  std::shared_ptr<const analysis_resolutions> names= std::make_shared<analysis_resolutions> ();
  std::shared_ptr<const analysis_symbols> labels= std::make_shared<analysis_symbols> ();
  contract_scope last_publication;
  contract_scope last_visible_scope;
  evaluation_input_snapshot latest_inputs;
  analysis_context_ptr baseline;
  std::optional<contract_scope> baseline_scope;
  analysis_capture_request_ptr wanted;
  analysis_capture_plan_ptr plan;
  std::size_t offset= 0;
  std::unique_ptr<node::frozen_tree_builder> freezing;
  analysis_proposal_ptr last_accepted;
  bool initialized= false, capture_pending= false, capture_failed= false;
  std::string last_error;

  void symbol (tree_label label) {
    if (labels->count (label)) return;
    const auto name= bytes (as_string (label));
    labels= std::make_shared<const analysis_symbols> (labels->set (label, name));
  }

  void release_capture () {
    // Only the partial freezer retains live owner-side source handles.
    freezing.reset ();
    analysis_service::release_on_worker (std::exchange (plan, {}));
    analysis_service::release_on_worker (std::exchange (wanted, {}));
    offset= 0;
    capture_pending= capture_failed= false;
  }

  void restore_baseline () {
    analysis_service::release_on_worker (std::exchange (names,
      baseline ? baseline->names : std::make_shared<const analysis_resolutions> ()));
    analysis_service::release_on_worker (std::exchange (labels,
      baseline ? baseline->symbols : std::make_shared<const analysis_symbols> ()));
  }

  analysis_context_ptr capture (const contract_scope& scope,
      const evaluation_input_snapshot& inputs, hashmap<string,tree> values,
      const retained_analysis_value& retained, bool bounded,
      const std::function<void ()>& notify) {
    latest_inputs= inputs;
    if (!initialized) {
      if (!scope.inference_sources ().empty ()) {
        baseline= scope.inference_sources ().begin ()->first->context;
        if (baseline->scope_inputs == scope.input_identity ()) baseline_scope= scope;
        restore_baseline ();
      }
      initialized= true;
    }
    if (baseline && baseline->inputs.same_snapshot (inputs) &&
        baseline->scope_inputs == scope.input_identity ()) {
      if (wanted) {
        release_capture ();
        restore_baseline ();
      }
      return baseline;
    }
    if (!wanted || !wanted->inputs.same_snapshot (inputs) ||
        wanted->scope.input_identity () != scope.input_identity ()) {
      release_capture ();
      restore_baseline ();
      wanted= std::make_shared<const analysis_capture_request> (
        analysis_capture_request {scope, inputs, baseline, baseline_scope});
      capture_pending= true;
    }
    if (capture_failed) return {};
    if (!plan) {
      if (!bounded) plan= service.plan_now (wanted);
      else {
        auto ready= service.poll_plan ();
        if (ready && ready->source == wanted) plan= std::move (ready);
        else analysis_service::release_on_worker (std::move (ready));
        if (!plan) {
          service.request_plan (wanted, notify);
          return {};
        }
      }
      if (!plan->error.empty ()) {
        if (!bounded) throw std::runtime_error (plan->error);
        if (plan->error != last_error)
          std_warning << "Native macro input planning: " << native (plan->error) << LF;
        last_error= plan->error;
        capture_failed= true;
        capture_pending= false;
        return {};
      }
    }
    const auto deadline= std::chrono::steady_clock::now () + std::chrono::milliseconds (4);
    unsigned processed= 0;
    while (offset < plan->items.size ()) {
      const auto& item= plan->items[offset];
      const auto& name= item.name;
      const auto previous= names->find (name);
      const auto old= previous ? *previous : nullptr;
      const string key= native (name);
      analysis_resolution entry;
      if (values->contains (key)) {
        const tree value= values[key];
        const auto label= make_tree_label (key);
        symbol (label);
        entry.binding= scope.definition (label);
        if (entry.binding) {
          // The evaluator pairs this binding with the current value. Reuse its
          // frozen payload instead of re-comparing and copying the expression.
          entry.value= entry.binding->value->expression;
        }
        else if (old && !item.value_changed) entry.value= old->value;
        else {
          if (retained) entry.value= retained (key);
          if (!entry.value && bounded) {
            if (!freezing)
              freezing= std::make_unique<node::frozen_tree_builder> (value);
            if (!freezing->advance (128)) return {};
            entry.value= freezing->finish ();
          }
          else if (!entry.value) entry.value= node::frozen_tree (value);
        }
      }
      if (existing_tree_label (key)) {
        const auto label= as_tree_label (key);
        symbol (label);
        entry.binding= scope.definition (label);
        entry.declaration= scope.find_declaration (label);
      }
      // Semantic equality belongs to detached analysis, not to input capture.
      if (!same_analysis_receipt (old.get (), &entry))
        names= std::make_shared<const analysis_resolutions> (names->set (
          name, std::make_shared<const analysis_resolution> (std::move (entry))));
      ++offset;
      freezing.reset ();
      if (bounded && (++processed >= 128 || std::chrono::steady_clock::now () >= deadline)) break;
    }
    if (offset < plan->items.size ()) return {};
    baseline= std::make_shared<const analysis_context> (
      analysis_context {names, labels, inputs, scope.input_identity ()});
    if (baseline_scope)
      analysis_service::release_on_worker (std::make_shared<contract_scope> (std::move (*baseline_scope)));
    baseline_scope= scope;
    release_capture ();
    return baseline;
  }

  bool accept (const analysis_proposal_ptr& proposal, contract_scope& scope) {
    if (!proposal) return false;
    if (!proposal->error.empty ()) throw std::runtime_error (proposal->error);
    if (proposal == last_accepted && scope.same_state (last_publication)) return false;
    const auto before= scope;
    scope.replace_inference (proposal->projections, proposal->provenance, proposal->receipts);
    last_publication= scope;
    last_accepted= proposal;
    return !scope.same_state (before);
  }
};

macro_analysis::macro_analysis (): state (std::make_unique<impl> ()) {}
macro_analysis::~macro_analysis () {
  state->service.cancel ();
  // An unfinished freeze still holds owner-local source handles.
  state->freezing.reset ();
  analysis_service::release_on_worker (std::shared_ptr<impl> (state.release ()));
}
void macro_analysis::synchronize (contract_scope& scope,
    const evaluation_input_snapshot& inputs, const hashmap<string,tree>& values) {
  const auto context= state->capture (scope, inputs, values, {}, false, {});
  scope.admit_inference (inputs);
  state->accept (state->service.run (context), scope);
  state->last_visible_scope= scope;
}
bool macro_analysis::request (const contract_scope& scope,
    const evaluation_input_snapshot& inputs, const hashmap<string,tree>& values,
    std::function<void ()> notify, const retained_analysis_value& retained) {
  const auto context= state->capture (scope, inputs, values, retained, true, notify);
  if (!context) {
    // Re-enter through the owner's coalesced mailbox, never through a retained
    // evaluator callback. The next slice reads the then-current input receipts.
    if (state->capture_pending && state->plan && notify) notify ();
    return state->capture_pending;
  }
  return state->service.request (context, std::move (notify));
}
bool macro_analysis::publish_ready (contract_scope& scope) {
  scope.admit_inference (state->latest_inputs);
  const auto proposal= state->service.poll ();
  const bool current= !state->capture_pending && !state->capture_failed && proposal &&
    proposal->evidence->context->names == state->names &&
    proposal->evidence->context->symbols == state->labels &&
    proposal->evidence->context->inputs.same_snapshot (state->latest_inputs) &&
    proposal->evidence->context->scope_inputs == scope.input_identity ();
  if (current && proposal->error.empty ()) {
    state->last_error.clear ();
    state->accept (proposal, scope);
  }
  else if (current) {
    if (proposal->error != state->last_error)
      std_warning << "Native macro analysis: " << native (proposal->error) << LF;
    state->last_error= proposal->error;
  }
  const bool changed= !scope.same_state (state->last_visible_scope);
  state->last_visible_scope= scope;
  return changed;
}
bool macro_analysis::pending () const {
  return state->capture_pending || state->service.pending ();
}

} // namespace athena::contracts

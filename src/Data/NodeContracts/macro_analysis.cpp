// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "macro_analysis.hpp"
#include "analysis_service.hpp"
#include "tm_ostream.hpp"
#include <stdexcept>
#include <utility>

namespace athena::contracts {
namespace {
std::string bytes (const string& value) { return std::string (value.data (), N(value)); }
string native (const std::string& value) { return string (value.data (), value.size ()); }
}

struct macro_analysis::impl {
  analysis_service service;
  std::shared_ptr<analysis_resolutions> names= std::make_shared<analysis_resolutions> ();
  std::shared_ptr<analysis_symbols> labels= std::make_shared<analysis_symbols> ();
  contract_scope observed, last_publication;
  contract_scope last_pruned_scope;
  std::shared_ptr<const analysis_resolutions> last_pruned_inputs;
  evaluation_input_snapshot consumed_inputs;
  analysis_proposal_ptr last_accepted;
  bool initialized= false;
  std::string last_error;

  void symbol (tree_label label) {
    if (labels->count (label)) return;
    if (!labels.unique ()) labels= std::make_shared<analysis_symbols> (*labels);
    const auto name= bytes (as_string (label));
    *labels= std::move (*labels).set (label, name);
  }

  analysis_context_ptr capture (const contract_scope& scope,
      const evaluation_input_snapshot& inputs, hashmap<string,tree> values,
      const retained_analysis_value& retained= {}) {
    const auto changed_values= inputs.changed_since (consumed_inputs);
    auto pending= changed_values;
    const auto include= [&] (tree_label label) {
      symbol (label);
      pending.insert (bytes (as_string (label)));
    };
    if (!initialized) {
      for (auto label: scope.snapshot ().labels ()) include (label);
      for (auto label: scope.snapshot ().definition_labels ()) include (label);
    }
    else {
      for (auto label: scope.changed_declarations (observed)) pending.insert (bytes (as_string (label)));
      for (auto label: scope.changed_definitions (observed)) pending.insert (bytes (as_string (label)));
    }
    for (const auto& name: pending) {
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
        else if (old && !changed_values.count (name)) entry.value= old->value;
        else {
          if (retained) entry.value= retained (key);
          if (!entry.value) entry.value= node::frozen_tree (value);
        }
      }
      if (existing_tree_label (key)) {
        const auto label= as_tree_label (key);
        symbol (label);
        entry.binding= scope.definition (label);
        entry.declaration= scope.find_declaration (label);
      }
      if (!same_analysis_resolution (old.get (), &entry)) {
        if (!names.unique ()) names= std::make_shared<analysis_resolutions> (*names);
        *names= std::move (*names).set (
          name, std::make_shared<const analysis_resolution> (std::move (entry)));
      }
    }
    observed= scope;
    consumed_inputs= inputs;
    initialized= true;
    return std::make_shared<const analysis_context> (analysis_context {names, labels});
  }

  bool retire_stale (contract_scope& scope) {
    if (last_pruned_inputs == names && scope.same_state (last_pruned_scope)) return false;
    auto draft= scope;
    for (const auto& [evidence, count]: scope.inference_sources ())
      for (auto index: reject_analysis (*evidence, *names))
        for (const auto& name: evidence->dependencies.components[index].members) {
          const auto label= as_tree_label (native (name));
          if (draft.inference_evidence (label) == evidence) draft.clear_inference (label);
        }
    const bool changed= !draft.same_state (scope);
    scope= std::move (draft);
    observed= scope;
    last_pruned_scope= scope;
    last_pruned_inputs= names;
    return changed;
  }

  bool accept (const analysis_proposal_ptr& proposal, contract_scope& scope) {
    if (!proposal) return false;
    if (!proposal->error.empty ()) throw std::runtime_error (proposal->error);
    if (proposal == last_accepted && scope.same_state (last_publication)) return false;
    const auto before= scope;
    scope.replace_inference (proposal->projections, proposal->provenance, proposal->receipts);
    last_publication= scope;
    last_accepted= proposal;
    last_pruned_inputs= names;
    last_pruned_scope= scope;
    observed= scope;
    return !scope.same_state (before);
  }
};

macro_analysis::macro_analysis (): state (std::make_unique<impl> ()) {}
macro_analysis::~macro_analysis () {
  state->service.cancel ();
  analysis_service::release_on_worker (std::shared_ptr<impl> (state.release ()));
}
void macro_analysis::synchronize (contract_scope& scope,
    const evaluation_input_snapshot& inputs, const hashmap<string,tree>& values) {
  const auto context= state->capture (scope, inputs, values);
  state->accept (state->service.run (context), scope);
}
bool macro_analysis::request (const contract_scope& scope,
    const evaluation_input_snapshot& inputs, const hashmap<string,tree>& values,
    std::function<void ()> notify, const retained_analysis_value& retained) {
  return state->service.request (state->capture (scope, inputs, values, retained), std::move (notify));
}
bool macro_analysis::publish_ready (contract_scope& scope) {
  const auto proposal= state->service.poll ();
  const bool current= proposal && proposal->evidence->context->names == state->names &&
    proposal->evidence->context->symbols == state->labels;
  if (current && proposal->error.empty ()) {
    state->last_error.clear ();
    return state->accept (proposal, scope);
  }
  const bool retired= state->retire_stale (scope);
  if (!current) return retired;
  if (proposal->error != state->last_error)
    std_warning << "Native macro analysis: " << native (proposal->error) << LF;
  state->last_error= proposal->error;
  return retired;
}
bool macro_analysis::pending () const { return state->service.pending (); }

} // namespace athena::contracts

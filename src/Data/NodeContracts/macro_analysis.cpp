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
  std::shared_ptr<analysis_resolutions> names= std::make_shared<analysis_resolutions> ();
  std::shared_ptr<analysis_symbols> labels= std::make_shared<analysis_symbols> ();
  contract_scope observed, last_publication;
  contract_scope last_visible_scope;
  evaluation_input_snapshot consumed_inputs;
  std::set<std::string> pending_names, changed_values;
  std::string freezing_name;
  std::unique_ptr<node::frozen_tree_builder> freezing;
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
      const retained_analysis_value& retained, bool bounded) {
    if (!initialized && !scope.inference_sources ().empty ()) {
      // A restored style already has analyzed inputs. Starting a new owner
      // must not retire all those facts while it incrementally captures them.
      const auto& inherited= scope.inference_sources ().begin ()->first->context;
      names= std::make_shared<analysis_resolutions> (*inherited->names);
      labels= std::make_shared<analysis_symbols> (*inherited->symbols);
      consumed_inputs= inherited->inputs;
    }
    const auto changed= inputs.changed_since (consumed_inputs);
    if (freezing && changed.count (freezing_name)) freezing.reset ();
    changed_values.insert (changed.begin (), changed.end ());
    pending_names.insert (changed.begin (), changed.end ());
    const auto include= [&] (tree_label label) {
      const auto name= bytes (as_string (label));
      const auto previous= names->find (name);
      if (!previous || (*previous)->binding != scope.definition (label) ||
          (*previous)->declaration != scope.find_declaration (label))
        pending_names.insert (name);
    };
    if (!initialized) {
      for (const auto& [label, name]: *labels) include (label);
      for (auto label: scope.snapshot ().labels ()) include (label);
      for (auto label: scope.snapshot ().definition_labels ()) include (label);
    }
    else {
      for (auto label: scope.changed_declarations (observed)) include (label);
      for (auto label: scope.changed_definitions (observed)) include (label);
    }
    observed= scope;
    consumed_inputs= inputs;
    initialized= true;
    const auto deadline= std::chrono::steady_clock::now () + std::chrono::milliseconds (4);
    unsigned processed= 0;
    while (!pending_names.empty ()) {
      const auto name= freezing ? freezing_name : *pending_names.begin ();
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
          if (!entry.value && bounded) {
            if (!freezing) {
              freezing_name= name;
              freezing= std::make_unique<node::frozen_tree_builder> (value);
            }
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
      if (!same_analysis_receipt (old.get (), &entry)) {
        if (!names.unique ()) names= std::make_shared<analysis_resolutions> (*names);
        *names= std::move (*names).set (
          name, std::make_shared<const analysis_resolution> (std::move (entry)));
      }
      changed_values.erase (name);
      pending_names.erase (name);
      freezing.reset ();
      if (bounded && (++processed >= 128 || std::chrono::steady_clock::now () >= deadline)) break;
    }
    if (!pending_names.empty ()) return {};
    return std::make_shared<const analysis_context> (
      analysis_context {names, labels, inputs, scope.input_identity ()});
  }

  bool accept (const analysis_proposal_ptr& proposal, contract_scope& scope) {
    if (!proposal) return false;
    if (!proposal->error.empty ()) throw std::runtime_error (proposal->error);
    if (proposal == last_accepted && scope.same_state (last_publication)) return false;
    const auto before= scope;
    scope.replace_inference (proposal->projections, proposal->provenance, proposal->receipts);
    last_publication= scope;
    last_accepted= proposal;
    observed= scope;
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
  const auto context= state->capture (scope, inputs, values, {}, false);
  scope.admit_inference (inputs);
  state->accept (state->service.run (context), scope);
  state->last_visible_scope= scope;
}
bool macro_analysis::request (const contract_scope& scope,
    const evaluation_input_snapshot& inputs, const hashmap<string,tree>& values,
    std::function<void ()> notify, const retained_analysis_value& retained) {
  const auto context= state->capture (scope, inputs, values, retained, true);
  if (!context) {
    // Re-enter through the owner's coalesced mailbox, never through a retained
    // evaluator callback. The next slice reads the then-current input receipts.
    if (notify) notify ();
    return true;
  }
  return state->service.request (context, std::move (notify));
}
bool macro_analysis::publish_ready (contract_scope& scope) {
  scope.admit_inference (state->consumed_inputs);
  const auto proposal= state->service.poll ();
  const bool current= state->pending_names.empty () && proposal &&
    proposal->evidence->context->names == state->names &&
    proposal->evidence->context->symbols == state->labels &&
    proposal->evidence->context->inputs.same_snapshot (state->consumed_inputs) &&
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
  state->observed= scope;
  return changed;
}
bool macro_analysis::pending () const {
  return !state->pending_names.empty () || state->service.pending ();
}

} // namespace athena::contracts

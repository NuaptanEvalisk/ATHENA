// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "contract_scope.hpp"
#include "contract_declaration.hpp"
#include "analysis_service.hpp"
#include <immer/algorithm.hpp>
#include <algorithm>
#include <array>
#include <set>
#include <tuple>

namespace athena::contracts {
definition_signature signature_of (const tree& expression) {
  definition_signature result;
  if (is_func (expression, MACRO) && N(expression)>0)
    result.kind= definition_kind::macro;
  else if (is_func (expression, XMACRO, 2))
    result.kind= definition_kind::variadic_macro;
  else return result;
  for (int i=0; i+1<N(expression); ++i) {
    if (!is_atomic (expression[i])) return {};
    const auto& name= expression[i]->label;
    result.parameters.emplace_back (name.data (), N(name));
  }
  return result;
}

namespace {
const node_contract& unknown_contract () {
  static const node_contract value;
  return value;
}

bool same_inference_inputs (const std::optional<evaluation_input_snapshot>& a,
                           const std::optional<evaluation_input_snapshot>& b) {
  return a && b ? a->same_snapshot (*b) : !a && !b;
}

template<class Scope>
bool inference_admitted (const Scope& scope, tree_label label,
                         const analysis_evidence& evidence,
                         const std::optional<evaluation_input_snapshot>& inputs) {
  if (!inputs) return true;
  const auto& context= *evidence.context;
  if (inputs->same_snapshot (context.inputs) &&
      scope.input_identity () == context.scope_inputs) return true;
  const auto& dependencies= *evidence.dependencies;
  const auto owner= dependencies.owners.find (label);
  if (owner == dependencies.owners.end ()) return false;

  // Queries never drain a dependency closure. Large or changed old results
  // stay unavailable until the scheduled analysis publishes matching evidence.
  constexpr unsigned limit= 128;
  std::array<std::size_t, limit> pending, visited;
  unsigned count= 1, seen= 0, budget= limit;
  pending[0]= owner->second;
  while (count) {
    if (!budget) return false;
    --budget;
    const auto index= pending[--count];
    if (std::find (visited.begin (), visited.begin () + seen, index) != visited.begin () + seen)
      continue;
    visited[seen++]= index;
    const auto& component= dependencies.components[index];
    for (const auto& name: component.reads) {
      if (!budget) return false;
      --budget;
      if (!inputs->same_receipt (context.inputs, name)) return false;
      const auto stored= context.names->find (name);
      const auto* expected= stored ? stored->get () : nullptr;
      const string key (name.data (), name.size ());
      const bool known= existing_tree_label (key);
      const auto target= known ? as_tree_label (key) : UNKNOWN;
      if ((known ? scope.definition (target) : nullptr) !=
            (expected ? expected->binding : nullptr) ||
          (known ? scope.find_declaration (target) : nullptr) !=
            (expected ? expected->declaration : nullptr)) return false;
    }
    for (auto prerequisite: component.prerequisites) {
      if (!budget) return false;
      --budget;
      pending[count++]= prerequisite;
    }
  }
  return true;
}
}

bool field_contract::operator == (const field_contract& other) const {
  return std::tie (type, accessible, writability, block, environment, type_origin,
           accessible_origin, writability_origin, block_origin, environment_origin, analysis) ==
         std::tie (other.type, other.accessible, other.writability, other.block,
           other.environment, other.type_origin, other.accessible_origin,
           other.writability_origin, other.block_origin, other.environment_origin, other.analysis);
}

bool node_contract::operator == (const node_contract& other) const {
  return std::tie (layout, type, border, block, with_like, variable_type,
           type_origin, layout_origin, border_origin, block_origin, with_origin,
           variable_type_origin, fields, attributes) ==
         std::tie (other.layout, other.type, other.border, other.block,
           other.with_like, other.variable_type, other.type_origin,
           other.layout_origin, other.border_origin, other.block_origin,
           other.with_origin, other.variable_type_origin, other.fields, other.attributes);
}

tree node_contract::attribute (std::string_view name) const {
  const auto found= attributes.find (name);
  return found == attributes.end () ? tree ("") : tree (found->second);
}

definition_binding capture_definition (const string& name, const tree& value,
                                       definition_payload previous,
                                       package_inclusion_handle package) {
  if (!previous || !previous->expression.matches (value))
    previous= std::make_shared<const definition_value> (
      definition_value {node::frozen_tree (value), signature_of (value)});
  return std::make_shared<const evaluated_definition> (evaluated_definition {
    make_tree_label (name), std::string (name.data (), N(name)), std::move (previous),
    std::move (package)});
}

struct contract_snapshot::layer {
  contract_snapshot parent;
  contract_entries entries;
  contract_entries projections;
  inference_origins provenance;
  inference_receipts receipts;
  std::shared_ptr<const group_extensions> groups;
  definition_entries definitions;
  std::shared_ptr<declaration_source_state> sources;
  bool inherit_definitions;
  contract_input_identity input_revision;
  std::optional<evaluation_input_snapshot> inference_inputs;
};

contract_snapshot::contract_snapshot (std::shared_ptr<const layer> value):
  value (std::move (value)) {}

const node_contract& contract_snapshot::resolve (tree_label label) const {
  const auto entry= find (label);
  return entry ? *entry : unknown_contract ();
}

bool contract_snapshot::contains (tree_label label) const {
  return bool (find (label));
}

std::shared_ptr<const node_contract> contract_snapshot::find (tree_label label) const {
  const auto inferred= find_inference (label);
  return inferred ? inferred : find_declaration (label);
}

const node_contract& contract_snapshot::declared (tree_label label) const {
  const auto entry= find_declaration (label);
  return entry ? *entry : unknown_contract ();
}

std::shared_ptr<const node_contract> contract_snapshot::find_declaration (tree_label label) const {
  for (auto current= value.get (); current; current= current->parent.value.get ()) {
    const auto found= current->entries.find (label);
    if (found) return *found;
  }
  return {};
}

std::shared_ptr<const node_contract> contract_snapshot::find_inference (tree_label label) const {
  const auto found= value ? value->projections.find (label) : nullptr;
  if (!found) return {};
  return inference_admitted (*this, label, **value->provenance.find (label), value->inference_inputs) ?
    *found : nullptr;
}

contract_input_identity contract_snapshot::input_identity () const {
  return value ? value->input_revision : nullptr;
}

std::shared_ptr<const declaration_chain> contract_snapshot::declaration_history (tree_label label) const {
  if (!value) return {};
  const auto found= value->sources->targets.find (label);
  return found ? *found : nullptr;
}

definition_binding contract_snapshot::definition (tree_label label) const {
  for (auto current= value.get (); current; current= current->parent.value.get ()) {
    const auto found= current->definitions.find (label);
    if (found) return *found;
    if (!current->inherit_definitions) break;
  }
  return {};
}

std::vector<tree_label> contract_snapshot::definition_labels () const {
  std::set<tree_label> labels;
  for (auto current= value.get (); current; current= current->parent.value.get ()) {
    for (const auto& [label, entry]: current->definitions) labels.insert (label);
    if (!current->inherit_definitions) break;
  }
  return {labels.begin (), labels.end ()};
}

std::vector<tree_label> contract_snapshot::labels () const {
  std::set<tree_label> labels;
  if (value)
    for (const auto& [label, entry]: value->projections) labels.insert (label);
  for (auto current= value.get (); current; current= current->parent.value.get ()) {
    for (const auto& [label, entry]: current->entries) labels.insert (label);
  }
  return {labels.begin (), labels.end ()};
}

const group_extensions& contract_snapshot::groups () const {
  static const group_extensions empty;
  return value ? *value->groups : empty;
}

contract_scope::contract_scope ():
  extensions (std::make_shared<group_extensions> ()),
  sources (std::make_shared<declaration_source_state> ()) {}

contract_scope::contract_scope (contract_snapshot parent):
  parent (std::move (parent)),
  projections (this->parent.value ? this->parent.value->projections : contract_entries {}),
  provenance (this->parent.value ? this->parent.value->provenance : inference_origins {}),
  receipts (this->parent.value ? this->parent.value->receipts : inference_receipts {}),
  extensions (this->parent.value ? this->parent.value->groups :
              std::make_shared<const group_extensions> ()),
  sources (this->parent.value ? this->parent.value->sources :
           std::make_shared<declaration_source_state> ()),
  input_revision (this->parent.value ? this->parent.value->input_revision :
                  std::make_shared<const contract_input_revision> ()),
  inference_inputs (this->parent.value ? this->parent.value->inference_inputs :
                    std::optional<evaluation_input_snapshot> {}) {}

const node_contract& contract_scope::resolve (tree_label label) const {
  const auto entry= find (label);
  return entry ? *entry : unknown_contract ();
}

bool contract_scope::contains (tree_label label) const {
  return bool (find (label));
}

std::shared_ptr<const node_contract> contract_scope::find (tree_label label) const {
  const auto projection= find_inference (label);
  if (projection) return projection;
  const auto found= entries.find (label);
  if (found) return *found;
  return parent.find_declaration (label);
}

const node_contract& contract_scope::declared (tree_label label) const {
  const auto entry= find_declaration (label);
  return entry ? *entry : unknown_contract ();
}

std::shared_ptr<const node_contract> contract_scope::find_declaration (tree_label label) const {
  const auto found= entries.find (label);
  return found ? *found : parent.find_declaration (label);
}

std::shared_ptr<const node_contract> contract_scope::find_inference (tree_label label) const {
  const auto found= projections.find (label);
  if (!found) return {};
  return inference_admitted (*this, label, **provenance.find (label), inference_inputs) ?
    *found : nullptr;
}

analysis_evidence_ptr contract_scope::inference_evidence (tree_label label) const {
  const auto found= provenance.find (label);
  return found ? *found : nullptr;
}

std::shared_ptr<const declaration_chain> contract_scope::declaration_history (tree_label label) const {
  const auto found= sources->targets.find (label);
  return found ? *found : nullptr;
}

definition_binding contract_scope::definition (tree_label label) const {
  const auto found= definitions.find (label);
  return found ? *found : (inherit_definitions ? parent.definition (label) : nullptr);
}

bool contract_scope::bind (definition_binding value) {
  if (definition (value->target) == value) return false;
  const auto target= value->target;
  definitions= std::move (definitions).set (target, std::move (value));
  touch_inputs ();
  refresh_declarations (target, true);
  invalidate_inference (target);
  return true;
}

void contract_scope::forget_definition (tree_label label) {
  if (!definition (label)) return;
  if (inherit_definitions && parent.definition (label))
    definitions= std::move (definitions).set (label, nullptr);
  else definitions= std::move (definitions).erase (label);
  touch_inputs ();
  refresh_declarations (label, false);
  invalidate_inference (label);
}

void contract_scope::reset_definitions (definition_entries baseline) {
  definitions= std::move (baseline);
  inherit_definitions= false;
  touch_inputs ();
  const auto histories= sources;
  for (const auto& [label, history]: histories->targets)
    refresh_declarations (label, false);
  clear_inference ();
}

contract_snapshot contract_scope::snapshot () const {
  return contract_snapshot (std::make_shared<const contract_snapshot::layer> (
    contract_snapshot::layer {parent, entries, projections, provenance, receipts, extensions, definitions, sources,
                             inherit_definitions, input_revision, inference_inputs}));
}

bool contract_scope::same_state (const contract_scope& other) const {
  return parent.value == other.parent.value && entries.identity () == other.entries.identity () &&
         projections.identity () == other.projections.identity () &&
         provenance.identity () == other.provenance.identity () &&
         sources == other.sources &&
         extensions == other.extensions && definitions.identity () == other.definitions.identity () &&
         inherit_definitions == other.inherit_definitions &&
         input_revision == other.input_revision &&
         same_inference_inputs (inference_inputs, other.inference_inputs);
}

namespace {
template<class Map>
void changed_keys (const Map& before, const Map& after, std::set<tree_label>& result) {
  const auto record= [&] (const auto& value) { result.insert (value.first); };
  immer::diff (before, after, immer::make_differ (
    record, record, [&] (const auto&, const auto& value) { record (value); }));
}
}

std::vector<tree_label> contract_scope::changed_declarations (const contract_scope& other) const {
  if (parent.value == other.parent.value && entries.identity () == other.entries.identity ()) return {};
  std::set<tree_label> candidates;
  if (parent.value != other.parent.value) {
    for (auto label: parent.labels ()) candidates.insert (label);
    for (auto label: other.parent.labels ()) candidates.insert (label);
  }
  changed_keys (other.entries, entries, candidates);
  std::vector<tree_label> changed;
  for (auto label: candidates) {
    const auto a= find_declaration (label), b= other.find_declaration (label);
    if (a != b) changed.push_back (label);
  }
  return changed;
}

bool contract_scope::contracts_differ (const contract_scope& other) const {
  if ((!projections.empty () || !other.projections.empty ()) &&
      (!same_inference_inputs (inference_inputs, other.inference_inputs) ||
       input_revision != other.input_revision)) return true;
  if (parent.value == other.parent.value && entries.identity () == other.entries.identity () &&
      projections.identity () == other.projections.identity ()) return false;
  std::set<tree_label> candidates;
  if (parent.value != other.parent.value) {
    for (auto label: parent.labels ()) candidates.insert (label);
    for (auto label: other.parent.labels ()) candidates.insert (label);
  }
  changed_keys (other.entries, entries, candidates);
  changed_keys (other.projections, projections, candidates);
  for (auto label: candidates) {
    const auto left= find (label), right= other.find (label);
    const auto a= find_declaration (label), b= other.find_declaration (label);
    if ((left != right && (!left || !right || !(*left == *right))) ||
        (a != b && (!a || !b || !(*a == *b)))) return true;
  }
  return false;
}

std::vector<tree_label> contract_scope::changed_definitions (const contract_scope& other) const {
  if (parent.value == other.parent.value && definitions.identity () == other.definitions.identity () &&
      inherit_definitions == other.inherit_definitions) return {};
  std::set<tree_label> candidates;
  if (parent.value != other.parent.value || inherit_definitions != other.inherit_definitions) {
    for (auto label: parent.definition_labels ()) candidates.insert (label);
    for (auto label: other.parent.definition_labels ()) candidates.insert (label);
  }
  changed_keys (other.definitions, definitions, candidates);
  std::vector<tree_label> result;
  for (auto label: candidates)
    if (definition (label) != other.definition (label)) result.push_back (label);
  return result;
}

void contract_scope::replace (tree_label label, node_contract value) {
  if (declaration_history (label)) {
    auto draft= *this;
    draft.discard_declarations (label);
    draft.materialize_declaration (label,
      std::make_shared<const node_contract> (std::move (value)));
    *this= std::move (draft);
    return;
  }
  const auto previous= find_declaration (label);
  if (previous && *previous == value) return;
  materialize_declaration (label, std::make_shared<const node_contract> (std::move (value)));
}

void contract_scope::materialize_declaration (
    tree_label label, std::shared_ptr<const node_contract> value) {
  const auto previous= find_declaration (label);
  if (previous == value || (previous && value && *previous == *value)) return;
  if (value == parent.find_declaration (label)) entries= std::move (entries).erase (label);
  else entries= std::move (entries).set (label, std::move (value));
  touch_inputs ();
  invalidate_inference (label);
}

void contract_scope::invalidate_inference (tree_label label) {
  const auto existing= projections.find (label);
  if (!existing) return;
  const auto evidence= *provenance.find (label);
  const auto count= *receipts.find (evidence);
  if (count == 1) receipts= std::move (receipts).erase (evidence);
  else receipts= std::move (receipts).set (evidence, count-1);
  projections= std::move (projections).erase (label);
  provenance= std::move (provenance).erase (label);
}

void contract_scope::set_inference (tree_label label, std::shared_ptr<const node_contract> value,
                                     analysis_evidence_ptr evidence) {
  const auto existing= projections.find (label);
  if (existing && *existing == value && inference_evidence (label) == evidence) return;
  invalidate_inference (label);
  const auto previous= receipts.find (evidence);
  const auto count= previous ? *previous : 0;
  receipts= std::move (receipts).set (evidence, count+1);
  provenance= std::move (provenance).set (label, std::move (evidence));
  projections= std::move (projections).set (label, std::move (value));
}

void contract_scope::replace_inference (contract_entries values, inference_origins origins,
                                         inference_receipts sources) {
  projections= std::move (values);
  provenance= std::move (origins);
  receipts= std::move (sources);
}

void contract_scope::clear_inference () {
  projections= {};
  provenance= {};
  receipts= {};
}

std::vector<tree_label> contract_scope::restore_targets (
    const contract_scope& before, const std::vector<contract_target>& targets) {
  std::vector<tree_label> changed;
  for (const auto& target: targets) {
    const bool source_changed= declaration_history (target.label) != before.declaration_history (target.label);
    const auto original_definition= before.definition (target.label);
    if (definition (target.label) != original_definition) {
      if (original_definition == (inherit_definitions ? parent.definition (target.label) : nullptr))
        definitions= std::move (definitions).erase (target.label);
      else definitions= std::move (definitions).set (target.label, original_definition);
      touch_inputs ();
    }
    const auto effective= find (target.label);
    const auto original= before.find_declaration (target.label);
    const bool declaration_changed= find_declaration (target.label) != original;
    if (declaration_changed) {
      if (original == parent.find_declaration (target.label)) entries= std::move (entries).erase (target.label);
      else entries= std::move (entries).set (target.label, original);
      touch_inputs ();
    }
    const auto inferred= before.projections.find (target.label);
    if (inferred) set_inference (target.label, *inferred, before.inference_evidence (target.label));
    else invalidate_inference (target.label);
    if (source_changed || declaration_changed || find (target.label) != effective)
      changed.push_back (target.label);
  }
  restore_declarations (before, targets);
  return changed;
}

} // namespace athena::contracts

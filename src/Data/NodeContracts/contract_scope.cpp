// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "contract_scope.hpp"
#include "contract_declaration.hpp"
#include <algorithm>
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
}

bool field_contract::operator == (const field_contract& other) const {
  return std::tie (type, accessible, writability, block, environment, type_origin,
           accessible_origin, writability_origin, block_origin, environment_origin) ==
         std::tie (other.type, other.accessible, other.writability, other.block,
           other.environment, other.type_origin, other.accessible_origin,
           other.writability_origin, other.block_origin, other.environment_origin);
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
  std::shared_ptr<const contract_entries> entries;
  std::shared_ptr<const contract_entries> projections;
  std::shared_ptr<const group_extensions> groups;
  std::shared_ptr<const definition_entries> definitions;
  std::shared_ptr<declaration_source_state> sources;
  bool inherit_definitions;
  bool inherit_inference;
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
  bool inferred= true;
  for (auto current= value.get (); current; current= current->parent.value.get ()) {
    if (inferred) {
      const auto projection= current->projections->find (label);
      if (projection != current->projections->end () && projection->second)
        return projection->second;
      if (projection != current->projections->end ()) inferred= false;
    }
    const auto found= current->entries->find (label);
    if (found != current->entries->end ()) return found->second;
    inferred= inferred && current->inherit_inference;
  }
  return {};
}

const node_contract& contract_snapshot::declared (tree_label label) const {
  const auto entry= find_declaration (label);
  return entry ? *entry : unknown_contract ();
}

std::shared_ptr<const node_contract> contract_snapshot::find_declaration (tree_label label) const {
  for (auto current= value.get (); current; current= current->parent.value.get ()) {
    const auto found= current->entries->find (label);
    if (found != current->entries->end ()) return found->second;
  }
  return {};
}

std::shared_ptr<const node_contract> contract_snapshot::find_inference (tree_label label) const {
  for (auto current= value.get (); current; current= current->parent.value.get ()) {
    const auto found= current->projections->find (label);
    if (found != current->projections->end ()) return found->second;
    if (current->entries->count (label) || !current->inherit_inference) break;
  }
  return {};
}

std::shared_ptr<const declaration_chain> contract_snapshot::declaration_history (tree_label label) const {
  if (!value) return {};
  const auto found= value->sources->targets.find (label);
  return found == value->sources->targets.end () ? nullptr : found->second;
}

definition_binding contract_snapshot::definition (tree_label label) const {
  for (auto current= value.get (); current; current= current->parent.value.get ()) {
    const auto found= current->definitions->find (label);
    if (found != current->definitions->end ()) return found->second;
    if (!current->inherit_definitions) break;
  }
  return {};
}

std::vector<tree_label> contract_snapshot::labels () const {
  std::set<tree_label> labels;
  bool inferred= true;
  for (auto current= value.get (); current; current= current->parent.value.get ()) {
    for (const auto& [label, entry]: *current->entries) labels.insert (label);
    if (inferred)
      for (const auto& [label, entry]: *current->projections) labels.insert (label);
    inferred= inferred && current->inherit_inference;
  }
  return {labels.begin (), labels.end ()};
}

const group_extensions& contract_snapshot::groups () const {
  static const group_extensions empty;
  return value ? *value->groups : empty;
}

contract_scope::contract_scope ():
  entries (std::make_shared<contract_entries> ()),
  projections (std::make_shared<contract_entries> ()),
  extensions (std::make_shared<group_extensions> ()),
  definitions (std::make_shared<definition_entries> ()),
  sources (std::make_shared<declaration_source_state> ()) {}

contract_scope::contract_scope (contract_snapshot parent):
  parent (std::move (parent)), entries (std::make_shared<contract_entries> ()),
  projections (std::make_shared<contract_entries> ()),
  extensions (std::make_shared<group_extensions> (this->parent.groups ())),
  definitions (std::make_shared<definition_entries> ()),
  sources (this->parent.value ? this->parent.value->sources :
           std::make_shared<declaration_source_state> ()) {}

const node_contract& contract_scope::resolve (tree_label label) const {
  const auto entry= find (label);
  return entry ? *entry : unknown_contract ();
}

bool contract_scope::contains (tree_label label) const {
  return bool (find (label));
}

std::shared_ptr<const node_contract> contract_scope::find (tree_label label) const {
  const auto projection= projections->find (label);
  if (projection != projections->end () && projection->second) return projection->second;
  const auto found= entries->find (label);
  if (found != entries->end ()) return found->second;
  return inherit_inference && projection == projections->end () ?
    parent.find (label) : parent.find_declaration (label);
}

const node_contract& contract_scope::declared (tree_label label) const {
  const auto entry= find_declaration (label);
  return entry ? *entry : unknown_contract ();
}

std::shared_ptr<const node_contract> contract_scope::find_declaration (tree_label label) const {
  const auto found= entries->find (label);
  return found == entries->end () ? parent.find_declaration (label) : found->second;
}

std::shared_ptr<const node_contract> contract_scope::find_inference (tree_label label) const {
  const auto found= projections->find (label);
  if (found != projections->end ()) return found->second;
  return inherit_inference && !entries->count (label) ? parent.find_inference (label) : nullptr;
}

std::shared_ptr<const declaration_chain> contract_scope::declaration_history (tree_label label) const {
  const auto found= sources->targets.find (label);
  return found == sources->targets.end () ? nullptr : found->second;
}

definition_binding contract_scope::definition (tree_label label) const {
  const auto found= definitions->find (label);
  return found == definitions->end () ?
    (inherit_definitions ? parent.definition (label) : nullptr) : found->second;
}

bool contract_scope::bind (definition_binding value) {
  if (definition (value->target) == value) return false;
  if (!definitions.unique ()) definitions= std::make_shared<definition_entries> (*definitions);
  const auto target= value->target;
  definitions->insert_or_assign (target, std::move (value));
  refresh_declarations (target, true);
  invalidate_inference (target);
  return true;
}

void contract_scope::forget_definition (tree_label label) {
  if (!definition (label)) return;
  if (!definitions.unique ()) definitions= std::make_shared<definition_entries> (*definitions);
  if (inherit_definitions && parent.definition (label)) definitions->insert_or_assign (label, nullptr);
  else definitions->erase (label);
  refresh_declarations (label, false);
  invalidate_inference (label);
}

void contract_scope::reset_definitions (definition_entries baseline) {
  definitions= std::make_shared<definition_entries> (std::move (baseline));
  inherit_definitions= false;
  const auto histories= sources;
  for (const auto& [label, history]: histories->targets)
    refresh_declarations (label, false);
  clear_inference ();
}

contract_snapshot contract_scope::snapshot () const {
  return contract_snapshot (std::make_shared<const contract_snapshot::layer> (
    contract_snapshot::layer {parent, entries, projections, extensions, definitions, sources,
                             inherit_definitions, inherit_inference}));
}

bool contract_scope::same_state (const contract_scope& other) const {
  return parent.value == other.parent.value && entries == other.entries &&
         projections == other.projections && inherit_inference == other.inherit_inference &&
         sources == other.sources &&
         extensions == other.extensions && definitions == other.definitions &&
         inherit_definitions == other.inherit_definitions;
}

std::vector<tree_label> contract_scope::changed_contracts (const contract_scope& other) const {
  if (parent.value == other.parent.value && entries == other.entries &&
      projections == other.projections && inherit_inference == other.inherit_inference) return {};
  std::set<tree_label> candidates;
  if (parent.value != other.parent.value || inherit_inference != other.inherit_inference) {
    for (auto label: parent.labels ()) candidates.insert (label);
    for (auto label: other.parent.labels ()) candidates.insert (label);
  }
  for (const auto& [label, value]: *entries) candidates.insert (label);
  for (const auto& [label, value]: *other.entries) candidates.insert (label);
  for (const auto& [label, value]: *projections) candidates.insert (label);
  for (const auto& [label, value]: *other.projections) candidates.insert (label);
  std::vector<tree_label> changed;
  for (auto label: candidates) {
    const auto left= find (label), right= other.find (label);
    const auto a= find_declaration (label), b= other.find_declaration (label);
    if ((left != right && (!left || !right || !(*left == *right))) ||
        (a != b && (!a || !b || !(*a == *b)))) changed.push_back (label);
  }
  return changed;
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
  if (!entries.unique ()) entries= std::make_shared<contract_entries> (*entries);
  if (value == parent.find_declaration (label)) entries->erase (label);
  else entries->insert_or_assign (label, std::move (value));
  invalidate_inference (label);
}

void contract_scope::invalidate_inference (tree_label label) {
  const bool inherited= inherit_inference && bool (parent.find_inference (label));
  const auto existing= projections->find (label);
  if (inherited ? existing != projections->end () && !existing->second :
                  existing == projections->end ()) return;
  if (!projections.unique ()) projections= std::make_shared<contract_entries> (*projections);
  if (inherited) projections->insert_or_assign (label, nullptr);
  else projections->erase (label);
}

void contract_scope::publish_inference (tree_label label, node_contract value) {
  const auto previous= find (label);
  if (previous && *previous == value) return;
  auto entry= std::make_shared<const node_contract> (std::move (value));
  if (!projections.unique ()) projections= std::make_shared<contract_entries> (*projections);
  projections->insert_or_assign (label, std::move (entry));
}

void contract_scope::clear_inference () {
  projections= std::make_shared<contract_entries> ();
  inherit_inference= false;
}

std::vector<tree_label> contract_scope::restore_targets (
    const contract_scope& before, const std::vector<contract_target>& targets) {
  std::vector<tree_label> changed;
  for (const auto& target: targets) {
    const bool source_changed= declaration_history (target.label) != before.declaration_history (target.label);
    const auto original_definition= before.definition (target.label);
    if (definition (target.label) != original_definition) {
      if (!definitions.unique ()) definitions= std::make_shared<definition_entries> (*definitions);
      if (original_definition == (inherit_definitions ? parent.definition (target.label) : nullptr))
        definitions->erase (target.label);
      else definitions->insert_or_assign (target.label, original_definition);
    }
    const auto effective= find (target.label);
    const auto original= before.find_declaration (target.label);
    const bool declaration_changed= find_declaration (target.label) != original;
    if (declaration_changed) {
      if (!entries.unique ()) entries= std::make_shared<contract_entries> (*entries);
      if (original == parent.find_declaration (target.label)) entries->erase (target.label);
      else entries->insert_or_assign (target.label, original);
    }
    const auto inferred= before.find_inference (target.label);
    if (find_inference (target.label) != inferred) {
      const auto inherited= inherit_inference && !entries->count (target.label) ?
        parent.find_inference (target.label) : nullptr;
      if (!projections.unique ()) projections= std::make_shared<contract_entries> (*projections);
      if (inferred == inherited) projections->erase (target.label);
      else projections->insert_or_assign (target.label, inferred);
    }
    if (source_changed || declaration_changed || find (target.label) != effective)
      changed.push_back (target.label);
  }
  restore_declarations (before, targets);
  return changed;
}

} // namespace athena::contracts

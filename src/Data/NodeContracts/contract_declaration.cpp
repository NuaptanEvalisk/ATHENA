// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "contract_declaration.hpp"
#include "node_group_scope.hpp"
#include "native_fields.hpp"
#include "analyze.hpp"
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <algorithm>

namespace athena::contracts {
namespace {
std::string bytes (const string& value) { return {value.data (), std::size_t (N(value))}; }

[[noreturn]] void invalid (const std::string& name, const std::string& property,
                          const char* reason) {
  throw std::invalid_argument ("Node contract '" + name + "', " + property + ": " + reason);
}

int integer (const tree& value, const std::string& name, const std::string& property) {
  if (!is_int (value) || as_int (value) < 0)
    invalid (name, property, "expected a nonnegative integer");
  return as_int (value);
}

std::string atom (const tree& value, const std::string& name, const std::string& property) {
  if (!is_atomic (value)) invalid (name, property, "expected text");
  return bytes (value->label);
}

template<class T> void declare (T& value, property_authority& origin, const T& next) {
  if (!accepts (origin, property_authority::declared)) return;
  value= next;
  origin= property_authority::declared;
}

bool field_property (scalar_property property) {
  return property == scalar_property::field_type ||
    property == scalar_property::accessibility || property == scalar_property::writability;
}

const char* property_name (scalar_property property) {
  switch (property) {
  case scalar_property::type: return "return type";
  case scalar_property::variable_type: return "variable type";
  case scalar_property::border: return "border";
  case scalar_property::with_like: return "with-like";
  case scalar_property::field_type: return "field type";
  case scalar_property::accessibility: return "accessibility";
  case scalar_property::writability: return "writability";
  }
  return "property";
}

void validate_rules (const contract_declaration& declaration) {
  std::optional<field_layout> layout;
  const environment_declaration* environment= nullptr;
  std::map<std::string, const attribute_declaration*> attributes;
  std::vector<const scalar_declaration*> scalars;
  for (const auto& rule: declaration.rules) std::visit ([&] (const auto& value) {
    using rule_type= std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<rule_type, layout_declaration>) {
      if (layout && *layout != value.value)
        invalid (declaration.name, "arity", "conflicting layouts in one declaration");
      layout= value.value;
    }
    else if constexpr (std::is_same_v<rule_type, scalar_declaration>) {
      for (const auto* previous: scalars)
        if (previous->property == value.property && previous->value != value.value &&
            (previous->field == value.field || previous->field == -1 || value.field == -1))
          invalid (declaration.name, property_name (value.property),
                   "conflicting assignments to the same field or overlapping selectors");
      scalars.push_back (&value);
    }
    else if constexpr (std::is_same_v<rule_type, attribute_declaration>) {
      const auto [it, inserted]= attributes.emplace (value.name, &value);
      if (!inserted && !(it->second->value == value.value))
        invalid (declaration.name, value.name, "conflicting attribute values");
    }
    else if constexpr (std::is_same_v<rule_type, environment_declaration>) {
      if (environment && !(environment->value == value.value))
        invalid (declaration.name, "locals", "conflicting field environments");
      environment= &value;
    }
  }, rule);
  if (layout)
    for (const auto* rule: scalars)
      if (field_property (rule->property) && rule->field >= layout->fields ())
        invalid (declaration.name, property_name (rule->property),
                 "field selector is outside the declared layout");
}
}

contract_declaration decode_contract_declaration (const tree& source) {
  if (!is_func (source, DRD_PROPS) || N(source) < 3 || (N(source) & 1) == 0 ||
      !is_atomic (source[0]) || source[0] == "")
    invalid ("", "declaration", "expected a target followed by property/value pairs");
  contract_declaration result {make_tree_label (source[0]->label), bytes (source[0]->label), {}};
  for (int i=1; i<N(source); i+=2) {
    const auto property= atom (source[i], result.name, "property name");
    const tree& value= source[i+1];
    if (property == "arity") {
      field_layout layout;
      layout.mapping= field_mapping::detailed;
      if (is_atomic (value)) layout.base= integer (value, result.name, property);
      else {
        if (!is_func (value, TUPLE, 3) || !is_atomic (value[0]))
          invalid (result.name, property, "expected an arity or a repeated/optional layout");
        layout.base= integer (value[1], result.name, property);
        layout.extra= integer (value[2], result.name, property);
        layout.mapping= field_mapping::split;
        if (value[0] == "repeat") layout.arity= arity_kind::repeated_suffix;
        else if (value[0] == "repeat*") {
          layout.arity= arity_kind::repeated_prefix;
          std::swap (layout.base, layout.extra);
        }
        else if (value[0] == "options") layout.arity= arity_kind::optional;
        else invalid (result.name, property, "unknown layout kind");
        if ((layout.arity == arity_kind::repeated_prefix ||
             layout.arity == arity_kind::repeated_suffix) && layout.extra == 0)
          invalid (result.name, property, "the repetition group cannot be empty");
      }
      result.rules.push_back (layout_declaration {layout});
    }
    else if (property == "name" || property == "syntax") {
      if (property == "name") (void) atom (value, result.name, property);
      result.rules.push_back (attribute_declaration {property, node::frozen_tree (value)});
    }
    else if (property == "group") {
      const auto group= atom (value, result.name, property);
      (void) group_contains (group_extensions {}, group, result.name);
      result.rules.push_back (group_declaration {group});
    }
    else if (property == "locals") {
      if (!is_func (value, ATTR) || N(value)%2 != 0)
        invalid (result.name, property, "expected environment key/value pairs");
      for (int j=0; j<N(value); j+=2) (void) atom (value[j], result.name, property);
      result.rules.push_back (environment_declaration {node::frozen_tree (value)});
    }
    else if (property == "border") {
      const auto mode= atom (value, result.name, property);
      int border= mode == "yes" ? BORDER_YES : mode == "no" ? BORDER_INNER :
        mode == "inner" ? BORDER_INNER : mode == "outer" ? BORDER_OUTER : -1;
      if (border < 0) invalid (result.name, property, "expected yes, no, inner or outer");
      result.rules.push_back (scalar_declaration {scalar_property::border, border});
    }
    else if (property == "with-like") {
      if (value != "yes" && value != "no")
        invalid (result.name, property, "expected yes or no");
      result.rules.push_back (scalar_declaration {scalar_property::with_like, value == "yes"});
    }
    else if (property == "returns" || property == "parameter" || property == "macro-parameter") {
      const int type= type_code (atom (value, result.name, property));
      if (type < 0) invalid (result.name, property, "unknown value type");
      result.rules.push_back (scalar_declaration {scalar_property::type, type});
      if (property != "returns") result.rules.push_back (scalar_declaration {
        scalar_property::variable_type, property == "parameter" ? VAR_PARAMETER : VAR_MACRO_PARAMETER});
    }
    else {
      scalar_property field_property;
      int setting= type_code (property);
      if (setting >= 0) field_property= scalar_property::field_type;
      else if (property == "accessible" || property == "unaccessible" || property == "hidden") {
        field_property= scalar_property::accessibility;
        setting= property == "accessible" ? ACCESSIBLE_ALWAYS :
          property == "hidden" ? ACCESSIBLE_HIDDEN : ACCESSIBLE_NEVER;
        if (value == "none") setting= ACCESSIBLE_NEVER;
      }
      else if (property == "normal-writability" || property == "disable-writability" ||
               property == "enable-writability") {
        field_property= scalar_property::writability;
        setting= property == "disable-writability" ? WRITABILITY_DISABLE :
          property == "enable-writability" ? WRITABILITY_ENABLE : WRITABILITY_NORMAL;
      }
      else invalid (result.name, property, "unknown contract property");
      const int field= value == "all" ||
        (field_property == scalar_property::accessibility && value == "none") ?
        -1 : integer (value, result.name, property);
      result.rules.push_back (scalar_declaration {field_property, setting, field});
    }
  }
  if (result.target < START_EXTENSIONS || native_fields (result.target))
    result.role= declaration_target_role::native_node;
  for (const auto& rule: result.rules)
    if (const auto* scalar= std::get_if<scalar_declaration> (&rule))
      if (scalar->property == scalar_property::variable_type)
        result.role= declaration_target_role::environment_parameter;
  validate_rules (result);
  return result;
}

namespace {
node_contract materialize_rules (node_contract value, const definition_binding& binding,
                                 const contract_declaration& declaration) {
  const bool fixed_signature= binding && binding->value->signature.kind == definition_kind::macro;
  bool has_layout= false;
  // Layout is a declaration fact, not an order-sensitive mutation. Field rules
  // refer to this final layout even if arity is written after them in the source.
  for (const auto& operation: declaration.rules)
    if (const auto* rule= std::get_if<layout_declaration> (&operation)) {
      has_layout= true;
      if (accepts (value.layout_origin, property_authority::declared)) {
        value.layout= rule->value;
        value.layout_origin= property_authority::declared;
        if (int(value.fields.size ()) != value.layout.fields ())
          value.fields.assign (value.layout.fields (), field_contract {});
      }
    }
  // A captured MACRO's formals establish the default field layout without
  // waiting for body inference. XMACRO's tuple formal is not one child slot.
  if (!has_layout && fixed_signature && value.layout_origin == property_authority::inferred) {
    value.layout= {int(binding->value->signature.parameters.size ()), 0,
                   arity_kind::fixed, field_mapping::detailed};
    value.fields.resize (value.layout.fields ());
  }
  const bool resolved_layout= has_layout || fixed_signature ||
    value.layout_origin != property_authority::inferred;
  if (resolved_layout)
    for (const auto& operation: declaration.rules)
      if (const auto* rule= std::get_if<scalar_declaration> (&operation))
        if (field_property (rule->property) && rule->field >= value.layout.fields ())
          invalid (declaration.name, property_name (rule->property),
                   "field selector is outside the resolved layout");
  for (const auto& operation: declaration.rules) std::visit ([&] (const auto& rule) {
    using rule_type= std::decay_t<decltype(rule)>;
    if constexpr (std::is_same_v<rule_type, layout_declaration>) {
      return;
    }
    else if constexpr (std::is_same_v<rule_type, attribute_declaration>)
      value.attributes.insert_or_assign (rule.name, rule.value);
    else if constexpr (std::is_same_v<rule_type, group_declaration>) {
      (void) group_contains (group_extensions {}, rule.name, declaration.name);
    }
    else if constexpr (std::is_same_v<rule_type, environment_declaration>)
      for (auto& field: value.fields)
        declare (field.environment, field.environment_origin, rule.value);
    else {
      switch (rule.property) {
      case scalar_property::type: declare (value.type, value.type_origin, rule.value); break;
      case scalar_property::variable_type:
        declare (value.variable_type, value.variable_type_origin, rule.value); break;
      case scalar_property::border: declare (value.border, value.border_origin, rule.value); break;
      case scalar_property::with_like:
        declare (value.with_like, value.with_origin, rule.value != 0); break;
      default:
        for (int i=0; i<int(value.fields.size ()); ++i) {
          if (rule.field != -1 && rule.field != i) continue;
          auto& field= value.fields[i];
          if (rule.property == scalar_property::field_type)
            declare (field.type, field.type_origin, rule.value);
          else if (rule.property == scalar_property::accessibility)
            declare (field.accessible, field.accessible_origin, rule.value);
          else declare (field.writability, field.writability_origin, rule.value);
        }
      }
    }
  }, operation);
  return value;
}

bool callable (const definition_binding& binding) {
  return binding && binding->value->signature.kind != definition_kind::value;
}

std::shared_ptr<const node_contract> materialize_chain (
    declaration_chain& chain, const definition_binding& selected,
    const declaration_event& required= {}) {
  node_contract value= chain.baseline ? *chain.baseline : node_contract {};
  bool applied= false;
  // Package defaults precede binding-local rules regardless of load order.
  for (bool package: {true, false}) for (auto& occurrence: chain.occurrences) {
    if (bool (occurrence.event->package) != package) continue;
    occurrence.diagnostic.clear ();
    definition_binding signature;
    if (occurrence.role == declaration_target_role::macro_fields) {
      if (!callable (selected)) {
        occurrence.status= !occurrence.binding || package ?
          declaration_status::pending : declaration_status::dormant;
        continue;
      }
      if (!package && occurrence.binding != selected) {
        occurrence.status= occurrence.binding ? declaration_status::dormant : declaration_status::pending;
        continue;
      }
      signature= selected;
    }
    try {
      value= materialize_rules (value, signature, *occurrence.event);
      occurrence.status= declaration_status::active;
      applied= true;
    }
    catch (const std::invalid_argument& error) {
      if (occurrence.event == required) throw;
      occurrence.diagnostic= error.what ();
      occurrence.status= declaration_status::invalid;
    }
  }
  return applied ? std::make_shared<const node_contract> (std::move (value)) : chain.baseline;
}

bool contributes_group (const declaration_occurrence& occurrence) {
  return occurrence.status == declaration_status::active && std::any_of (
        occurrence.event->rules.begin (), occurrence.event->rules.end (),
        [] (const auto& rule) { return std::holds_alternative<group_declaration> (rule); });
}

bool contributes_group (const std::shared_ptr<const declaration_chain>& chain) {
  return chain && std::any_of (chain->occurrences.begin (), chain->occurrences.end (),
    [] (const auto& occurrence) { return contributes_group (occurrence); });
}

using group_events= declaration_group_events;
void replace_group_events (group_events& groups,
                          const std::shared_ptr<const declaration_chain>& old,
                          const std::shared_ptr<const declaration_chain>& next) {
  if (old)
    for (const auto& occurrence: old->occurrences) groups.erase (occurrence.order);
  if (next)
    for (const auto& occurrence: next->occurrences)
      if (contributes_group (occurrence)) groups.emplace (occurrence.order, occurrence.event);
}

std::shared_ptr<group_extensions> materialize_groups (const group_events& groups) {
  auto result= std::make_shared<group_extensions> ();
  for (const auto& [order, event]: groups)
    for (const auto& operation: event->rules)
      if (const auto* group= std::get_if<group_declaration> (&operation))
        declare_group_member (*result, group->name, event->name);
  return result;
}
}

bool contract_scope::publish_declarations (
    tree_label target, std::shared_ptr<const declaration_chain> chain,
    std::shared_ptr<const node_contract> value) {
  const auto previous= declaration_history (target);
  std::optional<group_events> groups;
  auto projected_groups= extensions;
  if (contributes_group (previous) || contributes_group (chain)) {
    groups= *sources->groups;
    replace_group_events (*groups, previous, chain);
    if (*groups != *sources->groups) projected_groups= materialize_groups (*groups);
  }
  const auto old_value= find_declaration (target);
  materialize_declaration (target, std::move (value));
  const bool changed= find_declaration (target) != old_value || *projected_groups != *extensions;
  if (!sources.unique ()) sources= std::make_shared<declaration_source_state> (*sources);
  if (groups) sources->groups= std::make_shared<const group_events> (std::move (*groups));
  if (chain && !chain->occurrences.empty ()) {
    sources->next_order= std::max (sources->next_order, chain->occurrences.back ().order + 1);
    sources->targets= std::move (sources->targets).set (target, std::move (chain));
  }
  else sources->targets= std::move (sources->targets).erase (target);
  extensions= std::move (projected_groups);
  return changed;
}

bool contract_scope::apply_declaration (const declaration_event& event, bool retain_invalid) {
  const auto previous= declaration_history (event->target);
  if (previous && std::any_of (previous->occurrences.begin (), previous->occurrences.end (),
      [&] (const auto& item) { return item.event == event; })) return false;
  auto chain= previous ? std::make_shared<declaration_chain> (*previous) :
    std::make_shared<declaration_chain> (declaration_chain {find_declaration (event->target), {}});
  auto& occurrences= chain->occurrences;
  auto role= event->role;
  const auto& existing= declared (event->target);
  if (role == declaration_target_role::macro_fields &&
      existing.variable_type_origin != property_authority::inferred &&
      existing.variable_type != VAR_MACRO)
    role= declaration_target_role::environment_parameter;
  const auto binding= definition (event->target);
  occurrences.push_back ({event, callable (binding) ? binding : nullptr,
    sources->next_order, {}, role});
  auto value= materialize_chain (*chain, binding, retain_invalid ? declaration_event {} : event);
  return publish_declarations (event->target, std::move (chain), std::move (value));
}

bool contract_scope::withdraw_declaration (const declaration_event& event) {
  const auto previous= declaration_history (event->target);
  if (!previous) return false;
  auto chain= std::make_shared<declaration_chain> (*previous);
  auto& occurrences= chain->occurrences;
  const auto old_size= occurrences.size ();
  occurrences.erase (std::remove_if (occurrences.begin (), occurrences.end (),
    [&] (const auto& item) { return item.event == event; }), occurrences.end ());
  if (occurrences.size () == old_size) return false;
  auto value= materialize_chain (*chain, definition (event->target));
  publish_declarations (event->target, std::move (chain), std::move (value));
  return true;
}

void contract_scope::refresh_declarations (tree_label target, bool attach_pending) {
  const auto previous= declaration_history (target);
  if (!previous) return;
  auto chain= std::make_shared<declaration_chain> (*previous);
  const auto selected= definition (target);
  if (attach_pending && callable (selected))
    for (auto& occurrence: chain->occurrences)
      if (occurrence.role == declaration_target_role::macro_fields &&
          !occurrence.event->package && !occurrence.binding)
        occurrence.binding= selected;
  auto value= materialize_chain (*chain, selected);
  publish_declarations (target, std::move (chain), std::move (value));
}

void contract_scope::enter_local_targets (const std::vector<contract_target>& targets) {
  for (const auto& target: targets) {
    const auto previous= declaration_history (target.label);
    if (!previous) continue;
    auto chain= std::make_shared<declaration_chain> (*previous);
    auto& occurrences= chain->occurrences;
    occurrences.erase (std::remove_if (occurrences.begin (), occurrences.end (),
      [] (const auto& occurrence) {
        return !occurrence.event->package &&
          occurrence.role == declaration_target_role::macro_fields;
      }), occurrences.end ());
    if (occurrences.size () == previous->occurrences.size ()) continue;
    auto value= materialize_chain (*chain, definition (target.label));
    publish_declarations (target.label, std::move (chain), std::move (value));
  }
}

void contract_scope::discard_declarations (tree_label target) {
  if (!declaration_history (target)) return;
  publish_declarations (target, {}, find_declaration (target));
}

void contract_scope::restore_declarations (
    const contract_scope& before, const std::vector<contract_target>& targets) {
  if (sources == before.sources || targets.empty ()) return;
  std::optional<group_events> groups;
  for (const auto& target: targets) {
    const auto original= before.declaration_history (target.label);
    const auto current= declaration_history (target.label);
    if (current == original) continue;
    if (!sources.unique ()) sources= std::make_shared<declaration_source_state> (*sources);
    if (contributes_group (current) || contributes_group (original)) {
      if (!groups) groups= *sources->groups;
      replace_group_events (*groups, current, original);
    }
    if (original) sources->targets= std::move (sources->targets).set (target.label, original);
    else sources->targets= std::move (sources->targets).erase (target.label);
    sources->next_order= std::max (sources->next_order, before.sources->next_order);
  }
  if (groups) {
    extensions= materialize_groups (*groups);
    sources->groups= std::make_shared<const group_events> (std::move (*groups));
  }
}

bool apply_contract_declaration (contract_scope& scope, const declaration_event& event) {
  return scope.apply_declaration (event);
}

bool apply_contract_declaration (contract_scope& scope, const contract_declaration& declaration) {
  return scope.apply_declaration (std::make_shared<const contract_declaration> (declaration));
}

namespace {
thread_local contract_event_capture* active_queries= nullptr;

template<class Visit, class Enter, class Leave>
bool walk_events (const contract_event_log& log, Visit visit, Enter enter, Leave leave) {
  if (!log) return true;
  struct position { const contract_event_block* block; std::size_t next= 0; };
  std::vector<position> stack {{log.get ()}};
  if (!log->local_targets.empty ()) enter (log->local_targets);
  while (!stack.empty ()) {
    auto& top= stack.back ();
    if (top.next == top.block->records.size ()) {
      if (top.block->close_scope && !top.block->local_targets.empty ())
        leave (top.block->local_targets);
      stack.pop_back ();
      continue;
    }
    const auto& record= top.block->records[top.next++];
    if (const auto* child= std::get_if<contract_event_log> (&record)) {
      const auto* block= child->get ();
      if (!block->local_targets.empty ()) enter (block->local_targets);
      stack.push_back ({block});
    }
    else if (!visit (record)) return false;
  }
  return true;
}
}

void visit_contract_events (const contract_event_log& log,
                           const std::function<void(const contract_event&)>& visit,
                           const std::function<void(const std::vector<contract_target>&)>& enter,
                           const std::function<void(const std::vector<contract_target>&)>& leave,
                           const std::function<void(const definition_binding&)>& definition,
                           const std::function<void(const variable_write&)>& write) {
  walk_events (log, [&] (const auto& record) {
    if (const auto* event= std::get_if<contract_event> (&record)) visit (*event);
    else if (const auto* binding= std::get_if<definition_binding> (&record)) {
      if (definition) definition (*binding);
    }
    else if (const auto* variable= std::get_if<variable_write> (&record)) {
      if (write) write (*variable);
    }
    return true;
  }, [&] (const auto& targets) { if (enter) enter (targets); },
     [&] (const auto& targets) { if (leave) leave (targets); });
}

void visit_declaration_executions (const contract_event_log& log,
    const std::function<void(const declaration_execution_handle&)>& visit) {
  walk_events (log, [&] (const auto& record) {
    if (const auto* execution= std::get_if<declaration_execution_handle> (&record))
      visit (*execution);
    return true;
  }, [] (const auto&) {}, [] (const auto&) {});
}

contract_event_log contract_event_prefix (
    const contract_event_log& log, const evaluation_boundary_handle& boundary) {
  if (!log || !boundary || !log->has_boundaries) return {};
  struct position { contract_event_log block; std::size_t next= 0; };
  std::vector<position> stack {{log}};
  while (!stack.empty ()) {
    auto& top= stack.back ();
    if (top.next == top.block->records.size ()) {
      stack.pop_back ();
      continue;
    }
    const auto& record= top.block->records[top.next++];
    if (const auto* child= std::get_if<contract_event_log> (&record)) {
      if ((*child)->has_boundaries) stack.push_back ({*child});
    }
    else if (const auto* found= std::get_if<evaluation_boundary_handle> (&record)) {
      if (*found != boundary) continue;
      contract_event_log prefix;
      while (!stack.empty ()) {
        const auto& selected= stack.back ();
        contract_event_buffer records (selected.block->records.begin (),
          selected.block->records.begin () + selected.next);
        if (prefix) records.back ()= std::move (prefix);
        bool effects= false;
        for (const auto& event: records)
          effects= effects || std::holds_alternative<contract_event> (event) ||
            std::holds_alternative<definition_binding> (event) ||
            std::holds_alternative<variable_write> (event) ||
            std::holds_alternative<reference_write> (event) ||
            std::holds_alternative<declaration_execution_handle> (event) ||
            std::holds_alternative<evaluation_barrier> (event) ||
            (std::holds_alternative<contract_event_log> (event) &&
             std::get<contract_event_log> (event)->has_effects);
        // These ancestors have been entered but not exited at the boundary.
        prefix= std::make_shared<const contract_event_block> (
          contract_event_block {std::move (records), selected.block->local_targets,
                                effects, true, false});
        stack.pop_back ();
      }
      return prefix;
    }
  }
  return {};
}

bool contract_events_applicable (const contract_event_log& log, contract_scope input) {
  return evaluation_events_applicable (log, std::move (input), {});
}

bool evaluation_events_applicable (const contract_event_log& log, contract_scope input,
                                    const variable_lookup& lookup,
                                    const external_matcher& external,
                                    const package_matcher& packages,
                                    const reference_write_observer& reference) {
  return bool (resolve_evaluation_effects (log, std::move (input), lookup,
    execution_resolution::cache_admission, external, packages, {}, reference));
}

std::optional<evaluation_effects> resolve_evaluation_effects (
    const contract_event_log& log, contract_scope input, const variable_lookup& lookup,
    execution_resolution mode, const external_matcher& external, const package_matcher& packages,
    const evaluation_boundary_observer& boundary,
    const reference_write_observer& reference) {
  const bool validate_reads= mode == execution_resolution::cache_admission;
  using variable_value= std::optional<node::frozen_tree>;
  evaluation_variable_overlay variables;
  struct saved_variable { std::string name; bool overridden; variable_value value; };
  struct saved_scope { contract_scope contracts; std::vector<saved_variable> variables; };
  std::vector<saved_scope> scopes;
  const bool accepted= walk_events (log, [&] (const auto& record) {
    if (const auto* point= std::get_if<evaluation_boundary_handle> (&record)) {
      if (boundary) boundary (*point, input, variables);
      return true;
    }
    if (const auto* barrier= std::get_if<evaluation_barrier> (&record))
      return !validate_reads && barrier->kind == evaluation_barrier_kind::layout_output;
    if (const auto* query= std::get_if<package_source_query> (&record))
      return !validate_reads || (packages && packages (*query));
    if (const auto* query= std::get_if<external_query> (&record))
      return !validate_reads || (external && external (*query));
    if (const auto* write= std::get_if<reference_write> (&record)) {
      if (reference) reference (*write);
      return !validate_reads || bool (reference);
    }
    if (const auto* query= std::get_if<variable_query> (&record)) {
      if (!validate_reads || !lookup) return true;
      const auto changed= variables.find (query->name);
      if (changed) {
        const auto& value= *changed;
        return bool (value) == query->present &&
          (!query->value || (value && *query->value == *value));
      }
      const auto value= lookup (string (query->name.data (), query->name.size ()));
      return bool (value) == query->present &&
        (!query->value || (value && query->value->matches (*value)));
    }
    if (const auto* write= std::get_if<variable_write> (&record)) {
      variables= std::move (variables).set (write->name, write->value);
      const string name (write->name.data (), write->name.size ());
      if (existing_tree_label (name)) {
        const auto label= as_tree_label (name);
        const auto binding= input.definition (label);
        if (binding && (!write->value || !(binding->value->expression == *write->value)))
          input.forget_definition (label);
      }
      return true;
    }
    if (const auto* query= std::get_if<contract_query> (&record)) {
      if (!validate_reads) return true;
      const auto current= input.find (query->target);
      return current == query->expected ||
        (current && query->expected && *current == *query->expected);
    }
    if (const auto* query= std::get_if<group_query> (&record)) {
      if (!validate_reads) return true;
      return std::visit ([&] (const auto& expected) {
        using result_type= std::decay_t<decltype(expected)>;
        if constexpr (std::is_same_v<result_type, bool>)
          return group_contains (input.groups (), query->group, query->tag) == expected;
        else if constexpr (std::is_same_v<result_type, std::string>)
          return group_find (input.groups (), query->group, query->tag) == expected;
        else return group_members (input.groups (), query->group) == expected;
      }, query->expected);
    }
    if (const auto* query= std::get_if<definition_query> (&record))
      return !validate_reads || input.definition (query->target) == query->expected;
    if (const auto* absent= std::get_if<contract_absence> (&record)) {
      if (!validate_reads) return true;
      const string name (absent->name.data (), absent->name.size ());
      return !existing_tree_label (name) || !input.contains (as_tree_label (name));
    }
    if (const auto* event= std::get_if<contract_event> (&record)) {
      input.apply_declaration (*event, true);
    }
    else if (const auto* execution= std::get_if<declaration_execution_handle> (&record)) {
      if (!validate_reads || !(*execution)->declaration) return true;
      const auto& event= (*execution)->declaration;
      const auto history= input.declaration_history (event->target);
      if (!history) return false;
      const auto occurrence= std::find_if (history->occurrences.begin (), history->occurrences.end (),
        [&] (const auto& item) { return item.event == event; });
      // Cached error boxes are as dependent on the selected layout as valid
      // declarations. A changed outcome requires normal source preparation.
      return occurrence != history->occurrences.end () &&
        occurrence->status == (*execution)->status &&
        occurrence->diagnostic == (*execution)->diagnostic;
    }
    else if (const auto* binding= std::get_if<definition_binding> (&record))
      input.bind (*binding);
    return true;
  }, [&] (const auto& targets) {
       saved_scope saved {input, {}};
       for (const auto& target: targets) {
         const auto current= variables.find (target.name);
         saved.variables.push_back ({target.name, current != nullptr,
           current ? *current : variable_value {}});
       }
       scopes.push_back (std::move (saved));
       input.enter_local_targets (targets);
     },
     [&] (const auto& targets) {
       input.restore_targets (scopes.back ().contracts, targets);
       for (const auto& saved: scopes.back ().variables)
         if (saved.overridden) variables= std::move (variables).set (saved.name, saved.value);
         else variables= std::move (variables).erase (saved.name);
       scopes.pop_back ();
     });
  if (!accepted) return std::nullopt;
  return evaluation_effects {std::move (input), std::move (variables)};
}

contract_event_suspension::contract_event_suspension (contract_event_capture*& target):
  sink (target), previous (target), previous_queries (active_queries) {
  sink= nullptr;
  active_queries= nullptr;
}

contract_event_suspension::~contract_event_suspension () {
  sink= previous;
  active_queries= previous_queries;
}

void observe_contract_query (const contract_scope& scope, tree_label target) {
  if (active_queries && active_queries->owner == &scope)
    active_queries->record (contract_query {target, scope.find (target)});
}

void observe_contract_absence (const contract_scope& scope, const string& name) {
  if (active_queries && active_queries->owner == &scope)
    active_queries->record (contract_absence {bytes (name)});
}

void observe_contract_variable (const contract_scope& scope, const string& name,
                                 bool present, const tree* value) {
  if (active_queries && active_queries->owner == &scope)
    active_queries->read_variable (name, present, value);
}

void observe_group_members (const contract_scope& scope, std::string_view group,
                            const std::vector<std::string>& result) {
  if (active_queries && active_queries->owner == &scope)
    active_queries->record (group_query {std::string (group), {}, result});
}

void observe_group_contains (const contract_scope& scope, std::string_view group,
                             std::string_view tag, bool result) {
  if (active_queries && active_queries->owner == &scope)
    active_queries->record (group_query {std::string (group), std::string (tag), result});
}

void observe_group_find (const contract_scope& scope, std::string_view group,
                         std::string_view tag, const std::string& result) {
  if (active_queries && active_queries->owner == &scope)
    active_queries->record (group_query {std::string (group), std::string (tag), result});
}

contract_event_capture::contract_event_capture (contract_event_capture*& sink,
                                                contract_event_log& destination,
                                                const contract_scope* owner):
  sink (sink), parent (sink), destination (destination), previous (destination),
  owner (owner), previous_queries (active_queries) {
  if (!previous && parent) previous= parent->previous_block ();
  sink= this;
  active_queries= this;
}

void contract_event_capture::record (contract_event_record event) {
  if (std::holds_alternative<evaluation_boundary_handle> (event)) boundaries= true;
  else if (const auto* query= std::get_if<contract_query> (&event)) {
    const auto found= queries.find (query->target);
    if (found != queries.end () && found->second == query->expected) return;
    queries.insert_or_assign (query->target, query->expected);
  }
  else if (const auto* absent= std::get_if<contract_absence> (&event)) {
    if (!absences.insert (absent->name).second) return;
  }
  else if (const auto* query= std::get_if<group_query> (&event)) {
    const auto key= std::make_tuple (query->expected.index (), query->group, query->tag);
    const auto found= group_reads.find (key);
    if (found != group_reads.end () && found->second == query->expected) return;
    group_reads.insert_or_assign (key, query->expected);
  }
  else if (std::holds_alternative<definition_query> (event)) {
    // A binding read is an input, not an effect or a descriptor-value query.
  }
  else if (std::holds_alternative<package_source_query> (event)) {
    // Source selection is an input. Preserve its position before package effects.
  }
  else if (const auto* query= std::get_if<variable_query> (&event)) {
    const auto found= variable_reads.find (query->name);
    if (found != variable_reads.end () && found->second.present == query->present &&
        (!query->value || found->second.value == query->value)) return;
    variable_reads.insert_or_assign (query->name, *query);
  }
  else if (const auto* query= std::get_if<external_query> (&event)) {
    const auto key= std::make_pair (query->source, query->name);
    const auto found= external_reads.find (key);
    if (found != external_reads.end () && found->second.local == query->local &&
        found->second.present == query->present && found->second.value == query->value)
      return;
    external_reads.insert_or_assign (key, *query);
  }
  else {
    if (const auto* block= std::get_if<contract_event_log> (&event)) {
      if (!(*block)->has_effects && !(*block)->has_boundaries) {
        for (const auto& child: (*block)->records) record (child);
        return;
      }
      boundaries= boundaries || (*block)->has_boundaries;
      effects= effects || (*block)->has_effects;
    }
    else effects= true;
    if (const auto* write= std::get_if<variable_write> (&event)) variable_reads.erase (write->name);
    else if (std::holds_alternative<contract_event_log> (event) ||
             std::holds_alternative<evaluation_barrier> (event)) variable_reads.clear ();
    queries.clear ();
    absences.clear ();
    group_reads.clear ();
    external_reads.clear ();
  }
  events.push_back (std::move (event));
}

void contract_event_capture::read_variable (const string& name, bool present, const tree* value) {
  const auto key= bytes (name);
  const auto previous= variable_reads.find (key);
  if (previous != variable_reads.end () && previous->second.present == present &&
      (!value || (previous->second.value && previous->second.value->matches (*value)))) return;
  record (variable_query {key, present,
    value && present ? std::optional<node::frozen_tree> (*value) : std::nullopt});
}

variable_write contract_event_capture::write_variable (const string& name, const tree* value) {
  const auto key= bytes (name);
  if (!events.empty ())
    if (const auto* previous= std::get_if<variable_write> (&events.back ()))
      if (previous->name == key &&
          (value ? previous->value && previous->value->matches (*value) : !previous->value)) return *previous;
  variable_write write {key, value ? std::optional<node::frozen_tree> (*value) : std::nullopt};
  record (write);
  return write;
}

evaluation_boundary_handle contract_event_capture::mark_boundary () {
  auto boundary= std::make_shared<const evaluation_boundary> ();
  record (boundary);
  return boundary;
}

void contract_event_capture::discard_since (std::size_t count) {
  events.resize (count);
  queries.clear ();
  absences.clear ();
  group_reads.clear ();
  variable_reads.clear ();
  external_reads.clear ();
  effects= false;
  boundaries= false;
  for (const auto& event: events) {
    if (std::holds_alternative<contract_event> (event) ||
        std::holds_alternative<definition_binding> (event) ||
        std::holds_alternative<variable_write> (event) ||
        std::holds_alternative<reference_write> (event) ||
        std::holds_alternative<declaration_execution_handle> (event) ||
        std::holds_alternative<evaluation_barrier> (event) ||
        (std::holds_alternative<contract_event_log> (event) &&
         std::get<contract_event_log> (event)->has_effects)) effects= true;
    if (std::holds_alternative<evaluation_boundary_handle> (event) ||
        (std::holds_alternative<contract_event_log> (event) &&
         std::get<contract_event_log> (event)->has_boundaries)) boundaries= true;
  }
}

contract_event_log contract_event_capture::previous_block () const {
  if (previous && events.size () < previous->records.size ())
    if (const auto* block= std::get_if<contract_event_log> (&previous->records[events.size ()]))
      return *block;
  return {};
}

definition_payload contract_event_capture::previous_value (
    tree_label target, const tree& value) const {
  if (previous && events.size () < previous->records.size ())
    if (const auto* binding= std::get_if<definition_binding> (&previous->records[events.size ()]))
      if ((*binding)->target == target && (*binding)->value->expression.matches (value))
        return (*binding)->value;
  return {};
}

void contract_event_capture::commit (std::vector<contract_target> local_targets,
                                     bool close_scope) {
  contract_event_log completed;
  if (!events.empty ()) {
    completed= std::make_shared<const contract_event_block> (
      contract_event_block {std::move (events), std::move (local_targets),
                            effects, boundaries, close_scope});
    if (parent) parent->record (completed);
  }
  destination= std::move (completed);
  sink= parent;
  active_queries= previous_queries;
  committed= true;
}

contract_event_capture::~contract_event_capture () {
  if (!committed) {
    sink= parent;
    active_queries= previous_queries;
  }
}

} // namespace athena::contracts

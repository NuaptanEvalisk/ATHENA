// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "macro_analysis.hpp"
#include "macro_analysis_core.hpp"
#include "drd_info.hpp"
#include <map>
#include <set>
#include <string>
#include <algorithm>

namespace athena::contracts {
namespace {
std::string bytes (const string& value) { return std::string (value.data (), N(value)); }
string native (const std::string& value) { return string (value.data (), value.size ()); }

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
      is_atomic (name) ? bytes (name->label) : std::string {}});
  }
  return result;
}

bool same_declaration (const analysis_contract_input& a, const analysis_contract_input& b) {
  const auto same= [] (const auto& x, auto xo, const auto& y, auto yo) {
    return xo == yo && x == y;
  };
  if (!same (a.layout, a.layout_origin, b.layout, b.layout_origin) ||
      !same (a.type, a.type_origin, b.type, b.type_origin) ||
      !same (a.variable_type, a.variable_type_origin, b.variable_type, b.variable_type_origin) ||
      !same (a.with_like, a.with_origin, b.with_like, b.with_origin) ||
      a.fields.size () != b.fields.size ()) return false;
  for (std::size_t i=0; i<a.fields.size (); ++i) {
    const auto& x= a.fields[i];
    const auto& y= b.fields[i];
    if (!same (x.type, x.type_origin, y.type, y.type_origin) ||
        !same (x.access, x.access_origin, y.access, y.access_origin) ||
        !same (x.environment, x.environment_origin, y.environment, y.environment_origin) ||
        x.name != y.name) return false;
  }
  return true;
}
}

struct macro_analysis::impl {
  drd_info_rep& scope;
  macro_analysis_core core;
  std::map<std::string, node::frozen_tree> sources;
  std::set<std::string> changed_declarations;
  std::set<tree_label> known_labels, known_contracts;
  std::map<tree_label, analysis_contract_input> declarations;
  std::map<std::string, analysis_contract_result> results;
  contract_scope last_publication;
  evaluation_input_snapshot consumed_inputs;
  bool initialized= false;

  explicit impl (drd_info_rep& owner): scope (owner) {}

  void symbol (macro_analysis_input& input, tree_label label) {
    if (known_labels.insert (label).second)
      input.symbols.emplace (label, bytes (as_string (label)));
  }

  void declaration (macro_analysis_input& input, tree_label label) {
    symbol (input, label);
    auto next= detach_contract (bytes (as_string (label)), scope.contracts.declared (label));
    const auto previous= declarations.find (label);
    if (previous == declarations.end () || !same_declaration (previous->second, next)) {
      declarations.insert_or_assign (label, next);
      input.contracts.push_back (std::move (next));
    }
    known_contracts.insert (label);
  }

  void symbols (macro_analysis_input& input, const tree& value) {
    symbol (input, L(value));
    if (!known_contracts.count (L(value))) declaration (input, L(value));
    if (is_atomic (value)) return;
    if ((is_func (value, COMPOUND) || is_func (value, MAP_ARGS)) && N(value)>0)
      for (int i=0; i<(L(value) == MAP_ARGS ? 2 : 1) && i<N(value); ++i)
        if (is_atomic (value[i]) && existing_tree_label (value[i]->label)) {
          const tree_label label= as_tree_label (value[i]->label);
          symbol (input, label);
          if (!known_contracts.count (label)) declaration (input, label);
        }
    for (int i=0; i<N(value); ++i) symbols (input, value[i]);
  }

  macro_analysis_input prepare (const evaluation_input_snapshot& inputs,
                                hashmap<string, tree> values) {
    macro_analysis_input input;
    if (!initialized) {
      for (const auto label: scope.contracts.snapshot ().labels ())
        declaration (input, label);
      initialized= true;
    }
    for (const auto& name: changed_declarations)
      declaration (input, as_tree_label (native (name)));
    changed_declarations.clear ();
    for (const auto& name: inputs.changed_since (consumed_inputs)) {
      const string key= native (name);
      const auto found= sources.find (name);
      if (!values->contains (key)) {
        if (found != sources.end ()) {
          input.removed.push_back (name);
          sources.erase (found);
        }
        continue;
      }
      const tree value= values[key];
      if (found != sources.end () && found->second.matches (value)) continue;
      const auto label= make_tree_label (key);
      symbol (input, label);
      if (!known_contracts.count (label)) declaration (input, label);
      symbols (input, value);
      const auto binding= scope.contracts.definition (label);
      auto entry= sources.insert_or_assign (name,
        binding && binding->value->expression.matches (value) ?
          binding->value->expression : node::frozen_tree (value));
      input.changed.push_back ({name, entry.first->second});
    }
    return input;
  }

  void publish (contract_scope& draft, const std::vector<analysis_contract_result>& result) {
    const auto assign= [] (auto& value, auto& origin, const auto& next, auto source) {
      if (!accepts (origin, source)) return false;
      value= next;
      origin= source;
      return true;
    };
    for (const auto& item: result) {
      const auto label= as_tree_label (native (item.name));
      node_contract value= draft.declared (label);
      if (assign (value.layout, value.layout_origin, item.layout, item.layout_origin) &&
          int(value.fields.size ()) != value.layout.fields ())
        value.fields.assign (value.layout.fields (), field_contract {});
      assign (value.type, value.type_origin, item.type, item.type_origin);
      assign (value.variable_type, value.variable_type_origin,
              item.variable_type, item.variable_type_origin);
      assign (value.with_like, value.with_origin, item.with_like, item.with_origin);
      tree conflicts (TUPLE), conditional (TUPLE);
      for (int i=0; i<int(item.fields.size ()); ++i) {
        const auto& child= item.fields[i];
        if (i < int(value.fields.size ())) {
          auto& field= value.fields[i];
          assign (field.type, field.type_origin, child.type, child.type_origin);
          assign (field.accessible, field.accessible_origin, child.access, child.access_origin);
          assign (field.environment, field.environment_origin,
                  child.environment, child.environment_origin);
        }
        if (!child.name.empty () && value.layout_origin != property_authority::builtin &&
            !value.attributes.count ("name-" + std::to_string (i)))
          value.attributes.insert_or_assign ("name-" + std::to_string (i),
            node::frozen_tree (tree (native (child.name))));
        if (child.conditional) conditional << as_string (i);
        for (const auto& conflict: child.conflicts)
          conflicts << tree (TUPLE, as_string (i), native (conflict));
      }
      value.attributes.insert_or_assign ("analysis-conflicts", node::frozen_tree (conflicts));
      value.attributes.insert_or_assign ("analysis-conditional", node::frozen_tree (conditional));
      draft.publish_inference (label, std::move (value));
    }
  }

  void synchronize (const evaluation_input_snapshot& inputs,
                    const hashmap<string, tree>& values) {
    const auto restored= initialized ? scope.contracts.changed_contracts (last_publication) :
                                      std::vector<tree_label> {};
    auto draft= scope.contracts;
    if (!initialized) draft.clear_inference ();
    for (auto label: restored) changed_declarations.insert (bytes (as_string (label)));
    auto next= core.apply (prepare (inputs, values));
    std::set<std::string> updated;
    for (const auto& result: next) {
      if (sources.count (result.name)) results.insert_or_assign (result.name, result);
      else results.erase (result.name);
      updated.insert (result.name);
    }
    // A result may still be current even though restoring an earlier snapshot
    // removed its projection. Reapply it only after synchronizing actual inputs.
    for (auto label: restored) {
      const auto name= bytes (as_string (label));
      const auto found= results.find (name);
      if (found != results.end () && !updated.count (name)) next.push_back (found->second);
    }
    publish (draft, next);
    scope.contracts= std::move (draft);
    last_publication= scope.contracts;
    consumed_inputs= inputs;
  }
};

macro_analysis::macro_analysis (drd_info_rep& owner): state (std::make_unique<impl> (owner)) {}
macro_analysis::~macro_analysis ()= default;
void macro_analysis::invalidate (const string& binding) {
  state->changed_declarations.insert (bytes (binding));
}
void macro_analysis::synchronize (const evaluation_input_snapshot& inputs,
                                  const hashmap<string, tree>& values) {
  try {
    state->synchronize (inputs, values);
  }
  catch (...) {
    // Preparation and the dependency engine both retain incremental state.
    // A failed batch must restart from a complete input, not its partial delta.
    state= std::make_unique<impl> (state->scope);
    throw;
  }
}

} // namespace athena::contracts

// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once

#include "contract_scope.hpp"
#include <functional>
#include <optional>
#include <set>
#include <tuple>
#include <variant>
#include <cstdint>

namespace athena::contracts {

struct layout_declaration { field_layout value; };
enum class scalar_property {
  type, variable_type, border, with_like, field_type, accessibility, writability
};
struct scalar_declaration {
  scalar_property property;
  int value;
  int field= -1; // All fields for field properties, otherwise a node property.
};
struct environment_declaration { node::frozen_tree value; };
struct attribute_declaration { std::string name; node::frozen_tree value; };
struct group_declaration { std::string name; };
using declaration_rule= std::variant<layout_declaration, scalar_declaration,
  environment_declaration, attribute_declaration, group_declaration>;
enum class declaration_target_role { macro_fields, native_node, environment_parameter };
enum class declaration_status { pending, dormant, active, invalid };

struct contract_declaration {
  tree_label target;
  std::string name;
  std::vector<declaration_rule> rules;
  std::string source;
  std::vector<int> source_path;
  package_inclusion_handle package;
  declaration_target_role role= declaration_target_role::macro_fields;
};

struct declaration_occurrence {
  declaration_event event;
  definition_binding binding;
  std::uint64_t order;
  std::string diagnostic;
  declaration_target_role role= declaration_target_role::macro_fields;
  declaration_status status= declaration_status::pending;
};
struct declaration_chain {
  std::shared_ptr<const node_contract> baseline;
  std::vector<declaration_occurrence> occurrences;
};
using declaration_group_events= std::map<std::uint64_t, declaration_event>;
struct declaration_source_state {
  immer::map<tree_label, std::shared_ptr<const declaration_chain>> targets;
  std::shared_ptr<const declaration_group_events> groups=
    std::make_shared<const declaration_group_events> ();
  std::uint64_t next_order= 0;
};

// Source syntax is decoded once; cached execution replays native rules against
// the current scope rather than copying resolved facts from an old input scope.
contract_declaration decode_contract_declaration (const tree& source);
bool apply_contract_declaration (contract_scope&, const contract_declaration&);
bool apply_contract_declaration (contract_scope&, const declaration_event&);

using contract_event= declaration_event;
// One real declaration execution, including failures before a target exists.
// The enclosing execution log owns its revision; replay retains this identity.
struct declaration_execution {
  node::frozen_tree source;
  std::string file;
  std::vector<int> source_path;
  package_inclusion_handle package;
  declaration_event declaration;
  declaration_status status;
  std::string diagnostic;
};
using declaration_execution_handle= std::shared_ptr<const declaration_execution>;
struct contract_event_block;
class contract_event_capture;
using contract_event_log= std::shared_ptr<const contract_event_block>;
struct contract_query {
  tree_label target;
  std::shared_ptr<const node_contract> expected;
};
struct contract_absence { std::string name; };
using group_query_result= std::variant<std::vector<std::string>, bool, std::string>;
struct group_query {
  std::string group, tag;
  group_query_result expected; // Members/order, membership, or owning subgroup.
};
struct definition_query {
  tree_label target;
  definition_binding expected;
};
struct variable_query {
  std::string name;
  bool present;
  std::optional<node::frozen_tree> value; // Empty means a presence-only query.
};
struct variable_write {
  std::string name;
  std::optional<node::frozen_tree> value; // Empty removes the variable.
};
enum class external_source { reference, attachment };
// Reads of the evaluator's distinct local/global reference and attachment maps.
// The selected provider is an input even when both providers have equal values.
struct external_query {
  external_source source;
  std::string name;
  bool local, present;
  std::optional<node::frozen_tree> value;
};
// The actual package selected by source resolution, including nested inclusions.
struct package_source_query {
  std::string name, path;
  std::uint64_t generation;
  std::int64_t modified;
  std::uintmax_t bytes;
};
enum class evaluation_barrier_kind { unrecorded_context, layout_output };
struct evaluation_barrier {
  evaluation_barrier_kind kind= evaluation_barrier_kind::unrecorded_context;
};
using contract_event_record= std::variant<contract_event, definition_binding, contract_event_log,
  contract_query, contract_absence, group_query, definition_query, variable_query, variable_write,
  external_query, package_source_query,
  evaluation_boundary_handle, evaluation_barrier, declaration_execution_handle>;
using contract_event_buffer= std::vector<contract_event_record>;
struct contract_event_block {
  contract_event_buffer records;
  std::vector<contract_target> local_targets;
  bool has_effects= true;
  bool has_boundaries= false;
  bool close_scope= true;
};

// Keep scopes containing the selected boundary open; completed earlier child
// scopes keep their ordinary exits. A missing token returns an empty handle.
contract_event_log contract_event_prefix (const contract_event_log&,
                                          const evaluation_boundary_handle&);

void observe_contract_query (const contract_scope&, tree_label);
void observe_contract_absence (const contract_scope&, const string&);
void observe_contract_variable (const contract_scope&, const string&, bool present,
                                const tree* value);
void observe_group_members (const contract_scope&, std::string_view,
                           const std::vector<std::string>&);
void observe_group_contains (const contract_scope&, std::string_view, std::string_view, bool);
void observe_group_find (const contract_scope&, std::string_view, std::string_view,
                        const std::string&);
// Native descriptor dependencies only. Source/environment cache admission
// remains the caller's responsibility until their read sets are integrated.
bool contract_events_applicable (const contract_event_log&, contract_scope input);
using variable_lookup= std::function<std::optional<tree>(const string&)>;
using external_matcher= std::function<bool(const external_query&)>;
using package_matcher= std::function<bool(const package_source_query&)>;
struct evaluation_effects {
  contract_scope contracts;
  immer::map<std::string, std::optional<node::frozen_tree>> variables;
};
using evaluation_variable_overlay= decltype(evaluation_effects::variables);
using evaluation_boundary_observer= std::function<void (
  const evaluation_boundary_handle&, const contract_scope&, const evaluation_variable_overlay&)>;
enum class execution_resolution { cache_admission, recorded_context };
std::optional<evaluation_effects> resolve_evaluation_effects (
    const contract_event_log&, contract_scope input, const variable_lookup&,
    execution_resolution mode= execution_resolution::cache_admission,
    const external_matcher& external= {},
    const package_matcher& packages= {},
    const evaluation_boundary_observer& boundary= {});
bool evaluation_events_applicable (const contract_event_log&, contract_scope input,
                                    const variable_lookup&, const external_matcher& external= {},
                                    const package_matcher& packages= {});

void visit_contract_events (const contract_event_log&,
                           const std::function<void(const contract_event&)>&,
                           const std::function<void(const std::vector<contract_target>&)>& enter= {},
                           const std::function<void(const std::vector<contract_target>&)>& leave= {},
                           const std::function<void(const definition_binding&)>& definition= {},
                           const std::function<void(const variable_write&)>& write= {});

void visit_declaration_executions (const contract_event_log&,
  const std::function<void(const declaration_execution_handle&)>&);

class contract_event_suspension {
  contract_event_capture*& sink;
  contract_event_capture* previous;
  contract_event_capture* previous_queries;
public:
  explicit contract_event_suspension (contract_event_capture*&);
  ~contract_event_suspension ();
  contract_event_suspension (const contract_event_suspension&)= delete;
  contract_event_suspension& operator= (const contract_event_suspension&)= delete;
};

// Nested bridge captures append only completed events to their parent. An
// exceptional exit restores the capture sink without publishing partial logs.
class contract_event_capture {
  contract_event_capture*& sink;
  contract_event_capture* parent;
  contract_event_log& destination;
  contract_event_log previous;
  contract_event_buffer events;
  const contract_scope* owner;
  contract_event_capture* previous_queries;
  std::map<tree_label, std::shared_ptr<const node_contract>> queries;
  std::set<std::string> absences;
  std::map<std::tuple<std::size_t, std::string, std::string>, group_query_result> group_reads;
  std::map<std::string, variable_query> variable_reads;
  std::map<std::pair<external_source, std::string>, external_query> external_reads;
  bool effects= false;
  bool boundaries= false;
  bool committed= false;
  contract_event_log previous_block () const;
  friend void observe_contract_query (const contract_scope&, tree_label);
  friend void observe_contract_absence (const contract_scope&, const string&);
  friend void observe_contract_variable (const contract_scope&, const string&, bool, const tree*);
  friend void observe_group_members (const contract_scope&, std::string_view,
                                    const std::vector<std::string>&);
  friend void observe_group_contains (const contract_scope&, std::string_view, std::string_view, bool);
  friend void observe_group_find (const contract_scope&, std::string_view, std::string_view,
                                 const std::string&);
public:
  contract_event_capture (contract_event_capture*& sink, contract_event_log& destination,
                          const contract_scope* owner= nullptr);
  ~contract_event_capture ();
  contract_event_capture (const contract_event_capture&)= delete;
  contract_event_capture& operator= (const contract_event_capture&)= delete;
  bool has_events () const { return !events.empty (); }
  bool has_effects () const { return effects; }
  std::size_t record_count () const { return events.size (); }
  void discard_since (std::size_t count);
  void record (contract_event_record event);
  void read_variable (const string&, bool present, const tree* value= nullptr);
  variable_write write_variable (const string&, const tree* value);
  evaluation_boundary_handle mark_boundary ();
  definition_payload previous_value (tree_label target, const tree& value) const;
  void commit (std::vector<contract_target> local_targets= {}, bool close_scope= true);
};

} // namespace athena::contracts

// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once

#include "env.hpp"
#include "Data/NodeContracts/execution_state.hpp"

// These records retain actor-owned source/argument handles. They are not worker
// inputs; detached native events refer only to the separate boundary token.
struct source_occurrence_ancestor {
  athena::contracts::evaluation_boundary_handle boundary;
  std::shared_ptr<const source_occurrence_ancestor> parent;
};

struct source_occurrence {
  std::vector<int> address;
  tree value;
  hashmap<string,path> sources;
  list<hashmap<string,tree>> arguments;
  list<hashmap<string,path>> argument_sources;
  url file_name;
  athena::contracts::package_inclusion_handle package_origin;
  bool secure;
  athena::contracts::evaluation_boundary_handle boundary;
  athena::contracts::evaluation_boundary_handle end_boundary;
  std::shared_ptr<const source_occurrence_ancestor> parent;
  void restore_context (edit_env_rep&) const;
};

struct source_occurrence_entry {
  std::vector<int> first, last, prefix_last;
  std::vector<int> child_prefix;
  std::shared_ptr<const source_occurrence> occurrence;
  source_occurrence_log child;
};

struct source_occurrence_block {
  path anchor;
  std::vector<source_occurrence_entry> index;
};

// Returns no occurrence when the address is absent or has multiple displays.
std::shared_ptr<const source_occurrence> find_source_occurrence (
  const source_occurrence_log&, path source_address);
std::shared_ptr<const source_occurrence> find_source_occurrence (
  const source_occurrence_log&, path source_address, const tree& source,
  const athena::contracts::evaluation_boundary_handle& preferred= {});

class source_occurrence_capture {
  edit_env_rep& env;
  source_occurrence_capture* parent;
  source_occurrence_log& destination;
  path anchor;
  std::vector<source_occurrence_entry> entries;
  bool committed= false;
public:
  source_occurrence_capture (edit_env_rep&, source_occurrence_log&, path inverse_anchor= path ());
  ~source_occurrence_capture ();
  source_occurrence_capture (const source_occurrence_capture&)= delete;
  source_occurrence_capture& operator= (const source_occurrence_capture&)= delete;
  std::shared_ptr<source_occurrence> record (tree source, path inverse_address);
  void reuse (const source_occurrence_log&);
  std::size_t record_count () const { return entries.size (); }
  void discard_since (std::size_t count) { entries.resize (count); }
  void commit ();
};

// Bridge/Lazy/Concater may hand the same source through several layers. Keep
// that as one occurrence, but retain separate sequential displays of an ARG.
class source_occurrence_scope {
  edit_env_rep& env;
  source_occurrence_scope* previous;
  tree source;
  path address;
  std::shared_ptr<source_occurrence> occurrence;
  std::shared_ptr<const source_occurrence_ancestor> ancestry;
  athena::contracts::evaluation_boundary_handle previous_box_boundary;
public:
  source_occurrence_scope (edit_env_rep&, tree, path inverse_address);
  ~source_occurrence_scope ();
  source_occurrence_scope (const source_occurrence_scope&)= delete;
  source_occurrence_scope& operator= (const source_occurrence_scope&)= delete;
  void finish (bool reached_end= true);
  std::shared_ptr<const source_occurrence_ancestor> enclosing ();
};

// Owner-local captured evaluation state. It is not a transferable snapshot:
// legacy variable/source maps stay on the evaluator's actor.
class evaluation_environment_checkpoint {
  friend class evaluation_context_capture;
  std::shared_ptr<const hashmap<string,tree>> values;
  immer::map<std::string, std::optional<athena::node::frozen_tree>> overrides;
  athena::contracts::evaluation_input_snapshot inputs;
  hashmap<string,path> sources;
  athena::contracts::contract_scope contracts;
  list<hashmap<string,tree>> arguments;
  list<hashmap<string,path>> argument_sources;
  url file_name;
  athena::contracts::package_inclusion_handle package_origin;
  bool secure;
public:
  explicit evaluation_environment_checkpoint (const edit_env_rep&);
  evaluation_environment_checkpoint (const evaluation_environment_checkpoint&,
                                      const source_occurrence&);
  // This is reconstruction of this execution's state, not cache admission.
  // The caller must pair the log with the checkpoint from its original pass.
  std::shared_ptr<const evaluation_environment_checkpoint> at (
    const athena::contracts::contract_event_log&) const;
  // The caller refreshes derived font/page state after any additional overlays.
  void restore (edit_env_rep&) const;
  hashmap<string,tree> variables () const;
  bool contains (string name) const;
  tree value (string name) const;
  std::optional<athena::node::frozen_tree> retained_value (const string& name) const;
  // Editor-visible value, matching get_env_value without changing raw bindings.
  tree effective_value (string name) const;
  const athena::contracts::contract_scope& contract_context () const { return contracts; }
  bool same_field_context (const evaluation_environment_checkpoint&) const;
  std::shared_ptr<const athena::contracts::detached_environment> detach () const;
};

using prepared_context_index= immer::map<athena::contracts::evaluation_boundary_handle,
  std::shared_ptr<const evaluation_environment_checkpoint>>;

// Contexts are prepared by real execution or admitted replay. Getters select
// them without walking event prefixes or copying the complete variable table.
class evaluation_context_capture {
  edit_env_rep& env;
  evaluation_context_capture* previous;
public:
  struct state {
    evaluation_environment_checkpoint current;
    prepared_context_index boundaries;
  };
private:
  state recorded;
public:
  evaluation_context_capture (edit_env_rep&, const evaluation_environment_checkpoint&);
  ~evaluation_context_capture ();
  evaluation_context_capture (const evaluation_context_capture&)= delete;
  evaluation_context_capture& operator= (const evaluation_context_capture&)= delete;
  void write (const athena::contracts::variable_write&);
  void write (const string&, const tree*);
  void mark (const athena::contracts::evaluation_boundary_handle&);
  void rebase ();
  void rebase (const evaluation_environment_checkpoint&);
  state snapshot () const { return recorded; }
  void restore (state value) { recorded= std::move (value); }
  prepared_context_index contexts () const { return recorded.boundaries; }
  std::optional<athena::contracts::evaluation_effects> replay (
    const athena::contracts::contract_event_log&,
    const athena::contracts::variable_lookup&,
    const athena::contracts::external_matcher&,
    const athena::contracts::package_matcher&);
};

// A root evaluation commits variables and native contracts together. Unlike
// temporary evaluation, successful effects belong to the caller's owner.
class evaluation_environment_transaction {
  edit_env_rep& env;
  athena::contracts::contract_scope contracts;
  hashmap<string,tree> values, back;
  athena::contracts::evaluation_inputs inputs;
  hashmap<string,path> sources;
  list<hashmap<string,tree>> arguments;
  list<hashmap<string,path>> argument_sources;
  link_repository links;
  tree recovery;
  url file_name;
  athena::contracts::package_inclusion_handle package_origin;
  bool secure;
  athena::contracts::contract_event_capture* capture;
  std::size_t event_count;
  source_occurrence_capture* occurrences;
  std::size_t occurrence_count;
  std::optional<evaluation_context_capture::state> recorded;
  bool finished= false;
public:
  explicit evaluation_environment_transaction (edit_env_rep&);
  ~evaluation_environment_transaction ();
  void commit () { finished= true; }
  void rollback ();
  evaluation_environment_transaction (const evaluation_environment_transaction&)= delete;
  evaluation_environment_transaction& operator= (const evaluation_environment_transaction&)= delete;
};

// A temporary evaluator shares derived reference/auxiliary outputs, but not
// definitions, published contracts or its caller's bridge effect capture.
class temporary_environment_scope {
  edit_env_rep& env;
  athena::contracts::contract_scope previous_contracts;
  hashmap<string,tree> values, back;
  athena::contracts::evaluation_inputs inputs;
  hashmap<string,path> sources;
  list<hashmap<string,tree>> arguments;
  list<hashmap<string,path>> argument_sources;
  url file_name;
  bool secure;
  athena::contracts::package_inclusion_handle package_origin;
  athena::contracts::contract_event_capture* capture;
  source_occurrence_capture* occurrences;
  evaluation_context_capture* recorded;
  athena::contracts::evaluation_state_scope current;
  temporary_environment_scope (edit_env_rep&, const hashmap<string,tree>&, bool replace);
public:
  explicit temporary_environment_scope (edit_env_rep&);
  temporary_environment_scope (edit_env_rep&, const hashmap<string,tree>&);
  ~temporary_environment_scope ();
  temporary_environment_scope (const temporary_environment_scope&)= delete;
  temporary_environment_scope& operator= (const temporary_environment_scope&)= delete;
};

// Provenance only: package evaluation must not change document-relative URLs.
class package_execution_scope {
  edit_env_rep& env;
  athena::contracts::package_inclusion_handle previous;
public:
  package_execution_scope (edit_env_rep&, url source);
  ~package_execution_scope ();
  package_execution_scope (const package_execution_scope&)= delete;
  package_execution_scope& operator= (const package_execution_scope&)= delete;
};

struct local_environment_binding {
  string name;
  tree value, previous;
  bool previously_present;
};
using local_environment_bindings= std::vector<local_environment_binding>;

bool evaluate_local_environment (edit_env_rep&, const tree&, local_environment_bindings&);
local_environment_bindings table_cell_environment (edit_env_rep&, tree format);

enum class native_math_context { math, display, script, lower, upper, inherited };
native_math_context native_script_position (tree_label, int child);

// Shared lexical scopes for native layout and semantic preparation. Script
// style surrounds the whole slot group; vertical position surrounds one slot.
class native_math_context_scope {
  edit_env_rep& env;
  std::vector<std::pair<string,tree>> previous;
  bool finished= false;
  void bind (string, tree);
public:
  native_math_context_scope (edit_env_rep&, native_math_context);
  // The node has already passed native math validation.
  native_math_context_scope (edit_env_rep&, const tree& node);
  ~native_math_context_scope ();
  native_math_context_scope (const native_math_context_scope&)= delete;
  native_math_context_scope& operator= (const native_math_context_scope&)= delete;
  void finish (bool remain_inside= false);
};

// All WITH execution paths use the same temporary bindings and contract scope.
// Partial execution may deliberately retain the inner state at the cursor.
class local_environment_scope {
  edit_env_rep& env;
  const local_environment_bindings& bindings;
  athena::contracts::contract_scope before;
  std::vector<athena::contracts::contract_target> targets;
  athena::contracts::contract_event_log events;
  athena::contracts::contract_event_capture capture;
  bool finished= false;
  std::size_t installed= 0;
  void resolve_targets ();
  void restore ();
public:
  local_environment_scope (edit_env_rep&, const local_environment_bindings&, bool monitored= false);
  ~local_environment_scope ();
  local_environment_scope (const local_environment_scope&)= delete;
  local_environment_scope& operator= (const local_environment_scope&)= delete;
  void finish (bool remain_inside= false);
};

// Execute a package normally, then scope exactly the values and authored
// contracts that it changed. Other effects of the body remain in the caller.
class scoped_package_environment {
  edit_env_rep& env;
  hashmap<string,tree> previous_values;
  athena::contracts::contract_scope previous_contracts;
  std::vector<athena::contracts::contract_target> targets;
  athena::contracts::contract_event_log events;
  athena::contracts::contract_event_capture capture;
  bool finished= false;
  void restore ();
public:
  scoped_package_environment (edit_env_rep&, tree package);
  ~scoped_package_environment ();
  void finish (bool remain_inside= false);
};

// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once

#include "env.hpp"
#include "drd_std.hpp"

// These records retain actor-owned source/argument handles. They are not worker
// inputs; detached native events refer only to the separate boundary token.
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
  void record (tree source, path inverse_address);
  void reuse (const source_occurrence_log&);
  std::size_t record_count () const { return entries.size (); }
  void discard_since (std::size_t count) { entries.resize (count); }
  void commit ();
};

// Owner-local captured evaluation state. It is not a transferable snapshot:
// legacy variable/source maps stay on the evaluator's actor.
class evaluation_environment_checkpoint {
  hashmap<string,tree> values;
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
  // The caller refreshes derived font/page state after any additional overlays.
  void restore (edit_env_rep&) const;
  hashmap<string,tree> variables () const { return copy (values); }
  bool contains (string name) const {
    auto variables= values;
    return variables->contains (name);
  }
  tree value (string name) const { return values[name]; }
};

// A root evaluation commits variables and native contracts together. Unlike
// temporary evaluation, successful effects belong to the caller's owner.
class evaluation_environment_transaction {
  edit_env_rep& env;
  drd_info owner;
  athena::contracts::contract_scope contracts;
  hashmap<string,tree> values, back, syntax;
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
// definitions, analysis state or its caller's bridge effect capture.
class temporary_environment_scope {
  edit_env_rep& env;
  drd_info previous_drd;
  hashmap<string,tree> values, back;
  athena::contracts::evaluation_inputs inputs;
  hashmap<string,path> sources;
  list<hashmap<string,tree>> arguments;
  list<hashmap<string,path>> argument_sources;
  athena::contracts::package_inclusion_handle package_origin;
  athena::contracts::contract_event_capture* capture;
  source_occurrence_capture* occurrences;
  with_borrowed_drd current;
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

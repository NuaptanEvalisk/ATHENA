// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once

#include "field_layout.hpp"
#include "property_authority.hpp"
#include "inference.hpp"
#include "node_metadata.hpp"
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <atomic>
#include <chrono>
#include <optional>

namespace athena::contracts {

struct analysis_field_input {
  int type= 0, access= 0;
  property_authority type_origin= property_authority::inferred;
  property_authority access_origin= property_authority::inferred;
  property_authority environment_origin= property_authority::inferred;
  node::frozen_tree environment {tree (ATTR)};
  std::string name;
  bool operator == (const analysis_field_input&) const;
};

struct analysis_contract_input {
  std::string name;
  field_layout layout;
  int type= 0, variable_type= 0;
  bool with_like= false;
  property_authority type_origin= property_authority::inferred;
  property_authority layout_origin= property_authority::inferred;
  property_authority variable_type_origin= property_authority::inferred;
  property_authority with_origin= property_authority::inferred;
  std::vector<analysis_field_input> fields;
  bool operator == (const analysis_contract_input&) const;
};

struct analysis_definition_input {
  std::string name;
  node::frozen_tree expression;
};

// Immutable snapshots are reusable across owners. No mutable legacy tree,
// observer, descriptor handle or callback crosses this boundary.
struct macro_analysis_input {
  std::map<tree_label, std::string> symbols;
  std::vector<analysis_contract_input> contracts;
  std::vector<analysis_definition_input> changed;
  std::vector<std::string> removed;
};

struct analysis_field_result {
  int type= 0, access= 0;
  node::frozen_tree environment {tree (ATTR)};
  std::string name;
  field_analysis analysis;
  property_authority type_origin= property_authority::inferred;
  property_authority access_origin= property_authority::inferred;
  property_authority environment_origin= property_authority::inferred;
};

struct analysis_contract_result {
  std::string name;
  field_layout layout;
  int type= 0, variable_type= 0;
  bool with_like= false;
  std::vector<analysis_field_result> fields;
  property_authority type_origin= property_authority::inferred;
  property_authority layout_origin= property_authority::inferred;
  property_authority variable_type_origin= property_authority::inferred;
  property_authority with_origin= property_authority::inferred;
  std::vector<std::string> dependencies;
};

// One analysis owner at a time. Inputs/results may cross threads; mutable
// working trees are thawed privately and never leave this engine.
class macro_analysis_core {
  struct impl;
  std::unique_ptr<impl> state;
public:
  macro_analysis_core ();
  ~macro_analysis_core ();
  // One batch at a time. Only a completed batch exposes results; the worker
  // may resume its private state on another thread, never concurrently.
  void begin (macro_analysis_input input);
  std::optional<std::vector<analysis_contract_result>> resume (
    std::chrono::steady_clock::time_point deadline,
    const std::atomic<bool>* cancelled= nullptr);
};

} // namespace athena::contracts

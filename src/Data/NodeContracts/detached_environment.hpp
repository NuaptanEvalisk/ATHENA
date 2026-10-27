// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once

#include "contract_scope.hpp"
#include "evaluation_inputs.hpp"

class edit_env_rep;
class evaluation_environment_checkpoint;

namespace athena::contracts {

// A field editor receives values and their matching contracts, not the source
// editor's mutable trees, observers, paths or evaluator.
class detached_environment {
  using variables= std::map<std::string, node::frozen_tree>;
  variables values;
  evaluation_input_snapshot inputs;
  contract_snapshot contracts;
  std::vector<variables> arguments;
  node::frozen_tree file_name;
  package_inclusion_handle package;
  bool secure= false;
  friend class ::evaluation_environment_checkpoint;
public:
  explicit detached_environment (node::frozen_tree file): file_name (std::move (file)) {}
  const contract_snapshot& contract_context () const { return contracts; }
  void restore (edit_env_rep&) const;
};

} // namespace athena::contracts

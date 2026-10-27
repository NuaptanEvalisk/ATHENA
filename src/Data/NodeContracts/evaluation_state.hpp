// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once

#include "contract_scope.hpp"
#include "hashmap.hpp"
#include "tree.hpp"

namespace athena::contracts {

// Actor-local evaluator state. edit_env borrows values directly; it must not
// maintain a second variable map. This is not a worker-transfer snapshot.
struct evaluation_state {
  contract_scope contracts;
  hashmap<string,tree> values {UNINIT};

  evaluation_state ()= default;
  explicit evaluation_state (contract_snapshot base):
    contracts (std::move (base)) {}
  evaluation_state (const evaluation_state& other):
    contracts (other.contracts), values (copy (other.values)) {}
  evaluation_state& operator= (const evaluation_state& other) {
    if (this != &other) {
      contracts= other.contracts;
      values= copy (other.values);
    }
    return *this;
  }
  evaluation_state (evaluation_state&&)= default;
  evaluation_state& operator= (evaluation_state&&)= default;
};

} // namespace athena::contracts

// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once

#include "tree.hpp"
#include "hashmap.hpp"
#include "evaluation_inputs.hpp"
#include "contract_scope.hpp"
#include <functional>
#include <memory>

namespace athena::contracts {

// Owner-side capture/publication. No evaluator or DRD is retained. Detached
// analysis is keyed by the captured resolution, including binding identity.
class macro_analysis {
  struct impl;
  std::unique_ptr<impl> state;
public:
  macro_analysis ();
  ~macro_analysis ();
  void synchronize (contract_scope&, const evaluation_input_snapshot& inputs,
                    const hashmap<string, tree>& definitions);
  bool request (const contract_scope&, const evaluation_input_snapshot&,
                const hashmap<string,tree>&, std::function<void ()> notify);
  bool publish_ready (contract_scope&, const evaluation_input_snapshot&,
                      const hashmap<string,tree>&);
  bool pending () const;
};

} // namespace athena::contracts

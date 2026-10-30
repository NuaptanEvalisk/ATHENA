// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once

#include "tree.hpp"
#include "hashmap.hpp"
#include "evaluation_inputs.hpp"
#include "contract_scope.hpp"
#include <functional>
#include <memory>
#include <optional>

namespace athena::contracts {

// Synchronous owner lookup only. An empty result means the paired input has no
// retained frozen payload, not that the variable is absent.
using retained_analysis_value= std::function<std::optional<node::frozen_tree> (const string&)>;

// Owner-side capture/publication. No evaluator or DRD is retained. Detached
// analysis is keyed by the captured resolution, including binding identity.
// request() materializes inputs cooperatively and notifies the owner for another
// slice. Only complete inputs reach the worker; reads admit retained inference
// against current input receipts with a bounded dependency check. Large values
// freeze over several owner slices; input-diff enumeration remains synchronous.
// synchronize() is the explicit unbounded offline path.
class macro_analysis {
  struct impl;
  std::unique_ptr<impl> state;
public:
  macro_analysis ();
  ~macro_analysis ();
  void synchronize (contract_scope&, const evaluation_input_snapshot& inputs,
                    const hashmap<string, tree>& definitions);
  bool request (const contract_scope&, const evaluation_input_snapshot&,
                const hashmap<string,tree>&, std::function<void ()> notify,
                const retained_analysis_value& retained= {});
  // Publish against the exact input identities captured by the preceding request.
  bool publish_ready (contract_scope&);
  bool pending () const;
};

} // namespace athena::contracts

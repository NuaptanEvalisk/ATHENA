// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once

#include "tree.hpp"
#include "hashmap.hpp"
#include "evaluation_inputs.hpp"
#include <memory>

class drd_info_rep;

namespace athena::contracts {

// Owner-side adapter during the scope-lifecycle transition. It prepares
// immutable inputs and projects completed native results into positional
// consumers. The dependency engine itself has no access to this owner's DRD.
class macro_analysis {
  struct impl;
  std::unique_ptr<impl> state;
public:
  explicit macro_analysis (drd_info_rep&);
  ~macro_analysis ();
  void invalidate (const string& binding);
  void synchronize (const evaluation_input_snapshot& inputs,
                    const hashmap<string, tree>& definitions);
};

} // namespace athena::contracts

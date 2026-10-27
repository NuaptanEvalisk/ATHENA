// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once
#include "contract_scope.hpp"
#include "hashmap.hpp"

namespace athena::contracts {

// Process-wide native declarations are immutable after initialization. Each
// evaluator forks a scope from this baseline; it never modifies the baseline.
void initialize_builtins ();
const contract_scope& builtin_contracts ();
bool builtin_label (string name);

// Legacy source parsers need actor-local tree strings, not shared hashmap keys.
hashmap<string,int>& builtin_codes ();

} // namespace athena::contracts

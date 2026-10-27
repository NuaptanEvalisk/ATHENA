// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once
#include "editing_query.hpp"
#include "hashmap.hpp"

namespace athena::contracts {

// Reads the values paired with a prepared contract scope. This is substitution
// for syntax consumers, not evaluation or definition discovery.
struct syntax_query {
  editing_query contracts;
  const hashmap<string,tree>& values;
  tree definition (string name) const;
  tree expand (tree source, path address= path (-1)) const;
};

} // namespace athena::contracts

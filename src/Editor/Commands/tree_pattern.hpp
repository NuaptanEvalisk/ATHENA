// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once
#include "tree.hpp"
#include <vector>

// Structural selectors used by style markup. Selected trees retain their
// source identity and metadata; this syntax is never an executable value.
struct tree_selector {
  enum class kind { invalid, index, tag, depth, descendants, sequence,
                    unite, intersect, subtract, navigate, capture };
  kind operation= kind::invalid;
  string name;
  int count= 0;
  std::vector<tree_selector> branches;
};
array<tree> native_tree_select (tree source, const std::vector<tree_selector>& selectors);

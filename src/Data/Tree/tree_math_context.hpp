// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once

#include "tree_correct.hpp"
#include <unordered_map>

// One correction stage owns the source positions of its rewritten tokens.
// The provider refers to that stage's original tree, never its evolving output.
class math_correction_source {
  athena::contracts::source_environment_preparation prepare;
  struct origin { tree retained; path address; };
  std::unordered_map<const tree_rep*, origin> origins;
  void retain (tree, path);
  tree map (tree, path, const std::function<tree (tree, bool)>&, bool literal);
public:
  explicit math_correction_source (athena::contracts::source_environment_preparation);
  std::shared_ptr<const evaluation_environment_checkpoint> entry (path) const;
  std::shared_ptr<const evaluation_environment_checkpoint> context (tree) const;
  tree map (tree, const std::function<tree (tree, bool)>&);
  int classify (tree) const;
  array<int> types (array<tree>) const;
};

// A detached document keeps its envelope; a standalone snippet owns a generic
// baseline. Each transformed body gets a fresh source-position provider.
tree correct_detached_math (tree,
  const std::function<tree (tree, const correction_context_factory&)>&);

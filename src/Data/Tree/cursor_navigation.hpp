// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once
#include "tree_cursor.hpp"
#include "Data/NodeContracts/editing_query.hpp"

class cursor_navigation {
  athena::contracts::editing_query query;
  bool is_modified_accessible (tree, path, bool activate, bool persistent, bool mapped) const;
  bool accessible_cursor (tree, path, bool mapped) const;
  int lowest_accessible_child (tree) const;
  int highest_accessible_child (tree) const;
  path extreme (tree, int dir) const;
  path pre_correct (tree, path) const;
  path left_correct (tree, path) const;
  path right_correct (tree, path) const;
  path up_correct (tree, path, bool active= true) const;
public:
  explicit cursor_navigation (athena::contracts::editing_query query): query (query) {}
  const athena::contracts::editing_query& contracts () const { return query; }
  bool is_accessible_cursor (tree, path) const;
  // The caller must first admit the exact physical hit's source occurrence.
  bool is_mapped_cursor (tree, path) const;
  path closest_accessible (tree, path, int dir= 0) const;
  path closest_accessible_inside (tree, path, int dir= 0) const;
  bool valid_cursor (tree, path, bool start_flag= false) const;
  path start (tree, path) const;
  path end (tree, path) const;
  path start (tree) const;
  path end (tree) const;
  path correct_cursor (tree, path, bool forwards= false) const;
  path super_correct (tree, path, bool forwards= false) const;
  path shift (tree, path, int dir) const;
  path move_valid (tree, path, bool forwards) const;
  path move_accessible (tree, path, bool forwards) const;
  path move_any (tree, path, bool forwards) const;
};

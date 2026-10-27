// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
// Tree utility entry points borrowing this thread's active native context.
#include "cursor_navigation.hpp"
#include "Data/NodeContracts/execution_state.hpp"

cursor_navigation current_cursor_navigation () {
  return cursor_navigation ({athena::contracts::current_evaluation_state ().contracts});
}

bool is_accessible_cursor (tree t, path p) {
  return current_cursor_navigation ().is_accessible_cursor (t, p);
}
path closest_accessible (tree t, path p, int dir) {
  return current_cursor_navigation ().closest_accessible (t, p, dir);
}
path closest_accessible_inside (tree t, path p, int dir) {
  return current_cursor_navigation ().closest_accessible_inside (t, p, dir);
}
bool valid_cursor (tree t, path p, bool start_flag) {
  return current_cursor_navigation ().valid_cursor (t, p, start_flag);
}
path start (tree t, path p) { return current_cursor_navigation ().start (t, p); }
path end (tree t, path p) { return current_cursor_navigation ().end (t, p); }
path start (tree t) { return current_cursor_navigation ().start (t); }
path end (tree t) { return current_cursor_navigation ().end (t); }
path correct_cursor (tree t, path p, bool forwards) {
  return current_cursor_navigation ().correct_cursor (t, p, forwards);
}
path super_correct (tree t, path p, bool forwards) {
  return current_cursor_navigation ().super_correct (t, p, forwards);
}
path shift (tree t, path p, int dir) {
  return current_cursor_navigation ().shift (t, p, dir);
}

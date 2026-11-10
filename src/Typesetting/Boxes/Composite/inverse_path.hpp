// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once

#include "Core/Types/path.hpp"
#include <boost/container/small_vector.hpp>

// Equivalent to path_less(reverse(a), reverse(b)), without allocating a linked
// list node for every ancestor. The final 0/1 coordinate is a cursor border,
// not an ordinary subtree index when the two paths have different depths.
inline bool inverse_path_less (path a, path b) {
  boost::container::small_vector<int, 16> left, right;
  for (; !is_nil (a); a= a->next) left.push_back (a->item);
  for (; !is_nil (b); b= b->next) right.push_back (b->item);
  auto n= left.size (), m= right.size ();
  if (!n || !m) return false;
  while (n > 1 && m > 1) {
    if (left[n-1] != right[m-1]) return left[n-1] < right[m-1];
    --n; --m;
  }
  if (n == 1 && m == 1) return left[0] < right[0];
  return n == 1 ? left[0] == 0 : right[0] == 1;
}

/******************************************************************************
* MODULE     : resize_viewport_restore.hpp
* DESCRIPTION: Per-view reflow resize viewport invariant
* COPYRIGHT  : (C) 2026  Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef RESIZE_VIEWPORT_RESTORE_HPP
#define RESIZE_VIEWPORT_RESTORE_HPP

#include "basic.hpp"
#include "path.hpp"

#include <cstdint>

struct resize_viewport_restore_state {
  bool pending= false;
  path cursor_path;
  SI cursor_distance_from_top= 0;
  std::uint64_t programmatic_scroll_generation= 0;
  std::uint64_t user_scroll_generation= 0;

  void cancel () {
    pending= false;
    cursor_path= path ();
    cursor_distance_from_top= 0;
    programmatic_scroll_generation= 0;
    user_scroll_generation= 0;
  }

  void arm (path p, SI cursor_y, SI viewport_top,
            std::uint64_t programmatic_generation,
            std::uint64_t user_generation) {
    pending= true;
    cursor_path= copy (p);
    cursor_distance_from_top= viewport_top - cursor_y;
    programmatic_scroll_generation= programmatic_generation;
    user_scroll_generation= user_generation;
  }

  bool matches (path p, std::uint64_t programmatic_generation,
                std::uint64_t user_generation) const {
    return pending && cursor_path == p &&
           programmatic_scroll_generation == programmatic_generation &&
           user_scroll_generation == user_generation;
  }
};

inline bool
resize_viewport_snapshot_current (
  std::uint64_t programmatic_generation,
  std::uint64_t snapshot_programmatic_generation,
  std::uint64_t user_generation,
  std::uint64_t snapshot_user_generation) {
  return programmatic_generation == snapshot_programmatic_generation &&
         user_generation == snapshot_user_generation;
}

inline SI
resize_viewport_target_center_y (SI cursor_y, SI distance_from_top,
                                 SI viewport_height) {
  return cursor_y + distance_from_top - (viewport_height >> 1);
}

#endif // defined RESIZE_VIEWPORT_RESTORE_HPP

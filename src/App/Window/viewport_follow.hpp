/******************************************************************************
* MODULE     : viewport_follow.hpp
* DESCRIPTION: View-owned cursor following shared by compound source actors
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE.
******************************************************************************/

#ifndef ATHENA_VIEWPORT_FOLLOW_HPP
#define ATHENA_VIEWPORT_FOLLOW_HPP

#include <mutex>

enum class viewport_follow_mode { normal, typewriter, hanging };

// No source trees or GUI objects cross actors. The anchor belongs to the view,
// including when several source actors present one compound viewport.
class viewport_follow_state {
  mutable std::mutex mutex_;
  viewport_follow_mode mode_= viewport_follow_mode::normal;
  double anchor_= -1;

public:
  void configure (viewport_follow_mode mode) {
    std::lock_guard<std::mutex> lock (mutex_);
    if (mode_ != mode) { mode_= mode; anchor_= -1; }
  }

  double anchor () const {
    std::lock_guard<std::mutex> lock (mutex_);
    return anchor_;
  }

  bool capture (double fraction) {
    if (!(fraction >= 0 && fraction <= 1)) return false;
    std::lock_guard<std::mutex> lock (mutex_);
    if (mode_ != viewport_follow_mode::hanging || anchor_ == fraction)
      return false;
    anchor_= fraction;
    return true;
  }
};

#endif

/******************************************************************************
* MODULE     : pointer_shake_detector.hpp
* DESCRIPTION: robust pointer shake gesture detection
* COPYRIGHT  : (C) 2026  Nuaptan
* Based on KWin's ShakeDetector, (C) 2023 Vlad Zahorodnii
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
*******************************************************************************/

#ifndef POINTER_SHAKE_DETECTOR_H
#define POINTER_SHAKE_DETECTOR_H

#include "basic.hpp"
#include "tm_timer.hpp"

#include <deque>

class pointer_shake_detector {
  struct history_item {
    SI x;
    SI y;
    time_t timestamp;
  };

  std::deque<history_item> history;

public:
  void reset ();
  bool update (SI x, SI y, time_t timestamp, SI screen_pixel);
};

#endif // defined POINTER_SHAKE_DETECTOR_H

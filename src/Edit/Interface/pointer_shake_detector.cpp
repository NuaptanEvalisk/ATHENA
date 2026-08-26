/******************************************************************************
* MODULE     : pointer_shake_detector.cpp
* DESCRIPTION: robust pointer shake gesture detection
* COPYRIGHT  : (C) 2026  Nuaptan
* Based on KWin's ShakeDetector, (C) 2023 Vlad Zahorodnii
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
*******************************************************************************/

#include "pointer_shake_detector.hpp"

#include <algorithm>
#include <cmath>

static const time_t SHAKE_INTERVAL_MS= 1000;
static const double SHAKE_SENSITIVITY= 4.0;
static const int SHAKE_MINIMUM_DIAGONAL_PIXELS= 100;

static bool
same_sign (SI a, SI b, SI tolerance) {
  return (a >= -tolerance && b >= -tolerance) ||
         (a <= tolerance && b <= tolerance);
}

void
pointer_shake_detector::reset () {
  history.clear ();
}

bool
pointer_shake_detector::update (SI x, SI y, time_t timestamp,
                                SI screen_pixel) {
  while (!history.empty () &&
         timestamp - history.front ().timestamp >= SHAKE_INTERVAL_MS)
    history.pop_front ();

  if (history.size () >= 2) {
    history_item& last= history[history.size () - 1];
    const history_item& previous= history[history.size () - 2];
    SI tolerance= std::max (screen_pixel, (SI) 1);
    if (same_sign (last.x - previous.x, x - last.x, tolerance) &&
        same_sign (last.y - previous.y, y - last.y, tolerance)) {
      last= history_item { x, y, timestamp };
      return false;
    }
  }

  history.push_back (history_item { x, y, timestamp });
  if (history.size () < 2) return false;

  SI left= history.front ().x;
  SI right= left;
  SI top= history.front ().y;
  SI bottom= top;
  double distance= 0.0;
  for (size_t i=1; i<history.size (); ++i) {
    double dx= (double) history[i].x - (double) history[i-1].x;
    double dy= (double) history[i].y - (double) history[i-1].y;
    distance += std::hypot (dx, dy);
    left= std::min (left, history[i].x);
    right= std::max (right, history[i].x);
    top= std::min (top, history[i].y);
    bottom= std::max (bottom, history[i].y);
  }

  double diagonal= std::hypot ((double) right - (double) left,
                               (double) bottom - (double) top);
  double minimum_diagonal=
    (double) SHAKE_MINIMUM_DIAGONAL_PIXELS *
    (double) std::max (screen_pixel, (SI) 1);
  if (diagonal < minimum_diagonal) return false;

  if (distance / diagonal > SHAKE_SENSITIVITY) {
    history.clear ();
    return true;
  }
  return false;
}

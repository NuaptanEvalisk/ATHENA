/******************************************************************************
* MODULE     : QTMInertialScroll.hpp
* DESCRIPTION: Shared wheel inertia for editor and compound document viewports
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once

#include <QTimer>
#include <functional>

class QWheelEvent;

class QTMInertialScroll {
  QTimer timer_;
  double velocity_x_= 0, velocity_y_= 0, friction_= 0.90;
  std::function<void(int, int)> apply_;
public:
  QTMInertialScroll (QObject* owner, std::function<void(int, int)> apply);
  // Returns false when disabled, leaving the native wheel fallback to the view.
  bool wheel (QWheelEvent* event);
  void stop ();
};

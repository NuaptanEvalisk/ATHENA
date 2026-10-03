/******************************************************************************
* MODULE     : QTMInertialScroll.cpp
* DESCRIPTION: Shared preference-driven wheel momentum and decay
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "QTMInertialScroll.hpp"
#include "boot.hpp"
#include <QWheelEvent>
#include <cmath>
#include <utility>

QTMInertialScroll::QTMInertialScroll (
  QObject* owner, std::function<void(int, int)> apply):
  timer_ (owner), apply_ (std::move (apply)) {
  timer_.setTimerType (Qt::PreciseTimer);
  QObject::connect (&timer_, &QTimer::timeout, owner, [this] {
    if (std::abs (velocity_x_) < 1.0 && std::abs (velocity_y_) < 1.0) {
      stop ();
      return;
    }
    apply_ (qRound (velocity_x_), qRound (velocity_y_));
    velocity_x_ *= friction_;
    velocity_y_ *= friction_;
  });
}

void QTMInertialScroll::stop () {
  timer_.stop ();
  velocity_x_= velocity_y_= 0;
}

bool QTMInertialScroll::wheel (QWheelEvent* event) {
  if (get_user_preference ("inertial scrolling") != "on") {
    stop ();
    return false;
  }
  const QPoint delta= event->pixelDelta ().isNull () ?
    event->angleDelta () / 8 : event->pixelDelta ();
  friction_= as_double (get_user_preference ("inertial scrolling friction", "0.90"));
  const double sensitivity= as_double (get_user_preference ("inertial scrolling sensitivity", "1.0"));
  velocity_x_ += delta.x () * 0.15 * sensitivity;
  velocity_y_ += delta.y () * 0.15 * sensitivity;
  if (!timer_.isActive ()) timer_.start (16);
  apply_ (delta.x (), delta.y ());
  event->accept ();
  return true;
}

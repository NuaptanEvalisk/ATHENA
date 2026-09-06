/******************************************************************************
* MODULE     : commutative_diagram_geometry.hpp
* DESCRIPTION: Collision-aware placement of native diagram labels
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* See the file LICENSE in the root directory.
******************************************************************************/

#ifndef ATHENA_COMMUTATIVE_DIAGRAM_GEOMETRY_HPP
#define ATHENA_COMMUTATIVE_DIAGRAM_GEOMETRY_HPP

#include <QPainterPath>
#include <QRectF>
#include <algorithm>
#include <cmath>

// All coordinates are physical pixels, independent of the screen's zoom.
// Qt owns curve intersection; no sampled-line approximation of the arrow is
// used to decide whether a typeset label collides with it.
inline QPointF
cd_clear_label_position (const QPainterPath& ink, QPointF centre,
                         QPointF direction, QSizeF size, qreal padding) {
  qreal length= std::hypot (direction.x (), direction.y ());
  if (ink.isEmpty () || length <= 0) return centre;
  direction /= length;
  size += QSizeF (2*padding, 2*padding);
  auto collides= [&] (qreal distance) {
    QPointF p= centre + direction*distance;
    return ink.intersects (QRectF (p-QPointF (size.width ()/2, size.height ()/2),
                                   size));
  };
  if (!collides (0)) return centre;

  // Beyond this support plane the whole label is outside the arrow bounds.
  // Keep a known-clear upper bound throughout the search, including for loops.
  QRectF bounds= ink.boundingRect ();
  qreal high= 0;
  for (QPointF p: {bounds.topLeft (), bounds.topRight (),
                  bounds.bottomLeft (), bounds.bottomRight ()})
    high= std::max (high, QPointF::dotProduct (p-centre, direction));
  high += (std::abs (direction.x ())*size.width () +
           std::abs (direction.y ())*size.height ())/2 + 1;
  qreal low= 0;
  for (int i=0; i<64 && high-low > 0.25; ++i) {
    qreal middle= low + (high-low)/2;
    if (collides (middle)) low= middle;
    else high= middle;
  }
  return centre + direction*high;
}

#endif

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
#include <utility>

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

inline std::pair<qreal,qreal>
cd_visible_curve_interval (const QPainterPath& curve,
                           const QRectF& source,
                           const QRectF& target) {
  auto boundary= [&] (const QRectF& rect, bool fromStart) {
    qreal endpoint= fromStart ? 0.0 : 1.0;
    if (!rect.contains (curve.pointAtPercent (endpoint))) return endpoint;

    const int samples= 128;
    qreal inside= endpoint;
    for (int i=1; i<=samples; ++i) {
      qreal outside= fromStart ? ((qreal) i) / samples
                               : 1.0 - ((qreal) i) / samples;
      if (!rect.contains (curve.pointAtPercent (outside))) {
        qreal low= std::min (inside, outside);
        qreal high= std::max (inside, outside);
        for (int j=0; j<48; ++j) {
          qreal middle= (low + high) / 2.0;
          bool contained= rect.contains (curve.pointAtPercent (middle));
          if (fromStart) {
            if (contained) low= middle;
            else high= middle;
          }
          else {
            if (contained) high= middle;
            else low= middle;
          }
        }
        return fromStart ? high : low;
      }
      inside= outside;
    }
    return fromStart ? 1.0 : 0.0;
  };

  qreal start= boundary (source, true);
  qreal end= boundary (target, false);
  return {start, end};
}

#endif

/******************************************************************************
* MODULE     : commutative_diagram_geometry_test.cpp
* DESCRIPTION: Native diagram label clearance using Qt curve intersections
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* See the file LICENSE in the root directory.
******************************************************************************/

#include <QtTest/QtTest>
#include <QPainterPathStroker>
#include "Typeset/Concat/commutative_diagram_geometry.hpp"

class DiagramGeometryTest: public QObject {
  Q_OBJECT
private slots:
  void clearance () {
    // Straight, curved, loop and multi-shaft edges, in both orientations.
    for (int shape=0; shape<4; ++shape) {
      QPainterPath path (QPointF (-100, 0));
      if (shape == 0) path.lineTo (100, 0);
      if (shape == 1) path.cubicTo (-50, 90, 50, 90, 100, 0);
      if (shape == 2) path.cubicTo (130, -130, 130, 130, -100, 0);
      if (shape == 3) {
        path.lineTo (100, 0);
        path.moveTo (-100, 12); path.lineTo (100, 12);
      }
      QPainterPathStroker stroker;
      stroker.setWidth (3);
      QPainterPath ink= stroker.createStroke (path);
      for (int side: {-1, 1})
        for (QSizeF size: {QSizeF (8, 12), QSizeF (65, 35), QSizeF (120, 70)}) {
          QPointF centre= path.pointAtPercent (0.5);
          QPointF direction (0, side);
          QPointF p= cd_clear_label_position (ink, centre, direction, size, 4);
          QSizeF padded= size + QSizeF (8, 8);
          QRectF label (p-QPointF (padded.width ()/2, padded.height ()/2), padded);
          QVERIFY (!ink.intersects (label));
          QVERIFY (QPointF::dotProduct (p-centre, direction) >= 0);
          QCOMPARE (p.x (), centre.x ());
          if (shape == 0)
            QVERIFY (std::abs (std::abs (p.y ())-(size.height ()/2+5.5)) < 0.26);
        }
    }
  }

  void diagonalAndReverse () {
    QPainterPath path (QPointF (-100, -60));
    path.cubicTo (-33, -20, 33, 20, 100, 60);
    QPainterPathStroker stroker;
    stroker.setWidth (2);
    auto ink= stroker.createStroke (path);
    QPointF normal (-0.6, 1);
    QPointF p= cd_clear_label_position (ink, {}, normal, {90, 40}, 3);
    QSizeF size (96, 46);
    QVERIFY (!ink.intersects (QRectF (p-QPointF (48, 23), size)));
    QVERIFY (std::abs (QPointF::dotProduct (p, QPointF (1, .6))) < 1e-9);
    QCOMPARE (p, cd_clear_label_position (
      stroker.createStroke (path.toReversed ()), {}, normal, {90, 40}, 3));
  }

  void noDisplacementWhenClear () {
    QPainterPath empty;
    QCOMPARE (cd_clear_label_position (empty, {1, 2}, {0, 1}, {20, 10}, 3),
              QPointF (1, 2));
    QPainterPath rect;
    rect.addRect (-10, -10, 20, 20);
    QCOMPARE (cd_clear_label_position (rect, {0, 40}, {0, 1}, {20, 10}, 3),
              QPointF (0, 40));
  }
};

QTEST_APPLESS_MAIN (DiagramGeometryTest)
#include "commutative_diagram_geometry_test.moc"

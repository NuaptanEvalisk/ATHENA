/******************************************************************************
* MODULE     : commutative_diagram_layout_test.cpp
* DESCRIPTION: commutative-diagram rendered geometry regressions
*******************************************************************************/

#include <QtTest/QtTest>

#include "Boxes/construct.hpp"
#include "Concat/commutative_diagram_geometry.hpp"
#include "frame.hpp"

class CommutativeDiagramLayoutTest: public QObject {
  Q_OBJECT

private slots:
  void trimsBezierOutsideRenderedVertices ();
  void protrudingChildrenExpandDiagramBounds ();
};

void
CommutativeDiagramLayoutTest::trimsBezierOutsideRenderedVertices () {
  QPainterPath curve (QPointF (-2.0, 0.0));
  curve.cubicTo (QPointF (-0.8, 0.7), QPointF (0.8, -0.7),
                 QPointF (2.0, 0.0));
  QRectF source (-2.4, -0.7, 1.45, 1.4);
  QRectF target (0.95, -0.7, 1.45, 1.4);

  auto interval= cd_visible_curve_interval (curve, source, target);
  QVERIFY (interval.first > 0.0);
  QVERIFY (interval.second < 1.0);
  QVERIFY (interval.first < interval.second);
  QVERIFY (!source.contains (
    curve.pointAtPercent (qMin (1.0, interval.first + 0.002))));
  QVERIFY (!target.contains (
    curve.pointAtPercent (qMax (0.0, interval.second - 0.002))));
}

void
CommutativeDiagramLayoutTest::protrudingChildrenExpandDiagramBounds () {
  array<box> children;
  array<SI> xs, ys;
  children << empty_box (path (), -240, -330, 1260, 370);
  xs << 0;
  ys << 0;
  frame coordinates= scaling (100.0, point (500.0, 0.0));
  box diagram= commutative_diagram_box (
    path (), children, xs, ys, coordinates, 1000, 500);

  QVERIFY (diagram->x1 <= -240);
  QVERIFY (diagram->x2 >= 1260);
  QVERIFY (diagram->y1 <= -330);
  QVERIFY (diagram->y2 >= 370);
  QVERIFY (diagram->w () >= 1500);
}

QTEST_MAIN (CommutativeDiagramLayoutTest)
#include "commutative_diagram_layout_test.moc"

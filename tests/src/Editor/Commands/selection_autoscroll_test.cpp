/******************************************************************************
* MODULE     : selection_autoscroll_test.cpp
* DESCRIPTION: tests for bounded selection edge scrolling
* COPYRIGHT  : (C) 2026  Nuaptan
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include <QtTest/QtTest>

#include "selection_autoscroll.hpp"

class TestSelectionAutoscroll: public QObject {
  Q_OBJECT

private slots:
  void remainsStillAwayFromEdges();
  void followsEdgePenetration();
  void boundsOutsideMotion();
  void keepsAxesIndependentWithoutEdgeZone();
  void projectsSelectionHitTestingInsideDocument();
  void recognizesEquivalentWholeDocumentRanges();
};

void TestSelectionAutoscroll::remainsStillAwayFromEdges() {
  QCOMPARE (selection_autoscroll_delta (500, 0, 1000, 20, 40), 0);
}

void TestSelectionAutoscroll::followsEdgePenetration() {
  QCOMPARE (selection_autoscroll_delta (10, 0, 1000, 20, 40), -10);
  QCOMPARE (selection_autoscroll_delta (990, 0, 1000, 20, 40), 10);
  QCOMPARE (selection_autoscroll_delta (0, 0, 1000, 20, 40), -20);
  QCOMPARE (selection_autoscroll_delta (1000, 0, 1000, 20, 40), 20);
}

void TestSelectionAutoscroll::boundsOutsideMotion() {
  QCOMPARE (selection_autoscroll_delta (-500, 0, 1000, 20, 40), -40);
  QCOMPARE (selection_autoscroll_delta (1500, 0, 1000, 20, 40), 40);
}

void TestSelectionAutoscroll::keepsAxesIndependentWithoutEdgeZone() {
  QCOMPARE (selection_autoscroll_delta (500, 0, 1000, 0, 40), 0);
  QCOMPARE (selection_autoscroll_delta (-12, 0, 1000, 0, 40), -12);
  QCOMPARE (selection_autoscroll_delta (1012, 0, 1000, 0, 40), 12);
}

void TestSelectionAutoscroll::projectsSelectionHitTestingInsideDocument() {
  QCOMPARE (selection_hit_test_x (-500, 100, 900), 100);
  QCOMPARE (selection_hit_test_x (400, 100, 900), 400);
  QCOMPARE (selection_hit_test_x (1500, 100, 900), 899);
  QCOMPARE (selection_hit_test_x (1500, 100, 100), 100);
}

void TestSelectionAutoscroll::recognizesEquivalentWholeDocumentRanges() {
  path root= path (2);
  path first_cursor= root * 0 * 0;
  path last_cursor= root * 3 * 1;

  QVERIFY (selection_covers_range (root * 0, root * 1,
                                   first_cursor, last_cursor));
  QVERIFY (selection_covers_range (first_cursor, last_cursor,
                                   first_cursor, last_cursor));
  QVERIFY (!selection_covers_range (root * 1 * 0, last_cursor,
                                    first_cursor, last_cursor));
  QVERIFY (!selection_covers_range (first_cursor, root * 2 * 1,
                                    first_cursor, last_cursor));
}

QTEST_MAIN (TestSelectionAutoscroll)
#include "selection_autoscroll_test.moc"

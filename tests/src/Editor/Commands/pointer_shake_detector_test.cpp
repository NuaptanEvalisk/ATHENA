/******************************************************************************
* MODULE     : pointer_shake_detector_test.cpp
* DESCRIPTION: tests for pointer shake gesture detection
* COPYRIGHT  : (C) 2026  Nuaptan
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
*******************************************************************************/

#include <QtTest/QtTest>

#include "pointer_shake_detector.hpp"

class TestPointerShakeDetector: public QObject {
  Q_OBJECT

private slots:
  void rejectsStraightMotion();
  void rejectsSmallJitter();
  void rejectsMotionOutsideTimeWindow();
  void acceptsSubstantialBacktracking();
  void resetsGestureHistory();
};

static bool
feed (pointer_shake_detector& detector, int x, int y, time_t t) {
  return detector.update (x * 256, y * 256, t, 256);
}

void TestPointerShakeDetector::rejectsStraightMotion() {
  pointer_shake_detector detector;
  for (int i=0; i<=10; ++i)
    QVERIFY (!feed (detector, i * 30, 0, i * 20));
}

void TestPointerShakeDetector::rejectsSmallJitter() {
  pointer_shake_detector detector;
  for (int i=0; i<30; ++i)
    QVERIFY (!feed (detector, (i & 1)? 20: 0, 0, i * 20));
}

void TestPointerShakeDetector::rejectsMotionOutsideTimeWindow() {
  pointer_shake_detector detector;
  for (int i=0; i<12; ++i)
    QVERIFY (!feed (detector, (i & 1)? 140: 0, 0, i * 1100));
}

void TestPointerShakeDetector::acceptsSubstantialBacktracking() {
  pointer_shake_detector detector;
  bool detected= false;
  for (int i=0; i<12 && !detected; ++i)
    detected= feed (detector, (i & 1)? 140: 0, 0, i * 50);
  QVERIFY (detected);
}

void TestPointerShakeDetector::resetsGestureHistory() {
  pointer_shake_detector detector;
  for (int i=0; i<5; ++i)
    QVERIFY (!feed (detector, (i & 1)? 140: 0, 0, i * 50));
  detector.reset ();
  for (int i=0; i<5; ++i)
    QVERIFY (!feed (detector, (i & 1)? 140: 0, 0, 500 + i * 50));
}

QTEST_MAIN (TestPointerShakeDetector)
#include "pointer_shake_detector_test.moc"

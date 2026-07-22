/******************************************************************************
* MODULE     : QTMPerformanceMonitor.hpp
* DESCRIPTION: Per-canvas rendering FPS and editing latency monitor
* COPYRIGHT  : (C) 2026  Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMPERFORMANCEMONITOR_HPP
#define QTMPERFORMANCEMONITOR_HPP

#include <QElapsedTimer>
#include <QRect>
#include <QTimer>
#include <QVector>

class QPaintEvent;
class QPainter;
class QTMWidget;

class QTMPerformanceMonitor {
public:
  explicit QTMPerformanceMonitor (QTMWidget* owner);

  void refresh ();
  void recordEditingInput ();
  void finishPaint (QPaintEvent* event, QPainter& painter);
  bool inputBatchActive () const { return inputBatch; }
  void setInputBatchActive (bool active) { inputBatch= active; }

private:
  struct LatencySample {
    qint64 completedNs;
    double milliseconds;
  };

  QTMWidget* owner;
  QElapsedTimer clock;
  QTimer hudTimer;
  QVector<qint64> frameTimes;
  QVector<qint64> pendingInputTimes;
  QVector<LatencySample> latencySamples;
  bool enabled= false;
  bool hudRefreshPending= false;
  bool inputBatch= false;

  bool isDocumentCanvas () const;
  void pruneSamples (qint64 nowNs);
  QRect hudRect () const;
  void drawHud (QPainter& painter, qint64 nowNs);
};

#endif // QTMPERFORMANCEMONITOR_HPP

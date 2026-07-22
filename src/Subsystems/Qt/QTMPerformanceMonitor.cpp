/******************************************************************************
* MODULE     : QTMPerformanceMonitor.cpp
* DESCRIPTION: Per-canvas rendering FPS and editing latency monitor
* COPYRIGHT  : (C) 2026  Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMPerformanceMonitor.hpp"
#include "QTMWidget.hpp"
#include "qt_simple_widget.hpp"
#include "scheme.hpp"

#include <QFontDatabase>
#include <QFontMetrics>
#include <QPaintEvent>
#include <QPainter>
#include <QRegion>

#include <algorithm>
#include <cmath>

QTMPerformanceMonitor::QTMPerformanceMonitor (QTMWidget* owner2):
  owner (owner2) {
  clock.start ();
  hudTimer.setInterval (250);
  hudTimer.setTimerType (Qt::CoarseTimer);
  QObject::connect (&hudTimer, &QTimer::timeout, owner, [this] () {
    if (!enabled || !isDocumentCanvas ()) {
      refresh ();
      return;
    }
    hudRefreshPending= true;
    owner->surface ()->update (hudRect ());
  });
}

bool
QTMPerformanceMonitor::isDocumentCanvas () const {
  return owner != nullptr && !is_nil (owner->tmwid) &&
         owner->tm_widget () != nullptr &&
         owner->tm_widget ()->is_editor_widget () && !owner->isEmbedded ();
}

QRect
QTMPerformanceMonitor::hudRect () const {
  QFont font= QFontDatabase::systemFont (QFontDatabase::FixedFont);
  qreal appPointSize= owner->surface ()->font ().pointSizeF ();
  if (appPointSize > 0.0)
    font.setPointSizeF (std::max (8.0, appPointSize * 0.82));
  QFontMetrics metrics (font);
  int width= metrics.horizontalAdvance (
    QStringLiteral ("Edit: 9999.9 ms   p95: 9999.9 ms")) + 18;
  int height= metrics.height () * 2 + 14;
  QRect bounds= owner->surface ()->rect ();
  width= std::min (width, std::max (1, bounds.width () - 12));
  height= std::min (height, std::max (1, bounds.height () - 12));
  return QRect (6, std::max (6, bounds.height () - height - 6),
                width, height);
}

void
QTMPerformanceMonitor::refresh () {
  QRect oldRect= hudRect ();
  bool shouldEnable=
    get_preference ("rendering performance monitor", "off") == "on" &&
    isDocumentCanvas ();
  if (shouldEnable == enabled) {
    if (enabled && !hudTimer.isActive ()) hudTimer.start ();
    if (enabled) {
      hudRefreshPending= true;
      owner->surface ()->update (oldRect);
    }
    return;
  }

  enabled= shouldEnable;
  hudRefreshPending= false;
  frameTimes.clear ();
  pendingInputTimes.clear ();
  latencySamples.clear ();
  clock.restart ();
  if (enabled) {
    hudTimer.start ();
    hudRefreshPending= true;
  }
  else hudTimer.stop ();
  owner->surface ()->update (oldRect);
}

void
QTMPerformanceMonitor::recordEditingInput () {
  if (!enabled || !clock.isValid ()) return;
  pendingInputTimes << clock.nsecsElapsed ();
  if (pendingInputTimes.size () > 128)
    pendingInputTimes.remove (0, pendingInputTimes.size () - 128);
}

void
QTMPerformanceMonitor::pruneSamples (qint64 nowNs) {
  static const qint64 frameWindowNs= 1000000000LL;
  static const qint64 latencyWindowNs= 5000000000LL;
  static const qint64 pendingTimeoutNs= 5000000000LL;
  while (!frameTimes.isEmpty () &&
         nowNs - frameTimes.front () > frameWindowNs)
    frameTimes.remove (0);
  while (!latencySamples.isEmpty () &&
         nowNs - latencySamples.front ().completedNs > latencyWindowNs)
    latencySamples.remove (0);
  while (!pendingInputTimes.isEmpty () &&
         nowNs - pendingInputTimes.front () > pendingTimeoutNs)
    pendingInputTimes.remove (0);
}

void
QTMPerformanceMonitor::drawHud (QPainter& painter, qint64 nowNs) {
  pruneSamples (nowNs);
  double fps= 0.0;
  if (frameTimes.size () >= 2) {
    qint64 span= frameTimes.back () - frameTimes.front ();
    if (span > 0) fps= (frameTimes.size () - 1) * 1000000000.0 / span;
  }

  QString latencyText= QStringLiteral ("Edit: --   p95: --");
  if (!latencySamples.isEmpty ()) {
    QVector<double> values;
    values.reserve (latencySamples.size ());
    for (const LatencySample& sample: latencySamples)
      values << sample.milliseconds;
    std::sort (values.begin (), values.end ());
    int p95Index= std::max (0, (int) std::ceil (values.size () * 0.95) - 1);
    double latest= latencySamples.back ().milliseconds;
    latencyText= QStringLiteral ("Edit: %1 ms   p95: %2 ms")
      .arg (latest, 0, 'f', 1).arg (values[p95Index], 0, 'f', 1);
  }

  QFont font= QFontDatabase::systemFont (QFontDatabase::FixedFont);
  qreal appPointSize= owner->surface ()->font ().pointSizeF ();
  if (appPointSize > 0.0)
    font.setPointSizeF (std::max (8.0, appPointSize * 0.82));
  font.setWeight (QFont::DemiBold);
  QFontMetrics metrics (font);
  QRect hud= hudRect ();
  int textLeft= hud.left () + 9;
  int firstBaseline= hud.top () + 7 + metrics.ascent ();

  painter.save ();
  painter.setRenderHint (QPainter::Antialiasing, true);
  painter.setPen (QColor (255, 255, 255, 70));
  painter.setBrush (QColor (20, 24, 28, 190));
  painter.drawRoundedRect (hud, 3, 3);
  painter.setFont (font);
  painter.setPen (QColor (245, 247, 248));
  painter.drawText (textLeft, firstBaseline,
                    QStringLiteral ("FPS: %1").arg (fps, 0, 'f', 1));
  painter.setPen (QColor (209, 235, 230));
  painter.drawText (textLeft, firstBaseline + metrics.height (), latencyText);
  painter.restore ();
}

void
QTMPerformanceMonitor::finishPaint (QPaintEvent* event, QPainter& painter) {
  if (!enabled || !clock.isValid ()) return;
  QRect hud= hudRect ().intersected (owner->surface ()->rect ());
  bool hudOnly= hudRefreshPending && event->region () == QRegion (hud);
  hudRefreshPending= false;
  qint64 nowNs= clock.nsecsElapsed ();
  if (!hudOnly) {
    frameTimes << nowNs;
    for (qint64 inputNs: pendingInputTimes)
      latencySamples << LatencySample {
        nowNs, (nowNs - inputNs) / 1000000.0
      };
    pendingInputTimes.clear ();
  }
  drawHud (painter, nowNs);
}

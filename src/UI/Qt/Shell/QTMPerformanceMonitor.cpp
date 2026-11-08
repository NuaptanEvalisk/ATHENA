/******************************************************************************
* MODULE     : QTMPerformanceMonitor.cpp
* DESCRIPTION: Per-canvas rendering FPS and editing latency monitor
* COPYRIGHT  : (C) 2026  Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "UI/Qt/Shell/QTMPerformanceMonitor.hpp"
#include "boot.hpp"
#include "UI/Qt/Widgets/QTMWidget.hpp"
#include "UI/Qt/Toolkit/qt_simple_widget.hpp"

#include <QEnterEvent>
#include <QEvent>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QPaintEvent>
#include <QPainter>
#include <QToolButton>
#include <QWidget>

#include <algorithm>
#include <cmath>

class QTMPerformanceHudOverlay final: public QWidget {
  friend class QTMPerformanceMonitor;
  QTMPerformanceMonitor& monitor;
  QToolButton* minimizeButton;
  QToolButton* closeButton;
  QToolButton* restoreButton;
  bool collapsed= false;
  bool hovered= false;

  void positionAtViewportCorner () {
    QWidget* host= parentWidget ();
    if (host == nullptr) return;
    const QSize wanted= collapsed ? QSize (44, 24) : monitor.hudSize ();
    const QSize fitted (
      std::min (wanted.width (), std::max (1, host->width () - 12)),
      std::min (wanted.height (), std::max (1, host->height () - 12)));
    setGeometry (6, std::max (6, host->height () - fitted.height () - 6),
                 fitted.width (), fitted.height ());
    minimizeButton->setGeometry (std::max (0, width () - 46), 2, 22, 20);
    closeButton->setGeometry (std::max (0, width () - 23), 2, 22, 20);
    restoreButton->setGeometry (rect ());
  }

  void showControls () {
    minimizeButton->setVisible (!collapsed && hovered);
    closeButton->setVisible (!collapsed && hovered);
    restoreButton->setVisible (collapsed);
  }

  void setCollapsed (bool value) {
    collapsed= value;
    hovered= false;
    positionAtViewportCorner ();
    showControls ();
    update ();
  }

protected:
  bool eventFilter (QObject* watched, QEvent* event) override {
    if (watched == parentWidget () &&
        (event->type () == QEvent::Resize ||
         event->type () == QEvent::Show))
      positionAtViewportCorner ();
    return QWidget::eventFilter (watched, event);
  }

  void enterEvent (QEnterEvent* event) override {
    hovered= true;
    showControls ();
    QWidget::enterEvent (event);
  }

  void leaveEvent (QEvent* event) override {
    // The mouse may be entering one of our child buttons. Defer the check
    // until Qt has delivered its enter event to that child, so the controls
    // never blink away while the pointer moves over them (also on Wayland).
    QTimer::singleShot (0, this, [this] {
      hovered= underMouse () || minimizeButton->underMouse () ||
               closeButton->underMouse ();
      showControls ();
    });
    QWidget::leaveEvent (event);
  }

  void paintEvent (QPaintEvent*) override {
    QPainter painter (this);
    if (collapsed) {
      painter.setPen (QColor (255, 255, 255, 70));
      painter.setBrush (QColor (20, 24, 28, 190));
      painter.drawRect (rect ().adjusted (0, 0, -1, -1));
    }
    else monitor.drawHud (painter, monitor.clock.nsecsElapsed ());
  }

public:
  QTMPerformanceHudOverlay (QTMPerformanceMonitor& monitor2, QWidget* host):
    QWidget (host), monitor (monitor2),
    minimizeButton (new QToolButton (this)),
    closeButton (new QToolButton (this)),
    restoreButton (new QToolButton (this)) {
    setObjectName ("athenaPerformanceHud");
    setFocusPolicy (Qt::NoFocus);
    setMouseTracking (true);
    for (QToolButton* button: {minimizeButton, closeButton, restoreButton}) {
      button->setFocusPolicy (Qt::NoFocus);
      button->setAutoRaise (true);
    }
    minimizeButton->setText (QString::fromUtf8 ("−"));
    minimizeButton->setToolTip (tr ("Minimize performance HUD"));
    closeButton->setText (QString::fromUtf8 ("×"));
    closeButton->setToolTip (tr ("Hide performance HUD (Preferences)"));
    restoreButton->setText (QStringLiteral ("FPS"));
    restoreButton->setToolTip (tr ("Expand performance HUD"));
    connect (minimizeButton, &QToolButton::clicked, this,
             [this] { setCollapsed (true); });
    connect (restoreButton, &QToolButton::clicked, this,
             [this] { setCollapsed (false); });
    connect (closeButton, &QToolButton::clicked, this, [this] {
      set_preference ("rendering performance monitor", "off");
      QTMWidget::refreshAllPerformanceMonitors ();
      monitor.refresh ();
    });
    host->installEventFilter (this);
    positionAtViewportCorner ();
    showControls ();
    hide ();
  }
};

QTMPerformanceMonitor::QTMPerformanceMonitor (QTMWidget* owner2):
  QTMPerformanceMonitor (owner2->surface (), owner2->viewport ()) {
  owner= owner2;
}

QTMPerformanceMonitor::QTMPerformanceMonitor (QWidget* surface2):
  QTMPerformanceMonitor (surface2, surface2) {}

QTMPerformanceMonitor::QTMPerformanceMonitor (
    QWidget* surface2, QWidget* viewport2):
  surface (surface2), viewport (viewport2) {
  clock.start ();
  hud= new QTMPerformanceHudOverlay (*this, viewport);
  hudTimer.setInterval (250);
  hudTimer.setTimerType (Qt::CoarseTimer);
  QObject::connect (&hudTimer, &QTimer::timeout, viewport, [this] () {
    if (!enabled || !isDocumentCanvas ()) {
      refresh ();
      return;
    }
    if (hud) hud->update ();
  });
}

QTMPerformanceMonitor::~QTMPerformanceMonitor () { delete hud.data (); }

bool
QTMPerformanceMonitor::isDocumentCanvas () const {
  if (owner == nullptr) return surface->isVisible ();
  if (owner->externallyPresented () || !owner->isVisible () || is_nil (owner->tmwid))
    return false;
  qt_simple_widget_rep* widget= owner->tm_widget ();
  return widget != nullptr && widget->is_editor_widget () &&
         !owner->isEmbedded ();
}

QSize
QTMPerformanceMonitor::hudSize () const {
  QFont font= QFontDatabase::systemFont (QFontDatabase::FixedFont);
  qreal appPointSize= surface->font ().pointSizeF ();
  if (appPointSize > 0.0)
    font.setPointSizeF (std::max (8.0, appPointSize * 0.82));
  QFontMetrics metrics (font);
  int width= metrics.horizontalAdvance (
    QStringLiteral ("Edit: 9999.9 ms   p95: 9999.9 ms")) + 18;
  int height= metrics.height () * 2 + 14;
  width= std::min (width, std::max (1, viewport->width () - 12));
  height= std::min (height, std::max (1, viewport->height () - 12));
  return QSize (width, height);
}

void
QTMPerformanceMonitor::refresh () {
  bool shouldEnable=
    get_preference ("rendering performance monitor", "off") == "on" &&
    isDocumentCanvas ();
  if (enabled != shouldEnable) {
    enabled= shouldEnable;
    frameTimes.clear ();
    pendingInputTimes.clear ();
    latencySamples.clear ();
    clock.restart ();
  }
  if (enabled) {
    if (!hudTimer.isActive ()) hudTimer.start ();
    if (hud) {
      hud->positionAtViewportCorner ();
      hud->show ();
      hud->raise ();
      hud->update ();
    }
  }
  else {
    hudTimer.stop ();
    if (hud) hud->hide ();
  }
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
  qreal appPointSize= surface->font ().pointSizeF ();
  if (appPointSize > 0.0)
    font.setPointSizeF (std::max (8.0, appPointSize * 0.82));
  font.setWeight (QFont::DemiBold);
  QFontMetrics metrics (font);
  QRect hud (QPoint (0, 0), hudSize ());
  int textLeft= hud.left () + 9;
  int firstBaseline= hud.top () + 7 + metrics.ascent ();

  painter.save ();
  painter.setRenderHint (QPainter::Antialiasing, false);
  painter.setPen (QColor (255, 255, 255, 70));
  painter.setBrush (QColor (20, 24, 28, 190));
  painter.drawRect (hud.adjusted (0, 0, -1, -1));
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
  (void) event;
  (void) painter;
  if (!enabled || !clock.isValid ()) return;
  qint64 nowNs= clock.nsecsElapsed ();
  // The HUD now lives on the viewport, not the document surface. Its own
  // repaint never reaches this callback and cannot count as an editor frame.
  frameTimes << nowNs;
  for (qint64 inputNs: pendingInputTimes)
    latencySamples << LatencySample {
      nowNs, (nowNs - inputNs) / 1000000.0
    };
  pendingInputTimes.clear ();
}

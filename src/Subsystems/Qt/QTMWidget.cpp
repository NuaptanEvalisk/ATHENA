
/******************************************************************************
* MODULE     : QTMWidget.cpp
* DESCRIPTION: QT Texmacs widget class
* COPYRIGHT  : (C) 2008 Massimiliano Gubinelli and Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMWidget.hpp"
#include "QTMRenderService.hpp"
#include "QTMDocumentSearchBar.hpp"
#include "qt_gui.hpp"
#include "tm_window.hpp"
#include "qt_utilities.hpp"
#include "qt_simple_widget.hpp"
#include "converter.hpp"
#include "boot.hpp"
#include "scheme.hpp"
#include "new_view.hpp"
#include "ATHENA/Data/new_buffer.hpp"
#include "ATHENA/Data/vault_image_insertion.hpp"
#include "editor.hpp"
#include "Interface/edit_graphics.hpp"
#include "renderer.hpp"
#include "qt_renderer.hpp"
#include "QTMApplication.hpp"
#include "QTMKeyboardEvent.hpp"
#include "QTMNeighborhoodsPane.hpp"

#include "config.h"

#include <QDebug>
#include <QEvent>
#include <QResizeEvent>
#include <QKeyEvent>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QFocusEvent>
#include <QPainter>
#include <QApplication>
#include <QInputMethod>
#include <QNativeGestureEvent>
#include <QStyleHints>
#include <QScrollBar>
#include <QTouchEvent>

#include <QBuffer>
#include <QMimeData>
#include <QByteArray>
#include <QImage>
#include <QUrl>
#include <QFileInfo>

#include <algorithm>
#include <cmath>

static long int QTMWcounter = 0; // debugging hack

namespace {

class scoped_event_check_suppression {
  qt_gui_rep* gui;
  bool restore;

public:
  explicit scoped_event_check_suppression (qt_gui_rep* gui2)
    : gui (gui2), restore (gui2 != nullptr && gui2->event_checking_enabled ()) {
    if (gui != nullptr) gui->set_check_events (false);
  }

  ~scoped_event_check_suppression () {
    if (gui != nullptr) gui->set_check_events (restore);
  }
};

}

static string
gestureEventTypeName (QEvent::Type type) {
  if (type == QEvent::TouchBegin) return "TouchBegin";
  if (type == QEvent::TouchEnd) return "TouchEnd";
  if (type == QEvent::TouchUpdate) return "TouchUpdate";
  if (type == QEvent::TouchCancel) return "TouchCancel";
  if (type == QEvent::NativeGesture) return "NativeGesture";
  if (type == QEvent::Gesture) return "Gesture";
  if (type == QEvent::GestureOverride) return "GestureOverride";
  return "type_" * as_string ((int) type);
}

/*! Constructor.
 
  \param _parent The parent QWidget.
  \param _tmwid the TeXmacs widget who owns this object.
 */
QTMWidget::QTMWidget (QWidget* _parent, qt_widget _tmwid)
: QTMScrollView (_parent), tmwid (_tmwid),  imwidget (NULL),
  preediting (false), performanceMonitor (this)
{
  setObjectName (to_qstring ("QTMWidget" * as_string (QTMWcounter++)));// What is this for? (maybe only debugging?)
  setFocusPolicy (Qt::StrongFocus);
  setAttribute (Qt::WA_InputMethodEnabled);
  surface ()->setMouseTracking (true);
  surface ()->setAcceptDrops (true);
  setAttribute (Qt::WA_AcceptTouchEvents);
  viewport ()->setAttribute (Qt::WA_AcceptTouchEvents);
  surface ()->setAttribute (Qt::WA_AcceptTouchEvents);

  grabGesture (Qt::PanGesture);
  grabGesture (Qt::PinchGesture);
  grabGesture (Qt::SwipeGesture);
  connect (horizontalScrollBar (), &QScrollBar::actionTriggered,
           this, [this] (int) { notifyUserScroll (); });
  connect (verticalScrollBar (), &QScrollBar::actionTriggered,
           this, [this] (int) { notifyUserScroll (); });
  connect (horizontalScrollBar (), &QScrollBar::sliderMoved,
           this, [this] (int) { notifyUserScroll (); });
  connect (verticalScrollBar (), &QScrollBar::sliderMoved,
           this, [this] (int) { notifyUserScroll (); });
  fractionalScrollSettleTimer.setSingleShot (true);
  fractionalScrollSettleTimer.setInterval (80);
  fractionalScrollSettleTimer.setTimerType (Qt::CoarseTimer);
  connect (&fractionalScrollSettleTimer, &QTimer::timeout, this, [this] () {
    if (athena_qt_is_closing () || is_nil (tmwid) || !isVisible ()) return;
    tm_widget ()->invalidate_all ();
    the_gui->need_update ();
  });
  cursorBlinkTimer.setTimerType (Qt::CoarseTimer);
  connect (&cursorBlinkTimer, &QTimer::timeout, this, [this] () {
    if (get_preference ("blinking cursor", "on") != "on" ||
        !hasFocus () || is_nil (tmwid) || !tm_widget ()->is_editor_widget ()) {
      refreshCursorBlinking (false);
      return;
    }
    setCursorBlinkVisible (!cursorBlinkVisible);
  });
  QStyleHints* hints= QGuiApplication::styleHints ();
  if (hints != nullptr)
    connect (hints, &QStyleHints::cursorFlashTimeChanged, this,
             [this] (int) { refreshCursorBlinking (true); });

  performanceMonitor.refresh ();

  surface ()->setTabletTracking (true);
  for (QWidget *parent = surface()->parentWidget();
       parent != nullptr; parent = parent->parentWidget())
    parent->setTabletTracking(true);

  if (DEBUG_QT)
    debug_qt << "Creating " << from_qstring(objectName()) << " of widget "
             << (tm_widget() ? tm_widget()->type_as_string() : "NULL") << LF;
  //part 1/2 of the fix for 43373
  if (!isEmbedded ())
    QApplication::postEvent(this, new QFocusEvent(QEvent::FocusIn, Qt::OtherFocusReason));
}

QTMWidget::~QTMWidget () {
  if (DEBUG_QT)
    debug_qt << "Destroying " << from_qstring(objectName()) << " of widget "
             << (tm_widget() ? tm_widget()->type_as_string() : "NULL") << LF;
}

void
QTMWidget::setCursorBlinkVisible (bool visible) {
  if (cursorBlinkVisible == visible) return;
  cursorBlinkVisible= visible;
  if (athena_qt_is_closing () || is_nil (tmwid)) return;
  tm_widget ()->handle_cursor_blink (visible);
  the_gui->need_update ();
}

void
QTMWidget::refreshCursorBlinking (bool restart) {
  bool enabled= get_preference ("blinking cursor", "on") == "on" &&
                hasFocus () && !is_nil (tmwid) &&
                tm_widget ()->is_editor_widget ();
  QStyleHints* hints= QGuiApplication::styleHints ();
  int flashTime= hints == nullptr ? 0 : hints->cursorFlashTime ();
  if (!enabled || flashTime <= 0) {
    cursorBlinkTimer.stop ();
    setCursorBlinkVisible (true);
    return;
  }
  cursorBlinkTimer.setInterval (std::max (1, flashTime / 2));
  if (restart || !cursorBlinkTimer.isActive ()) {
    cursorBlinkTimer.stop ();
    setCursorBlinkVisible (true);
    cursorBlinkTimer.start ();
  }
}

void
QTMWidget::refreshAllCursorBlinking () {
  const auto widgets= QApplication::allWidgets ();
  for (QWidget* widget: widgets)
    if (QTMWidget* canvas= qobject_cast<QTMWidget*> (widget))
      canvas->refreshCursorBlinking (true);
}

void
QTMWidget::refreshAllPerformanceMonitors () {
  const auto widgets= QApplication::allWidgets ();
  for (QWidget* widget: widgets)
    if (QTMWidget* canvas= qobject_cast<QTMWidget*> (widget))
      canvas->performanceMonitor.refresh ();
}

bool
QTMWidget::isEmbedded () const {
  return tm_widget() -> is_embedded_widget ();
}

qt_simple_widget_rep*
QTMWidget::tm_widget () const { 
  return concrete_simple_widget (tmwid); 
}

void
QTMWidget::refreshEmbeddedBackingStore () {
  if (athena_qt_is_closing () || is_nil (tmwid) || !isVisible ()) return;
  if (!isEmbedded ()) return;

  // A modal preview may be opened while qt_gui_rep::update() is processing
  // the command which created it.  In that nested event loop, the outer
  // update's interrupted flag cannot be cleared until the dialog closes.
  // Embedded output is immutable and must complete its local repaint instead
  // of inheriting that unrelated interruption state.
  scoped_event_check_suppression suppress_event_checks (the_gui);
  tm_widget ()->repaint_invalid_regions ();
  if (surface () != nullptr) surface ()->update ();
}

void
QTMWidget::notifyUserScroll () {
  if (athena_qt_is_closing ()) return;
  if (is_nil (tmwid)) return;
  tm_widget ()->handle_user_scroll (texmacs_time ());
}

void
QTMWidget::scrollContentsBy (int dx, int dy) {
  QTMScrollView::scrollContentsBy (dx,dy);
  updateInputMethodCursorRectangle ();
  scheduleFractionalScrollSettle ();
  if (athena_qt_is_closing ()) return;
  if (internalScrollChange ()) return;
  the_gui->force_update();
  if (isEmbedded ()) scheduleEmbeddedScrollRefresh ();
  // we force an update of the internal state to be in sync with the moving
  // scrollbars
}

void
QTMWidget::scheduleFractionalScrollSettle () {
  qreal dpr= devicePixelRatioF ();
  if (std::fabs (dpr - std::round (dpr)) <= 0.001) return;
  fractionalScrollSettleTimer.start ();
}

void
QTMWidget::scheduleEmbeddedScrollRefresh () {
  if (embeddedScrollRefreshPending) return;
  embeddedScrollRefreshPending= true;
  QTimer::singleShot (0, this, [this] () {
    embeddedScrollRefreshPending= false;
    // Output widgets keep their document in a backing pixmap.  A scrollbar
    // value change updates the logical origin immediately, but may occur while
    // the global GUI update is already active.  Synchronize this widget after
    // Qt has finished the scrollbar event so the backing pixmap follows the
    // new origin before it is painted.
    refreshEmbeddedBackingStore ();
  });
}

void 
QTMWidget::resizeEvent (QResizeEvent* event) {
  (void) event;
  checkDprChange();
  // Is this ok?
  //coord2 s = from_qsize (event->size());
  //the_gui -> process_resize (tm_widget(), s.x1, s.x2);

  // the_gui->force_update();

  //FIXME: I would like to have a force_update here but this causes a failed
  //assertion in TeXmacs since the at the boot not every internal structure is
  //initialized at this point. It seems not too difficult to fix but I
  //postpone this to discuss with Joris. 
  //
  //Not having a force_update results in some lack of sync of the surface
  //while the user is actively resizing with the mouse.
}

void
QTMWidget::resizeEventBis () {
  coord2 s = from_qsize (surface()->size());
  the_gui -> process_resize (tm_widget(), s.x1, s.x2);
  updateInputMethodCursorRectangle ();
}

/*!
 In the current implementation repainting takes place during the call to
 the widget's repaint_invalid_regions() method in the_gui::update. All
 we have to do is to take the backing store and put it on screen according
 to the QRegion marked invalid. 
 CHECK: Maybe just putting onscreen all the region bounding rectangles might 
 be less expensive.
*/
void
QTMWidget::surfacePaintEvent (QPaintEvent *event, QWidget *surfaceWidget) {
  (void) surfaceWidget;
  if (checkDprChange()) return;
  {
    std::shared_ptr<QTMRenderConnection> connection;
    {
      std::lock_guard<std::mutex> guard (tm_widget ()->render_connection_lock);
      connection= tm_widget ()->render_connection;
    }
    if (connection != nullptr) {
      QTMSharedFrame next= connection->acquireLatestFrame ();
      if (next) renderedFrame= std::move (next);
    }
  }
  QPainter p (surface());
  if ((viewPinchActive || viewPinchCommitPending) &&
      !viewPinchPreview.isNull()) {
    drawViewPinchPreview (p);
  }
  else if (renderedFrame) {
    p.drawImage (QPointF (0.0, 0.0), renderedFrame.image ());
  }
  else {
    qreal pixel_ratio= lastPixelRatio;
    QRegion reg= event->region();
    QRegion::const_iterator it;
    QRectF qr;
    for (it= reg.begin (); it != reg.end (); ++it) {
      qr= *it;
      p.drawPixmap (qr, *(tm_widget()->backingPixmap),
		    QRectF (pixel_ratio * qr.x(),
			    pixel_ratio * qr.y(),
			    pixel_ratio * qr.width(),
			    pixel_ratio * qr.height()));
    }
  }
  performanceMonitor.finishPaint (event, p);
}

void
QTMWidget::presentRenderedFrame (
  std::uint64_t bufferGeneration, std::uint64_t frameGeneration,
  render_damage damage) {
  if (bufferGeneration < renderedBufferGeneration ||
      (bufferGeneration == renderedBufferGeneration &&
       frameGeneration <= renderedFrameGeneration))
    return;
  renderedBufferGeneration= bufferGeneration;
  renderedFrameGeneration= frameGeneration;

  double ratio= surface ()->devicePixelRatio ();
  int x1= static_cast<int> (std::floor (damage.x1 / ratio));
  int y1= static_cast<int> (std::floor (damage.y1 / ratio));
  int x2= static_cast<int> (std::ceil (damage.x2 / ratio));
  int y2= static_cast<int> (std::ceil (damage.y2 / ratio));
  QRect damaged_rect (x1, y1, x2 - x1, y2 - y1);
  damaged_rect= damaged_rect.intersected (surface ()->rect ());
  if (damaged_rect.isEmpty ()) surface ()->update ();
  else surface ()->update (damaged_rect);
}

bool
QTMWidget::checkDprChange() {
  double currentPixelRatio= surface()->devicePixelRatio();
  if (lastPixelRatio == 0.0) {
    lastPixelRatio = currentPixelRatio;;
    return false;
  }
  if (lastPixelRatio == currentPixelRatio) {
    return false;
  }
  lastPixelRatio = currentPixelRatio;
  //cout << "QTMWidget " << (long) this
  //     << ", device pixel ratio changed to " << lastPixelRatio << LF;
  devicePixelRatioChanged();
  return true;
}

void
QTMWidget::devicePixelRatioChanged () {
  tm_widget ()->handle_device_pixel_ratio_changed ();
  tm_widget ()->invalidate_all ();
  needs_update ();
}

static double
athena_clamp_view_zoom (double zoom) {
  if (zoom < 0.04) return 0.04;
  if (zoom > 25.0) return 25.0;
  return zoom;
}

bool
QTMWidget::gesturesSupportedForViewZoom () const {
#ifdef Q_OS_WIN
  return true;
#else
  return QApplication::platformName().startsWith ("wayland");
#endif
}

bool
QTMWidget::activateOwningViewForGesture (const char* source) const {
  if (is_nil (tmwid) || !tm_widget()->is_editor_widget ()) return false;
  if (tm_widget ()->handle_activate_owning_view ()) {
    if (gestureDebugEnabled ())
      cout << "[gesture] activate-view route=" << source << LF;
    return true;
  }
  if (gestureDebugEnabled ())
    cout << "[gesture] activate-view route=" << source
         << " view=(not-found)" << LF;
  return false;
}

bool
QTMWidget::inActiveGraphicsMode () const {
  try {
    if (!as_bool (call ("defined?", symbol_object ("in-active-graphics?"))))
      return false;
    return as_bool (call ("in-active-graphics?"));
  }
  catch (...) {
    return false;
  }
}

bool
QTMWidget::gestureDebugEnabled () const {
  QByteArray value= qgetenv ("ATHENA_GESTURE_DEBUG");
  return !value.isEmpty () && value != "0";
}

void
QTMWidget::logGesture (const char* phase, const char* route, double scale,
                       const QPointF& focal) const {
  if (!gestureDebugEnabled ()) return;
  cout << "[gesture] " << phase << " route=" << route
       << " platform=" << from_qstring (QApplication::platformName())
       << " focal=(" << as_string ((double) focal.x()) << ","
       << as_string ((double) focal.y()) << ") scale="
       << as_string (scale) << " zoom0="
       << as_string (viewPinchStartZoom) << LF;
}

void
QTMWidget::beginViewPinchZoom (const QPointF& focal, const char* source) {
  if (is_nil (tmwid) || tm_widget()->backingPixmap == NULL) return;
  activateOwningViewForGesture (source);
  viewPinchActive= true;
  viewPinchCommitPending= false;
  viewPinchStartZoom= athena_clamp_view_zoom (
    as_double (call ("get-window-zoom-factor")));
  viewPinchScale= 1.0;
  viewPinchCommittedScale= 1.0;
  viewPinchPixelRatio= surface()->devicePixelRatio();
  viewPinchFocal= focal;
  viewPinchStartOrigin= origin();
  viewPinchPreview= !renderedFrame
    ? *(tm_widget()->backingPixmap)
    : QPixmap::fromImage (renderedFrame.image ());
  logGesture ("begin", source, viewPinchScale, viewPinchFocal);
}

void
QTMWidget::updateViewPinchZoom (double scale, const QPointF& focal,
                                const char* source) {
  if (!viewPinchActive)
    beginViewPinchZoom (focal, source);
  if (!viewPinchActive) return;
  double minScale= 0.04 / viewPinchStartZoom;
  double maxScale= 25.0 / viewPinchStartZoom;
  if (scale < minScale) scale= minScale;
  if (scale > maxScale) scale= maxScale;
  viewPinchScale= scale;
  viewPinchFocal= focal;
  logGesture ("update", source, viewPinchScale, viewPinchFocal);
  surface()->update();
}

void
QTMWidget::finishViewPinchZoom (bool commit, const char* source) {
  if (!viewPinchActive) return;
  double finalZoom= athena_clamp_view_zoom (
    normal_zoom (athena_clamp_view_zoom (viewPinchStartZoom *
                                         viewPinchScale)));
  double appliedScale= finalZoom / viewPinchStartZoom;
  bool shouldCommit= commit && std::fabs (appliedScale - 1.0) > 0.001;
  logGesture (shouldCommit ? "commit" : "cancel", source, appliedScale,
              viewPinchFocal);
  viewPinchActive= false;
  if (!shouldCommit) {
    viewPinchCommitPending= false;
    viewPinchPreview= QPixmap();
    surface()->update();
    return;
  }

  QPoint newOrigin (
    qRound ((viewPinchStartOrigin.x() + viewPinchFocal.x()) *
            appliedScale - viewPinchFocal.x()),
    qRound ((viewPinchStartOrigin.y() + viewPinchFocal.y()) *
            appliedScale - viewPinchFocal.y()));
  viewPinchCommitPending= true;
  viewPinchCommittedScale= appliedScale;
  setPendingOriginAfterNextExtents (newOrigin);
  activateOwningViewForGesture (source);
  call ("change-zoom-factor", object (finalZoom));
}

bool
QTMWidget::handleNativeGestureEvent (QNativeGestureEvent* event) {
  if (!gesturesSupportedForViewZoom () || is_nil (tmwid)) {
    if (gestureDebugEnabled ()) {
      cout << "[gesture] native-event route=dispatch ignored "
           << "supported=" << (gesturesSupportedForViewZoom () ? "yes" : "no")
           << " widget=" << (is_nil (tmwid) ? "nil" : "ok")
           << LF;
    }
    return false;
  }
  QPointF focal= surface()->mapFromGlobal (
    event->globalPosition().toPoint());
  if (gestureDebugEnabled ()) {
    cout << "[gesture] native-event route=dispatch type="
         << gestureEventTypeName (static_cast<QEvent::Type> (event->gestureType ()))
         << " value=" << as_string (event->value())
         << " finger-count=" << event->fingerCount () << " focal=("
         << as_string ((double) focal.x()) << ","
         << as_string ((double) focal.y()) << ")" << LF;
  }
  activateOwningViewForGesture ("native-dispatch");
  if (inActiveGraphicsMode ()) {
    coord2 pt= from_qpoint (focal.toPoint() + origin());
    array<double> data;
    if (event->gestureType() == Qt::BeginNativeGesture) {
      the_gui->process_mouse (tm_widget(), "pinch-start", pt.x1, pt.x2, 0,
                              texmacs_time (), data);
      nativeLegacyPinchActive= true;
      nativeLegacyPinchScale= 1.0;
      logGesture ("begin", "native-graphics", 1.0, focal);
      event->accept();
      return true;
    }
    if (event->gestureType() == Qt::ZoomNativeGesture) {
      if (!nativeLegacyPinchActive) {
        the_gui->process_mouse (tm_widget(), "pinch-start", pt.x1, pt.x2, 0,
                                texmacs_time (), data);
        nativeLegacyPinchActive= true;
        nativeLegacyPinchScale= 1.0;
      }
      nativeLegacyPinchScale *= 1.0 + event->value();
      data << nativeLegacyPinchScale;
      the_gui->process_mouse (tm_widget(), "scale", pt.x1, pt.x2, 0,
                              texmacs_time (), data);
      logGesture ("update", "native-graphics", nativeLegacyPinchScale, focal);
      event->accept();
      return true;
    }
    if (event->gestureType() == Qt::EndNativeGesture &&
        nativeLegacyPinchActive) {
      the_gui->process_mouse (tm_widget(), "pinch-end", pt.x1, pt.x2, 0,
                              texmacs_time (), data);
      nativeLegacyPinchActive= false;
      nativeLegacyPinchScale= 1.0;
      logGesture ("end", "native-graphics", 1.0, focal);
      event->accept();
      return true;
    }
    return false;
  }

  switch (event->gestureType()) {
    case Qt::SwipeNativeGesture: {
      QPointF delta= event->delta();
      double dx= delta.x();
      if (std::fabs (dx) < 0.001) dx= event->value();
      if (std::fabs (dx) < 0.001) {
        logGesture ("native-swipe-ignored", "neighborhood", dx, focal);
        event->accept();
        return true;
      }
      int direction= dx < 0.0 ? -1 : 1;
      logGesture (direction < 0 ? "swipe-left" : "swipe-right",
                  "native-neighborhood", dx, focal);
      neighborhoods_open_neighbor (direction);
      event->accept();
      return true;
    }
    case Qt::SmartZoomNativeGesture:
      if (event->fingerCount() == 3) {
        logGesture ("tap-cycle", "native-neighborhood", event->value(),
                    focal);
        neighborhoods_cycle_selected ();
        event->accept();
        return true;
      }
      if (gestureDebugEnabled ())
        logGesture ("smart-zoom-ignored", "neighborhood", event->value(),
                    focal);
      return false;
    case Qt::BeginNativeGesture:
      logGesture ("native-begin", "native-view", event->value(), focal);
      return false;
    case Qt::ZoomNativeGesture:
      updateViewPinchZoom (viewPinchScale * (1.0 + event->value()), focal,
                           "native-view");
      event->accept();
      return true;
    case Qt::EndNativeGesture:
      finishViewPinchZoom (true, "native-view");
      event->accept();
      return true;
    default:
      if (gestureDebugEnabled ())
        logGesture ("native-ignored", "native-unknown", 1.0, focal);
      return false;
  }
}

bool
QTMWidget::handlePinchGestureForViewZoom (QPinchGesture* pinch) {
  if (!gesturesSupportedForViewZoom ()) return false;
  activateOwningViewForGesture ("qt-pinch-dispatch");
  if (inActiveGraphicsMode ()) return false;
  QPointF focal= pinch->hasHotSpot()
    ? surface()->mapFromGlobal (pinch->hotSpot().toPoint())
    : QPointF (surface()->width() / 2.0, surface()->height() / 2.0);
  if (pinch->state() == Qt::GestureStarted) {
    beginViewPinchZoom (focal, "qt-pinch-view");
    return true;
  }
  if (pinch->state() == Qt::GestureCanceled) {
    finishViewPinchZoom (false, "qt-pinch-view");
    return true;
  }
  if (pinch->state() == Qt::GestureFinished) {
    updateViewPinchZoom ((double) pinch->totalScaleFactor(), focal,
                         "qt-pinch-view");
    finishViewPinchZoom (true, "qt-pinch-view");
    return true;
  }
  if (pinch->changeFlags() & QPinchGesture::ScaleFactorChanged) {
    updateViewPinchZoom ((double) pinch->totalScaleFactor(), focal,
                         "qt-pinch-view");
    return true;
  }
  return false;
}

bool
QTMWidget::handleNeighborhoodTouchTap (QEvent* event) {
  if (!gesturesSupportedForViewZoom () || inActiveGraphicsMode ()) {
    if (gestureDebugEnabled ()) {
      cout << "[gesture] touch-ignored route=touch-neighborhood "
           << "reason="
           << (!gesturesSupportedForViewZoom () ? "unsupported-platform" : "active-graphics")
           << LF;
    }
    return false;
  }
  QTouchEvent* touch= static_cast<QTouchEvent*> (event);
  const QList<QEventPoint> points= touch->points ();
  if (gestureDebugEnabled ()) {
    cout << "[gesture] touch-input route=touch-neighborhood type="
         << gestureEventTypeName (event->type())
         << " points=" << points.size () << " candidate="
         << (neighborhoodTapCandidate ? "true" : "false")
         << " supported=" << (gesturesSupportedForViewZoom () ? "yes" : "no")
         << " active-graphics=" << (inActiveGraphicsMode () ? "yes" : "no")
         << LF;
  }
  if (event->type() == QEvent::TouchBegin) {
    neighborhoodTapCandidate= points.size () == 3;
    if (gestureDebugEnabled ()) {
      logGesture (neighborhoodTapCandidate ? "touch-begin" : "touch-begin-ignored",
                  "touch-neighborhood", 1.0,
                  points.isEmpty () ? QPointF () : points.front ().position ());
    }
    if (!neighborhoodTapCandidate) return false;
    QPointF center;
    for (const QEventPoint& point: points) center += point.position ();
    neighborhoodTapStartCenter= center / 3.0;
    neighborhoodTapTimer.restart ();
    event->accept ();
    return true;
  }

  if (!neighborhoodTapCandidate) return false;
  if (event->type() == QEvent::TouchUpdate) {
    if (gestureDebugEnabled ()) {
      logGesture ("touch-update", "touch-neighborhood", 1.0,
                  points.isEmpty () ? QPointF () : points.front ().position ());
    }
    if (points.size () != 3) {
      neighborhoodTapCandidate= false;
      if (gestureDebugEnabled ())
        logGesture ("touch-update-ignored", "touch-neighborhood", 1.0,
                    points.isEmpty () ? QPointF () : points.front ().position ());
      return false;
    }
    QPointF center;
    for (const QEventPoint& point: points) center += point.position ();
    center /= 3.0;
    if ((center - neighborhoodTapStartCenter).manhattanLength () > 28.0)
      neighborhoodTapCandidate= false;
    event->accept ();
    return true;
  }

  if (event->type() == QEvent::TouchEnd ||
      event->type() == QEvent::TouchCancel) {
    if (gestureDebugEnabled ()) {
      logGesture ("touch-end", "touch-neighborhood", 1.0,
                  neighborhoodTapStartCenter);
    }
    bool trigger= event->type() == QEvent::TouchEnd &&
                  neighborhoodTapTimer.isValid () &&
                  neighborhoodTapTimer.elapsed () < 650;
    neighborhoodTapCandidate= false;
    if (trigger) {
      logGesture ("tap-cycle", "touch-neighborhood", 1.0,
                  neighborhoodTapStartCenter);
      neighborhoods_cycle_selected ();
      event->accept ();
      return true;
    }
  }
  return false;
}

bool
QTMWidget::handleNeighborhoodMiddleClick (QMouseEvent* event) {
  if (!gesturesSupportedForViewZoom () || inActiveGraphicsMode ()) return false;
  if (event->button () != Qt::MiddleButton) return false;
  if (get_preference ("disable unix primary selection", "off") != "on")
    return false;

  QPointF focal= event->position ();
  logGesture ("tap-cycle", "middle-click-neighborhood", 1.0, focal);
  neighborhoods_cycle_selected ();
  event->accept ();
  return true;
}

bool
QTMWidget::handleNeighborhoodKeyShortcut (QKeyEvent* event) {
  if (is_nil (tmwid) || inActiveGraphicsMode ()) return false;
  Qt::KeyboardModifiers mods= event->modifiers ();
  Qt::KeyboardModifiers wanted= Qt::ControlModifier | Qt::AltModifier;
  Qt::KeyboardModifiers extras= Qt::ShiftModifier | Qt::MetaModifier;
  if ((mods & wanted) != wanted || (mods & extras) != Qt::NoModifier)
    return false;

  int direction= 0;
  if (event->key () == Qt::Key_Left) direction= -1;
  else if (event->key () == Qt::Key_Right) direction= 1;
  else return false;

  logGesture (direction < 0 ? "shortcut-left" : "shortcut-right",
              "keyboard-neighborhood", direction, QPointF ());
  neighborhoods_open_neighbor (direction);
  event->accept ();
  return true;
}

bool
QTMWidget::handleNeighborhoodWheelSwipe (QWheelEvent* event) {
  if (!gesturesSupportedForViewZoom () || inActiveGraphicsMode ()) return false;

  Qt::KeyboardModifiers mods= event->modifiers ();
  if ((mods & Qt::ShiftModifier) == 0 ||
      (mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) != 0) {
    neighborhoodWheelSwipeAccum= 0.0;
    return false;
  }

  QPoint pixels= event->pixelDelta ();
  QPoint angles= event->angleDelta ();
  bool pixelBased= !pixels.isNull ();
  double dx= pixelBased ? (double) pixels.x () : ((double) angles.x ()) / 8.0;
  double dy= pixelBased ? (double) pixels.y () : ((double) angles.y ()) / 8.0;

  if (std::fabs (dx) < 0.001) return false;
  if (std::fabs (dx) < std::fabs (dy)) return false;

  if (neighborhoodWheelSwipeCooldown.isValid () &&
      neighborhoodWheelSwipeCooldown.elapsed () < 450) {
    event->accept ();
    return true;
  }

  neighborhoodWheelSwipeAccum += dx;
  double threshold= pixelBased ? 80.0 : 15.0;
  if (gestureDebugEnabled ()) {
    cout << "[gesture] shift-wheel route=wheel-neighborhood dx="
         << as_string (dx) << " dy=" << as_string (dy)
         << " accum=" << as_string (neighborhoodWheelSwipeAccum)
         << " threshold=" << as_string (threshold) << LF;
  }

  if (std::fabs (neighborhoodWheelSwipeAccum) >= threshold) {
    int direction= neighborhoodWheelSwipeAccum < 0.0 ? -1 : 1;
    logGesture (direction < 0 ? "swipe-left" : "swipe-right",
                "shift-wheel-neighborhood", neighborhoodWheelSwipeAccum,
                event->position ());
    neighborhoods_open_neighbor (direction);
    neighborhoodWheelSwipeAccum= 0.0;
    neighborhoodWheelSwipeCooldown.restart ();
  }

  event->accept ();
  return true;
}

void
QTMWidget::drawViewPinchPreview (QPainter& p) const {
  if (viewPinchPreview.isNull()) return;
  p.save ();
  double scale= viewPinchCommitPending
    ? viewPinchCommittedScale : viewPinchScale;
  p.fillRect (surface()->rect(), surface()->palette().brush (QPalette::Base));
  p.setRenderHint (QPainter::SmoothPixmapTransform, true);
  QRectF target (0.0, 0.0,
                 viewPinchPreview.width() / viewPinchPixelRatio,
                 viewPinchPreview.height() / viewPinchPixelRatio);
  QRectF source (0.0, 0.0,
                 viewPinchPreview.width(), viewPinchPreview.height());
  p.translate (viewPinchFocal);
  p.scale (scale, scale);
  p.translate (-viewPinchFocal);
  p.drawPixmap (target, viewPinchPreview, source);
  p.restore ();
}

void
QTMWidget::finishGestureZoomCommitPreview () {
  if (!viewPinchCommitPending) return;
  viewPinchCommitPending= false;
  viewPinchPreview= QPixmap();
  surface()->update();
}


void
QTMWidget::setCursorPos (QPoint pos) {
  if (cursor_pos == pos) return;
  cursor_pos= pos;
  refreshCursorBlinking (true);
  updateInputMethodCursorRectangle ();
}

void
QTMWidget::updateInputMethodCursorRectangle () const {
  if (!hasFocus ()) return;
  QInputMethod* im= QApplication::inputMethod ();
  if (im == nullptr) return;
  im->update (Qt::ImCursorRectangle);
}

void
setShiftPreference (int key_code, char shifted) {
  set_user_preference ("shift-" * as_string (key_code), string (shifted));
}

bool
hasShiftPreference (int key_code) {
  return has_user_preference ("shift-" * as_string (key_code));
}

string
getShiftPreference (char key_code) {
  return get_user_preference ("shift-" * as_string (key_code));
}

void
QTMWidget::keyPressEvent (QKeyEvent* event) {
  refreshCursorBlinking (true);
  Qt::KeyboardModifiers commandModifiers=
    event->modifiers () & (Qt::ControlModifier | Qt::ShiftModifier |
                           Qt::AltModifier | Qt::MetaModifier);
  if (event->key () == Qt::Key_F &&
      commandModifiers == Qt::ControlModifier) {
    document_search_open ();
    return;
  }
  if (handleNeighborhoodKeyShortcut (event)) return;
  QTMKeyboardEvent ke (tmapp()->keyboard(), *event);
  string r = ke.texmacsKeyCombination();
  if (r == "") {
    if (DEBUG_QT && DEBUG_KEYBOARD) debug_qt << "key press: unhandled key" << LF;
    return;
  }
  if (DEBUG_QT && DEBUG_KEYBOARD) debug_qt << "key press: " << r << LF;
  if (!performanceMonitor.inputBatchActive ())
    performanceMonitor.recordEditingInput ();
  the_gui->process_keypress (tm_widget(), r, texmacs_time());

}

static unsigned int
mouse_state (QMouseEvent* event, bool flag) {
  unsigned int i= 0;
  Qt::MouseButtons bstate= event->buttons ();
  Qt::MouseButton  tstate= event->button ();
  Qt::KeyboardModifiers kstate= event->modifiers ();
  if (flag) bstate= bstate | tstate;
  if ((bstate & Qt::LeftButton     ) != 0) i += 1;
    if ((bstate & Qt::MiddleButton   ) != 0) i += 2;
  if ((bstate & Qt::RightButton    ) != 0) i += 4;
  if ((bstate & Qt::XButton1       ) != 0) i += 8;
  if ((bstate & Qt::XButton2       ) != 0) i += 16;
#ifdef Q_OS_MAC
    // We emulate right and middle clicks with ctrl and option, but we pass the
    // modifiers anyway: old code continues to work and new one can use them.
  if ((kstate & Qt::MetaModifier   ) != 0) i = 1024+4; // control key
  if ((kstate & Qt::AltModifier    ) != 0) i = 2048+2; // option key
  if ((kstate & Qt::ShiftModifier  ) != 0) i += 256;
  if ((kstate & Qt::ControlModifier) != 0) i += 4096;   // cmd key
#else
  if ((kstate & Qt::ShiftModifier  ) != 0) i += 256;
  if ((kstate & Qt::ControlModifier) != 0) i += 1024;
  if ((kstate & Qt::AltModifier    ) != 0) i += 2048;
  if ((kstate & Qt::MetaModifier   ) != 0) i += 4096;
#endif
  return i;
}

static string
mouse_decode (unsigned int mstate) {
  if (mstate & 2) return "middle";
  else if (mstate & 4) return "right";
    // we check for left clicks after the others for macos (see ifdef in mouse_state)
  else if (mstate & 1) return "left";
  else if (mstate & 8) return "up";
  else if (mstate & 16) return "down";
  return "unknown";
}

void
QTMWidget::kbdEvent (int key, Qt::KeyboardModifiers mods, const QString& s) {
  QKeyEvent ev (QEvent::KeyPress, key, mods, s);
  keyPressEvent (&ev);
}

void
QTMWidget::inputMethodEvent (QInputMethodEvent* event) {
  refreshCursorBlinking (true);
  QString const & preedit_string = event->preeditString();
  QString const & commit_string = event->commitString();
  bool visibleInput= !commit_string.isEmpty () ||
                     !preedit_string.isEmpty () || preediting;
  if (visibleInput) performanceMonitor.recordEditingInput ();
  performanceMonitor.setInputBatchActive (visibleInput);
  
  if (!commit_string.isEmpty()) {
    bool done= false;
    if (!done) {
      if (DEBUG_QT)
        debug_qt << "IM committing: " << commit_string.toUtf8().data() << LF;
      if (!is_nil (tmwid))
        the_gui->process_text_input (
          tm_widget (), from_qstring (commit_string), texmacs_time ());
    }
  }
  
  if (DEBUG_QT)
    debug_qt << "IM preediting :" << preedit_string.toUtf8().data() << LF;
  
  string r = "pre-edit:";
  if (!preedit_string.isEmpty())
  {
    
    // find cursor position in the preedit string
    QList<QInputMethodEvent::Attribute>  const & attrs = event->attributes();
    //    int pos = preedit_string.count();
    int pos = 0;
    bool visible_cur = false;
    for (int i=0; i< attrs.count(); i++) 
      if (attrs[i].type == QInputMethodEvent::Cursor) {
        pos = attrs[i].start;
        visible_cur = (attrs[i].length != 0);
      }
    
    // find selection in the preedit string
    int sel_start = 0;
    int sel_length = 0;
    if (pos <  preedit_string.size()) {
      for (int i=0; i< attrs.count(); i++)
        if ((attrs[i].type == QInputMethodEvent::TextFormat) &&
            (attrs[i].start <= pos) &&
            (pos < attrs[i].start + attrs[i].length)) {
          sel_start = attrs[i].start;
          sel_length =  attrs[i].length;
          if (!visible_cur) pos += attrs[i].length;
        }
    } else {
      sel_start = pos;
      sel_length = 0;
    }
    (void) sel_start; (void) sel_length;
    
    int utf16Pos= std::clamp (pos, 0, (int) preedit_string.size ());
    if (utf16Pos > 0 && utf16Pos < preedit_string.size () &&
        preedit_string[utf16Pos].isLowSurrogate () &&
        preedit_string[utf16Pos - 1].isHighSurrogate ())
      utf16Pos--;
    int scalarPos=
      preedit_string.left (utf16Pos).toUcs4 ().size ();
    r = r * as_string (scalarPos) * ":" * from_qstring (preedit_string);
  }

  if (!is_nil (tmwid)) {
    preediting = !preedit_string.isEmpty();
    the_gui->process_keypress (tm_widget(), r, texmacs_time());
  }

  performanceMonitor.setInputBatchActive (false);
  event->accept();
}

QVariant 
QTMWidget::inputMethodQuery (Qt::InputMethodQuery query) const {
  switch (query) {
    case Qt::ImEnabled : {
      return QVariant (true);
    }
    case Qt::ImCursorRectangle : {
      const QPoint &topleft= cursor_pos - tm_widget()->backing_pos + surface()->geometry().topLeft();
      return QVariant (QRect (topleft, QSize (5, 5)));
    }
    default:
      return QWidget::inputMethodQuery (query);
  }
}

void
QTMWidget::mousePressEvent (QMouseEvent* event) {
  if (is_nil (tmwid)) return;
  refreshCursorBlinking (true);
  if (focusPolicy () != Qt::NoFocus) {
    if (!hasFocus ()) setFocus (Qt::MouseFocusReason);
  }
  if (handleNeighborhoodMiddleClick (event)) return;
  QPoint point = event->pos() + origin();
  coord2 pt = from_qpoint(point);
  unsigned int mstate= mouse_state (event, false);
  string s= "press-" * mouse_decode (mstate);
  the_gui -> process_mouse (tm_widget(), s, pt.x1, pt.x2,  
                            mstate, texmacs_time ());
  event->accept();
}

void
QTMWidget::mouseReleaseEvent (QMouseEvent* event) {
  if (is_nil (tmwid)) return;
  QPoint point = event->pos() + origin();
  coord2 pt = from_qpoint(point);
  unsigned int mstate = mouse_state (event, true);
  string s = "release-" * mouse_decode (mstate);
  the_gui->process_mouse (tm_widget(), s, pt.x1, pt.x2,
                            mstate, texmacs_time());
  event->accept();
}

void
QTMWidget::mouseMoveEvent (QMouseEvent* event) {
  if (is_nil (tmwid)) return;
  QPointF localPoint= event->position ();
  QPoint point = event->pos() + origin();
  coord2 pt = from_qpoint(point);
  unsigned int mstate = mouse_state (event, false);
  string s = "move";
  array<double> data;
  data << localPoint.x () << localPoint.y ();
  the_gui->process_mouse (tm_widget(), s, pt.x1, pt.x2, 
                          mstate, texmacs_time (), data);
  event->accept();
}

static unsigned int
tablet_state (QTabletEvent* event, bool flag) {
  unsigned int i= 0;
  Qt::MouseButtons bstate= event->buttons ();
  Qt::MouseButton  tstate= event->button ();
  if (flag) bstate= bstate | tstate;
  if ((bstate & Qt::LeftButton     ) != 0) i += 1;
  if ((bstate & Qt::MiddleButton   ) != 0) i += 2;
  if ((bstate & Qt::RightButton    ) != 0) i += 4;
  if ((bstate & Qt::XButton1       ) != 0) i += 8;
  if ((bstate & Qt::XButton2       ) != 0) i += 16;
  return i;
}

QScrollBar*
QTMWidget::scrollBarAtGlobalPosition (const QPoint& globalPos) const {
  QScrollBar* h= horizontalScrollBar ();
  QScrollBar* v= verticalScrollBar ();
  if (v != nullptr && v->isVisible () && v->isEnabled () &&
      QRect (v->mapToGlobal (QPoint (0, 0)), v->size ()).contains (globalPos))
    return v;
  if (h != nullptr && h->isVisible () && h->isEnabled () &&
      QRect (h->mapToGlobal (QPoint (0, 0)), h->size ()).contains (globalPos))
    return h;
  return nullptr;
}

bool
QTMWidget::forwardTabletEventToScrollBar (QTabletEvent* event) {
  QPointF globalPosF= event->globalPosition ();
  QPoint globalPos= globalPosF.toPoint ();
  QScrollBar* target= tabletScrollBarTarget;
  bool release= event->type () == QEvent::TabletRelease ||
                (event->button () != Qt::NoButton && event->pressure () == 0);
  if (target == nullptr)
    target= scrollBarAtGlobalPosition (globalPos);
  if (target == nullptr)
    return false;

  bool begin= event->type () == QEvent::TabletPress ||
              (event->button () != Qt::NoButton && !release);
  if (begin)
    tabletScrollBarTarget= target;
  else if (tabletScrollBarTarget == nullptr)
    return false;

  QEvent::Type type= QEvent::MouseMove;
  if (begin) type= QEvent::MouseButtonPress;
  else if (release) type= QEvent::MouseButtonRelease;

  QPointF localPos= target->mapFromGlobal (globalPos);
  Qt::MouseButton button= event->button ();
  if (button == Qt::NoButton && (begin || release)) button= Qt::LeftButton;
  Qt::MouseButtons buttons= event->buttons ();
  if (begin) buttons |= button;
  if (release) buttons &= ~button;

  QMouseEvent mouseEvent (type, localPos, localPos, globalPosF, button,
                          buttons, event->modifiers ());
  QApplication::sendEvent (target, &mouseEvent);
  event->accept ();
  if (release)
    tabletScrollBarTarget.clear ();
  return true;
}

void
QTMWidget::tabletEvent (QTabletEvent* event) {
  // for testing purposes
  // cout << "tablet name= " << from_qstring(event->pointingDevice ()->name ()) << "\n";
  if (forwardTabletEventToScrollBar (event)) return;
  if (is_nil (tmwid)) return;
  unsigned int mstate = tablet_state (event, true);
  string s= "move";
  if (event->button() != 0) {
    if (event->pressure () == 0) s= "release-" * mouse_decode (mstate);
    else s= "press-" * mouse_decode (mstate);
  }
  if ((mstate & 4) == 0 || s == "press-right") {
    QPoint point = event->position().toPoint() + origin() - surface()->pos();
    double x= point.x();
    double y= point.y();
    coord2 pt= coord2 ((SI) (x * PIXEL), (SI) (-y * PIXEL));
    array<double> data;
    data << ((double) event->pressure())
         << ((double) event->rotation())
         << ((double) event->xTilt())
         << ((double) event->yTilt())
         << ((double) event->z())
         << ((double) event->tangentialPressure());
    the_gui->process_mouse (tm_widget(), s, pt.x1, pt.x2, 
                            mstate, texmacs_time (), data);
  }
  /*
  cout << HRULE << LF;
  cout << "button= " << event->button() << LF;
  cout << "globalX= " << event->globalX() << LF;
  cout << "globalY= " << event->globalY() << LF;
  cout << "hiResGlobalX= " << event->hiResGlobalX() << LF;
  cout << "hiResGlobalY= " << event->hiResGlobalY() << LF;
  cout << "globalX= " << event->globalX() << LF;
  cout << "globalY= " << event->globalY() << LF;
  cout << "x= " << event->x() << LF;
  cout << "y= " << event->y() << LF;
  cout << "z= " << event->z() << LF;
  cout << "xTilt= " << event->xTilt() << LF;
  cout << "yTilt= " << event->yTilt() << LF;
  cout << "pressure= " << event->pressure() << LF;
  cout << "rotation= " << event->rotation() << LF;
  cout << "tangentialPressure= " << event->tangentialPressure() << LF;
  cout << "pointerType= " << event->pointerType() << LF;
  cout << "uniqueId= " << event->uniqueId() << LF;
  */
  event->accept();
}

void
QTMWidget::gestureEvent (QGestureEvent* event) {
  if (is_nil (tmwid)) return;

  string s= "gesture";
  array<double> data;
  QPointF hotspot;
  if (QGesture *swipe_gesture = event->gesture(Qt::SwipeGesture)) {
    QSwipeGesture *swipe= static_cast<QSwipeGesture *> (swipe_gesture);
    if (gesturesSupportedForViewZoom () && !inActiveGraphicsMode ()) {
      if (swipe->state() != Qt::GestureFinished) {
        event->accept (swipe);
        return;
      }
      int direction= 0;
      if (swipe->horizontalDirection() == QSwipeGesture::Left)
        direction= -1;
      else if (swipe->horizontalDirection() == QSwipeGesture::Right)
        direction= 1;
      if (direction != 0) {
        QPointF focal= swipe->hasHotSpot()
          ? surface()->mapFromGlobal (swipe->hotSpot().toPoint())
          : QPointF (surface()->width() / 2.0, surface()->height() / 2.0);
        logGesture (direction < 0 ? "swipe-left" : "swipe-right",
                    "qt-neighborhood", direction, focal);
        neighborhoods_open_neighbor (direction);
        event->accept (swipe);
        return;
      }
      if (gestureDebugEnabled ())
        logGesture ("swipe-ignored", "qt-neighborhood", 1.0,
                    swipe->hasHotSpot () ? swipe->hotSpot().toPoint ()
                                         : QPointF ());
      event->accept (swipe);
      return;
    }
    s= "swipe";
    hotspot = swipe->hotSpot ();
    if (swipe->state() == Qt::GestureFinished) {
      if (swipe->horizontalDirection() == QSwipeGesture::Left)
        s= "swipe-left";
      else if (swipe->horizontalDirection() == QSwipeGesture::Right)
        s= "swipe-right";
      else if (swipe->verticalDirection() == QSwipeGesture::Up)
        s= "swipe-up";
      else if (swipe->verticalDirection() == QSwipeGesture::Down)
        s= "swipe-down";
    }
    else {
      event->accept ();
      return;
    }
  }
  else if (QGesture *pan_gesture = event->gesture(Qt::PanGesture)) {
    QPanGesture *pan= static_cast<QPanGesture *> (pan_gesture);
    string s= "pan";
    hotspot = pan->hotSpot ();
    //QPointF delta = pan->delta();
    //cout << "Pan " << delta.x() << ", " << delta.y() << LF;
  }
  else if (QGesture *pinch_gesture = event->gesture(Qt::PinchGesture)) {
    QPinchGesture *pinch= static_cast<QPinchGesture *> (pinch_gesture);
    if (handlePinchGestureForViewZoom (pinch)) {
      event->accept (pinch);
      return;
    }
    s= "pinch";
    hotspot = pinch->hotSpot ();
    QPinchGesture::ChangeFlags changeFlags = pinch->changeFlags();
    if (pinch->state() == Qt::GestureStarted) {
      pinch->setRotationAngle (0.0);
      pinch->setScaleFactor (1.0);
      s= "pinch-start";
    }
    else if (pinch->state() == Qt::GestureFinished) {
      pinch->setRotationAngle (0.0);
      pinch->setScaleFactor (1.0);
      s= "pinch-end";
    }
    else if (changeFlags & QPinchGesture::RotationAngleChanged) {
      qreal angle = pinch->rotationAngle();
      s= "rotate";
      data << ((double) angle);
    }
    else if (changeFlags & QPinchGesture::ScaleFactorChanged) {
      qreal scale = pinch->totalScaleFactor();
      s= "scale";
      data << ((double) scale);
    }
  }
  else {
    if (gestureDebugEnabled ())
      cout << "[gesture] qt-gesture route=none platform="
           << from_qstring (QApplication::platformName ()) << LF;
    return;
  }
  QPoint point (hotspot.x(), hotspot.y());
  coord2 pt = from_qpoint (point);
  //cout << s << ", " << pt.x1 << ", " << pt.x2 << LF;
  the_gui->process_mouse (tm_widget(), s, pt.x1, pt.x2, 
                          0, texmacs_time (), data);
  event->accept();
}

 
bool
QTMWidget::event (QEvent* event) {
    // Catch Keypresses to avoid default handling of (Shift+)Tab keys
  if (event->type() == QEvent::KeyPress) {
    QKeyEvent *ke = static_cast<QKeyEvent*> (event);
    keyPressEvent (ke);
    return true;
  } 
  /* NOTE: we catch ShortcutOverride in order to disable the QKeySequences we
   assigned to QActions while building menus, etc. In doing this, we keep the
   shortcut text in the menus while relaying all keypresses through the editor*/
  if (event->type() == QEvent::ShortcutOverride) {
    QKeyEvent *ke = static_cast<QKeyEvent*> (event);
    if (ke->modifiers() == Qt::AltModifier && ke->key() == Qt::Key_F) {
      return false; // Let native Qt mnemonic handling take over for Alt+F
    }
    event->accept();
    return true;
  }
  if (event->type() == QEvent::TouchBegin ||
      event->type() == QEvent::TouchUpdate ||
      event->type() == QEvent::TouchEnd ||
      event->type() == QEvent::TouchCancel) {
    QTouchEvent* touch_event= static_cast<QTouchEvent*> (event);
    bool handled= handleNeighborhoodTouchTap (event);
    if (gestureDebugEnabled ()) {
      cout << "[gesture] event type="
           << gestureEventTypeName (event->type())
           << " route=touch handled=" << (handled ? "yes" : "no")
           << " points=" << touch_event->points ().size () << " platform="
           << from_qstring (QApplication::platformName ()) << LF;
    }
    if (handled) return true;
  }
  if (event->type() == QEvent::NativeGesture) {
    QNativeGestureEvent* native_event= static_cast<QNativeGestureEvent*> (event);
    bool handled= handleNativeGestureEvent (native_event);
    if (gestureDebugEnabled ()) {
      cout << "[gesture] event type=NativeGesture route=native captured="
           << (handled ? "yes" : "no") << " gesture-type="
           << gestureEventTypeName (static_cast<QEvent::Type> (native_event->gestureType()))
           << " fingers=" << native_event->fingerCount () << " value="
           << as_string (native_event->value ()) << LF;
    }
    if (handled) return true;
  }
  if (event->type() == QEvent::Gesture) {
    gestureEvent(static_cast<QGestureEvent*>(event));
    if (gestureDebugEnabled ())
      cout << "[gesture] event type=Gesture route=qt accepted" << LF;
    return true;
  }
  return QTMScrollView::event (event);
}

QTMWidget *last_focused_widget = nullptr;

QTMWidget *QTMWidget::getLastFocusedWidget() {
  return last_focused_widget;
}

void QTMWidget::setFocusToLast() {
  if (!last_focused_widget) return;

  last_focused_widget->setFocus();
  
  if (is_nil (last_focused_widget->tmwid)) return;

  if (DEBUG_QT)
    debug_qt << "FOCUSIN: " 
             << last_focused_widget->tm_widget()->type_as_string() 
             << LF;

  the_gui->process_keyboard_focus (last_focused_widget->tm_widget(),
                                   true, texmacs_time());
}

void
QTMWidget::focusInEvent (QFocusEvent * event) {
  if (!is_nil (tmwid)) {
    last_focused_widget = this;
  }
  if (!is_nil (tmwid)) {
    if (DEBUG_QT) debug_qt << "FOCUSIN: " << tm_widget()->type_as_string() << LF;
    the_gui->process_keyboard_focus (tm_widget(), true, texmacs_time());
  }
  QTMScrollView::focusInEvent (event);
  refreshCursorBlinking (true);
  updateInputMethodCursorRectangle ();
  neighborhoods_pane_refresh ();
  // part 2/2 of the fix for bug 43373.
  if (!isEmbedded ()) {
    if (!isActiveWindow() && QApplication::platformName() != "wayland") activateWindow();
    if (isActiveWindow() && !hasFocus()) setFocus (Qt::OtherFocusReason);
    //=> this will send us back here...
    //This redundancy is weird but definitely needed to properly get focus with Qt >= 5.15. Qt bug?
  }
}

void
QTMWidget::focusOutEvent (QFocusEvent * event) {
  if (is_nil (tmwid)) return;
  
  if (DEBUG_QT)
    debug_qt << "FOCUSOUT: " << tm_widget()->type_as_string() << LF;

  cursorBlinkTimer.stop ();
  setCursorBlinkVisible (true);
  the_gui -> process_keyboard_focus (tm_widget(), false, texmacs_time());
  
  QTMScrollView::focusOutEvent (event);
}

QSize
QTMWidget::sizeHint () const {
  SI w = 0, h = 0;
  if (!is_nil (tmwid)) tm_widget()->handle_get_size_hint (w, h);
  return to_qsize (w, h);
}

void 
QTMWidget::dragEnterEvent (QDragEnterEvent *event)
{
  if (is_nil (tmwid)) return;
  const QMimeData *md = event->mimeData();

  if (md->hasText() ||
      md->hasUrls() ||
      md->hasImage() ||
      md->hasFormat("application/pdf") ||
      md->hasFormat("application/postscript"))
      event->acceptProposedAction();
}


// cache to transfer drop data to the editor
// via standard mouse events, see dropEvent below

int drop_payload_serial  =0;
hashmap<int, tree> payloads;
 
void
QTMWidget::dropEvent (QDropEvent *event) {
  if (is_nil (tmwid)) return;

  QPoint point = event->position ().toPoint () + origin ();
  coord2 pt= from_qpoint (point);

  tree doc (CONCAT);
  const QMimeData *md= event->mimeData ();
  QByteArray buf;

  if (DEBUG_QT) debug_qt << "DropEvent formats :" << from_qstring(md->formats().join(",")) << LF;

  if (md->hasUrls ()) {
    QList<QUrl> l= md->urls ();
    for (int i=0; i<l.size (); i++) {

      string url = from_qstring(l[i].toString ());
      if (DEBUG_QT) debug_qt << "DropEvent URL [" << i << "] : " << url << LF;

      if (l[i].isLocalFile()) {
        string name;
#ifdef OS_MACOS
        name= from_qstring (fromNSUrl (l[i]));
#else
        name= from_qstring (l[i].toLocalFile ());
#endif
        string orig_name= name;
#ifdef OS_MINGW
        if (N(name) >=2 && is_alpha (name[0]) && name[1] == ':')
          name= "/" * locase_all (name (0, 1)) * name (2, N(name));
#endif
        string extension = suffix (name);
        if ((extension == "eps") || (extension == "ps")   ||
            (extension == "svg") ||
            (extension == "pdf") || (extension == "png")  ||
            (extension == "jpg") || (extension == "jpeg")) {
          string w, h;
          qt_pretty_image_size (url_system (orig_name), w, h);
          string ref, error;
          bool prepared= vault_image_insertion_prepare_file (
            get_current_buffer_safe (), url_system (orig_name), ref, error);
          tree im (IMAGE, prepared && ref != "" ? ref : name, w, h, "", "");
          doc << im;
        } else {
          doc << tree (make_tree_label ("cardlink"), "", name);
        }
      } else {
        // not a local file, drop an slink to the document
        string label= url;
        if (md->hasText ()) {
          // retrieve a meaningful string for the label if available
          // and only the first line as it can be multiline
          buf= md->text ().toUtf8 ().split('\n')[0];
          label= string (buf.constData (), buf.size ());
        }
        tree ln (HLINK, label, url);
        doc << ln;
        //FIXME: this is still not very nice as clicking on HLINK only works for a limited group
        //of schemas (http, https, ftp). For unrecognized schemas one would like to use the default OS behaviour
        //(e.g. the "message:" schema identify local email messages)
      }
    }
  } else if (md->hasImage ()) {
    QBuffer qbuf (&buf);
    QImage image= qvariant_cast<QImage> (md->imageData());
    QSize size= image.size ();
    qbuf.open (QIODevice::WriteOnly);
    image.save (&qbuf, "PNG");
    int ww= size.width (), hh= size.height ();
    string w, h;
    qt_pretty_image_size (ww, hh, w, h);
    string raw (buf.constData (), buf.size ());
    string ref, error;
    bool prepared= vault_image_insertion_prepare_data (
      get_current_buffer_safe (), raw, "png", ref, error);
    tree t= prepared && ref != "" ?
      tree (IMAGE, ref, w, h, "", "") :
      tree (IMAGE, tree (RAW_DATA, raw, "png"), w, h, "", "");
    doc << t;
  } else if (md->hasFormat("application/postscript")) {
    buf= md->data("application/postscript");
    string raw (buf.constData (), buf.size ());
    string ref, error;
    bool prepared= vault_image_insertion_prepare_data (
      get_current_buffer_safe (), raw, "ps", ref, error);
    tree t= prepared && ref != "" ?
      tree (IMAGE, ref, "", "", "", "") :
      tree (IMAGE, tree (RAW_DATA, raw, "ps"), "", "", "", "");
    doc << t;
  } else if (md->hasFormat("application/pdf")) {
    buf= md->data("application/pdf");
    string raw (buf.constData (), buf.size ());
    string ref, error;
    bool prepared= vault_image_insertion_prepare_data (
      get_current_buffer_safe (), raw, "pdf", ref, error);
    tree t= prepared && ref != "" ?
      tree (IMAGE, ref, "", "", "", "") :
      tree (IMAGE, tree (RAW_DATA, raw, "pdf"), "", "", "", "");
    doc << t;
  } else if (md->hasText ()) {
    buf= md->text ().toUtf8 ();
    doc << string (buf.constData (), buf.size ());
  }

  if (N(doc)>0) {
    if (N(doc) == 1)
      doc= doc[0];
    else {
      tree sec (CONCAT, doc[0]);
      for (int i=1; i<N(doc); i++)
        sec << " " << doc[i];
      doc= sec;
    }
    int ticket= drop_payload_serial++;
    payloads (ticket)= doc;
    the_gui->process_mouse (tm_widget(), "drop", pt.x1, pt.x2,
                            ticket, texmacs_time ());
    event->acceptProposedAction();
  }
}

static unsigned int
wheel_state (QWheelEvent* event) {
  // TODO: factor mouse_state, tablet_state, wheel_state
  // This should be easier on modern versions of Qt
  unsigned int i= 0;
  Qt::MouseButtons bstate= event->buttons ();
  Qt::KeyboardModifiers kstate= event->modifiers ();
  if ((bstate & Qt::LeftButton     ) != 0) i += 1;
  if ((bstate & Qt::MiddleButton   ) != 0) i += 2;
  if ((bstate & Qt::RightButton    ) != 0) i += 4;
  if ((bstate & Qt::XButton1       ) != 0) i += 8;
  if ((bstate & Qt::XButton2       ) != 0) i += 16;
#ifdef Q_OS_MAC
    // We emulate right and middle clicks with ctrl and option, but we pass the
    // modifiers anyway: old code continues to work and new one can use them.
  if ((kstate & Qt::MetaModifier   ) != 0) i = 1024+4; // control key
  if ((kstate & Qt::AltModifier    ) != 0) i = 2048+2; // option key
  if ((kstate & Qt::ShiftModifier  ) != 0) i += 256;
  if ((kstate & Qt::ControlModifier) != 0) i += 4096;   // cmd key
#else
  if ((kstate & Qt::ShiftModifier  ) != 0) i += 256;
  if ((kstate & Qt::ControlModifier) != 0) i += 1024;
  if ((kstate & Qt::AltModifier    ) != 0) i += 2048;
  if ((kstate & Qt::MetaModifier   ) != 0) i += 4096;
#endif
  return i;
}

void
QTMWidget::wheelEvent(QWheelEvent *event) {
  if (is_nil (tmwid)) return; 
  if (handleNeighborhoodWheelSwipe (event)) return;
  if (as_bool (call ("wheel-capture?"))) {
    QPointF pos  = event->position();
    QPoint  point= QPointF (pos.x(), pos.y()).toPoint () + origin();
    QPoint  wheel= event->pixelDelta();
    coord2 pt = from_qpoint (point);
    coord2 wh = from_qpoint (wheel);
    unsigned int mstate= wheel_state (event);
    array<double> data; data << ((double) wh.x1) << ((double) wh.x2);
    the_gui -> process_mouse (tm_widget(), "wheel", pt.x1, pt.x2,
                              mstate, texmacs_time (), data);
  }
  else if (QApplication::keyboardModifiers() == Qt::ControlModifier) {
    QPoint numPixels = event->pixelDelta();
    QPoint numDegrees = event->angleDelta() / 8;
    
    // compute the zoom factor from numPixels or numDegrees
    double zoomFactor = 0.0; (void) zoomFactor;
    if (!numPixels.isNull()) {
      if (numPixels.y() > 0) {
        call ("zoom-in", object (sqrt (sqrt (sqrt (sqrt (numPixels.y()))))));
      } else {
        call ("zoom-out", object (sqrt (sqrt (sqrt (sqrt (-numPixels.y()))))));
      }
    } else if (!numDegrees.isNull()) {
      if (numDegrees.y() > 0) {
        call ("zoom-in", object (sqrt (sqrt (sqrt (sqrt (numDegrees.y()))))));
      } else {
        call ("zoom-out", object (sqrt (sqrt (sqrt (sqrt (-numDegrees.y()))))));
      }
    }
  }
  else {
    notifyUserScroll ();
    if (get_user_preference("inertial scrolling") == "on") {
      QPoint numPixels = event->pixelDelta();
      QPoint numDegrees = event->angleDelta() / 8;
      double dx = 0, dy = 0;
      if (!numPixels.isNull()) {
        dx = numPixels.x();
        dy = numPixels.y();
      } else if (!numDegrees.isNull()) {
        dx = numDegrees.x();
        dy = numDegrees.y();
      }
      mInertiaFriction = as_double(get_user_preference("inertial scrolling friction", "0.90"));
      double sensitivity = as_double(get_user_preference("inertial scrolling sensitivity", "1.0"));
      mInertiaVelocityX += dx * 0.15 * sensitivity;
      mInertiaVelocityY += dy * 0.15 * sensitivity;
      if (!mInertiaTimer->isActive()) mInertiaTimer->start(16);
      
      QScrollBar *hBar = horizontalScrollBar();
      QScrollBar *vBar = verticalScrollBar();
      hBar->setValue(hBar->value() - qRound(dx));
      vBar->setValue(vBar->value() - qRound(dy));
      event->accept();
    } else {
      QAbstractScrollArea::wheelEvent (event);
    }
  }
}

void QTMWidget::showEvent (QShowEvent *event) {
  the_gui->force_update();
  QTMScrollView::showEvent (event);
  performanceMonitor.refresh ();
}

void QTMWidget::closeEvent (QCloseEvent *event) {
  if (DEBUG_QT_WIDGETS) debug_widgets << "Close QTMWidget" << LF;
  event->ignore ();
  emit closed ();
}

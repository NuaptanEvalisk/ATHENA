
/******************************************************************************
* MODULE     : QTMWidget.hpp
* DESCRIPTION: QT Texmacs widget class
* COPYRIGHT  : (C) 2008 Massimiliano Gubinelli
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMWIDGET_HPP
#define QTMWIDGET_HPP

#include "qt_widget.hpp"
#include "QTMScrollView.hpp"
#include "QTMPerformanceMonitor.hpp"
#include <QLabel>
#include <QGesture>
#include <QGestureEvent>
#include <QPointer>
#include <QPixmap>
#include <QScreen>
#include <QElapsedTimer>
#include <QImage>
#include <QTimer>

class qt_simple_widget_rep;
#include "QTMRenderService.hpp"
class QScrollBar;
class QNativeGestureEvent;
class QPainter;

/*! The underlying QWidget for a qt_simple_widget_rep handles drawing for a 
    texmacs canvas, as well as keypresses, international input methods, etc.
 
 QTMWidget needs a valid qt_simple_widget_rep object to function properly, see
 set_tm_widget() for more on this.
 
 */
class QTMWidget: public QTMScrollView {
  Q_OBJECT

  qt_widget    tmwid;
  QLabel*      imwidget;
  QPoint       cursor_pos;
  bool         preediting;

public:

  
  QTMWidget (QWidget* _parent=0, qt_widget _tmwid=0);
  virtual ~QTMWidget ();
  virtual bool isEmbedded () const;
  
  virtual QSize	sizeHint () const override;
  virtual void scrollContentsBy (int dx, int dy) override;

  void setCursorPos (QPoint pos);
  void presentRenderedFrame (std::uint64_t bufferGeneration,
                             std::uint64_t frameGeneration,
                             render_damage damage);
  QPoint cursorGlobalPos () const {
    QPoint p = contentsToViewport (cursor_pos);
    return viewport ()->mapToGlobal (p + QPoint (0, 22));
  }
  qt_simple_widget_rep* tm_widget () const;
  void refreshEmbeddedBackingStore ();
  void finishGestureZoomCommitPreview ();

signals:
  void closed ();

public:
  bool isPreediting () { return preediting; }
  static QTMWidget *getLastFocusedWidget();
  static void setFocusToLast();
  static void refreshAllCursorBlinking ();
  static void refreshAllPerformanceMonitors ();
protected slots:
  void devicePixelRatioChanged ();

protected:

  virtual bool event (QEvent *event) override;

  void surfacePaintEvent (QPaintEvent *e, QWidget *surface) override;
  bool checkDprChange();
  virtual void focusInEvent (QFocusEvent* event) override;
  virtual void focusOutEvent (QFocusEvent* event) override;
  virtual void keyPressEvent (QKeyEvent* event) override;
  virtual void kbdEvent (int key, Qt::KeyboardModifiers mods, const QString& s);
  virtual void inputMethodEvent (QInputMethodEvent* event) override;
  virtual void mousePressEvent (QMouseEvent* event) override;
  virtual void mouseReleaseEvent (QMouseEvent* event) override;
  virtual void mouseMoveEvent (QMouseEvent* event) override;
  virtual void tabletEvent (QTabletEvent* event) override;
  virtual void gestureEvent (QGestureEvent* event);
  virtual void resizeEvent (QResizeEvent* event) override;
  virtual void resizeEventBis () override;
  virtual void dragEnterEvent(QDragEnterEvent *event) override;
  //virtual void dragMoveEvent (QDragMoveEvent *event) override;
  virtual void dropEvent(QDropEvent *event) override;

  virtual void wheelEvent(QWheelEvent *event) override;
  virtual QVariant inputMethodQuery (Qt::InputMethodQuery query) const override;
  void notifyUserScroll ();

  void showEvent (QShowEvent *event) override;
  void closeEvent (QCloseEvent *event) override;

private:
  friend class QTMPerformanceMonitor;

  qreal lastPixelRatio = 0.0;
  QPointer<QScrollBar> tabletScrollBarTarget;
  QTimer cursorBlinkTimer;
  QTimer fractionalScrollSettleTimer;
  bool cursorBlinkVisible= true;
  bool embeddedScrollRefreshPending= false;
  QTMPerformanceMonitor performanceMonitor;
  bool viewPinchActive = false;
  bool viewPinchCommitPending = false;
  bool nativeLegacyPinchActive = false;
  double nativeLegacyPinchScale = 1.0;
  double viewPinchStartZoom = 1.0;
  double viewPinchScale = 1.0;
  double viewPinchCommittedScale = 1.0;
  qreal viewPinchPixelRatio = 1.0;
  QPointF viewPinchFocal;
  QPoint viewPinchStartOrigin;
  QPixmap viewPinchPreview;
  QTMSharedFrame renderedFrame;
  std::uint64_t renderedBufferGeneration= 0;
  std::uint64_t renderedFrameGeneration= 0;
  bool neighborhoodTapCandidate = false;
  QElapsedTimer neighborhoodTapTimer;
  QPointF neighborhoodTapStartCenter;
  double neighborhoodWheelSwipeAccum = 0.0;
  QElapsedTimer neighborhoodWheelSwipeCooldown;

  void updateInputMethodCursorRectangle () const;
  void scheduleEmbeddedScrollRefresh ();
  void scheduleFractionalScrollSettle ();
  void setCursorBlinkVisible (bool visible);
  void refreshCursorBlinking (bool restart);
  bool forwardTabletEventToScrollBar (QTabletEvent* event);
  QScrollBar* scrollBarAtGlobalPosition (const QPoint& globalPos) const;
  bool gesturesSupportedForViewZoom () const;
  bool activateOwningViewForGesture (const char* source) const;
  bool inActiveGraphicsMode () const;
  bool gestureDebugEnabled () const;
  void logGesture (const char* phase, const char* route, double scale,
                   const QPointF& focal) const;
  void beginViewPinchZoom (const QPointF& focal, const char* source);
  void updateViewPinchZoom (double scale, const QPointF& focal,
                            const char* source);
  void finishViewPinchZoom (bool commit, const char* source);
  bool handleNativeGestureEvent (QNativeGestureEvent* event);
  bool handlePinchGestureForViewZoom (QPinchGesture* pinch);
  bool handleNeighborhoodTouchTap (QEvent* event);
  bool handleNeighborhoodMiddleClick (QMouseEvent* event);
  bool handleNeighborhoodKeyShortcut (QKeyEvent* event);
  bool handleNeighborhoodWheelSwipe (QWheelEvent* event);
  void drawViewPinchPreview (QPainter& p) const;

};

#endif

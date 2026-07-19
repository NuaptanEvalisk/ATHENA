/******************************************************************************
* MODULE     : QTMToolbarController.cpp
* DESCRIPTION: Main window toolbar layout and hover reveal policy
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "QTMToolbarController.hpp"

#include "scheme.hpp"

#include <QAction>
#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QLabel>
#include <QMainWindow>
#include <QPalette>
#include <QSizePolicy>
#include <QToolBar>
#include <QWidget>

namespace {

static QWidget*
toolbar_stretch (QToolBar* toolbar) {
  QWidget* stretch= new QWidget (toolbar);
  stretch->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Preferred);
  return stretch;
}

static bool
contains_global_point (const QWidget* widget, const QPoint& point) {
  if (widget == nullptr || !widget->isVisible ()) return false;
  return QRect (widget->mapToGlobal (QPoint (0, 0)), widget->size ())
    .contains (point);
}

} // namespace

QTMToolbarController::QTMToolbarController (
  QMainWindow* _window, QToolBar* _mainToolbar, QToolBar* _modeToolbar,
  QToolBar* _focusToolbar, QToolBar* _userToolbar)
  : QObject (_window), window (_window), mainToolbar (_mainToolbar),
    modeToolbar (_modeToolbar), focusToolbar (_focusToolbar),
    userToolbar (_userToolbar) {
  reveal= new QToolBar (QObject::tr ("Reveal toolbars"), _window);
  reveal->setObjectName ("toolbarRevealBar");
  reveal->setMovable (false);
  reveal->setFloatable (false);
  reveal->setAllowedAreas (Qt::TopToolBarArea);
  reveal->setFixedHeight (16);
  reveal->setContentsMargins (0, 0, 0, 0);
  reveal->addWidget (toolbar_stretch (reveal));
  dots= new QLabel (QStringLiteral ("..."), reveal);
  QFont dotsFont= dots->font ();
  dotsFont.setPixelSize (9);
  dots->setFont (dotsFont);
  dots->setFixedSize (28, 16);
  dots->setAlignment (Qt::AlignCenter);
  dots->setContentsMargins (0, 0, 0, 0);
  reveal->addWidget (dots);
  reveal->addWidget (toolbar_stretch (reveal));
  _window->insertToolBar (_mainToolbar, reveal);

  QWidget* overlayParent= _window->centralWidget ();
  if (overlayParent == nullptr) overlayParent= _window;
  overlay= new QWidget (overlayParent);
  overlay->setObjectName ("toolbarOverlay");
  overlay->setAutoFillBackground (true);
  overlay->setBackgroundRole (QPalette::Window);
  overlay->setPalette (_mainToolbar->palette ());
  overlay->hide ();

  joinSeparator= _mainToolbar->addSeparator ();
  collapseTimer.setSingleShot (true);
  collapseTimer.setInterval (280);
  connect (&collapseTimer, &QTimer::timeout, this, [this] () {
    if (!autoHide || collapsed) return;
    if (QApplication::activePopupWidget () != nullptr || pointerOverToolbar ()) {
      scheduleCollapseCheck ();
      return;
    }
    collapse ();
  });

  for (QObject* object: { static_cast<QObject*> (_window),
                          static_cast<QObject*> (_mainToolbar),
                          static_cast<QObject*> (_modeToolbar),
                          static_cast<QObject*> (_focusToolbar),
                          static_cast<QObject*> (_userToolbar),
                          static_cast<QObject*> (reveal.data ()),
                          static_cast<QObject*> (overlay.data ()),
                          static_cast<QObject*> (overlayParent),
                          static_cast<QObject*> (dots) }) {
    object->installEventFilter (this);
  }
  refreshPreference ();
}

void
QTMToolbarController::setRequestedVisibility (
  bool mainVisible, bool modeVisible, bool focusVisible, bool userVisible) {
  refreshPreference ();
  requestedMain= mainVisible;
  requestedMode= modeVisible;
  requestedFocus= focusVisible;
  requestedUser= userVisible;
  applyVisibility ();
}

void
QTMToolbarController::refreshPreference () {
  setAutoHideEnabled (
    get_preference ("hide toolbars when not using them", "off") == "on");
}

void
QTMToolbarController::setAutoHideEnabled (bool enabled) {
  if (autoHide == enabled) {
    applyVisibility ();
    return;
  }
  autoHide= enabled;
  collapsed= enabled;
  if (enabled) enterOverlayMode ();
  else {
    collapseTimer.stop ();
    leaveOverlayMode ();
  }
  applyVisibility ();
}

void
QTMToolbarController::expand () {
  if (!autoHide || !collapsed) return;
  collapsed= false;
  applyVisibility ();
  scheduleCollapseCheck ();
}

void
QTMToolbarController::collapse () {
  if (!autoHide || collapsed) return;
  collapsed= true;
  collapseTimer.stop ();
  applyVisibility ();
}

void
QTMToolbarController::ensureJoinSeparator () {
  if (mainToolbar == nullptr || joinSeparator == nullptr) return;
  if (!mainToolbar->actions ().contains (joinSeparator))
    mainToolbar->addAction (joinSeparator);
}

void
QTMToolbarController::applyVisibility () {
  if (applying || window == nullptr || mainToolbar == nullptr ||
      modeToolbar == nullptr || focusToolbar == nullptr ||
      userToolbar == nullptr || reveal == nullptr)
    return;
  applying= true;
  ensureJoinSeparator ();

  const bool anyRequested= requestedMain || requestedMode ||
                           requestedFocus || requestedUser;
  const bool showTools= !autoHide || !collapsed;
  mainToolbar->setVisible (showTools && requestedMain);
  modeToolbar->setVisible (showTools && requestedMode);
  focusToolbar->setVisible (showTools && requestedFocus);
  userToolbar->setVisible (showTools && requestedUser);
  reveal->setVisible (autoHide && anyRequested);
  if (dots != nullptr) dots->setVisible (autoHide && collapsed && anyRequested);
  if (overlay != nullptr)
    overlay->setVisible (autoHide && !collapsed && anyRequested);

  refreshLayout ();
  if (overlay != nullptr && overlay->isVisible ()) overlay->raise ();
  applying= false;
}

void
QTMToolbarController::refreshLayout () {
  if (window == nullptr || mainToolbar == nullptr || modeToolbar == nullptr)
    return;
  ensureJoinSeparator ();

  bool canMerge= requestedMain && requestedMode;
  if (canMerge) {
    const int required= mainToolbar->sizeHint ().width () +
                        modeToolbar->sizeHint ().width () + 8;
    QWidget* available= overlayMode && overlay != nullptr ?
      overlay->parentWidget () : window.data ();
    canMerge= available != nullptr && required <= available->width ();
  }
  modeMerged= canMerge;
  if (overlayMode) positionOverlay ();
  else if (canMerge) window->removeToolBarBreak (modeToolbar);
  else window->insertToolBarBreak (modeToolbar);
  if (joinSeparator != nullptr) joinSeparator->setVisible (canMerge);
}

void
QTMToolbarController::enterOverlayMode () {
  if (overlayMode || window == nullptr || overlay == nullptr) return;
  overlayMode= true;
  for (QToolBar* toolbar: { mainToolbar.data (), modeToolbar.data (),
                           focusToolbar.data (), userToolbar.data () }) {
    if (toolbar == nullptr) continue;
    window->removeToolBar (toolbar);
    toolbar->setParent (overlay);
  }
  positionOverlay ();
}

void
QTMToolbarController::leaveOverlayMode () {
  if (!overlayMode || window == nullptr) return;
  if (overlay != nullptr) overlay->hide ();
  for (QToolBar* toolbar: { mainToolbar.data (), modeToolbar.data (),
                           focusToolbar.data (), userToolbar.data () }) {
    if (toolbar == nullptr) continue;
    toolbar->hide ();
    toolbar->setParent (window);
    window->addToolBar (Qt::TopToolBarArea, toolbar);
  }
  if (modeToolbar != nullptr) window->insertToolBarBreak (modeToolbar);
  if (focusToolbar != nullptr) window->insertToolBarBreak (focusToolbar);
  if (userToolbar != nullptr) window->insertToolBarBreak (userToolbar);
  overlayMode= false;
}

int
QTMToolbarController::positionOverlayToolbar (QToolBar* toolbar, int y,
                                              int width) {
  if (toolbar == nullptr || !toolbar->isVisible ()) return y;
  int height= toolbar->height ();
  if (height <= 0) height= toolbar->sizeHint ().height ();
  toolbar->setGeometry (0, y, width, height);
  return y + height;
}

void
QTMToolbarController::positionOverlay () {
  if (!overlayMode || overlay == nullptr || overlay->parentWidget () == nullptr)
    return;

  int width= overlay->parentWidget ()->width ();
  int y= 0;
  if (mainToolbar != nullptr && mainToolbar->isVisible () &&
      modeToolbar != nullptr && modeToolbar->isVisible () && modeMerged) {
    int mainWidth= qMin (mainToolbar->sizeHint ().width (), width);
    int modeWidth= qMin (modeToolbar->sizeHint ().width (),
                         qMax (0, width - mainWidth));
    int height= qMax (mainToolbar->height (), modeToolbar->height ());
    if (height <= 0)
      height= qMax (mainToolbar->sizeHint ().height (),
                    modeToolbar->sizeHint ().height ());
    mainToolbar->setGeometry (0, y, mainWidth, height);
    modeToolbar->setGeometry (mainWidth, y, modeWidth, height);
    y += height;
  }
  else {
    y= positionOverlayToolbar (mainToolbar, y, width);
    y= positionOverlayToolbar (modeToolbar, y, width);
  }
  y= positionOverlayToolbar (focusToolbar, y, width);
  y= positionOverlayToolbar (userToolbar, y, width);

  overlay->setGeometry (0, 0, width, y);
  if (overlay->isVisible ()) overlay->raise ();
}

void
QTMToolbarController::scheduleCollapseCheck () {
  if (autoHide && !collapsed) collapseTimer.start ();
}

bool
QTMToolbarController::pointerOverToolbar () const {
  QPoint point= QCursor::pos ();
  return contains_global_point (mainToolbar, point) ||
         contains_global_point (modeToolbar, point) ||
         contains_global_point (focusToolbar, point) ||
         contains_global_point (userToolbar, point) ||
         contains_global_point (overlay, point) ||
         contains_global_point (reveal, point);
}

bool
QTMToolbarController::eventFilter (QObject* watched, QEvent* event) {
  if ((watched == window || (overlay != nullptr &&
       watched == overlay->parentWidget ())) &&
      event->type () == QEvent::Resize) {
    QTimer::singleShot (0, this, [this] () { refreshLayout (); });
  }
  else if ((watched == reveal || (reveal != nullptr &&
            watched->parent () == reveal)) &&
           (event->type () == QEvent::Enter ||
            event->type () == QEvent::HoverEnter ||
            event->type () == QEvent::TabletMove)) {
    expand ();
  }
  else if (watched != window && event->type () == QEvent::Enter) {
    collapseTimer.stop ();
  }
  else if (watched != window && event->type () == QEvent::Leave) {
    scheduleCollapseCheck ();
  }
  else if (overlayMode &&
           (event->type () == QEvent::LayoutRequest ||
            event->type () == QEvent::Show || event->type () == QEvent::Hide)) {
    QTimer::singleShot (0, this, [this] () { positionOverlay (); });
  }
  return QObject::eventFilter (watched, event);
}

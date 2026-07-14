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
#include <QSizePolicy>
#include <QToolBar>

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
  QLabel* dots= new QLabel (QStringLiteral ("..."), reveal);
  QFont dotsFont= dots->font ();
  dotsFont.setPixelSize (9);
  dots->setFont (dotsFont);
  dots->setFixedSize (28, 16);
  dots->setAlignment (Qt::AlignCenter);
  dots->setContentsMargins (0, 0, 0, 0);
  reveal->addWidget (dots);
  reveal->addWidget (toolbar_stretch (reveal));
  _window->insertToolBar (_mainToolbar, reveal);

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
  if (!enabled) collapseTimer.stop ();
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
  reveal->setVisible (autoHide && collapsed && anyRequested);

  refreshLayout ();
  applying= false;
}

void
QTMToolbarController::refreshLayout () {
  if (window == nullptr || mainToolbar == nullptr || modeToolbar == nullptr)
    return;
  ensureJoinSeparator ();

  bool canMerge= (!autoHide || !collapsed) && requestedMain && requestedMode;
  if (canMerge) {
    const int required= mainToolbar->sizeHint ().width () +
                        modeToolbar->sizeHint ().width () + 8;
    canMerge= required <= window->width ();
  }
  modeMerged= canMerge;
  if (canMerge) window->removeToolBarBreak (modeToolbar);
  else window->insertToolBarBreak (modeToolbar);
  if (joinSeparator != nullptr) joinSeparator->setVisible (canMerge);
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
         contains_global_point (reveal, point);
}

bool
QTMToolbarController::eventFilter (QObject* watched, QEvent* event) {
  if (watched == window && event->type () == QEvent::Resize) {
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
  return QObject::eventFilter (watched, event);
}

/******************************************************************************
* MODULE     : QTMVaultLinkFocus.cpp
* DESCRIPTION: TeXmacs focus preservation for vault link dialogs
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMVaultLinkFocus.hpp"
#include "new_view.hpp"
#include <QApplication>
#include <QEvent>
#include <QPointer>
#include <QScrollBar>
#include <QTimer>
#include <QWidget>

static bool
is_window_backed_view (url view) {
  return !is_none (view) && concrete_view (view) != NULL &&
         !is_none (view_to_window (view));
}

static url
active_view_or_recent_active (url view) {
  if (is_window_backed_view (view)) return view;
  return get_recent_view (url_none (), false, false, true, false);
}

TeXmacsFocusSnapshot
capture_texmacs_focus_snapshot () {
  TeXmacsFocusSnapshot s;
  s.view= active_view_or_recent_active (get_current_view_safe ());
  s.widget= QTMWidget::getLastFocusedWidget ();
  s.hScroll= 0;
  s.vScroll= 0;
  s.hasScroll= false;

  if (!s.widget.isNull () && !s.widget->isEmbedded ()) {
    QScrollBar* h= s.widget->horizontalScrollBar ();
    QScrollBar* v= s.widget->verticalScrollBar ();
    if (h != nullptr && v != nullptr) {
      s.hScroll= h->value ();
      s.vScroll= v->value ();
      s.hasScroll= true;
    }
  }
  else s.widget= nullptr;
  return s;
}

static void
restore_texmacs_focus_snapshot (const TeXmacsFocusSnapshot& s,
                                bool restoreScroll) {
  url view= active_view_or_recent_active (s.view);
  if (is_window_backed_view (view)) set_current_view (view);

  if (s.widget.isNull ()) return;
  if (restoreScroll && s.hasScroll) {
    if (s.widget->horizontalScrollBar () != nullptr)
      s.widget->horizontalScrollBar ()->setValue (s.hScroll);
    if (s.widget->verticalScrollBar () != nullptr)
      s.widget->verticalScrollBar ()->setValue (s.vScroll);
  }

  s.widget->setFocus (Qt::OtherFocusReason);

  if (restoreScroll && s.hasScroll) {
    if (s.widget->horizontalScrollBar () != nullptr)
      s.widget->horizontalScrollBar ()->setValue (s.hScroll);
    if (s.widget->verticalScrollBar () != nullptr)
      s.widget->verticalScrollBar ()->setValue (s.vScroll);
  }
}

class TeXmacsFocusRestorer final: public QObject {
  TeXmacsFocusSnapshot snapshot;
  QPointer<QWidget>    targetWindow;
  bool                 restoreScheduled;

  void scheduleRestore () {
    if (restoreScheduled) return;
    restoreScheduled= true;
    QTimer::singleShot (0, this, [this] () {
      restoreScheduled= false;
      if (!targetWindow.isNull () && !targetWindow->isActiveWindow ()) return;
      if (!targetWindow.isNull ()) targetWindow->removeEventFilter (this);
      restore_texmacs_focus_snapshot (snapshot, true);
      deleteLater ();
    });
  }

public:
  explicit TeXmacsFocusRestorer (const TeXmacsFocusSnapshot& s)
    : QObject (qApp), snapshot (s), restoreScheduled (false) {
    if (!snapshot.widget.isNull ()) targetWindow= snapshot.widget->window ();
    if (targetWindow.isNull ()) {
      scheduleRestore ();
      return;
    }
    targetWindow->installEventFilter (this);
    connect (targetWindow, &QObject::destroyed, this, &QObject::deleteLater);
    if (targetWindow->isActiveWindow ()) scheduleRestore ();
  }

  bool eventFilter (QObject* watched, QEvent* event) override {
    if (watched == targetWindow && event->type () == QEvent::WindowActivate)
      scheduleRestore ();
    return QObject::eventFilter (watched, event);
  }
};

void
restore_texmacs_focus_snapshot_later (const TeXmacsFocusSnapshot& s) {
  // Native Wayland titlebar closure can return from the modal loop before the
  // compositor reactivates ATHENA's document window.  Wait for that activation
  // boundary instead of repainting the editor during the focus transition.
  new TeXmacsFocusRestorer (s);
}

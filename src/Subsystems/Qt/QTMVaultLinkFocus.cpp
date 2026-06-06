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
#include "qt_gui.hpp"
#include "tm_timer.hpp"
#include <QScrollBar>
#include <QTimer>

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

void
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
  if (the_gui != nullptr && s.widget->tm_widget () != nullptr) {
    the_gui->process_keyboard_focus (s.widget->tm_widget (), true,
                                     texmacs_time ());
    the_gui->process_queued_events (4);
  }

  if (restoreScroll && s.hasScroll) {
    if (s.widget->horizontalScrollBar () != nullptr)
      s.widget->horizontalScrollBar ()->setValue (s.hScroll);
    if (s.widget->verticalScrollBar () != nullptr)
      s.widget->verticalScrollBar ()->setValue (s.vScroll);
  }
}

void
restore_texmacs_focus_snapshot_later (const TeXmacsFocusSnapshot& s) {
  QTimer::singleShot (0, [s] () {
    restore_texmacs_focus_snapshot (s, true);
    QTimer::singleShot (80, [s] () {
      restore_texmacs_focus_snapshot (s, true);
      QTimer::singleShot (220, [s] () {
        restore_texmacs_focus_snapshot (s, true);
      });
    });
  });
}

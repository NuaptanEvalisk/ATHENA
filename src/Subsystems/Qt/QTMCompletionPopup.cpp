/******************************************************************************
* MODULE     : QTMCompletionPopup.cpp
* DESCRIPTION: GUI-only completion presentation; choices carry session IDs
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include "QTMCompletionPopup.hpp"
#include <QGuiApplication>
#include <QHideEvent>
#include <QKeyEvent>
#include <QScreen>
#include <QTimer>
#include <algorithm>

QTMCompletionPopup::QTMCompletionPopup (QWidget* editor, Choice choice):
  QTMCompletionPopupBase (editor), editor_ (editor), choice_ (std::move (choice)) {
  setObjectName ("athenaCompletionPopup");
#if ATHENA_PLATFORM_IPADOS
  setWindowFlags (Qt::Popup | Qt::FramelessWindowHint);
  setFocusPolicy (Qt::NoFocus);
  setSelectionMode (QAbstractItemView::SingleSelection);
  setHorizontalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
  editor_->installEventFilter (this);
  connect (this, &QListWidget::itemClicked, this,
           [this] (QListWidgetItem* item) { finish (row (item)); });
  connect (this, &QListWidget::itemActivated, this,
           [this] (QListWidgetItem* item) { finish (row (item)); });
#else
  setTabHandling (false);
  setActivateOnSelect (false);
  connect (this, &KCompletionBox::textActivated, this,
           [this] (const QString&) { finish (currentRow ()); });
#endif
  setFixedWidth (420);
}

void QTMCompletionPopup::present (
  std::uint64_t session, const QStringList& items, QPoint anchor, int selected) {
  cancel ();
  if (items.isEmpty ()) return;
  session_= session;
  anchor_= anchor;
#if ATHENA_PLATFORM_IPADOS
  clear ();
  addItems (items);
  int rows= std::min (count (), 8);
  int row_height= count () > 0 ? sizeHintForRow (0) : 0;
  if (row_height <= 0) row_height= fontMetrics ().height () + 8;
  setFixedHeight (rows * row_height + 2 * frameWidth () + 2);
  setCurrentRow (std::clamp (selected, 0, count () - 1));
  show ();
  raise ();
#else
  setItems (items);
  popup ();
  setCurrentRow (std::clamp (selected, 0, count ()-1));
#endif
  if (QScreen* screen= QGuiApplication::screenAt (anchor)) {
    QRect bounds= screen->availableGeometry ();
    setFixedWidth (std::min (420, bounds.width ()));
    int y= anchor.y ();
    if (y+height () > bounds.bottom ()) y= anchor.y ()-height ()-20;
    move (std::clamp (anchor.x (), bounds.left (), bounds.right ()-width ()+1),
          std::max (bounds.top (), y));
  }
}

void QTMCompletionPopup::select (std::uint64_t session, int row) {
  if (session_ == session && row >= 0 && row < count ()) setCurrentRow (row);
}

void QTMCompletionPopup::dismiss (std::uint64_t session) {
  if (session_ != session) return;
  session_= 0;
  hide ();
}

void QTMCompletionPopup::cancel () { finish (-1); }

void QTMCompletionPopup::finish (int row) {
  std::uint64_t session= session_;
  session_= 0;
  hide ();
  if (session) choice_ (session, row);
}

#if !ATHENA_PLATFORM_IPADOS
QPoint QTMCompletionPopup::globalPositionHint () const { return anchor_; }
#endif

void QTMCompletionPopup::hideEvent (QHideEvent* event) {
  QTMCompletionPopupBase::hideEvent (event);
  // KDE hides before emitting textActivated on a mouse click. Let that
  // synchronous acceptance win over cancellation caused by the same hide.
  std::uint64_t session= session_;
  if (session) QTimer::singleShot (0, this, [this, session] {
    if (session_ == session) finish (-1);
  });
}

#if ATHENA_PLATFORM_IPADOS
void QTMCompletionPopup::move_selection (int delta, bool wrap) {
  if (count () <= 0) return;
  int next= currentRow ();
  if (next < 0) next= 0;
  next+= delta;
  if (wrap) {
    while (next < 0) next+= count ();
    next%= count ();
  }
  else next= std::clamp (next, 0, count () - 1);
  setCurrentRow (next);
  if (QListWidgetItem* item= currentItem ()) scrollToItem (item);
}
#endif

bool QTMCompletionPopup::eventFilter (QObject* object, QEvent* event) {
  QWidget* target= qobject_cast<QWidget*> (object);
  bool input= target && (target == editor_ || editor_->isAncestorOf (target) ||
                         target->isAncestorOf (editor_));
  if (isVisible () && input &&
      (event->type () == QEvent::KeyPress || event->type () == QEvent::ShortcutOverride)) {
    auto* key= static_cast<QKeyEvent*> (event);
    bool modified= key->modifiers () & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    bool command= !modified && (key->key () == Qt::Key_Tab || key->key () == Qt::Key_Backtab ||
      key->key () == Qt::Key_Return || key->key () == Qt::Key_Enter ||
      key->key () == Qt::Key_Up || key->key () == Qt::Key_Down ||
      key->key () == Qt::Key_PageUp || key->key () == Qt::Key_PageDown || key->key () == Qt::Key_Escape);
    if (command) {
      key->accept ();
      if (event->type () == QEvent::ShortcutOverride) return true;
      switch (key->key ()) {
#if ATHENA_PLATFORM_IPADOS
      case Qt::Key_Up: case Qt::Key_Backtab: move_selection (-1, true); break;
      case Qt::Key_Down: move_selection (1, true); break;
      case Qt::Key_PageUp: move_selection (-8, false); break;
      case Qt::Key_PageDown: move_selection (8, false); break;
#else
      case Qt::Key_Up: case Qt::Key_Backtab: up (); break;
      case Qt::Key_Down: down (); break;
      case Qt::Key_PageUp: pageUp (); break;
      case Qt::Key_PageDown: pageDown (); break;
#endif
      case Qt::Key_Escape: cancel (); break;
      default: finish (std::max (0, currentRow ())); break;
      }
      return true;
    }
    if (event->type () == QEvent::KeyPress) cancel ();
  }
  return QTMCompletionPopupBase::eventFilter (object, event);
}

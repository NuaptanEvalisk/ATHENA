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
  KCompletionBox (editor), editor_ (editor), choice_ (std::move (choice)) {
  setObjectName ("athenaCompletionPopup");
  setTabHandling (false);
  setActivateOnSelect (false);
  setFixedWidth (420);
  connect (this, &KCompletionBox::textActivated, this,
           [this] (const QString&) { finish (currentRow ()); });
}

void QTMCompletionPopup::present (
  std::uint64_t session, const QStringList& items, QPoint anchor, int selected) {
  cancel ();
  if (items.isEmpty ()) return;
  session_= session;
  anchor_= anchor;
  setItems (items);
  popup ();
  setCurrentRow (std::clamp (selected, 0, count ()-1));
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

QPoint QTMCompletionPopup::globalPositionHint () const { return anchor_; }

void QTMCompletionPopup::hideEvent (QHideEvent* event) {
  KCompletionBox::hideEvent (event);
  // KDE hides before emitting textActivated on a mouse click. Let that
  // synchronous acceptance win over cancellation caused by the same hide.
  std::uint64_t session= session_;
  if (session) QTimer::singleShot (0, this, [this, session] {
    if (session_ == session) finish (-1);
  });
}

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
      case Qt::Key_Up: case Qt::Key_Backtab: up (); break;
      case Qt::Key_Down: down (); break;
      case Qt::Key_PageUp: pageUp (); break;
      case Qt::Key_PageDown: pageDown (); break;
      case Qt::Key_Escape: cancel (); break;
      default: finish (std::max (0, currentRow ())); break;
      }
      return true;
    }
    if (event->type () == QEvent::KeyPress) cancel ();
  }
  return KCompletionBox::eventFilter (object, event);
}

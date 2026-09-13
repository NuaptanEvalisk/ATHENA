/******************************************************************************
* MODULE     : QTMChoiceNavigation.hpp
* DESCRIPTION: Cyclic wizard choices and list navigation from filter inputs
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include <QAbstractButton>
#include <QButtonGroup>
#include <QKeyEvent>
#include <QListWidget>
#include <initializer_list>

class QTMRadioChoiceNavigation: public QButtonGroup {
public:
  QTMRadioChoiceNavigation (QWidget* page, std::initializer_list<QAbstractButton*> choices):
    QButtonGroup (page) {
    for (auto* choice: choices) { addButton (choice); choice->installEventFilter (this); }
  }
protected:
  bool eventFilter (QObject* watched, QEvent* event) override {
    if (event->type () == QEvent::KeyPress) {
      auto* key= static_cast<QKeyEvent*> (event);
      if (key->modifiers () == Qt::NoModifier &&
          (key->key () == Qt::Key_Up || key->key () == Qt::Key_Down)) {
        const auto choices= buttons ();
        int index= choices.indexOf (qobject_cast<QAbstractButton*> (watched));
        if (index < 0) return false;
        const int count= int (choices.size ());
        const int step= key->key () == Qt::Key_Up ? -1 : 1;
        for (int i= 0; i<count; ++i) {
          index= (index + step + count) % count;
          auto* choice= choices[index];
          if (!choice->isEnabled () || !choice->isVisible ()) continue;
          choice->setFocus (Qt::OtherFocusReason);
          choice->click ();
          break;
        }
        return true;
      }
    }
    return QButtonGroup::eventFilter (watched, event);
  }
};

class QTMListChoiceNavigation: public QObject {
  QListWidget* list;
public:
  QTMListChoiceNavigation (QListWidget* list, std::initializer_list<QWidget*> inputs):
    QObject (list), list (list) {
    list->installEventFilter (this);
    for (auto* input: inputs) input->installEventFilter (this);
  }
protected:
  bool eventFilter (QObject* watched, QEvent* event) override {
    if (event->type () == QEvent::KeyPress) {
      auto* key= static_cast<QKeyEvent*> (event);
      if (key->modifiers () == Qt::NoModifier &&
          (key->key () == Qt::Key_Up || key->key () == Qt::Key_Down)) {
        const int count= list->count ();
        if (count) {
          const int step= key->key () == Qt::Key_Up ? -1 : 1;
          int row= list->currentRow ();
          if (row < 0) row= step > 0 ? -1 : 0;
          list->setCurrentRow ((row + step + count) % count);
          list->scrollToItem (list->currentItem ());
        }
        return true;
      }
    }
    return QObject::eventFilter (watched, event);
  }
};

/******************************************************************************
* MODULE     : QTMCompletingComboBox.hpp
* DESCRIPTION: Editable combo box with explicit keyboard completion commits
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMCOMPLETINGCOMBOBOX_HPP
#define QTMCOMPLETINGCOMBOBOX_HPP

#include <QAbstractItemView>
#include <QComboBox>
#include <QCompleter>
#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>

inline bool
qtm_commit_list_completion (QLineEdit* edit, QListWidget* list, int role) {
  if (edit == nullptr || list == nullptr) return false;
  QListWidgetItem* item= list->currentItem ();
  if (item == nullptr && list->count () > 0) item= list->item (0);
  if (item == nullptr) return false;

  const QString completion= item->data (role).toString ();
  if (completion.isEmpty () || completion == edit->text ()) return false;
  edit->setText (completion);
  edit->setCursorPosition (completion.size ());
  edit->deselect ();
  return true;
}

class QTMCompletingComboBox: public QComboBox {
public:
  explicit QTMCompletingComboBox (QWidget* parent= nullptr)
    : QComboBox (parent) {
    setEditable (true);
    completer ()->setCompletionMode (QCompleter::PopupCompletion);
    lineEdit ()->installEventFilter (this);
    if (completer ()->popup () != nullptr) {
      completer ()->popup ()->installEventFilter (this);
      completer ()->popup ()->viewport ()->installEventFilter (this);
    }
  }

  bool commitCurrentCompletion () {
    QCompleter* c= completer ();
    QLineEdit* edit= lineEdit ();
    if (c == nullptr || edit == nullptr) return false;

    QModelIndex current;
    if (c->popup () != nullptr && c->popup ()->isVisible ()) {
      current= c->popup ()->currentIndex ();
    }
    if (!current.isValid ()) current= c->currentIndex ();

    QString completion;
    if (current.isValid ())
      completion= current.data (c->completionRole ()).toString ();
    if (completion.isEmpty ()) completion= c->currentCompletion ();
    if (completion.isEmpty ()) return false;

    const int index= findText (completion, Qt::MatchFixedString);
    if (index >= 0) setCurrentIndex (index);
    else setEditText (completion);
    edit->setCursorPosition (edit->text ().size ());
    edit->deselect ();
    c->setCompletionPrefix (edit->text ());
    if (c->popup () != nullptr) c->popup ()->hide ();
    return true;
  }

protected:
  bool eventFilter (QObject* watched, QEvent* event) override {
    QAbstractItemView* popup= completer () == nullptr ? nullptr :
      completer ()->popup ();
    const bool completionTarget= watched == lineEdit () || watched == popup ||
      (popup != nullptr && watched == popup->viewport ());
    if (completionTarget &&
        (event->type () == QEvent::ShortcutOverride ||
         event->type () == QEvent::KeyPress)) {
      QKeyEvent* key= static_cast<QKeyEvent*> (event);
      const bool commitKey= key->key () == Qt::Key_Tab ||
        key->key () == Qt::Key_Enter || key->key () == Qt::Key_Return;
      if (commitKey && hasPendingCompletion ()) {
        if (event->type () == QEvent::ShortcutOverride) {
          event->accept ();
          return true;
        }
        if (commitCurrentCompletion ()) {
          event->accept ();
          return true;
        }
      }
    }
    return QComboBox::eventFilter (watched, event);
  }

private:
  bool hasPendingCompletion () const {
    QCompleter* c= completer ();
    QLineEdit* edit= lineEdit ();
    if (c == nullptr || edit == nullptr) return false;
    const bool modelCompletion= !c->currentCompletion ().isEmpty () &&
      c->currentCompletion () != edit->text ();
    return modelCompletion || edit->hasSelectedText () ||
      (c->popup () != nullptr && c->popup ()->isVisible () &&
       c->popup ()->currentIndex ().isValid ());
  }
};

#endif // QTMCOMPLETINGCOMBOBOX_HPP

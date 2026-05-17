/******************************************************************************
* MODULE     : QTMBufferSwitcher.cpp
* DESCRIPTION: Visual Studio style document buffer switcher
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMBufferSwitcher.hpp"
#include "QTMMainTabWindow.hpp"
#include "qt_utilities.hpp"

#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPointer>
#include <QShowEvent>
#include <QVBoxLayout>

#include <algorithm>
#include <iostream>
#include <vector>

static std::vector<QPointer<QWidget> > buffer_switcher_mru;

void
buffer_switcher_note_widget (QWidget* widget) {
  if (widget == nullptr) return;

  buffer_switcher_mru.erase (
    std::remove_if (buffer_switcher_mru.begin (), buffer_switcher_mru.end (),
                    [widget] (const QPointer<QWidget>& ptr) {
                      return ptr.isNull () || ptr.data () == widget;
                    }),
    buffer_switcher_mru.end ());
  buffer_switcher_mru.insert (buffer_switcher_mru.begin (), widget);
}

class QTMBufferSwitcher : public QDialog {
public:
  explicit QTMBufferSwitcher (QTMMainTabWindow* win, QWidget* parent= nullptr)
    : QDialog (parent), mainWindow (win), list (new QListWidget (this)),
      acceptedWidget (nullptr), payloadMode (false) {
    initializeUi ();
    populateWidgets ();
  }

  explicit QTMBufferSwitcher (array<string> entries, QWidget* parent= nullptr)
    : QDialog (parent), mainWindow (nullptr), list (new QListWidget (this)),
      acceptedWidget (nullptr), payloadMode (true) {
    initializeUi ();
    populateEntries (entries);
  }

  void initializeUi () {
    setWindowTitle ("Switch buffer");
    setWindowFlags (windowFlags () | Qt::Tool | Qt::FramelessWindowHint);
    setModal (true);
    resize (520, 360);

    QVBoxLayout* layout= new QVBoxLayout (this);
    layout->setContentsMargins (12, 12, 12, 12);
    layout->setSpacing (8);

    QLabel* title= new QLabel ("Switch buffer", this);
    QFont titleFont= title->font ();
    titleFont.setBold (true);
    title->setFont (titleFont);
    layout->addWidget (title);

    list->setFrameShape (QFrame::NoFrame);
    list->setUniformItemSizes (true);
    list->installEventFilter (this);
    layout->addWidget (list);

    QLabel* hint= new QLabel (
      "Hold Ctrl and press Tab to cycle. Release Ctrl to switch.", this);
    hint->setStyleSheet ("color: palette(mid);");
    layout->addWidget (hint);

    connect (list, &QListWidget::itemDoubleClicked, this,
             [this] (QListWidgetItem*) { acceptCurrent (); });
  }

  QWidget* selectedWidget () const { return acceptedWidget; }
  QString selectedPayload () const { return acceptedPayload; }

protected:
  void showEvent (QShowEvent* event) override {
    QDialog::showEvent (event);
    QWidget* w= parentWidget ();
    if (w == nullptr) w= QApplication::activeWindow ();
    if (w != nullptr) {
      QRect r= geometry ();
      r.moveCenter (w->geometry ().center ());
      move (r.topLeft ());
    }
    list->setFocus ();
  }

  void keyPressEvent (QKeyEvent* event) override {
    if (event->key () == Qt::Key_Escape) {
      reject ();
      return;
    }
    if (event->key () == Qt::Key_Return || event->key () == Qt::Key_Enter) {
      acceptCurrent ();
      return;
    }
    if (event->key () == Qt::Key_Tab ||
        event->key () == Qt::Key_Backtab) {
      cycle (1);
      return;
    }
    if (event->key () == Qt::Key_Up) {
      cycle (-1);
      return;
    }
    if (event->key () == Qt::Key_Down) {
      cycle (1);
      return;
    }
    QDialog::keyPressEvent (event);
  }

  void keyReleaseEvent (QKeyEvent* event) override {
    if (event->key () == Qt::Key_Control ||
        event->key () == Qt::Key_Meta) {
      acceptCurrent ();
      return;
    }
    QDialog::keyReleaseEvent (event);
  }

  bool eventFilter (QObject* watched, QEvent* event) override {
    if (watched == list) {
      if (event->type () == QEvent::KeyPress) {
        keyPressEvent (static_cast<QKeyEvent*> (event));
        return true;
      }
      if (event->type () == QEvent::KeyRelease) {
        keyReleaseEvent (static_cast<QKeyEvent*> (event));
        return true;
      }
    }
    return QDialog::eventFilter (watched, event);
  }

private:
  void populateWidgets () {
    if (mainWindow == nullptr) return;

    QList<QWidget*> open= mainWindow->documentWidgets ();
    QWidget* current= mainWindow->currentDocumentWidget ();
    QList<QWidget*> ordered;

    for (const QPointer<QWidget>& ptr : buffer_switcher_mru) {
      QWidget* widget= ptr.data ();
      if (widget != nullptr && open.contains (widget) &&
          !ordered.contains (widget))
        ordered.append (widget);
    }
    for (QWidget* widget : open)
      if (widget != nullptr && !ordered.contains (widget))
        ordered.append (widget);

    for (QWidget* widget : ordered) {
      QListWidgetItem* item=
        new QListWidgetItem (mainWindow->documentWidgetTitle (widget));
      item->setData (Qt::UserRole,
                     QVariant::fromValue (reinterpret_cast<quintptr> (widget)));
      list->addItem (item);
    }

    int row= 0;
    if (list->count () > 1 && current == ordered.value (0)) row= 1;
    list->setCurrentRow (row);
  }

  void populateEntries (array<string> entries) {
    for (int i=0; i+2<N(entries); i += 3) {
      QString payload= to_qstring (entries[i]);
      QString label= to_qstring (entries[i + 1]);
      QString detail= to_qstring (entries[i + 2]);

      QListWidgetItem* item= new QListWidgetItem (label);
      item->setToolTip (detail);
      item->setData (Qt::UserRole, payload);
      list->addItem (item);
    }

    int row= list->count () > 1 ? 1 : 0;
    if (list->count () > 0) list->setCurrentRow (row);
  }

  QWidget* widgetAt (int row) const {
    QListWidgetItem* item= list->item (row);
    if (item == nullptr) return nullptr;
    quintptr raw= item->data (Qt::UserRole).value<quintptr> ();
    return reinterpret_cast<QWidget*> (raw);
  }

  void cycle (int delta) {
    int n= list->count ();
    if (n <= 0) return;
    int row= list->currentRow ();
    if (row < 0) row= 0;
    row= (row + delta + n) % n;
    list->setCurrentRow (row);
  }

  void acceptCurrent () {
    if (payloadMode) {
      QListWidgetItem* item= list->item (list->currentRow ());
      if (item == nullptr) return;
      acceptedPayload= item->data (Qt::UserRole).toString ();
      if (!acceptedPayload.isEmpty ()) accept ();
    }
    else {
      acceptedWidget= widgetAt (list->currentRow ());
      if (acceptedWidget != nullptr) accept ();
    }
  }

  QTMMainTabWindow* mainWindow;
  QListWidget*      list;
  QWidget*          acceptedWidget;
  QString           acceptedPayload;
  bool              payloadMode;
};

void
visual_buffer_switcher_show () {
  QTMMainTabWindow* win= QTMMainTabWindow::topTabWindow ();
  if (win == nullptr) {
    std::cout << "ATHENA] buffer switcher: no top tab window" << std::endl;
    return;
  }

  QList<QWidget*> docs= win->documentWidgets ();
  QWidget* current= win->currentDocumentWidget ();
  std::cout << "ATHENA] buffer switcher: invoked, documents="
            << docs.size ();
  if (current != nullptr)
    std::cout << ", current=\""
              << win->documentWidgetTitle (current).toStdString () << "\"";
  std::cout << std::endl;

  if (docs.size () <= 1) {
    std::cout << "ATHENA] buffer switcher: not showing, need at least two "
              << "document buffers" << std::endl;
    return;
  }

  QWidget* parent= QApplication::activeWindow ();
  QTMBufferSwitcher switcher (win, parent);
  if (switcher.exec () == QDialog::Accepted) {
    QWidget* selected= switcher.selectedWidget ();
    if (selected != nullptr) {
      buffer_switcher_note_widget (selected);
      win->activateDocumentWidget (selected);
    }
  }
}

string
visual_buffer_switcher_choose (array<string> entries) {
  std::cout << "ATHENA] buffer switcher: invoked, session entries="
            << (N(entries) / 3) << std::endl;

  if (N(entries) <= 3) {
    std::cout << "ATHENA] buffer switcher: not showing, need at least two "
              << "session buffers" << std::endl;
    return "";
  }

  QWidget* parent= QApplication::activeWindow ();
  QTMBufferSwitcher switcher (entries, parent);
  if (switcher.exec () == QDialog::Accepted)
    return from_qstring (switcher.selectedPayload ());
  return "";
}

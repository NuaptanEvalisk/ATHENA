
/******************************************************************************
* MODULE     : QTMGuiHelper.cpp
* DESCRIPTION: QT Gui helper class.
*              Infrastructure for delayed menu installation 
* COPYRIGHT  : (C) 2008 Massimiliano Gubinelli
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "UI/Qt/Shell/QTMGuiHelper.hpp"
#include "UI/Qt/Toolkit/qt_tm_widget.hpp"
#include "UI/Qt/Toolkit/qt_utilities.hpp"
#include "App/Frontend/application_lifecycle.hpp"
#include "iterator.hpp"
#include <QFileOpenEvent>

void
QTMGuiHelper::doUpdate () {
BEGIN_SLOT
  // cout << "UPDATE " << texmacs_time () << LF;
  gui->update();
END_SLOT
}

void
QTMGuiHelper::doRefresh () {
BEGIN_SLOT
  emit refresh();
END_SLOT
}

bool
QTMGuiHelper::eventFilter (QObject *obj, QEvent *event) {
  if (event->type() == QEvent::FileOpen) {
    static bool new_window_flag= false;
    QFileOpenEvent* openEvent = static_cast<QFileOpenEvent *>(event);
    QByteArray tmp= openEvent->file().toUtf8();
    const string s (tmp.constData(), tmp.size());
    const char *win= new_window_flag? ":new-window": ":current-window";
    if (DEBUG_EVENTS)
      debug_events << "File open event: " << s << " " << win << LF;
    const std::string filename (tmp.constData (), tmp.size ());
    const bool new_window= new_window_flag;
    // OS open events may arrive before startup creates the first editor.
    qt_schedule_editor_task (0, [filename, new_window] {
      const url source= url_system (string (filename.c_str ()));
      if (new_window) application_open_window_url (source);
      else application_open_document_url (source);
      return -1;
    });
    new_window_flag= true;
    return true;
  } else {
      // standard event processing
    return QObject::eventFilter (obj, event);
  }
}

void
QTMGuiHelper::aboutToShowMainMenu() {
BEGIN_SLOT
  gui->refresh_external_clipboard_cache ();
END_SLOT
}

void 
QTMGuiHelper::aboutToHideMainMenu() {
BEGIN_SLOT
  QTMWidget::setFocusToLast();
END_SLOT
}

void
QTMGuiHelper::emitTmSlotRefresh (string kind) {
BEGIN_SLOT
  emit tmSlotRefresh (kind);
END_SLOT
}

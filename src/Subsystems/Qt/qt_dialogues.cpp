/******************************************************************************
* MODULE     : qt_dialogues.cpp
* DESCRIPTION: Inline text input widgets used by menus and toolbars
* COPYRIGHT  : (C) 2008  Massimiliano Gubinelli
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "qt_dialogues.hpp"

#include "QTMMenuHelper.hpp"
#include "qt_window_widget.hpp"
#include "scheme.hpp"

#include <QCompleter>
#include <QDir>
#include <QFileSystemModel>

qt_input_text_widget_rep::qt_input_text_widget_rep (
    command _cmd, string _type, array<string> _proposals,
    int _style, string _width):
  qt_widget_rep (input_widget), cmd (_cmd), type (_type),
  proposals (_proposals), input (""), style (_style), width (_width),
  ok (false), done (false) {
  if (type == "password") proposals= array<string> (0);
  if (N(proposals) > 0) input= proposals[0];
}

QAction*
qt_input_text_widget_rep::as_qaction () {
  return new QTMWidgetAction (this);
}

QWidget*
qt_input_text_widget_rep::as_qwidget (QWidget* parent_widget) {
  QTMLineEdit* le= new QTMLineEdit (parent_widget, type, width, style, cmd);
  qwid= le;
  bool can_autocommit= !(ends (type, "search") ||
                         ends (type, "replace") ||
                         starts (type, "interactive"));
  QTMInputTextWidgetHelper* helper=
    new QTMInputTextWidgetHelper (this, can_autocommit);
  (void) helper;
  le->setText (to_qstring (input));
  le->setObjectName (to_qstring (type));
  if (ends (type, "file") || type == "directory") {
    QCompleter* completer= new QCompleter (le);
    QFileSystemModel* fsModel= new QFileSystemModel (le);
    fsModel->setRootPath (QDir::homePath ());
    completer->setModel (fsModel);
    le->setCompleter (completer);
  }
  else if (type != "password" && N(proposals) > 0 &&
           !(N(proposals) == 1 && N(proposals[0]) == 0)) {
    QCompleter* completer= new QCompleter (to_qstringlist (proposals), le);
    completer->setCaseSensitivity (Qt::CaseSensitive);
    completer->setCompletionMode (QCompleter::InlineCompletion);
    le->setCompleter (completer);
  }
  return qwid;
}

void
qt_input_text_widget_rep::commit (bool flag) {
  QTMLineEdit* le= qobject_cast<QTMLineEdit*> (qwid);
  widget_rep* win= qt_window_widget_rep::widget_from_qwidget (le);
  if (flag) {
    done= false;
    ok= true;
    input= from_qstring (le->text ());
  }
  else le->setText (to_qstring (input));
  if (win) {
    if (done) return;
    done= true;
    the_gui->process_command (
      cmd, ok ? list_object (object (input)): list_object (object (false)));
  }
}

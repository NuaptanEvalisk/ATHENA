
/******************************************************************************
* MODULE     : qt_dialogues.cpp
* DESCRIPTION: Widgets for automatically created dialogues (questions in popups)
* COPYRIGHT  : (C) 2008  Massimiliano Gubinelli
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "widget.hpp"
#include "message.hpp"
#include "qt_dialogues.hpp"
#include "qt_utilities.hpp"
#include "qt_tm_widget.hpp"
#include "qt_chooser_widget.hpp"
#include "qt_color_picker_widget.hpp"
#include "url.hpp"
#include "analyze.hpp"
#include "converter.hpp"
#include "QTMMenuHelper.hpp"
#include "QTMGuiHelper.hpp"
#include "server.hpp"

#include <QMessageBox>
#include <QCoreApplication>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QCompleter>
#include <QFileSystemModel>
#include <QDir>
#include <QVector>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QApplication>
#include <QGridLayout>
#include <QSpacerItem>
#include <QMetaObject>
#include <QThread>

#include <memory>
#include <mutex>
#include <unordered_map>

#include "string.hpp"
#include "scheme.hpp"


/******************************************************************************
 * qt_field_widget_rep
 ******************************************************************************/

qt_field_widget_rep::qt_field_widget_rep (qt_inputs_list_widget_rep* _parent,
                                          string _prompt)
  : qt_widget_rep (field_widget),
    prompt (_prompt), input (""), proposals (), parent (_parent)
{ }

void
qt_field_widget_rep::send (slot s, blackbox val) {
  if (DEBUG_QT_WIDGETS)
    debug_widgets << "qt_field_widget_rep::send " << slot_name(s) << LF;
  switch (s) {
  case SLOT_STRING_INPUT:
    check_type<string>(val, s);
    input= scm_quote (open_box<string> (val));
    break;
  case SLOT_INPUT_TYPE:
    check_type<string>(val, s);
    type= open_box<string> (val);
    break;
  case SLOT_INPUT_PROPOSAL:
    check_type<string>(val, s);
    proposals << open_box<string> (val);
    break;
  case SLOT_KEYBOARD_FOCUS:
    parent->send (s, val);
    break;
  default:
    qt_widget_rep::send (s, val);
  }
}

blackbox
qt_field_widget_rep::query (slot s, int type_id) {
  if (DEBUG_QT_WIDGETS)
    debug_widgets << "qt_field_widget_rep::query " << slot_name(s) << LF;
  switch (s) {
  case SLOT_STRING_INPUT:
    check_type_id<string> (type_id, s);
    return close_box<string> (input);
  default:
    return qt_widget_rep::query (s, type_id);
  }
}

QWidget*
qt_field_widget_rep::as_qwidget (QWidget* parent_widget) {
  qwid = new QWidget (parent_widget);
  
  QHBoxLayout* hl = new QHBoxLayout (qwid);
  QLabel*     lab = new QLabel (to_qstring (prompt), qwid);
  
  qwid->setLayout (hl);
  hl->addWidget (lab, 0, Qt::AlignRight);

  if (ends (type, "file") || type == "directory") {
    widget wid    = input_text_widget (command(), type,
                                       array<string>(0), 0, "20em");
    QLineEdit* le = qobject_cast<QTMLineEdit*> (concrete(wid)->as_qwidget(qwid));
    ASSERT (le != NULL, "qt_field_widget_rep: expecting QTMLineEdit");
    le->setObjectName (to_qstring (type));
    lab->setBuddy (le);
    hl->addWidget (le);
  } 
  else if (type == "password") {
    QTMLineEdit* le= new QTMLineEdit (qwid, "password", "20em", 0);
    QTMFieldWidgetHelper* helper = new QTMFieldWidgetHelper (this, le);
    (void) helper;
    le->setCompleter (0);
    lab->setBuddy (le);
    hl->addWidget (le);
  }
  else {
    QTMComboBox* cb              = new QTMComboBox (qwid);
    QTMFieldWidgetHelper* helper = new QTMFieldWidgetHelper (this, cb);
    (void) helper;
    cb->addItems (to_qstringlist (proposals));
    cb->setEditText (to_qstring (scm_unquote (input)));
    cb->setEditable (true);
    cb->setLineEdit (new QTMLineEdit (cb, type, "1w", WIDGET_STYLE_MINI));
    cb->setSizeAdjustPolicy (QComboBox::AdjustToContents);
    cb->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Fixed);
    cb->setMinimumWidth (80);
    cb->setDuplicatesEnabled (true); 
    cb->completer()->setCaseSensitivity (Qt::CaseSensitive);
    if (N(type) == 0) cb->setObjectName ("default focus target");
    else              cb->setObjectName (to_qstring (type));
    lab->setBuddy (cb);
    hl->addWidget (cb);
  }
  return qwid;
}


/******************************************************************************
 * qt_inputs_list_widget_rep
 ******************************************************************************/

#ifdef Q_OS_MAC
#include <QKeyEvent>

/*! An event filter to circumvent a Qt Mac bug in QMessageBox.
 
 Pressing tab has no effect on QMessageBox dialogs under MacOS.
 See e.g. https://bugreports.qt-project.org/browse/QTBUG-13330
 
 The bug is present at least in versions >= 4.6.1 and <= 4.8.5
 */
class QTMFilterHack : public QWidget {
  typedef QList<QAbstractButton*> ButtonList;
  ButtonList buttons;
  int current;
  int N;
public:
  QTMFilterHack (ButtonList _buttons) : buttons (_buttons), current (1),
  N (_buttons.size()) { }
  bool eventFilter(QObject *target, QEvent *event)
  {
    if (event->type() == QEvent::KeyPress) {
      QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
      if (keyEvent->key() == Qt::Key_Tab) {
        if (keyEvent->modifiers() & Qt::ShiftModifier)
          current = current - 1 < 0 ? N-1 : current - 1;
        else
          current = current + 1 >= N? 0 : current + 1;
        buttons[current]->setFocus (Qt::TabFocusReason);
        return true;
      }
    }
    return QWidget::eventFilter (target, event);
  }
};
#endif

qt_inputs_list_widget_rep::qt_inputs_list_widget_rep (command _cmd,
                                                      array<string> _prompts)
: qt_widget_rep (input_widget), cmd (_cmd), size (coord2 (100, 100)),
  position (coord2 (0, 0)), win_title (""), style (0)
{
  for (int i = 0; i < N(_prompts); i++)
    add_child (tm_new<qt_field_widget_rep> ((qt_inputs_list_widget_rep*)this, _prompts[i]));
}

widget
qt_inputs_list_widget_rep::plain_window_widget (string s, command q, int b) {
  (void) b;
  (void) q; // The widget already has a command (dialogue_command)
  win_title = s;
  return this;
}

void
qt_inputs_list_widget_rep::send (slot s, blackbox val) {
  if (DEBUG_QT_WIDGETS)
    debug_widgets << "qt_inputs_list_widget_rep::send " << slot_name(s) << LF;

  switch (s) {
  case SLOT_VISIBILITY:
    {   
      check_type<bool> (val, s);
      bool flag = open_box<bool> (val);
      (void) flag;
      NOT_IMPLEMENTED("qt_inputs_list_widget::SLOT_VISIBILITY")
    }   
    break;
  case SLOT_SIZE:
    check_type<coord2> (val, s);
    size = open_box<coord2> (val);
    break;
  case SLOT_POSITION:
    check_type<coord2> (val, s);
    position = open_box<coord2> (val);
    break;
  case SLOT_KEYBOARD_FOCUS:
    check_type<bool> (val, s);
    perform_dialog ();
    break;
  default:
    qt_widget_rep::send (s, val);
  }
}

blackbox
qt_inputs_list_widget_rep::query (slot s, int type_id) {
  if (DEBUG_QT_WIDGETS)
    debug_widgets << "qt_inputs_list_widget_rep::query " << slot_name(s) << LF;
  switch (s) {
  case SLOT_POSITION:
    {
      check_type_id<coord2> (type_id, s);
      return close_box<coord2> (position);
    }
  case SLOT_SIZE:
    {
      check_type_id<coord2> (type_id, s);
      return close_box<coord2> (size);
    }
  case SLOT_STRING_INPUT:
    if (N(children) > 0) return field(0)->query (s, type_id);
  default:
    return qt_widget_rep::query (s, type_id);
  }
}

widget
qt_inputs_list_widget_rep::read (slot s, blackbox val) {
  if (DEBUG_QT_WIDGETS)
    debug_widgets << "qt_inputs_list_widget_rep::read " << slot_name(s) << LF;
  switch (s) {
  case SLOT_WINDOW:
    check_type_void (val, s);
    return this;
  case SLOT_FORM_FIELD:
  {
    check_type<int> (val, s);
    int index = open_box<int> (val);
    if (N(children) > index)
      return static_cast<widget_rep*> (children[index].rep);
  }
  default:
    return qt_widget_rep::read (s, val);
  }
}

qt_field_widget_rep*
qt_inputs_list_widget_rep::field (int i) {
  return static_cast<qt_field_widget_rep*> (children[i].rep);
}

static bool
question_proposals_are_yes_no (const array<string>& proposals, int& yes, int& no) {
  yes = -1;
  no = -1;
  int choices = N(proposals);
  if (choices != 2) return false;

  for (int i=0; i<choices; i++) {
    if (get_server ()->is_yes (proposals[i])) yes = i;
    else no = i;
  }
  return yes >= 0 && no >= 0;
}

namespace {

struct QuestionDialogRequest {
  string prompt;
  array<string> proposals;
  int selected= -1;
};

std::mutex questionDialogRequestMutex;
std::unordered_map<std::uint64_t, std::unique_ptr<QuestionDialogRequest> >
  questionDialogRequests;
std::uint64_t nextQuestionDialogRequestId= 1;

int
runQuestionDialog (const string& prompt, const array<string>& proposals) {
  QWidget* mainwindow= QApplication::activeWindow ();
  QMessageBox msgBox (mainwindow);
  msgBox.setMinimumWidth (620);
  msgBox.setText (to_qstring (prompt));
  msgBox.setTextFormat (Qt::PlainText);
  for (QLabel* label: msgBox.findChildren<QLabel*> ())
    label->setWordWrap (true);
  if (QGridLayout* layout= qobject_cast<QGridLayout*> (msgBox.layout ())) {
    QSpacerItem* spacer=
      new QSpacerItem (560, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
    layout->addItem (spacer, layout->rowCount (), 0, 1,
                     layout->columnCount ());
  }

  int yes_index= -1;
  int no_index= -1;
  if (question_proposals_are_yes_no (proposals, yes_index, no_index)) {
    msgBox.setStandardButtons (QMessageBox::Yes | QMessageBox::No |
                               QMessageBox::Cancel);
    msgBox.setDefaultButton (yes_index == 0 ? QMessageBox::Yes :
                                              QMessageBox::No);
    msgBox.setWindowTitle (QStringLiteral ("Question"));
    msgBox.setIcon (QMessageBox::Question);
    int result= msgBox.exec ();
    if (result == QMessageBox::Yes) return yes_index;
    if (result == QMessageBox::No) return no_index;
    return -1;
  }

  msgBox.setStandardButtons (QMessageBox::Cancel);
  QVector<QPushButton*> buttons (N (proposals));
  for (int i= 0; i < N (proposals); ++i) {
    string label= "&" * upcase_first (proposals[i]);
    buttons[i]= msgBox.addButton (to_qstring (label),
                                  QMessageBox::ActionRole);
  }
  if (!buttons.isEmpty ()) {
    msgBox.setDefaultButton (buttons[0]);
    for (int i= 0; i + 1 < buttons.size (); ++i)
      QWidget::setTabOrder (buttons[i], buttons[i + 1]);
    QWidget::setTabOrder (buttons.back (), msgBox.escapeButton ());
  }
  msgBox.setWindowTitle (QStringLiteral ("Question"));
  msgBox.setIcon (QMessageBox::Question);
#ifdef Q_OS_MAC
  QTMFilterHack filter (msgBox.buttons ());
  msgBox.installEventFilter (&filter);
#endif
  msgBox.exec ();
  for (int i= 0; i < buttons.size (); ++i)
    if (msgBox.clickedButton () == buttons[i]) return i;
  return -1;
}

std::uint64_t
registerQuestionDialogRequest (string prompt, array<string> proposals) {
  prompt.ensure_transferable ();
  for (int i= 0; i < N (proposals); ++i)
    proposals[i].ensure_transferable ();
  auto request= std::make_unique<QuestionDialogRequest> ();
  request->prompt= std::move (prompt);
  request->proposals= std::move (proposals);
  std::lock_guard<std::mutex> guard (questionDialogRequestMutex);
  std::uint64_t id= nextQuestionDialogRequestId++;
  if (id == 0) id= nextQuestionDialogRequestId++;
  questionDialogRequests.emplace (id, std::move (request));
  return id;
}

void
executeQuestionDialogRequest (std::uint64_t id) {
  QuestionDialogRequest* request= nullptr;
  {
    std::lock_guard<std::mutex> guard (questionDialogRequestMutex);
    auto found= questionDialogRequests.find (id);
    if (found == questionDialogRequests.end ()) return;
    request= found->second.get ();
  }
  request->selected=
    runQuestionDialog (request->prompt, request->proposals);
}

int
takeQuestionDialogResult (std::uint64_t id) {
  std::unique_ptr<QuestionDialogRequest> request;
  {
    std::lock_guard<std::mutex> guard (questionDialogRequestMutex);
    auto found= questionDialogRequests.find (id);
    if (found == questionDialogRequests.end ()) return -1;
    request= std::move (found->second);
    questionDialogRequests.erase (found);
  }
  return request->selected;
}

int
showQuestionDialog (string prompt, array<string> proposals) {
  QCoreApplication* app= QCoreApplication::instance ();
  if (app == nullptr || QThread::currentThread () == app->thread ())
    return runQuestionDialog (prompt, proposals);

  std::uint64_t requestId= registerQuestionDialogRequest (
    std::move (prompt), std::move (proposals));
  bool invoked= QMetaObject::invokeMethod (
    app, [requestId] () { executeQuestionDialogRequest (requestId); },
    Qt::BlockingQueuedConnection);
  if (!invoked) {
    (void) takeQuestionDialogResult (requestId);
    return -1;
  }
  return takeQuestionDialogResult (requestId);
}

} // namespace

void
qt_inputs_list_widget_rep::perform_dialog() {
  if ((N(children)==1) && (field(0)->type == "question")) {
    int selected= showQuestionDialog (
      copy (field(0)->prompt), copy (field(0)->proposals));
    field(0)->input= selected >= 0 && selected < N (field(0)->proposals) ?
      scm_quote (field(0)->proposals[selected]) : string ("#f");
  }
  
  else {  //usual dialog layout
    QDialog d (0, Qt::Sheet);
    QVBoxLayout* vl = new QVBoxLayout(&d);
    QVector<QWidget*> widgets;
    for(int i = 0; i < N(children); ++i) {
      widgets.push_back (field(i)->as_qwidget(&d));
      vl->addWidget(widgets[i]);
    }
    for (int i = 0; i < N(children) - 1; ++i)
      QWidget::setTabOrder (widgets[i], widgets[i+1]);
    
    QDialogButtonBox* buttonBox =
          new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                Qt::Horizontal, &d);
    QObject::connect (buttonBox, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    QObject::connect (buttonBox, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    vl->addWidget (buttonBox);
    
    d.setWindowTitle (to_qstring (win_title)); 
    d.updateGeometry();
    QRect r;
    r.setSize (d.sizeHint ());
    r.moveCenter (to_qpoint (position));
    d.setGeometry (r);
    d.setSizePolicy (QSizePolicy::Preferred, QSizePolicy::Fixed);
    
    if (d.exec() != QDialog::Accepted)
      for(int i=0; i < N(children); ++i)
        field(i)->input = "#f";
  }

  if (!is_nil(cmd)) cmd ();
}


/******************************************************************************
 * qt_input_text_widget_rep
 ******************************************************************************/

qt_input_text_widget_rep::qt_input_text_widget_rep (command _cmd,
                                                    string _type,
                                                    array<string> _proposals,
                                                    int _style,
                                                    string _width)
: qt_widget_rep (input_widget), cmd (_cmd), type (_type),
  proposals (_proposals), input (""), style (_style), width (_width),
  ok (false), done (false)
{
  if (type == "password") proposals = array<string> (0);
  if (N(proposals) > 0) input = proposals[0];
}

QAction*
qt_input_text_widget_rep::as_qaction () {
  return new QTMWidgetAction (this);
}

/*!
 Returns a QTMLineEdit with the proper completer and the helper object to
 keep it in sync with us.
 */
QWidget*
qt_input_text_widget_rep::as_qwidget (QWidget* parent_widget) {
  QTMLineEdit* le = new QTMLineEdit (parent_widget, type, width, style, cmd);
  qwid = le;
  bool can_autocommit= !(ends (type, "search") ||
                         ends (type, "replace") ||
                         ends (type, "replace") ||
                         starts (type, "interactive"));
  QTMInputTextWidgetHelper* helper =
    new QTMInputTextWidgetHelper (this, can_autocommit);
  (void) helper;
  le->setText (to_qstring (input));
  le->setObjectName (to_qstring (type));
  if (ends (type, "file") || type == "directory") {
    QCompleter*     completer = new QCompleter(le);
    QFileSystemModel* fsModel = new QFileSystemModel(le);
    fsModel->setRootPath (QDir::homePath());// This is NOT the starting location
    completer->setModel (fsModel);
    le->setCompleter (completer);
  }
  else if (type != "password" && N(proposals) > 0 && ! (N(proposals) == 1 && N(proposals[0]) == 0)){
    //else if (N(proposals) > 0 && ! (N(proposals) == 1 && N(proposals[0]) == 0)){
    QCompleter* completer = new QCompleter (to_qstringlist(proposals), le);
    completer->setCaseSensitivity (Qt::CaseSensitive);
    completer->setCompletionMode (QCompleter::InlineCompletion);
    le->setCompleter (completer);
  }
  return qwid;
}

void
qt_input_text_widget_rep::commit(bool flag) {
  QTMLineEdit* le = qobject_cast<QTMLineEdit*>(qwid);
  widget_rep* win = qt_window_widget_rep::widget_from_qwidget (le);

  if (flag) {
    done         = false;
    ok    = true;
    input = from_qstring (le->text());
  } else {
    le->setText (to_qstring (input));
  }
  if (win) // This is 0 inside a dialog => no command
  {
    if (done) return;
    done = true;
    the_gui->process_command (cmd, ok
                              ? list_object (object (input))
                              : list_object (object (false)));
  }
}

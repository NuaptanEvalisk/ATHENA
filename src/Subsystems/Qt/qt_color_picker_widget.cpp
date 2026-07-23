
/******************************************************************************
 * MODULE     : qt_color_picker_widget.cpp
 * DESCRIPTION: 
 * COPYRIGHT  : (C) 2010 Miguel de Benito Delgado
 *******************************************************************************
 * This software falls under the GNU general public license version 3 or later.
 * It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
 * in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
 ******************************************************************************/

#include "qt_color_picker_widget.hpp"
#include "qt_utilities.hpp"

#include "message.hpp"
#include "scheme.hpp"

#include <QColorDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QSet>
#include <QToolButton>
#include <QVBoxLayout>


/**
 * Needed for whitebox_rep::display
 */
inline tm_ostream& 
operator << (tm_ostream& out, const QColor& col) {
  return out << "Color: " << from_qcolor (col) << "\n";
}

qt_color_picker_widget_rep::qt_color_picker_widget_rep 
  (command call_back, bool pickPattern, array<tree> proposals)
: _commandAfterExecution(call_back), _pickPattern(pickPattern),
  _proposals(proposals) {}

qt_color_picker_widget_rep::~qt_color_picker_widget_rep() { }

void
qt_color_picker_widget_rep::send (slot s, blackbox val) {
  if (DEBUG_QT_WIDGETS)
    debug_widgets << "qt_color_picker_widget_rep::send " << slot_name(s) << LF;
  switch (s) {
    case SLOT_VISIBILITY:   // Activates the widget
      check_type<bool>(val, s);
      if (open_box<bool>(val) == true)
        showDialog();
      break;
    default:
      qt_widget_rep::send (s, val);
  }
}

/*!
 window_create() expects this method in widgets which implement windows
 @note: name is a unique identifier for the window, but for this widget we
 identify it with the window title. This is not always the case.
 */
widget
qt_color_picker_widget_rep::plain_window_widget (string name, command q, int b) {
  (void) b; (void) q;
  _windowTitle = name;
  return this;
}

void
qt_color_picker_widget_rep::showDialog() {
  if (_pickPattern) {
    // do stuff
  } else {
    QList<QColor> recent;
    QSet<QString> seen;
    auto addRecent= [&] (const string& name) {
      QColor color= to_qcolor (name);
      QString canonical= color.name ();
      if (color.isValid () && !seen.contains (canonical)) {
        recent << color;
        seen.insert (canonical);
      }
    };

    for (int i=0; i<N(_proposals); ++i)
      if (is_atomic (_proposals[i]))
        addRecent (_proposals[i]->label);
    list<string> recentNames=
      as_list_string (call ("color-picker-recent-colors"));
    for (list<string> it= recentNames; !is_nil (it); it= it->next)
      addRecent (it->item);

    list<string> savedNames=
      as_list_string (call ("color-picker-saved-colors"));
    for (int i=0; i<QColorDialog::customCount (); ++i)
      QColorDialog::setCustomColor (i, Qt::transparent);
    int customIndex= 0;
    for (list<string> it= savedNames;
         !is_nil (it) && customIndex < QColorDialog::customCount ();
         it= it->next) {
      QColor color= to_qcolor (it->item);
      if (color.isValid ())
        QColorDialog::setCustomColor (customIndex++, color);
    }

    QColor initial= recent.isEmpty () ? Qt::white : recent.first ();
    QColorDialog dialog (initial);
    dialog.setWindowTitle (to_qstring (_windowTitle));
    dialog.setOption (QColorDialog::DontUseNativeDialog);

    if (!recent.isEmpty ()) {
      QWidget* recentWidget= new QWidget (&dialog);
      QHBoxLayout* recentLayout= new QHBoxLayout (recentWidget);
      recentLayout->setContentsMargins (6, 4, 6, 2);
      recentLayout->setSpacing (4);
      recentLayout->addWidget (new QLabel (QObject::tr ("Recent colors:"),
                                            recentWidget));
      for (const QColor& color: recent) {
        QToolButton* button= new QToolButton (recentWidget);
        button->setFixedSize (28, 28);
        button->setToolTip (color.name ());
        button->setStyleSheet (
          QString ("QToolButton { background-color: %1; border: 1px solid "
                   "#777; } QToolButton:hover { border: 2px solid #222; }")
            .arg (color.name ()));
        QObject::connect (button, &QToolButton::clicked, &dialog,
                          [&dialog, color] { dialog.setCurrentColor (color); });
        recentLayout->addWidget (button);
      }
      recentLayout->addStretch ();
      if (QVBoxLayout* layout= qobject_cast<QVBoxLayout*> (dialog.layout ()))
        layout->insertWidget (0, recentWidget);
      else
        dialog.layout ()->addWidget (recentWidget);
    }

    int result= dialog.exec ();

    list<string> saved;
    QSet<QString> savedSeen;
    for (int i=0; i<QColorDialog::customCount () && N(saved) < 8; ++i) {
      QColor color= QColorDialog::customColor (i);
      QString canonical= color.name ();
      if (color.isValid () && color.alpha () != 0 &&
          !savedSeen.contains (canonical)) {
        saved << from_qcolor (color);
        savedSeen.insert (canonical);
      }
    }
    call ("color-picker-set-saved-colors", object (saved));

    QColor selected= dialog.selectedColor ();
    if (result == QDialog::Accepted && selected.isValid ()) {
      string name= from_qcolor (selected);
      call ("color-picker-remember-color", object (name));
      _commandAfterExecution (list_object (object (tree (name))));
    }
  }
}

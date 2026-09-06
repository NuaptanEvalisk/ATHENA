/******************************************************************************
 * MODULE     : qt_chooser_widget.hpp
 * DESCRIPTION: File chooser widget, native and otherwise
 * COPYRIGHT  : (C) 2008  Massimiliano Gubinelli
 *******************************************************************************
 * This software falls under the GNU general public license version 3 or later.
 * It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
 * in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
 ******************************************************************************/

#ifndef QT_CHOOSER_WIDGET_HPP
#define QT_CHOOSER_WIDGET_HPP

#include "qt_widget.hpp"
#include "qt_utilities.hpp"
#include <QDialog>
#include <QPointer>
#include <functional>

/*!
  A file/directory chooser dialog, using native dialogs where available.
  See @link widget.cpp @endlink for an explanation of send(), query(),
  read(), etc.
 */
class qt_chooser_widget_rep: public qt_widget_rep {
protected:      
  command cmd;           //!< Scheme closure to execute when the file is chosen
  command quit;          //!< Execute when the dialog closes.
  string type;           //!< File types to filter in the dialog
  string prompt;         //!< Is this a "Save" dialog?
  string win_title;      //!< Set by plain_window_widget()
  
  string directory; //!< Set this property sending SLOT_DIRECTORY to this widget
  coord2 position;  //!< Set this property sending SLOT_POSITION to this widget
  coord2 size;      //!< Set this property sending SLOT_SIZE to this widget
  string file;      //!< Set this property sending SLOT_FILE to this widget

  QString nameFilter;    //!< For use in QFileDialog::setNameFilter()
  QString defaultSuffix; //!< For use in QFileDialog::setDefaultSuffix()

  QPointer<QDialog> dialog;
  void show_dialog (QDialog*, std::function<void ()> read_result);

public:
  qt_chooser_widget_rep (command, string, string);
  
  void send (slot s, blackbox val) override;
  blackbox query (slot s, int type_id) override;
  widget read (slot s, blackbox index) override;
  widget plain_window_widget (string s, command q, int b) override;
  
  bool set_type (const string& _type);
  void perform_dialog();
  void perform_dialog_with_qfiledialog();
  void perform_dialog_with_kfiledialog();
};

#endif  // QT_CHOOSER_WIDGET_HPP

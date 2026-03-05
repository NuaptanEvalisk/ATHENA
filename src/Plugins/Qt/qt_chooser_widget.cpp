
/******************************************************************************
* MODULE     : qt_chooser_widget.cpp
* DESCRIPTION: File chooser widget, native and otherwise
* COPYRIGHT  : (C) 2008  Massimiliano Gubinelli
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "qt_chooser_widget.hpp"
#include "qt_utilities.hpp"
#include "widget.hpp"
#include "message.hpp"
#include "analyze.hpp"
#include "convert.hpp"
#include "converter.hpp"
#include "scheme.hpp"
#include "dictionary.hpp"
#include "editor.hpp"
#include "new_view.hpp"      // get_current_editor()
#include "image_files.hpp"
#include "QTMFileDialog.hpp"

#include <QString>
#include <QStringList>
#include <QFileDialog>
#include <QByteArray>

#ifdef OS_ANDROID
#include "android.hpp"
#endif

/*!
  \param _cmd  Scheme closure to execute after the dialog is closed.
  \param _type What kind of dialog to show. Can be one of "image", "directory",
               or any of the supported file formats: "texmacs", "tmml",
               "postscript", etc. See perform_dialog()
 */
qt_chooser_widget_rep::qt_chooser_widget_rep (command _cmd, string _type, string _prompt)
 : qt_widget_rep (file_chooser), cmd (_cmd), prompt (_prompt),
   position (coord2 (0, 0)), size (coord2 (100, 100)), file ("")
{
  if (DEBUG_QT_WIDGETS)
    debug_widgets << "qt_chooser_widget_rep::qt_chooser_widget_rep type=\""
                  << type << "\" prompt=\"" << prompt << "\"" << LF;
  if (! set_type (_type))
    set_type ("generic");
}

void
qt_chooser_widget_rep::send (slot s, blackbox val) {
  switch (s) {
    case SLOT_VISIBILITY:
    {   
      check_type<bool> (val, s);
      bool flag = open_box<bool> (val);
      (void) flag;
      NOT_IMPLEMENTED("qt_chooser_widget::SLOT_VISIBILITY");
    }
      break;
    case SLOT_SIZE:
      check_type<coord2>(val, s);
      size = open_box<coord2> (val);
      break;
    case SLOT_POSITION:
      check_type<coord2>(val, s);
      position = open_box<coord2> (val);
      break;
    case SLOT_KEYBOARD_FOCUS:
      check_type<bool>(val, s);
      perform_dialog ();
      break;              
    case SLOT_STRING_INPUT:
      check_type<string>(val, s);
      if (DEBUG_QT_WIDGETS)
        debug_widgets << "\tString input: " << open_box<string> (val) << LF;
      NOT_IMPLEMENTED("qt_chooser_widget::SLOT_STRING_INPUT");
      break;
    case SLOT_INPUT_TYPE:
      check_type<string>(val, s);
      set_type (open_box<string> (val));
      break;
    case SLOT_FILE:
        //send_string (THIS, "file", val);
      check_type<string>(val, s);
      if (DEBUG_QT_WIDGETS)
        debug_widgets << "\tFile: " << open_box<string> (val) << LF;
      file = open_box<string> (val);
      break;
    case SLOT_DIRECTORY:
      check_type<string>(val, s);
      directory = open_box<string> (val);
      directory = as_string (url_pwd () * url_system (directory));
      break;
      
    default:
      qt_widget_rep::send (s, val);
  }
  if (DEBUG_QT_WIDGETS)
    debug_widgets << "qt_chooser_widget_rep: sent " << slot_name (s) 
                  << "\t\tto widget\t"      << type_as_string() << LF;
}

blackbox
qt_chooser_widget_rep::query (slot s, int type_id) {
  if (DEBUG_QT_WIDGETS)
    debug_widgets << "qt_chooser_widget_rep::query " << slot_name(s) << LF;
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
    {
      check_type_id<string> (type_id, s);
      if (DEBUG_QT_WIDGETS) debug_widgets << "\tString: " << file << LF;
      return close_box<string> (file);
    }
    default:
      return qt_widget_rep::query (s, type_id);
  }
}

widget
qt_chooser_widget_rep::read (slot s, blackbox index) {
  if (DEBUG_QT_WIDGETS)
    debug_widgets << "qt_chooser_widget_rep::read " << slot_name(s) << LF;
  switch (s) {
    case SLOT_WINDOW:
      check_type_void (index, s);
      return this;
    case SLOT_FORM_FIELD:
      check_type<int> (index, s);
      return this;
    case SLOT_FILE:
      check_type_void (index, s);
      return this;
    case SLOT_DIRECTORY:
      check_type_void (index, s);
      return this;
    default:
      return qt_widget_rep::read(s,index);
  }
}

/*!
 @note: name is a unique identifier for the window, but for this widget we
 identify it with the window title. This is not always the case.
 */
widget
qt_chooser_widget_rep::plain_window_widget (string s, command q, int b) {
  (void) b;
  win_title = s;
  quit      = q;
  return this;
}

bool
qt_chooser_widget_rep::set_type (const string& _type)
{
  if (_type == "directory") {
    type = _type;
    return true;
  } else if (_type == "generic") {
    nameFilter = "";
    type = _type;
    return true;
  }

  if (format_exists (_type)) {
    nameFilter = to_qstring (translate
                             (as_string (call ("format-get-name", _type))
                              * " file"));
  } else if (_type == "image") {
    nameFilter = to_qstring (translate ("Image file"));
  } else {
    if (DEBUG_STD)
      debug_widgets << "qt_chooser_widget: IGNORING unknown format "
                    << _type << LF;
    return false;
  }

  nameFilter += " (";
  object ret = call ("format-get-suffixes*", _type);
  array<object> suffixes = as_array_object (ret);
  if (N(suffixes) > 1)
    defaultSuffix = to_qstring (as_string (suffixes[1]));
  for (int i = 1; i < N(suffixes); ++i)
    nameFilter += " *." + to_qstring (as_string (suffixes[i]));
  nameFilter += " )";
  
  type = _type;
  return true;
}

void
qt_chooser_widget_rep::perform_dialog_with_qfiledialog() {
  QString caption = to_qstring (win_title);
  c_string tmp (directory * "/" * file);
  QString path = QString::fromUtf8 (&tmp[0]);
  QString filter = nameFilter;
  if (type == "image")
    filter = to_qstring (translate ("Image file")) + " (*.png *.jpg *.jpeg *.bmp *.gif *.pdf)";
  else if (type == "directory")
    filter = "";
  else if (type == "generic")
    filter = to_qstring (translate ("All files (*)"));
  else
    filter = to_qstring (translate (as_string (call ("format-get-name", type))
                                    * " file")) + " (" + filter + ")";
  if (prompt != "") {
    string text= prompt;
    if (ends (text, ":")) text= text (0, N(text) - 1);
    if (ends (text, " as")) text= text (0, N(text) - 3);
    filter = to_qstring (translate (text)) + " (" + filter + ")";
  }
  //QString imqstring = QFileDialog::getSaveFileName (NULL, caption, path, filter);
  // if save dialog, then use getSaveFileName, otherwise use getOpenFileName
  QString imqstring;
  if (prompt != "")
    imqstring = QFileDialog::getSaveFileName (NULL, caption, path, filter);
  else
    imqstring = QFileDialog::getOpenFileName (NULL, caption, path, filter);
  if (imqstring.isEmpty()) {
    file = "#f";
    return;
  }

  QByteArray imqstringutf8 = imqstring.toUtf8();
  string imname(imqstringutf8.data(), imqstringutf8.size());

  file = "(system->url " * scm_quote (imname) * ")";
  if (type == "image") {
    url u= url_system (imname);
    string w, h;
    qt_pretty_image_size (u, w, h);
    string params;
    params << "\"" << w << "\" "
           << "\"" << h << "\" "
           << "\"" << "" << "\" "  // xps ??
           << "\"" << "" << "\"";   // yps ??
    file = "(list " * file * " " * params * ")";
  }

  cmd ();
  if (!is_nil (quit)) quit ();

}


/*! Actually displays the dialog with all the options set.
 * Uses a native dialog on Mac/Win and opens a custom dialog with image preview
 * for other platforms.
 */
void
qt_chooser_widget_rep::perform_dialog () {
#if QT_VERSION >= 0x060000 && defined(OS_ANDROID)
  return perform_dialog_with_qfiledialog();
#endif
  
  QString caption = to_qstring (win_title);
  c_string tmp (directory * "/" * file);
  QString path = QString::fromUtf8 (&tmp[0]);
  
  QFileDialog* native_dialog = 0;
  QTMFileDialog* custom_dialog = 0;
  QTMImageDialog* img_dialog = 0;
  
  if (type == "image") {
    custom_dialog = img_dialog = new QTMImageDialog (NULL, caption, path);
  } else {
    native_dialog = new QFileDialog (NULL, caption, path);
  }

  QFileDialog* file_ptr = native_dialog ? native_dialog : custom_dialog->get_qfiledialog();

  file_ptr->setViewMode (QFileDialog::Detail);
  if (type == "directory")
    file_ptr->setFileMode (QFileDialog::Directory);
  else if (type == "image" && prompt == "")
    // check non saving mode just in case we support it
    file_ptr->setFileMode (QFileDialog::ExistingFile);
  else
    file_ptr->setFileMode (QFileDialog::AnyFile);

  if (prompt != "") {
    string text= prompt;
    if (ends (text, ":")) text= text (0, N(text) - 1);
    if (ends (text, " as")) text= text (0, N(text) - 3);
    file_ptr->setDefaultSuffix (defaultSuffix);
    file_ptr->setAcceptMode (QFileDialog::AcceptSave);
    file_ptr->setLabelText (QFileDialog::Accept, to_qstring (translate (text)));
  }

#if (QT_VERSION >= 0x040400)
  if (type != "directory") {
    QStringList filters;
    if (nameFilter != "")
      filters << nameFilter;
    filters << to_qstring (translate ("All files (*)"));
    file_ptr->setNameFilters (filters);
  }
#endif

  QWidget* actual_dialog = native_dialog ? (QWidget*)native_dialog : (QWidget*)custom_dialog;
  actual_dialog->updateGeometry();
  QSize   sz = actual_dialog->sizeHint();
  QPoint pos = to_qpoint (position);
  QRect r;

  r.setSize (sz);
  r.moveCenter (pos);
  actual_dialog->setGeometry (r);
  
  QStringList fileNames;
  file = "#f";
  int result = native_dialog ? native_dialog->exec() : custom_dialog->exec();
  if (result) {
    fileNames = file_ptr->selectedFiles();
    if (fileNames.count() > 0) {
      QString imqstring = fileNames.first();
      // QTBUG-59401: QFileDialog::setDefaultSuffix doesn't work when file path contains a dot
      if (!defaultSuffix.isEmpty() && imqstring.contains(QLatin1Char('/'))
          && !imqstring.endsWith(QLatin1Char('/'))
          && imqstring.indexOf(QLatin1Char('.'), imqstring.lastIndexOf(QLatin1Char('/'))) == -1) {
            imqstring = imqstring + QLatin1Char('.') + defaultSuffix;
          }
      string imname    = from_qstring_utf8 (imqstring);
      file = "(system->url " * scm_quote (imname) * ")";
      if (type == "image") {
        if (img_dialog) {
          file = "(list " * file * img_dialog->getParamsAsString () * ")"; //set image size from preview
        } else {
          url u= url_system (imname);
          string w, h;
          qt_pretty_image_size (u, w, h);
          string params;
          params << "\"" << w << "\" "
                 << "\"" << h << "\" "
                 << "\"" << "" << "\" "  // xps ??
                 << "\"" << "" << "\"";   // yps ??
          file = "(list " * file * " " * params * ")";
        }
      }
    }
  }

  if (native_dialog) delete native_dialog;
  else delete custom_dialog;
  
  cmd ();
  if (!is_nil (quit)) quit ();
}

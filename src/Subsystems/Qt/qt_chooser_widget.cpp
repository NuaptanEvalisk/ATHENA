
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
#include "gui_text.hpp"
#include "editor.hpp"
#include "new_view.hpp"      // get_current_editor()
#include "image_files.hpp"
#include "QTMFileDialog.hpp"

#include <QString>
#include <QStringList>
#include <QFileDialog>
#include <QCheckBox>
#include <QGridLayout>
#include <QByteArray>
#include <QUrl>
#include <QApplication>
#include <QThread>

#ifdef USE_KF6
#include <KIOFileWidgets/KFileCustomDialog>
#include <KIOFileWidgets/KFileWidget>
#include <KIOWidgets/kfile.h>
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
      // dialogue_start sets visibility before keyboard focus opens the chooser.
      if (!open_box<bool> (val) && dialog) dialog->reject ();
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
      if (open_box<bool> (val)) perform_dialog ();
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
    nameFilter = to_qstring (ui_text
                             (as_string (call ("format-get-name", _type))
                              * " file"));
  } else if (_type == "image") {
    nameFilter = QStringLiteral ("Image file");
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
qt_chooser_widget_rep::show_dialog (
    QDialog* window, std::function<void ()> read_result) {
  dialog= window;
  file= "#f";
  // Completion reads this widget even when the caller has released its copy.
  widget owner (this);
  QObject::connect (window, &QDialog::finished, window,
                    [this, owner, window, read_result, completed= false]
                    (int result) mutable {
    if (completed) return;
    completed= true;
    widget keep_alive= owner;
    if (result == QDialog::Accepted) read_result ();
    window->deleteLater ();
    cmd ();
    if (!is_nil (quit)) quit ();
  });
  // Keep application modality without exec() reentering the GUI update loop.
  window->setModal (true);
  window->show ();
}


#ifdef USE_KF6
static QString
qt_chooser_kde_filter (const string& type) {
  if (type == "image")
    return "*.png *.jpg *.jpeg *.bmp *.gif *.pdf *.svg|" +
           QStringLiteral ("Image file") +
           " (*.png *.jpg *.jpeg *.bmp *.gif *.pdf *.svg)";
  if (type == "directory")
    return QString ();
  if (type == "generic")
    return QStringLiteral ("*|All files (*)");

  object ret= call ("format-get-suffixes*", type);
  array<object> suffixes= as_array_object (ret);
  QString patterns;
  for (int i=1; i<N(suffixes); ++i) {
    if (!patterns.isEmpty ()) patterns += " ";
    patterns += "*." + to_qstring (as_string (suffixes[i]));
  }
  if (patterns.isEmpty ()) patterns= "*";
  QString label= to_qstring (ui_text (as_string (call ("format-get-name", type))
                                      * " file"));
  return patterns + "|" + label + " (" + patterns + ")";
}

static QString
qt_chooser_selected_local_file (KFileWidget* file_widget) {
  QString selected= file_widget->selectedFile ();
  if (!selected.isEmpty ()) return selected;
  QUrl selected_url= file_widget->selectedUrl ();
  if (selected_url.isLocalFile ()) return selected_url.toLocalFile ();
  return QString ();
}

static void
qt_chooser_set_kde_filter (KFileWidget* file_widget, const QString& filter) {
  if (file_widget == nullptr || filter.isEmpty ()) return;
  QList<KFileFilter> filters;
  for (const QString& entry : filter.split ('\n', Qt::SkipEmptyParts)) {
    QStringList parts= entry.split ('|');
    QString patterns= parts.value (0).trimmed ();
    QString label= parts.value (1, patterns).trimmed ();
    filters << KFileFilter (label, patterns.split (' ', Qt::SkipEmptyParts),
                            QStringList ());
  }
  file_widget->setFilters (filters);
}

void
qt_chooser_widget_rep::perform_dialog_with_kfiledialog() {
  Q_ASSERT (qApp && QThread::currentThread () == qApp->thread ());
  if (dialog) return;
  QString caption= to_qstring (win_title);
  c_string tmp (directory * "/" * file);
  QString path= QString::fromUtf8 (&tmp[0]);
  QUrl start_url= QUrl::fromLocalFile (path);

  auto* window= new KFileCustomDialog (start_url);
  window->setWindowTitle (caption);
  window->setOperationMode (prompt == ""?
                           KFileWidget::Opening:
                           KFileWidget::Saving);

  KFileWidget* file_widget= window->fileWidget ();
  if (type == "directory")
    file_widget->setMode (KFile::Directory | KFile::ExistingOnly |
                          KFile::LocalOnly);
  else if (prompt == "")
    file_widget->setMode (KFile::File | KFile::ExistingOnly |
                          KFile::LocalOnly);
  else
    file_widget->setMode (KFile::File | KFile::LocalOnly);

  QString filter= qt_chooser_kde_filter (type);
  qt_chooser_set_kde_filter (file_widget, filter);
  if (prompt != "")
    file_widget->setConfirmOverwrite (true);

  QTMImagePreview* preview= NULL;
  if (type == "image") {
    preview= new QTMImagePreview ();
    window->setCustomWidget (preview);
    QObject::connect (file_widget, &KFileWidget::fileHighlighted,
                      preview, [preview] (const QUrl& url) {
      preview->setImage (url.isLocalFile ()? url.toLocalFile (): QString ());
    });
  }

  window->updateGeometry ();
  QRect r;
  QSize dialog_size= window->sizeHint ();
  int max_width= type == "image"? 980: 860;
  if (dialog_size.width () > max_width)
    dialog_size.setWidth (max_width);
  r.setSize (dialog_size);
  r.moveCenter (to_qpoint (position));
  window->setGeometry (r);

  show_dialog (window, [this, file_widget, preview] {
    QString imqstring= qt_chooser_selected_local_file (file_widget);
    if (!defaultSuffix.isEmpty () && imqstring.contains (QLatin1Char ('/'))
        && !imqstring.endsWith (QLatin1Char ('/'))
        && imqstring.indexOf (QLatin1Char ('.'),
                              imqstring.lastIndexOf (QLatin1Char ('/'))) == -1)
      imqstring= imqstring + QLatin1Char ('.') + defaultSuffix;

    if (!imqstring.isEmpty ()) {
      string imname= from_qstring_utf8 (imqstring);
      file= "(system->url " * scm_quote (imname) * ")";
      if (type == "image") {
        string params;
        string w, h, x, y;
        if (preview) {
          w= from_qstring (preview->wid->text ());
          h= from_qstring (preview->hei->text ());
          x= from_qstring (preview->xps->text ());
          y= from_qstring (preview->yps->text ());
        }
        if (w == "" && h == "") {
          url u= url_system (imname);
          qt_pretty_image_size (u, w, h);
        }
        params << "\"" << w << "\" "
               << "\"" << h << "\" "
               << "\"" << x << "\" "
               << "\"" << y << "\"";
        file= "(list " * file * " " * params * ")";
      }
    }
  });
}
#endif


/*! Actually displays the dialog with all the options set.
 * Uses a native dialog on Mac/Win and opens a custom dialog with image preview
 * for other platforms.
 */
void
qt_chooser_widget_rep::perform_dialog () {
#ifdef USE_KF6
  return perform_dialog_with_kfiledialog();
#else
  return perform_dialog_with_qfiledialog();
#endif
}

void
qt_chooser_widget_rep::perform_dialog_with_qfiledialog () {
  Q_ASSERT (qApp && QThread::currentThread () == qApp->thread ());
  if (dialog) return;
  QString caption = to_qstring (win_title);
  c_string tmp (directory * "/" * file);
  QString path = QString::fromUtf8 (&tmp[0]);
  
  QFileDialog* native_dialog = 0;
  QTMFileDialog* custom_dialog = 0;
  QTMImageDialog* img_dialog = 0;
  QCheckBox* portable_latex = 0;
  
  if (type == "image") {
    custom_dialog = img_dialog = new QTMImageDialog (NULL, caption, path);
  } else {
    native_dialog = new QFileDialog (NULL, caption, path);
  }

  QFileDialog* file_ptr = native_dialog ? native_dialog : custom_dialog->get_qfiledialog();

  if (native_dialog && type == "latex" && prompt != "") {
    file_ptr->setOption (QFileDialog::DontUseNativeDialog, true);
    portable_latex =
      new QCheckBox (QStringLiteral ("Make converted file portable"),
                     file_ptr);
    portable_latex->setChecked (false);
    QGridLayout* layout = qobject_cast<QGridLayout*> (file_ptr->layout ());
    if (layout != 0)
      layout->addWidget (portable_latex, layout->rowCount (), 0, 1,
                         layout->columnCount ());
  }

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
    file_ptr->setLabelText (QFileDialog::Accept, to_qstring (ui_text (text)));
  }

  if (type != "directory") {
    QStringList filters;
    if (nameFilter != "")
      filters << nameFilter;
    filters << QStringLiteral ("All files (*)");
    file_ptr->setNameFilters (filters);
  }

  QDialog* actual_dialog = native_dialog ? (QDialog*)native_dialog : (QDialog*)custom_dialog;
  actual_dialog->updateGeometry();
  QSize   sz = actual_dialog->sizeHint();
  QPoint pos = to_qpoint (position);
  QRect r;

  r.setSize (sz);
  r.moveCenter (pos);
  actual_dialog->setGeometry (r);
  
  show_dialog (actual_dialog, [this, file_ptr, portable_latex, img_dialog] {
    QStringList fileNames = file_ptr->selectedFiles();
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
      if (portable_latex != 0)
        file = "(list " * file * " " *
               scm_quote (portable_latex->isChecked () ? "on" : "off") * ")";
      if (type == "image") {
        if (img_dialog) {
          file = "(list " * file * " " * img_dialog->getParamsAsString () * ")";
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
  });
}

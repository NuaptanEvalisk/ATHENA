
/******************************************************************************
 * MODULE     : qt_gui.cpp
 * DESCRIPTION: QT display class
 * COPYRIGHT  : (C) 2008  Massimiliano Gubinelli
 *******************************************************************************
 * This software falls under the GNU general public license version 3 or later.
 * It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
 * in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
 ******************************************************************************/

#include <locale.h>

#include "convert.hpp"
#include "iterator.hpp"
#include "file.hpp" // added for copy_as_graphics
#include "analyze.hpp"
#include "message.hpp"
#include "scheme.hpp"
#include "sys_utils.hpp"
#include "locale.hpp"
#include "tm_window.hpp"
#include "new_window.hpp"
#include "ATHENA/Data/new_buffer.hpp"
#include "ATHENA/Data/vault_image_insertion.hpp"
#include "actor_ui_bridge.hpp"
#include "buffer_actor.hpp"
#include "boot.hpp"
#include "guile_tm.hpp"
#include "object.hpp"
#include "scheme_execution_context.hpp"

#include "qt_gui.hpp"
#include "qt_ui_element.hpp"
#include "qt_utilities.hpp"
#include "qt_renderer.hpp" // for the_qt_renderer
#include "qt_simple_widget.hpp"
#include "qt_window_widget.hpp"
#include "QTMApplication.hpp"
#include "QTMProgressWindow.hpp"


#include <QDialog>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScreen>
#include <QFile>
#include <QClipboard>
#include <QApplication>
#include <QCryptographicHash>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QMimeType>
#include <QPixmap>
#include <QStyle>
#include <QBuffer>
#include <QFileOpenEvent>
#include <QStackedLayout>
#include <QLabel>
#include <QSocketNotifier>
#include <QSetIterator>
#include <QMimeData>
#include <QByteArray>
#include <QCoreApplication>
#include <QLibraryInfo>
#include <QImage>
#include <QUrl>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDockWidget>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QThread>

#include "QTMGuiHelper.hpp"
#include "QTMMainTabWindow.hpp"
#include "QTMWidget.hpp"
#include "QTMWindow.hpp"

#ifdef MACOSX_EXTENSIONS
#include "MacOS/mac_utilities.h"
#endif

#include <QtPlugin>

#ifdef qt_static_plugin_xcb
Q_IMPORT_PLUGIN(QXcbIntegrationPlugin)
#endif
#ifdef qt_static_plugin_qjpeg
Q_IMPORT_PLUGIN(qjpeg)
#endif
#ifdef qt_static_plugin_qgif
Q_IMPORT_PLUGIN(qgif)
#endif
#ifdef qt_static_plugin_qico
Q_IMPORT_PLUGIN(qico)
#endif
#ifdef qt_static_plugin_qsvg
Q_IMPORT_PLUGIN(qsvg)
#endif

#ifdef QT_MAC_USE_COCOA
Q_IMPORT_PLUGIN(QCocoaIntegrationPlugin)
#endif

qt_gui_rep* the_gui = NULL;
int nr_windows = 0; // FIXME: fake variable, referenced in tm_server

static bool gui_event_loop_started= false;
static bool startup_splash_hide_allowed= false;
static bool startup_splash_hidden= false;

/******************************************************************************
* FIXME: temporary hack by Joris
* Additional wait mechanism to keep CPU usage down
******************************************************************************/

#ifdef QT_CPU_FIX
#include <unistd.h>

static double tm_count= 0.0;
static double tm_delay= 1.0;

void
tm_wake_up () {
  tm_delay= 1.0;
}

void
tm_sleep () {
  //tm_count += 1.0;
  tm_delay= 1.0001 * tm_delay;
  if (tm_delay > 250000.0) tm_delay= 250000;
  //cout << tm_count << ", " << tm_delay << "\r";
  //cout.flush ();
  usleep ((int) floor (tm_delay));
}
#endif

static void
athena_sync_logical_ui_font (const QFont& font) {
  if (qApp == nullptr) return;

  const char* class_names[]= {
    "QWidget",
    "QMainWindow",
    "QDialog",
    "QMenuBar",
    "QMenu",
    "QToolBar",
    "QToolButton",
    "QTabBar",
    "QTabWidget",
    "QStatusBar",
    "QDockWidget",
    "QComboBox",
    // QComboMenuDelegate reads this private registry entry instead of the
    // popup view font when painting non-native combo box menus.
    "QComboMenuItem",
    "QLineEdit",
    "QPushButton",
    "QCheckBox",
    "QRadioButton",
    "QLabel",
    "QGroupBox"
  };
  for (const char* class_name: class_names)
    qApp->setFont (font, class_name);

  for (QWidget* widget: QApplication::allWidgets ()) {
    if (qobject_cast<QMainWindow*> (widget) ||
        qobject_cast<QDialog*> (widget) ||
        qobject_cast<QMenuBar*> (widget) ||
        qobject_cast<QMenu*> (widget) ||
        qobject_cast<QToolBar*> (widget) ||
        qobject_cast<QToolButton*> (widget) ||
        qobject_cast<QTabBar*> (widget) ||
        qobject_cast<QTabWidget*> (widget) ||
        qobject_cast<QStatusBar*> (widget) ||
        qobject_cast<QDockWidget*> (widget) ||
        qobject_cast<QComboBox*> (widget) ||
        qobject_cast<QLineEdit*> (widget) ||
        qobject_cast<QPushButton*> (widget) ||
        qobject_cast<QCheckBox*> (widget) ||
        qobject_cast<QRadioButton*> (widget) ||
        qobject_cast<QLabel*> (widget) ||
        qobject_cast<QGroupBox*> (widget))
      widget->setFont (font);
  }
}

void
athena_resync_wayland_ui_fonts () {
  if (qApp == nullptr ||
      !QApplication::platformName ().startsWith (QStringLiteral ("wayland")))
    return;
  athena_sync_logical_ui_font (qApp->font ());
}

void
athena_initialize_wayland_ui_scale () {
  if (qApp == nullptr ||
      !QApplication::platformName ().startsWith (QStringLiteral ("wayland")) ||
      retina_manual)
    return;

  double dpr= 1.0;
  if (QScreen* screen= QGuiApplication::primaryScreen ())
    dpr= screen->devicePixelRatio ();

  // QtWayland already exposes screen geometry, fonts, and widget dimensions
  // in compositor-scaled logical coordinates.  Only the backing store needs
  // the physical device-pixel ratio; applying the KScreen scale again to
  // retina_scale would enlarge every Qt metric and the document zoom twice.
  retina_manual= true;
  retina_factor= max (1, (int) ceil (dpr));
  retina_zoom= 1;
  retina_scale= 1.0;
  if (dpr > 1.0) {
    if (!retina_iman) {
      retina_iman= true;
      retina_icons= 1;
    }
  }

  if (has_user_preference ("retina-factor"))
    retina_factor= get_user_preference ("retina-factor") == "on"? 2: 1;
  if (has_user_preference ("retina-zoom"))
    retina_zoom= get_user_preference ("retina-zoom") == "on"? 2: 1;
  if (has_user_preference ("retina-icons"))
    retina_icons= get_user_preference ("retina-icons") == "on"? 2: 1;
  if (has_user_preference ("retina-scale"))
    retina_scale= as_double (get_user_preference ("retina-scale"));

  athena_sync_logical_ui_font (qApp->font ());
}

/******************************************************************************
* Constructor and geometry
******************************************************************************/

qt_gui_rep::qt_gui_rep (int &argc, char **argv):
interrupted (false), waitWindow (NULL), popup_wid_time (0),
clipboard_text_cache_valid (false),
time_credit (100), do_check_events (false), updating (false),
needing_update (false)
{
  (void) argc; (void) argv;
  timeout_time = texmacs_time () + time_credit;
  
  gui_helper = new QTMGuiHelper (this);
  qApp->installEventFilter (gui_helper);

  QClipboard* clipboard= QApplication::clipboard ();
  QObject::connect (clipboard, &QClipboard::dataChanged, gui_helper,
                    [this] () {
                      if (!headless_mode) refresh_external_clipboard_cache ();
                    });
  if (!headless_mode)
    QTimer::singleShot (0, gui_helper,
                        [this] () { refresh_external_clipboard_cache (); });
  
#if defined(QT_MAC_USE_COCOA) \
  || (defined(OS_MACOS) && QT_VERSION >= 0x060000)
    //HACK: this filter is needed to overcome a bug in Qt/Cocoa
  extern void mac_install_filter();
  mac_install_filter();
#endif
  
  updatetimer = new QTimer (gui_helper);
  updatetimer->setSingleShot (true);
  QObject::connect (updatetimer, &QTimer::timeout,
                    gui_helper, &QTMGuiHelper::doUpdate);
  // (void) default_font ();

  if (!retina_manual) {
    retina_manual= true;
#  ifdef MACOSX_EXTENSIONS
    double mac_hidpi = mac_screen_scale_factor();
    if (DEBUG_STD)
      debug_boot << "Mac Screen scaleFfactor: " << mac_hidpi <<  "\n";
          
    if (mac_hidpi == 2) {
      if (DEBUG_STD) debug_boot << "Setting up HiDPI mode\n";
      retina_factor= 2;      
    }
#else
    double dpr = 1.0;
    if (QGuiApplication::primaryScreen())
      dpr = QGuiApplication::primaryScreen()->devicePixelRatio();
    if (DEBUG_STD)
      debug_boot << "Device pixel ratio: " << dpr << "\n";

    if (dpr > 1.0) {
      retina_factor= (int) ceil (dpr);
      retina_zoom  = 1;
      retina_scale = 1.0;
      if (!retina_iman) {
        retina_iman  = true;
        retina_icons = 1;
      }
    }
    else {
      SI w, h;
      get_extents (w, h);
      if (DEBUG_STD)
        debug_boot << "Screen extents: " << w/PIXEL << " x " << h/PIXEL << "\n";
      if (min (w, h) >= 1440 * PIXEL) {
        retina_zoom  = 1;
        retina_scale = 1.0;
        if (!retina_iman) {
          retina_iman  = true;
          retina_icons = 1;
        }
      }
    }
#endif
  }
  if (has_user_preference ("retina-factor"))
    retina_factor= get_user_preference ("retina-factor") == "on"? 2: 1;
  if (has_user_preference ("retina-zoom"))
    retina_zoom= get_user_preference ("retina-zoom") == "on"? 2: 1;
  if (has_user_preference ("retina-icons"))
    retina_icons= get_user_preference ("retina-icons") == "on"? 2: 1;
  if (has_user_preference ("retina-scale"))
    retina_scale= as_double (get_user_preference ("retina-scale"));
}

/* important routines */
void
qt_gui_rep::get_extents (SI& width, SI& height) {
  coord2 size = headless_mode ? coord2 (480, 320)
    : from_qsize (QGuiApplication::primaryScreen()->size()); // todo : improve this
  width  = size.x1;
  height = size.x2;
}

void
qt_gui_rep::get_max_size (SI& width, SI& height) {
  width = 8000 * PIXEL;
  height = 6000 * PIXEL;
}

qt_gui_rep::~qt_gui_rep()  {
  delete gui_helper;
  if (waitWindow) waitWindow->deleteLater();
}

/******************************************************************************
 * interclient communication
 ******************************************************************************/

void
qt_gui_rep::refresh_external_clipboard_cache () {
  QClipboard* clipboard= QApplication::clipboard ();
  const QMimeData* data= clipboard->mimeData (QClipboard::Clipboard);
  if (data == nullptr || data->formats ().isEmpty ()) return;
  if (!data->hasText ()) return;

  clipboard_text_cache= data->text ().toUtf8 ();
  clipboard_text_cache_valid= true;
}

bool
qt_gui_rep::get_selection (string key, tree& t, string& s, string format) {
  QClipboard *cb = QApplication::clipboard ();
  QClipboard::Mode mode = QClipboard::Clipboard;
  bool direct_selection= (key == "extern");
  if (direct_selection) key= "primary";
  if (key == "primary" || (key == "mouse" && cb->supportsSelection ()))
    if (key == "mouse") mode = QClipboard::Selection;
  
  const QMimeData *md = cb->mimeData (mode);
  bool empty_offer= md == nullptr || md->formats ().isEmpty ();
  if (!empty_offer && mode == QClipboard::Clipboard)
    refresh_external_clipboard_cache ();
  QByteArray buf;
  string input_format;
  
  s = "";
  t = "none";
    // Knowing when we owns (or not) the content is not clear
  bool owns = (format != "temp" && format != "wrapbuf" && key != "primary") &&
  !(key == "mouse" && cb->supportsSelection ());
  
  if (!owns && md != nullptr &&
      md->hasFormat ("application/x-texmacs-pid")) {
    buf = md->data ("application/x-texmacs-pid");
    if (!(buf.isEmpty())) {
      owns = string (buf.constData(), buf.size())
      == as_string (QCoreApplication::applicationPid ());
    }
  }
  
  if (owns) {
    if (!selection_t->contains (key)) return false;
    t = copy (selection_t [key]);
    s = copy (selection_s [key]);
    return true;
  }
  
  if (DEBUG_QT)
    debug_qt << "get_selection format: ["  << format << "] mime-types: [" 
             << (md == nullptr ? string ("<null>") :
                 from_qstring(md->formats().join(","))) << "]" << LF;

  if (empty_offer && clipboard_text_cache_valid) {
    buf= clipboard_text_cache;
    if (format == "default") input_format= "verbatim-snippet";
  }
  else if (format == "default") {
    if (md->hasFormat ("application/x-texmacs-clipboard")) {
      buf = md->data ("application/x-texmacs-clipboard");
      input_format = "texmacs-snippet";
    }
    else if (md->hasImage ()) {
      if (md->hasUrls ()) {
        QList<QUrl> l= md->urls ();
        if (l.size () == 1) {
          string ref, error;
          string local;
          bool prepared= false;
          if (l[0].isLocalFile ()) {
#ifdef OS_MACOS
            local= from_qstring (fromNSUrl (l[0]));
#else
            local= from_qstring (l[0].toLocalFile ());
#endif
            prepared= vault_image_insertion_prepare_file (
              get_current_buffer_safe (), url_system (local), ref, error);
          }
          else
            prepared= vault_image_insertion_prepare_remote (
              get_current_buffer_safe (),
              url (from_qstring (l[0].toString ())), ref, error);
          if (prepared && ref != "") {
            string w, h;
            url image= l[0].isLocalFile () ? url_system (local) :
              relative (get_current_buffer_safe (), url_unix (ref));
            qt_pretty_image_size (image, w, h);
            tree im (IMAGE, ref, w, h, "", "");
            s= as_string (call ("convert", im, "texmacs-tree",
                                "texmacs-snippet"));
            input_format= "";
          }
          else if (prepared) {
            QMessageBox::warning (
              QApplication::activeWindow (), "Paste image",
              to_qstring (error));
            return false;
          }
          else {
            s= from_qstring (l[0].toString ());
            input_format = "linked-picture";
          }
        }
      }
      else {
        QBuffer qbuf(&buf);
        QImage image= qvariant_cast<QImage> (md->imageData());
        qbuf.open (QIODevice::WriteOnly);
        image.save (&qbuf, "PNG");
        string raw (buf.constData (), buf.size ());
        string ref, error;
        bool prepared= vault_image_insertion_prepare_data (
              get_current_buffer_safe (), raw, "png", ref, error);
        if (prepared && ref != "") {
          QSize size= image.size ();
          int ww= size.width (), hh= size.height ();
          string w, h;
          qt_pretty_image_size (ww, hh, w, h);
          tree im (IMAGE, ref, w, h, "", "");
          s= as_string (call ("convert", im, "texmacs-tree",
                              "texmacs-snippet"));
          input_format= "";
          buf.clear ();
        }
        else if (prepared) return false;
        else input_format = "picture";
      }
    }
    else if (md->hasHtml ()) {
      buf = md->html().toUtf8 ();
      input_format = "html-snippet";
    }
    else if (md->hasFormat ("text/plain;charset=utf8")) {
      buf = md->data ("text/plain;charset=utf8");
      input_format = "verbatim-snippet";
    }
    else {
      buf = md->text().toUtf8 ();
      input_format = "verbatim-snippet";
    }
  }
  else if (format == "verbatim"
           && (get_preference ("verbatim->texmacs:encoding") == "utf-8" ||
               get_preference ("verbatim->texmacs:encoding") == "auto"  ))
    buf = md->text().toUtf8 ();
  else {
    if (md->hasFormat ("plain/text")) buf = md->data ("plain/text").data();
    else buf = md->text().toUtf8 ();
  }
  if (!(buf.isEmpty())) s << string (buf.constData(), buf.size());
  if (input_format == "html-snippet" && seems_buggy_html_paste (s))
    s = correct_buggy_html_paste (s);
  if (input_format != "picture" && seems_buggy_paste (s))
    s = correct_buggy_paste (s);
  if (input_format != "" &&
      input_format != "picture" &&
      input_format != "linked-picture" &&
      !direct_selection)
    s = as_string (call ("convert", s, input_format, "texmacs-snippet"));
  if (input_format == "html-snippet") {
    tree t = as_tree (call ("convert", s, "texmacs-snippet", "texmacs-tree"));
    t = default_with_simplify (t);
    s = as_string (call ("convert", t, "texmacs-tree", "texmacs-snippet"));
  }
  if (input_format == "picture") {
    tree t (IMAGE);
    QSize size= qvariant_cast<QImage>(md->imageData()).size ();
    int ww= size.width (), hh= size.height ();
    string w, h;
    qt_pretty_image_size (ww, hh, w, h);
    t << tuple (tree (RAW_DATA, s), "png") << w << h << "" << "";
    s= as_string (call ("convert", t, "texmacs-tree", "texmacs-snippet"));
  }
  if (input_format == "linked-picture") {
    tree im (IMAGE, s, "", "", "", "");
    s= as_string (call ("convert", im, "texmacs-tree", "texmacs-snippet"));
  }
  t = tuple ("extern", s);

  if (DEBUG_QT)
    debug_qt << "get_selection t: " << t << LF;

  return true;
}

bool
qt_gui_rep::set_selection (string key, tree t,
                           string s, string sv, string sh, string format) {
  selection_t (key)= copy (t);
  selection_s (key)= copy (s);
  
  QClipboard *cb = QApplication::clipboard ();
  QClipboard::Mode mode = QClipboard::Clipboard;
  if (key == "primary");
  else if (key == "mouse" && cb->supportsSelection())
    mode = QClipboard::Selection;
  else return true;
  c_string selection (s);
  int N_selection= N(s);

  QMimeData *md = new QMimeData;

  if (format == "verbatim" || format == "default") {
    if (format == "default") {
      md->setData ("application/x-texmacs-clipboard",
                   QByteArray ((char*) selection, N_selection));
      
      QString pid_str;
      pid_str.setNum (QCoreApplication::applicationPid ());
      md->setData ("application/x-texmacs-pid", pid_str.toLatin1());
      
      (void) sh;
      
      selection = c_string (sv);
      N_selection = N(sv);
    }
    
    string enc = get_preference ("texmacs->verbatim:encoding");
    if (enc == "auto")
      enc = get_locale_charset ();
    
    if (enc == "utf-8" || enc == "UTF-8")
      md->setText (QString::fromUtf8 (selection, N_selection));
    else if (enc == "iso-8859-1" || enc == "ISO-8859-1")
      md->setText (QString::fromLatin1 (selection, N_selection));
    else
      md->setText (QString::fromLatin1 (selection, N_selection));
  }
  else if (format == "html") 
      md->setHtml (QString::fromUtf8 (selection, N_selection));
  else if (format == "latex") {
    string enc = get_preference ("texmacs->latex:encoding"); 
    if (enc == "utf-8" || enc == "UTF-8" || enc == "cork")
      md->setText (utf8_to_qstring (string ((char*) selection, N_selection)));
    else
      md->setText (QString::fromLatin1 (selection, N_selection));
  }
  else
    md->setText (QString::fromLatin1 (selection, N_selection));
  cb->setMimeData (md, mode);
    // according to the docs, ownership of mimedata is transferred to clipboard
    // so no memory leak here
  return true;
}

void
qt_gui_rep::clear_selection (string key) {
  selection_t->reset (key);
  selection_s->reset (key);
  
  QClipboard *cb = QApplication::clipboard();
  QClipboard::Mode mode = QClipboard::Clipboard;
  if (key == "primary");
  else if (key == "mouse" && cb->supportsSelection())
    mode = QClipboard::Selection;
  else return;
  
  bool owns = false;
  const QMimeData *md = cb->mimeData (mode);
  if (md) owns = md->hasFormat ("application/x-texmacs-clipboard");
  if (owns) cb->clear (mode);
  if (mode == QClipboard::Clipboard) {
    clipboard_text_cache.clear ();
    clipboard_text_cache_valid= false;
  }
}

/******************************************************************************
 * Miscellaneous
 ******************************************************************************/

void qt_gui_rep::set_mouse_pointer (string name) { (void) name; }
  // FIXME: implement this function
void qt_gui_rep::set_mouse_pointer (string curs_name, string mask_name)
{ (void) curs_name; (void) mask_name; } ;

/******************************************************************************
 * Main loop
 ******************************************************************************/

void
qt_gui_rep::show_wait_indicator (widget w, string message, string arg)  {
  if (headless_mode) return;
  
  if (!waitWindow) {
    waitWindow= new QTMProgressWindow ("ATHENA", true);
    waitWindow->setWindowModality (Qt::ApplicationModal);
  }
  
  if (N(message)) {
    string tmp = message;
    if (arg != "") tmp = tmp * " " * arg * "...";
    waitDialogs << to_qstring (tmp);
  } else {
    if (waitDialogs.count()) waitDialogs.removeLast();
  }
  
  if (waitDialogs.count()) {
    QString msg = waitDialogs.first();
    if (waitDialogs.count() >= 2)
      msg = msg + QString ("\n") + waitDialogs.last();
    
    waitWindow->setMessage (msg);
    waitWindow->setBusy (true);
    waitWindow->show();
    waitWindow->raise ();

    if (w != NULL) {
      qt_window_widget_rep* win_wid = static_cast<qt_window_widget_rep*> (w.rep);
      waitWindow->centerOn (win_wid->qwid);
    }
    else waitWindow->centerOnScreen ();
    
    waitWindow->repaint();
    qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
  } else {
    if (waitWindow) waitWindow->hide();
  }

  need_update();
}


void (*the_interpose_handler) (void) = NULL;
void (*the_post_repaint_handler) (void) = NULL;

void gui_interpose (void (*r) (void)) { the_interpose_handler = r; }
void gui_post_repaint (void (*r) (void)) { the_post_repaint_handler = r; }

void
qt_gui_rep::event_loop () {
  QCoreApplication* app;
  if (headless_mode)
    app = QCoreApplication::instance ();
  else
    app = QApplication::instance ();
  gui_event_loop_started= true;
  if (!headless_mode) tmapp()->set_splash_progress (98, "Preparing editor");
  update();
  startup_splash_hide_allowed= true;
    //need_update();
  app->exec();
}


/******************************************************************************
 * Main routines
 ******************************************************************************/

void
gui_open (int& argc, char** argv) {
    // start the gui
    // new QApplication (argc,argv); now in texmacs.cpp
  the_gui = tm_new<qt_gui_rep> (argc, argv);
  
#ifdef MACOSX_EXTENSIONS
  mac_begin_remote();
#endif

  // Qt and Guile want to change the locale.
  // We reset it to have POSIX functions parse correctly the configuration files
  // (see as_double() in string.cpp)

  setlocale (LC_NUMERIC, "C");

  // From Qt docs:
  // On Unix/Linux Qt is configured to use the system locale settings by
  // default. This can cause a conflict when using POSIX functions, for
  // instance, when converting between data types such as floats and strings,
  // since the notation may differ between locales. To get around this problem,
  // call the POSIX function setlocale(LC_NUMERIC,"C") right after initializing
  // QApplication, QGuiApplication or QCoreApplication to reset the locale
  // that is used for number formatting to "C"-locale.
  // See https://doc.qt.io/qt-5/qcoreapplication.html#locale-settings
  if (!headless_mode)
    init_style_sheet (tmapp());
}

void
gui_start_loop () {
  // start the main loop
  the_gui->event_loop ();
}

void
gui_hide_splash () {
  if (tmapp()) tmapp()->hide_splash ();
}

void
gui_close () {
    // cleanly close the gui
  ASSERT (the_gui != NULL, "gui not yet open");
  tm_delete (the_gui);
  the_gui = NULL;
  
#ifdef MACOSX_EXTENSIONS
  mac_end_remote();
#endif
}

void
gui_root_extents (SI& width, SI& height) {
    // get the screen size
  the_gui->get_extents (width, height);
}

void
gui_maximal_extents (SI& width, SI& height) {
    // get the maximal size of a window (can be larger than the screen size)
  the_gui->get_max_size (width, height);
}

void
gui_refresh () {
  the_gui->refresh_ui();
}

string
gui_version () {
  return "qt6";
}

static QIcon
system_icon_for_mime (const QString& target, const QString& type) {
  QFileIconProvider provider;
  QString lower= type.toLower ();
  if (target.startsWith ("http://") || target.startsWith ("https://") ||
      lower == "web") {
    QIcon icon= provider.icon (QFileIconProvider::Network);
    if (!icon.isNull ()) return icon;
    return QApplication::style ()->standardIcon (QStyle::SP_DriveNetIcon);
  }
  if (target.endsWith ("/") || lower == "folder")
    return provider.icon (QFileIconProvider::Folder);

  QFileInfo info (target);
  QMimeDatabase db;
  QMimeType mime= db.mimeTypeForFile (info.fileName (),
                                      QMimeDatabase::MatchExtension);
  QIcon icon;
  if (mime.isValid ()) {
    icon= QIcon::fromTheme (mime.iconName ());
    if (icon.isNull ()) icon= QIcon::fromTheme (mime.genericIconName ());
  }
  if (icon.isNull ()) icon= provider.icon (info);
  if (icon.isNull ()) icon= provider.icon (QFileIconProvider::File);
  return icon;
}

string
system_icon_for_link (string target, string type) {
  QString qtarget= to_qstring (target);
  QString qtype= to_qstring (type);
  QIcon icon= system_icon_for_mime (qtarget, qtype);
  if (icon.isNull ()) return "";

  QByteArray key= (qtarget + "|" + qtype).toUtf8 ();
  QString hash= QCryptographicHash::hash (key, QCryptographicHash::Sha1).toHex ();
  url dir= get_texmacs_home_path () * url ("system/cache/cardlink-icons");
  mkdir (dir);
  url file= dir * url (from_qstring (hash) * ".png");
  QString path= to_qstring (concretize (file));
  if (!QFileInfo::exists (path)) {
    QPixmap pixmap= icon.pixmap (QSize (64, 64));
    if (pixmap.isNull () || !pixmap.save (path, "PNG")) return "";
  }
  return concretize (file);
}

/******************************************************************************
 * Queued processing
 ******************************************************************************/

/*!
 We process a maximum of max events. There are two kind of events: those
 which need a pass on interpose_handler just after and the others. We count
 only the first kind of events. In update() we call this function with
 max = 1 so that only one of these "sensible" events is handled. Otherwise
 updating the internal TeXmacs structure becomes very slow. This can be
 considered a limitation of the current implementation of interpose_handler
 Likewise this function is just a hack to get things working properly.
 */

static int keyboard_events = 0;
static int keyboard_special= 0;

void
qt_gui_rep::process_queued_events (int max) {
  int count = 0;
  while (max < 0 || count < max)  {
    const queued_event& ev = waiting_events.next();
    if (ev.x1 == qp_type::QP_NULL) break;
#ifdef QT_CPU_FIX
    if (ev.x1 != qp_type::QP_NULL &&
        ev.x1 != qp_type::QP_SOCKET_NOTIFICATION &&
        ev.x1 != qp_type::QP_DELAYED_COMMANDS)
      tm_wake_up ();
#endif
    switch (ev.x1) {
      case qp_type::QP_NULL :
        break;
      case qp_type::QP_KEYPRESS :
      {
        typedef triple<widget, string, time_t > T;
        T x = open_box <T> (ev.x2);
        if (!is_nil (x.x1)) {
          concrete_simple_widget (x.x1)->handle_keypress (x.x2, x.x3);
          keyboard_events++;
          if (N(x.x2) > 1) keyboard_special++;
        }
      }
        break;
      case qp_type::QP_TEXT_INPUT :
      {
        typedef triple<widget, string, time_t > T;
        T x = open_box <T> (ev.x2);
        if (!is_nil (x.x1))
          concrete_simple_widget (x.x1)->handle_text_input (x.x2, x.x3);
      }
        break;
      case qp_type::QP_KEYBOARD_FOCUS :
      {
        typedef triple<widget, bool, time_t > T;
        T x = open_box <T> (ev.x2);
        if (!is_nil (x.x1))
          concrete_simple_widget (x.x1)->handle_keyboard_focus (x.x2, x.x3);
      }
        break;
      case qp_type::QP_MOUSE :
      {
        typedef sextuple<string, SI, SI, int, time_t, array<double> > T1;
        typedef pair<widget, T1> T;
        T x = open_box <T> (ev.x2);
        if (!is_nil (x.x1))
          concrete_simple_widget (x.x1)->handle_mouse (x.x2.x1, x.x2.x2,
                                                       x.x2.x3, x.x2.x4,
                                                       x.x2.x5, x.x2.x6);
      }
        break;
      case qp_type::QP_RESIZE :
      {
        typedef triple<widget, SI, SI > T;
        T x = open_box <T> (ev.x2);
        if (!is_nil (x.x1))
          concrete_simple_widget (x.x1)->handle_notify_resize (x.x2, x.x3) ;
      }
        break;
      case qp_type::QP_COMMAND :
      {
        command cmd = open_box <command> (ev.x2) ;
        cmd->apply();
      }
        break;
      case qp_type::QP_COMMAND_ARGS :
      {
        typedef pair<command, object> T;
        T x = open_box <T> (ev.x2);
        x.x1->apply (x.x2);
      }
        break;
      case qp_type::QP_DELAYED_COMMANDS :
      {
        delayed_commands.exec_pending();
      }
        break;
        
      default:
        FAILED ("Unexpected queued event");
    }
    switch (ev.x1) {
      case qp_type::QP_COMMAND:
      case qp_type::QP_COMMAND_ARGS:
      case qp_type::QP_RESIZE:
      case qp_type::QP_DELAYED_COMMANDS:
        break;
      default:
        count++;
        break;
    }
  }
}

void
qt_gui_rep::process_keypress (qt_simple_widget_rep *wid, string key, time_t t) {
  typedef triple<widget, string, time_t > T;
  add_event (queued_event (qp_type::QP_KEYPRESS,
                           close_box<T> (T (wid, key, t))));
}

void
qt_gui_rep::process_text_input (qt_simple_widget_rep *wid, string text,
                                time_t t) {
  typedef triple<widget, string, time_t > T;
  add_event (queued_event (qp_type::QP_TEXT_INPUT,
                           close_box<T> (T (wid, text, t))));
}

void
qt_gui_rep::process_keyboard_focus (qt_simple_widget_rep *wid, bool has_focus,
                                    time_t t ) {
  typedef triple<widget, bool, time_t > T;
  add_event (queued_event (qp_type::QP_KEYBOARD_FOCUS,
                           close_box<T> (T (wid, has_focus, t))));
}

void
qt_gui_rep::process_mouse (qt_simple_widget_rep *wid, string kind, SI x, SI y,
                           int mods, time_t t, array<double> data) {
  typedef sextuple<string, SI, SI, int, time_t, array<double> > T1;
  typedef pair<widget, T1> T;
  add_event (queued_event (qp_type::QP_MOUSE,
                           close_box<T> (T (wid, T1 (kind, x, y, mods, t, data)))));
}

void
qt_gui_rep::process_resize (qt_simple_widget_rep *wid, SI x, SI y ) {
  typedef triple<widget, SI, SI > T;
  add_event (queued_event (qp_type::QP_RESIZE, close_box<T> (T (wid, x, y))));
}

void
qt_gui_rep::process_command (command _cmd) {
  add_event (queued_event (qp_type::QP_COMMAND, close_box<command> (_cmd)));
}

void
qt_gui_rep::process_command (command _cmd, object _args) {
  typedef pair<command, object > T;
  add_event (queued_event (qp_type::QP_COMMAND_ARGS,
                           close_box<T> (T (_cmd,_args))));
}

void
qt_gui_rep::process_delayed_commands () {
  add_event (queued_event (qp_type::QP_DELAYED_COMMANDS, blackbox()));
}

/*!
  FIXME: add more types and refine, compare with X11 version.
 */
bool
qt_gui_rep::check_event (int type) {
    // do not interrupt if not updating (e.g. while painting the icons in menus)
  if (!updating || !do_check_events) return false;
  
  switch (type) {
    case INTERRUPT_EVENT:
      if (interrupted) return true;
      else {
        time_t now = texmacs_time ();
        if (now - timeout_time < 0) return false;
        timeout_time = now + time_credit;
        interrupted  = !waiting_events.is_empty();
        return interrupted;
      }
    case INTERRUPTED_EVENT:
      return interrupted;
    default:
      return false;
  }
}

void
qt_gui_rep::set_check_events (bool enable_check) {
  do_check_events = enable_check;
}

void
qt_gui_rep::add_event (const queued_event& ev) {
  waiting_events.append (ev);
  if (updating) {
    needing_update = true;
  } else {
    need_update();
      // NOTE: we cannot update now since sometimes this seems to give problems
      // to the update of the window size after a resize. In that situation
      // sometimes when the window receives focus again, update will be called
      // for the focus_in event and interpose_handler is run which sends a
      // slot_extent message to the widget causing a wrong resize of the window.
      // This seems to cure the problem.
  }
}


/*!
 This is called by doUpdate(), which in turn is fired by a timer activated in
 needs_update(), and ensuring that interpose_handler() is run during a pass in
 the event loop after we reactivate the timer with a pause (see FIXME below).
 */

void
qt_gui_rep::update () {
  qt_drain_action_retirements ();
#ifdef QT_CPU_FIX
  time_t std_delay= 1;
  tm_sleep ();
#else
  time_t std_delay= 90 / 6;
#endif

  if (updating) {
    cout << "NESTED UPDATING: This should not happen" << LF;
    need_update();
    return;
  }

    // cout << "<" << texmacs_time() << " " << N(delayed_queue) << " ";
  
  updatetimer->stop();
  updating = true;
  
  static int count_events    = 0;
  static int max_proc_events = 40;

  time_t     now = texmacs_time();
  needing_update = false;
  time_credit    = 9 / (waiting_events.size() + 1);

  if (waitDialogs.count()) {
    if (waitWindow) waitWindow->hide();
    waitDialogs.clear ();
  }
    
  if (popup_wid_time > 0 && now > popup_wid_time) {
    popup_wid_time = 0;
    _popup_wid->send (SLOT_VISIBILITY, close_box<bool> (true));
  }
  
    // 2.
    // Manage delayed commands
  
  if (delayed_commands.must_wait (now))
    process_delayed_commands();
  
    // 3.
    // If there are pending events in the private queue process them until the
    // limit in processed events is reached.
    // If there are no events or the limit is reached then proceed to a redraw.
  
  if (waiting_events.size() == 0) {
      // If there are no waiting events call the interpose handler at least once
    //if (the_interpose_handler) the_interpose_handler();
  }
  else while (waiting_events.size() > 0 && count_events < max_proc_events) {
    process_queued_events (1);
    count_events++;
    //if (the_interpose_handler) the_interpose_handler();
  }

  if (waiting_events.size() > 0) {
    cout << "warning: too many pending events in qt_gui_rep::update()" << LF;
  }

  // Repaint invalid regions and redraw
  bool postpone_treatment= (keyboard_events > 0 && keyboard_special == 0);
  keyboard_events = 0;
  keyboard_special= 0;
  count_events    = 0;
  
  interrupted  = false;
  timeout_time = texmacs_time() + time_credit;

  if (!postpone_treatment) {
    if (the_interpose_handler) the_interpose_handler();
    qt_simple_widget_rep::repaint_all ();
    if (the_post_repaint_handler) the_post_repaint_handler ();
  }
  
  if (waiting_events.size() > 0) needing_update = true;
  if (interrupted)               needing_update = true;
  if (!headless_mode && nr_windows == 0 && !athena_has_open_ads_panes ())
    qApp->quit ();
  
  time_t delay = delayed_commands.lapse - texmacs_time();
  if (needing_update) delay = 0;
  else                delay = std::max ((time_t)0, std::min (std_delay, delay));
  if (postpone_treatment) delay= 9; // NOTE: force occasional display
 
  updatetimer->start (delay);
  updating = false;

  if (!headless_mode &&
      startup_splash_hide_allowed &&
      !startup_splash_hidden &&
      !delayed_commands.must_wait (texmacs_time())) {
    startup_splash_hidden= true;
    tmapp()->set_splash_progress (100, "Ready");
    tmapp()->hide_splash ();
  }
  
    // FIXME: we need to ensure that the interpose_handler is run at regular
    //        intervals (1/6th of sec) so that informations on the footbar are
    //        updated. (this should be better handled by promoting code in
    //        tm_editor::apply_changes (which is activated only after idle
    //        periods) at the level of delayed commands in the gui.
    //        The interval cannot be too small to keep CPU usage low in idle state
}

void
qt_gui_rep::force_update() {
  if (updating) needing_update = true;
  else          update();
}

void
qt_gui_rep::need_update () {
  if (updatetimer != nullptr &&
      QThread::currentThread () != updatetimer->thread ()) {
    QMetaObject::invokeMethod (
      updatetimer, "start", Qt::QueuedConnection, Q_ARG (int, 0));
    return;
  }
  if (updating) needing_update = true;
  else          updatetimer->start (0);
    // 0 ms - call immediately when all other events have been processed
}

void needs_update () {
  the_gui->need_update();
}

void
qt_gui_rep::refresh_ui() {
  gui_helper->doRefresh();
}

/*! Display a popup help balloon (i.e. a tooltip) at window coordinates x, y
 
 We use a dedicated wrapper QWidget which handles mouse events: as soon as the
 mouse is moved out of we hide it.
 Problem: the widget need not appear below the mouse pointer, thus making it
 impossible to access links or widgets inside it.
 Solution?? as soon as the mouse moves (out of the widget), start a timer,
 giving enough time to the user to move (back) into the widget, then abort the
 close operation if he gets there.
 */
void
qt_gui_rep::show_help_balloon (widget wid, SI x, SI y) {
  if (popup_wid_time > 0) return;
  
  _popup_wid = popup_window_widget (wid, "Balloon");
  SI winx, winy;
  // HACK around wrong? reporting of window widget for embedded texmacs-inputs:
  // call get_window on the current window (concrete_window()->win is set to
  // the texmacs-input widget whenever there is one)
  get_position (get_window (concrete_window()->win), winx, winy);
  set_position (_popup_wid, x+winx, y+winy);
  popup_wid_time = texmacs_time() + 66;
    // update() will eventually show the widget
}


/******************************************************************************
 * Font support
 ******************************************************************************/

/*! Sets the name of the default font.
 @note This is ignored since Qt handles fonts for the widgets.
 */
void
set_default_font (string name) {
  (void) name;
}

/*! Gets the default font or monospaced font (if tt is true).
 @return A null font since this function is not called in the Qt port.
 */
font
get_default_font (bool tt, bool mini, bool bold) {
  (void) tt; (void) mini; (void) bold;
  if (DEBUG_QT) debug_qt << "get_default_font(): SHOULD NOT BE CALLED\n";
  return NULL;  //return tex_font (this, "ecrm", 10, 300, 0);
}

/*! Loads the metric and glyphs of a system font.
 You are not forced to provide any system fonts.
 */
void
load_system_font (string fam, int sz, int dpi, font_metric& fnm, font_glyphs& fng)
{
  (void) fam; (void) sz; (void) dpi; (void) fnm; (void) fng;
  if (DEBUG_QT) debug_qt << "load_system_font(): SHOULD NOT BE CALLED\n";
}


/******************************************************************************
 * Clipboard support
 ******************************************************************************/

bool
set_selection (string key, tree t,
               string s, string sv, string sh, string format) {
    // Copy a selection 't' with string equivalent 's' to the clipboard 'cb'
    // and possibly the variants 'sv' and 'sh' for verbatim and html
    // Returns true on success
  return the_gui->set_selection (key, t, s, sv, sh, format);
}

bool
get_selection (string key, tree& t, string& s, string format) {
    // Retrieve the selection 't' with string equivalent 's' from clipboard 'cb'
    // Returns true on success; sets t to (extern s) for external selections
  return the_gui->get_selection (key, t, s, format);
}

void
clear_selection (string key) {
    // Clear the selection on clipboard 'cb'
  the_gui->clear_selection (key);
}

bool
qt_gui_rep::put_graphics_on_clipboard (url file) {
  string extension = suffix (file) ;
  
    // for bitmaps this works :
  if ((extension == "bmp") || (extension == "png") ||
      (extension == "jpg") || (extension == "jpeg")) {
    QClipboard *clipboard = QApplication::clipboard();
    c_string tmp (concretize (file));
    clipboard->setImage (QImage (QString (tmp)));
  }
  else {
      // vector formats
      // Are there applications receiving eps, pdf,... through the clipboard?
      // I have not experimented with EMF/WMF (windows) or SVM (Ooo)
    QString mime ="image/*"; // generic image format;
    if (extension == "eps") mime = "application/postscript";
    if (extension == "pdf") mime = "application/pdf";
    if (extension == "svg") mime = "image/svg+xml"; //this works with Inskcape version >= 0.47
    
    string filecontent;
    load_string (file, filecontent, true);
    
    // warning: we need to tell Qt the size of the byte buffer
    c_string tmp (filecontent);
    QByteArray rawdata (tmp, N(filecontent));

    QMimeData *mymimeData = new QMimeData;
    mymimeData->setData (mime, rawdata);
    
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setMimeData (mymimeData);// default mode = QClipboard::Clipboard
  }
  return true;
}

/******************************************************************************
* Miscellaneous
******************************************************************************/

int char_clip = 0;

void
beep () {
    // Issue a beep
  QApplication::beep();
}

bool
check_event (int type) {
    // Check whether an event of one of the above types has occurred;
    // we check for keyboard events while repainting windows
  const SchemeExecutionContext* context= current_scheme_execution_context ();
  if (context != nullptr && context->actor_id != ATHENA_NO_ACTOR)
    return false;
  return the_gui->check_event (type);
}

void
show_help_balloon (widget balloon, SI x, SI y) {
    // Display a help balloon at position (x, y); the help balloon should
    // disappear as soon as the user presses a key or moves the mouse
  the_gui->show_help_balloon (balloon, x, y);
}

void
show_wait_indicator (widget base, string message, string argument) {
  // Display a wait indicator with a message and an optional argument
  // The indicator might for instance be displayed at the center of
  // the base widget which triggered the lengthy operation;
  // the indicator should be removed if the message is empty
  the_gui->show_wait_indicator (base, message, argument);
}

void
external_event (string type, time_t t) {
    // External events, such as pushing a button of a remote infrared commander
  QTMWidget *tm_focus = qobject_cast<QTMWidget*>(qApp->focusWidget());
  if (tm_focus) {
    simple_widget_rep* wid = tm_focus->tm_widget();
    if (wid) the_gui -> process_keypress (wid, type, t);
  }
}

/******************************************************************************
 * Delayed commands
 ******************************************************************************/

command_queue::command_queue() : lapse (0), wait (true) { }
command_queue::~command_queue() { clear_pending(); /* implicit */ }

void
command_queue::exec (object cmd) {
  athena_scheme_handle_id handle=
    scheme_command_handle_acquire (object_to_tmscm (cmd));
  const SchemeExecutionContext* context= current_scheme_execution_context ();
  if (context != nullptr && context->view_id != ATHENA_NO_VIEW) {
    actor_ui_endpoint* endpoint=
      find_actor_ui_endpoint (context->view_id);
    if (endpoint != nullptr && endpoint->publish (
          actor_command_kind::ui_schedule_scheme, ATHENA_NO_BLOB,
          handle, 0))
      return;
    scheme_command_handle_release (handle);
    return;
  }
  exec_handle (handle, ATHENA_NO_ACTOR, ATHENA_NO_VIEW, false);
}

void
command_queue::exec_pause (object cmd) {
  athena_scheme_handle_id handle=
    scheme_command_handle_acquire (object_to_tmscm (cmd));
  const SchemeExecutionContext* context= current_scheme_execution_context ();
  if (context != nullptr && context->view_id != ATHENA_NO_VIEW) {
    actor_ui_endpoint* endpoint=
      find_actor_ui_endpoint (context->view_id);
    if (endpoint != nullptr && endpoint->publish (
          actor_command_kind::ui_schedule_scheme, ATHENA_NO_BLOB,
          handle, 1))
      return;
    scheme_command_handle_release (handle);
    return;
  }
  exec_handle (handle, ATHENA_NO_ACTOR, ATHENA_NO_VIEW, true);
}

void
command_queue::exec_global (object cmd) {
  const SchemeExecutionContext* context= current_scheme_execution_context ();
  if (context == nullptr) {
    (void) call_scheme (object_to_tmscm (cmd));
    return;
  }

  athena_scheme_handle_id handle=
    scheme_command_handle_acquire (object_to_tmscm (cmd));
  actor_ui_endpoint* endpoint= find_actor_ui_endpoint (context->view_id);
  if (endpoint != nullptr && endpoint->publish (
        actor_command_kind::ui_schedule_global_scheme, ATHENA_NO_BLOB,
        handle))
    return;
  scheme_command_handle_release (handle);
}

void
command_queue::exec_handle (
  std::uint64_t handle, std::uint64_t actor_id, std::uint64_t view_id,
  bool pause, bool force_global) {
  if (handle == ATHENA_NO_SCHEME_HANDLE) return;
  handles << handle;
  actor_ids << actor_id;
  view_ids << view_id;
  execution_domains << (force_global ? 1 : 0);
  time_t now= texmacs_time ();
  start_times << (pause ? now : now - 1000000000);
  lapse= now;
  if (gui_event_loop_started) the_gui->need_update ();
  wait= true;
}

void
command_queue::complete_handle (
  std::uint64_t handle, std::uint64_t actor_id, std::uint64_t view_id,
  bool repeat, std::int64_t delay) {
  if (repeat) {
    exec_handle (handle, actor_id, view_id, true);
    start_times[N (start_times) - 1]= texmacs_time () +
      static_cast<time_t> (delay);
  }
  else scheme_command_handle_release (handle);
}

void
command_queue::exec_pending () {
  static const int delayed_command_call_budget= 20;
  static const time_t delayed_command_time_budget= 25;
  array<std::uint64_t> h= handles;
  array<std::uint64_t> actors= actor_ids;
  array<std::uint64_t> views= view_ids;
  array<int> domains= execution_domains;
  array<time_t> times= start_times;
  handles= array<std::uint64_t> (0);
  actor_ids= array<std::uint64_t> (0);
  view_ids= array<std::uint64_t> (0);
  execution_domains= array<int> (0);
  start_times= array<time_t> (0);
  int i, n= N (h);
  int processed_calls= 0;
  time_t batch_begin= texmacs_time ();
  for (i = 0; i<n; i++) {
    time_t now =  texmacs_time ();
    if ((now - times[i]) >= 0) {
      athena_actor_id actor_id= actors[i];
      athena_view_id view_id= views[i];
      bool force_global= domains[i] != 0;
      if (!force_global && actor_id == ATHENA_NO_ACTOR &&
          has_current_view ()) {
        tm_view view= concrete_view (get_current_view_safe ());
        if (view != nullptr) {
          actor_id= view->buf->actor->id ();
          view_id= view->runtime_id;
        }
      }
      bool allow_repeat= now - times[i] < 1000000000;
      if (actor_id != ATHENA_NO_ACTOR) {
        actor_command_ticket ticket= buffer_actor::submit_to (
          actor_id, actor_command_kind::run_scheme_handle, view_id,
          ATHENA_NO_BLOB, ATHENA_NO_BLOB,
          SCHEME_CAPABILITY_BUFFER | SCHEME_CAPABILITY_UI |
            SCHEME_CAPABILITY_GLOBAL,
          h[i], allow_repeat ? 1 : 0);
        if (!ticket) scheme_command_handle_release (h[i]);
      }
      else {
        if (view_id != ATHENA_NO_VIEW) {
          tm_view view= concrete_runtime_view (view_id);
          if (view != nullptr) set_current_view (abstract_view (view));
        }
        tmscm command= scheme_command_handle_value (h[i]);
        if (scm_is_eq (command, SCM_UNDEFINED)) continue;
        try {
          tmscm result= call_scheme (command);
          if (allow_repeat && tmscm_is_int (result)) {
            handles << h[i];
            actor_ids << ATHENA_NO_ACTOR;
            view_ids << view_id;
            execution_domains << 1;
            start_times << now + static_cast<time_t> (tmscm_to_int (result));
          }
          else scheme_command_handle_release (h[i]);
        }
        catch (...) {
          scheme_command_handle_release (h[i]);
          throw;
        }
      }
      processed_calls++;
      time_t call_end= texmacs_time ();
      if (processed_calls >= delayed_command_call_budget ||
          call_end - batch_begin >= delayed_command_time_budget) {
        for (int j=i+1; j<n; j++) {
          handles << h[j];
          actor_ids << actors[j];
          view_ids << views[j];
          execution_domains << domains[j];
          start_times << times[j];
        }
        break;
      }
    }
    else {
      handles << h[i];
      actor_ids << actors[i];
      view_ids << views[i];
      execution_domains << domains[i];
      start_times << times[i];
    }
  }
  if (N(handles) > 0) {
    wait = true;  // wait_for_delayed_commands
    lapse = start_times[0];
    int n = N(start_times);
    for (i = 1; i<n; i++) {
      if (lapse > start_times[i]) lapse = start_times[i];
    }
  } else
    wait = false;
}

void
command_queue::clear_pending () {
  for (int i= 0; i < N (handles); ++i)
    scheme_command_handle_release (handles[i]);
  handles= array<std::uint64_t> (0);
  actor_ids= array<std::uint64_t> (0);
  view_ids= array<std::uint64_t> (0);
  execution_domains= array<int> (0);
  start_times = array<time_t> (0);
  wait = false;
}

bool
command_queue::must_wait (time_t now) const {
  return wait && (lapse <= now);
}


/******************************************************************************
 * Delayed commands interface
 ******************************************************************************/

void exec_delayed (object cmd) {
  the_gui->delayed_commands.exec(cmd);
}
void exec_delayed_pause (object cmd) {
  the_gui->delayed_commands.exec_pause(cmd);
}
void exec_global (object cmd) {
  the_gui->delayed_commands.exec_global(cmd);
}
void schedule_delayed_scheme_handle (
  std::uint64_t handle, std::uint64_t actor_id, std::uint64_t view_id,
  bool pause, bool force_global) {
  the_gui->delayed_commands.exec_handle (
    handle, actor_id, view_id, pause, force_global);
}
void complete_delayed_scheme_handle (
  std::uint64_t handle, std::uint64_t actor_id, std::uint64_t view_id,
  bool repeat, std::int64_t delay) {
  the_gui->delayed_commands.complete_handle (
    handle, actor_id, view_id, repeat, delay);
}
void clear_pending_commands () {
  the_gui->delayed_commands.clear_pending();
}


/******************************************************************************
 * Queued events
 ******************************************************************************/

event_queue::event_queue() : n(0) { }

void
event_queue::append (const queued_event& ev) {
  q << ev;
  ++n;
}

queued_event
event_queue::next () {
  if (is_nil(q))
    return queued_event();
  queued_event ev = q->item;
  q = q->next;
  --n;
  return ev;
}

bool
event_queue::is_empty() const {
  ASSERT (!(n!=0 && is_nil(q)), "WTF?");
  return n == 0;
}

int
event_queue::size() const {
  return n;
}


/******************************************************************************
 * MODULE     : QTMApplication.hpp
 * DESCRIPTION:
 * COPYRIGHT  :
 *******************************************************************************
 * This software falls under the GNU general public license version 3 or later.
 * It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
 * in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
 ******************************************************************************/

#ifndef QTMAPPLICATION_HPP
#define QTMAPPLICATION_HPP

#include <QApplication>
#include <QIcon>
#include <QStyle>
#include <QStyleFactory>
#include "string.hpp"
#include "sys_utils.hpp"
#include "url.hpp"
#include "boot.hpp"
#include "gui.hpp"
#include "QTMKeyboard.hpp"
#include "QTMIconManager.hpp"
#include "QTMMainTabWindow.hpp"

bool init_palette (QApplication* app);
void init_style_sheet (QApplication* app);
void set_standard_style_sheet (QWidget *w);
bool is_server_started ();
class QTMProgressWindow;

class QTMApplication: public QApplication {
  Q_OBJECT
  
public:
  
  QTMApplication (int& argc, char** argv);

  void load();
  
  void init_theme ();

  void set_window_icon (string icon_path);
  
  virtual bool notify (QObject* receiver, QEvent* event);

  QTMIconManager& icon_manager() {
    return mIconManager;
  }

  inline QTMKeyboard &keyboard() {
    return mKeyboard;
  }

  inline bool useTabWindow() {
    return mUseTabWindow;
  }

  inline bool useMdi() {
    return mUseMdi;
  }

  inline bool useAds() {
    return mUseAds;
  }

  inline bool useNewToolbar() {
    return mUseNewToolbar;
  }

  inline QTMMainTabWindow &mainTabWindow() {
    if (QTMMainTabWindow::topTabWindow() == nullptr) {
      if (is_server_started ()) new QTMMainTabWindow();
      else ASSERT (false, "mainTabWindow() called while server is not started");
    }
    return *QTMMainTabWindow::topTabWindow();
  }

  void show_splash ();
  void set_splash_progress (int progress, string message);
  void hide_splash ();

private:
  QTMProgressWindow* mStartupWindow;
  bool mPixmapManagerInitialized;
  QTMIconManager mIconManager;
  QTMKeyboard mKeyboard;
  bool mUseTabWindow;
  bool mUseMdi;
  bool mUseAds;
  bool mUseNewToolbar;
};

inline QTMApplication *tmapp() {
  ASSERT (!headless_mode, "invalid call of tmapp() in headless mode");
  return dynamic_cast<QTMApplication *>(qApp);
}

class QTMCoreApplication: public QCoreApplication {
  Q_OBJECT
  
public:
  QTMCoreApplication (int& argc, char** argv) :
    QCoreApplication (argc, argv) {}

  void set_window_icon (string icon_path) {
    (void) icon_path;
  }

  virtual bool notify (QObject* receiver, QEvent* event)
  {
    try {
      return QCoreApplication::notify (receiver, event);
    }
    catch (string s) {
      qt_error << "Thrown " << s << LF;
      the_exception= s;
    }
    return false;
  }
};

#endif

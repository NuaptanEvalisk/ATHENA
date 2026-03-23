#include "QTMApplication.hpp"
#include "qt_utilities.hpp"

  
QTMApplication::QTMApplication (int& argc, char** argv) :
  QApplication (argc, argv), mSplash (NULL) { }

#include <QPixmap>
#include <QPainter>
#include <QScreen>

void QTMApplication::show_splash () {
  if (headless_mode) return;
  string path = get_env ("ATHENA_PATH");
  url u1 = url_system (path * "/misc/pictures/splash/splashscr.png");
  url u2 = url_system (path * "/../misc/pictures/splash/splashscr.png");
  url u3 = url_system (path * "/misc/images/ATHENA-512.png");
  
  url logo_url;
  if (exists (u1)) logo_url = u1;
  else if (exists (u2)) logo_url = u2;
  else if (exists (u3)) logo_url = u3;
  else return;

  QPixmap pixmap (to_qstring (as_string (logo_url)));
  if (pixmap.isNull ()) return;
  
  // Scale if too big (e.g. high-res images on small screens)
  if (primaryScreen()) {
    QSize screenSize = primaryScreen()->availableGeometry().size();
    int maxW = screenSize.width() / 2;
    int maxH = screenSize.height() / 2;
    if (pixmap.width() > maxW || pixmap.height() > maxH) {
      pixmap = pixmap.scaled (maxW, maxH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
  }

  // Create a clean splash with white background to avoid "black square" if PNG has transparency
  QPixmap splash_pix (pixmap.size());
  splash_pix.fill (Qt::white);
  QPainter painter (&splash_pix);
  painter.drawPixmap (0, 0, pixmap);
  painter.end ();

  mSplash = new QSplashScreen (splash_pix, Qt::WindowStaysOnTopHint);
  mSplash->show ();
  mSplash->repaint ();
  mSplash->raise ();
  mSplash->activateWindow ();
  
  // 核心防黑屏 Hack：强行滞留主线程，等待异步 Window Manager 完成 Expose
  // 10次循环 * 5ms 睡眠 = 50ms。这足以让 KWin/Mutter 处理完映射请求并分配显存。
  for (int i = 0; i < 10; i++) {
    qApp->processEvents (QEventLoop::AllEvents, 10);
    QThread::msleep (5);
  }
}

void QTMApplication::hide_splash () {
  if (mSplash) {
    mSplash->finish (nullptr);
    delete mSplash;
    mSplash = nullptr;
  }
}

void QTMApplication::load() {
  string bm = get_user_preference ("buffer management");
  mUseMdi = (bm == "mdi");
  mUseAds = (bm == "ads");
  mUseTabWindow = (bm == "shared") || (mUseMdi) || (mUseAds) || (get_user_preference ("enable tab") == "on");

#if QT_VERSION >= 0x060000
  mUseNewToolbar = get_user_preference ("new toolbar") != "off";
#else
  mUseNewToolbar = false;
#endif

#if QT_VERSION >= 0x060000
  mPixmapManagerInitialized = false;
#endif

  init_theme ();

  if (mUseTabWindow) new QTMMainTabWindow();
}
  

void QTMApplication::init_theme () {
#if defined(OS_MINGW64) && QT_VERSION >= 0x060000
  setStyle(QStyleFactory::create("Windows"));
#endif    
  string theme= get_user_preference ("gui theme", "default");
  if (theme == "default") 
    theme = get_default_theme ();
  if (theme == "light")
    tm_style_sheet= "$ATHENA_PATH/misc/themes/standard-light.css";
  else if (theme == "dark")
    tm_style_sheet= "$ATHENA_PATH/misc/themes/standard-dark.css";
  else if (theme != "")
    tm_style_sheet= theme;

  init_palette (this);
  init_style_sheet (this);
}

void QTMApplication::set_window_icon (string icon_path) {
  url icon_url= url_system (get_env ("ATHENA_PATH") * icon_path);
  if (exists (icon_url)) {
    const c_string _icon (as_string (icon_url));
    setWindowIcon (QIcon ((const char*) _icon));
  }
  else {
    std_warning << "Could not find TeXmacs icon file: " << as_string (icon_url) << LF;
  }
}

bool QTMApplication::notify (QObject* receiver, QEvent* event)
{
  try {
    return QApplication::notify (receiver, event);
  }
  catch (string s) {
    //c_string cs (s);
    //tm_failure (cs);
    //qt_error << "Thrown " << s << LF;
    the_exception= s;
  }
  return false;
}

#include "QTMApplication.hpp"
#include "qt_utilities.hpp"
  
QTMApplication::QTMApplication (int& argc, char** argv) :
  QApplication (argc, argv), mSplash (NULL) { }

#include <QPixmap>
#include <QPainter>
#include <QScreen>
#include <algorithm>

class ATHENASplashScreen: public QSplashScreen {
public:
  ATHENASplashScreen (const QPixmap& pixmap)
    : QSplashScreen (pixmap, Qt::WindowStaysOnTopHint),
      progress (0), status ("Starting ATHENA") {}

  void set_progress (int new_progress, QString new_status) {
    progress= std::max (0, std::min (100, new_progress));
    status= new_status;
    repaint ();
  }

protected:
  void drawContents (QPainter* painter) override {
    QSplashScreen::drawContents (painter);

    QRect r= rect ();
    int margin= std::max (12, r.width () / 28);
    int bar_h= std::max (10, r.height () / 38);
    int panel_h= bar_h + 38;
    QRect panel (margin, r.height () - panel_h - margin,
                 r.width () - 2 * margin, panel_h);
    QRect bar (panel.left () + 12, panel.bottom () - bar_h - 10,
               panel.width () - 24, bar_h);
    int fill_w= (bar.width () * progress) / 100;

    painter->setRenderHint (QPainter::Antialiasing, true);
    painter->setPen (Qt::NoPen);
    painter->setBrush (QColor (255, 255, 255, 230));
    painter->drawRoundedRect (panel, 5, 5);

    painter->setPen (QColor (45, 52, 62));
    QFont f= painter->font ();
    f.setPointSize (std::max (9, f.pointSize ()));
    painter->setFont (f);
    QString label= status + QString ("  %1%").arg (progress);
    painter->drawText (panel.adjusted (12, 7, -12, -bar_h - 14),
                       Qt::AlignLeft | Qt::AlignVCenter, label);

    painter->setPen (QColor (170, 176, 184));
    painter->setBrush (QColor (236, 239, 243));
    painter->drawRoundedRect (bar, 4, 4);
    if (fill_w > 0) {
      QRect fill= bar;
      fill.setWidth (fill_w);
      painter->setPen (Qt::NoPen);
      painter->setBrush (QColor (49, 112, 184));
      painter->drawRoundedRect (fill, 4, 4);
    }
  }

private:
  int progress;
  QString status;
};

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

  mSplash = new ATHENASplashScreen (splash_pix);
  set_splash_progress (2, "Preparing application");
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

void QTMApplication::set_splash_progress (int progress, string message) {
  if (!mSplash) return;
  ATHENASplashScreen* splash= dynamic_cast<ATHENASplashScreen*> (mSplash);
  if (splash) splash->set_progress (progress, to_qstring (message));
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

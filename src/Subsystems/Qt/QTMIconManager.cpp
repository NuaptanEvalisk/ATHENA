
/******************************************************************************
* MODULE     : QTMIconManager.cpp
* DESCRIPTION: Utility class to manage icons
* COPYRIGHT  : (C) 2024 Liza Belos, 2025 Gregoire Lecerf
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMIconManager.hpp"
#include "file.hpp"
#include "qt_picture.hpp"
#include "qt_utilities.hpp"
#include "tm_frame.hpp"
#include "qt_widget.hpp"
#include "env.hpp"
#include "typesetter.hpp"
#include "drd_std.hpp"
#include "QTMCommandRegistry.hpp"
#include <QAction>

#include <QApplication>
#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QPixmap>
#include <QIconEngine>
#include <QPainter>
#include <QPixmapCache>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QSaveFile>
#include <QHash>
#include <QSet>
#include <QThread>
#include <algorithm>
#include <functional>
#include <memory>

bool may_transform (url file_name, const QImage& pm);

bool
QTMIconManager::is_dark_mode () {
  if (occurs ("dark", tm_style_sheet)) return true;
  if (tm_style_sheet != "" && !occurs ("native", tm_style_sheet)) return false;
  const QPalette palette= QApplication::palette ();
  return palette.color (QPalette::WindowText).lightness () >
         palette.color (QPalette::Window).lightness ();
}

namespace {

// All mathematical menu previews are rasterized on startup after the math
// font catalog is initialized. The GUI paint path uses only premade images;
// in particular a QMenu::paintEvent may never enter typeset_as_concat,
// virtual_font_rep::compile, or search_font.
constexpr int math_icon_raster_size= 48;
constexpr int math_icon_cache_schema= 2;

QHash<QString, QImage>&
math_icon_images () {
  static QHash<QString, QImage> images;
  return images;
}

QString
math_icon_digest (const QString& symbol) {
  return QString::fromLatin1 (QCryptographicHash::hash (
    symbol.toUtf8 (), QCryptographicHash::Sha256).toHex ());
}

QImage
render_math_icon_at_startup (const QString& symbol) {
  drd_info drd ("menu-symbol", std_drd);
  hashmap<string, tree> lr (UNINIT), gr (UNINIT), la (UNINIT), ga (UNINIT);
  hashmap<string, tree> lat (UNINIT), gat (UNINIT);
  edit_env env (drd, url_none (), lr, gr, la, ga, lat, gat);
  env->write (MODE, "math");
  // Cache a neutral alpha image; the palette is applied with Qt later,
  // without constructing a TeXmacs font or a box.
  env->write (COLOR, "#ffffff");
  tree content= symbol.startsWith (QStringLiteral ("texmacs:"))
    ? tree (NAMED_SYMBOL, from_qstring (symbol))
    : tree (from_qstring (symbol));
  box b= typeset_as_concat (env, content, path (DECORATION));
  widget preview= box_widget (b, true, PIXEL);
  std::unique_ptr<QAction> action (concrete (preview)->as_qaction ());
  return action->icon ().pixmap (
    QSize (math_icon_raster_size, math_icon_raster_size)).toImage ()
      .convertToFormat (QImage::Format_ARGB32_Premultiplied);
}

QString
math_icon_cache_signature (const QStringList& sortedSymbols) {
  QCryptographicHash hash (QCryptographicHash::Sha256);
  auto append= [&hash] (const QString& value) {
    hash.addData (value.toUtf8 ());
    hash.addData ("\0", 1);
  };
  append (QString::number (math_icon_cache_schema));
  append (QString::fromLatin1 (qVersion ()));
  const QFileInfo executable (QCoreApplication::applicationFilePath ());
  append (executable.absoluteFilePath ());
  append (QString::number (executable.size ()));
  append (QString::number (executable.lastModified ().toMSecsSinceEpoch ()));
  for (const QString& symbol: sortedSymbols) append (symbol);

  // Virtual recipes and bundled font files participate in the signature.
  // File sizes and timestamps keep the check cheap on subsequent launches.
  const QString fontsPath= qEnvironmentVariable ("ATHENA_PATH") +
                           QStringLiteral ("/fonts");
  QStringList filenames;
  QDirIterator files (fontsPath, QDir::Files,
                      QDirIterator::Subdirectories);
  while (files.hasNext ()) filenames << files.next ();
  filenames.sort ();
  for (const QString& name: filenames) {
    const QFileInfo info (name);
    append (name);
    append (QString::number (info.size ()));
    append (QString::number (info.lastModified ().toMSecsSinceEpoch ()));
  }
  const QFileInfo catalog (
    qEnvironmentVariable ("ATHENA_HOME_PATH") +
    QStringLiteral ("/system/cache/font-characteristics.json"));
  append (QString::number (catalog.size ()));
  append (QString::number (catalog.lastModified ().toMSecsSinceEpoch ()));
  return QString::fromLatin1 (hash.result ().toHex ());
}

bool
write_cached_image (const QString& name, const QImage& image) {
  QSaveFile file (name);
  if (!file.open (QIODevice::WriteOnly)) return false;
  if (!image.save (&file, "PNG")) return false;
  return file.commit ();
}

// No calls to font APIs or typesetting are permitted here.
class MathSymbolIconEngine final: public QIconEngine {
  QString symbol;
public:
  explicit MathSymbolIconEngine (QString value): symbol (std::move (value)) {}
  QIconEngine* clone () const override {
    return new MathSymbolIconEngine (symbol);
  }
  QPixmap pixmap (const QSize& size, QIcon::Mode mode,
                  QIcon::State state) override {
    const QImage source= math_icon_images ().value (symbol);
    if (source.isNull () || size.isEmpty ()) return QPixmap ();
    const QColor foreground= QApplication::palette ().color (
      mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active,
      QPalette::WindowText);
    const QString key= QStringLiteral ("athena-math-prepared:%1:%2:%3:%4:%5")
      .arg (symbol).arg (size.width ()).arg (size.height ())
      .arg (foreground.rgba ()).arg (int (state));
    QPixmap result;
    if (QPixmapCache::find (key, &result)) return result;
    result= QPixmap::fromImage (source).scaled (
      size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPainter tint (&result);
    tint.setCompositionMode (QPainter::CompositionMode_SourceIn);
    tint.fillRect (result.rect (), foreground);
    tint.end ();
    QPixmapCache::insert (key, result);
    return result;
  }
  void paint (QPainter* painter, const QRect& rect, QIcon::Mode mode,
              QIcon::State state) override {
    const qreal ratio= painter->device ()->devicePixelRatioF ();
    QPixmap image= pixmap (rect.size () * ratio, mode, state);
    if (image.isNull ()) return;
    image.setDevicePixelRatio (ratio);
    painter->drawPixmap (rect, image);
  }
};

// Select the asset at paint time, including for actions created before the
// system appearance changes. Reuse the existing fallback color transform.
class PaletteIconEngine final: public QIconEngine {
  QIcon light_, dark_;
  bool transform_;
public:
  PaletteIconEngine (QIcon light, QIcon dark, bool transform):
    light_ (std::move (light)), dark_ (std::move (dark)),
    transform_ (transform) {}
  QIconEngine* clone () const override { return new PaletteIconEngine (*this); }
  bool isNull () override { return light_.isNull (); }
  QSize actualSize (const QSize& size, QIcon::Mode mode,
                    QIcon::State state) override {
    const QIcon& source= QTMIconManager::is_dark_mode () && !dark_.isNull () ?
      dark_ : light_;
    return source.actualSize (size, mode, state);
  }
  QPixmap pixmap (const QSize& size, QIcon::Mode mode,
                  QIcon::State state) override {
    return scaledPixmap (size, mode, state, 1.0);
  }
  QPixmap scaledPixmap (const QSize& size, QIcon::Mode mode,
                        QIcon::State state, qreal scale) override {
    if (!QTMIconManager::is_dark_mode ())
      return light_.pixmap (size, scale, mode, state);
    if (!dark_.isNull ()) return dark_.pixmap (size, scale, mode, state);
    if (!transform_) return light_.pixmap (size, scale, mode, state);
    const QString key= QStringLiteral ("athena-dark-icon:%1:%2:%3:%4:%5:%6")
      .arg (light_.cacheKey ()).arg (size.width ()).arg (size.height ())
      .arg (scale).arg (int (mode)).arg (int (state));
    QPixmap result;
    if (QPixmapCache::find (key, &result)) return result;
    QImage image= light_.pixmap (size, scale, mode, state).toImage ();
    invert_colors (image);
    saturate (image);
    result= QPixmap::fromImage (image);
    QPixmapCache::insert (key, result);
    return result;
  }
  void paint (QPainter* painter, const QRect& rect,
              QIcon::Mode mode, QIcon::State state) override {
    const QPixmap pm= scaledPixmap (
      rect.size (), mode, state, painter->device ()->devicePixelRatioF ());
    painter->drawPixmap (rect, pm);
  }
};

QIcon adaptive_icon (url name, QIcon light, QIcon dark= QIcon ()) {
  return QIcon (new PaletteIconEngine (
    std::move (light), std::move (dark), may_transform (name, QImage ())));
}

} // namespace

static QString
icon_key (url file_name) {
  QString key= to_qstring (as_string (file_name));
  int slash= key.lastIndexOf ('/');
  int backslash= key.lastIndexOf ('\\');
  int pos= qMax (slash, backslash);
  if (pos >= 0) key= key.mid (pos + 1);
  int dot= key.lastIndexOf ('.');
  if (dot > 0) key= key.left (dot);
  return key;
}

static QStringList
json_string_list (const QJsonObject& obj, const char* field) {
  QStringList result;
  QJsonValue value= obj.value (field);
  if (!value.isArray ()) return result;
  for (const QJsonValue& item: value.toArray ())
    if (item.isString ()) result << item.toString ();
  return result;
}

void
QTMIconManager::warn_icon_map (const char* message) {
  if (icon_map_warned) return;
  icon_map_warned= true;
  std_warning << "icon theme map warning: " << message << LF;
}

void
QTMIconManager::load_icon_map () {
  if (icon_map_loaded) return;
  icon_map_loaded= true;

  string text;
  if (load_string (url ("$ATHENA_PATH/misc/input/icon-theme-map.json"),
                   text, false)) {
    warn_icon_map ("cannot read $ATHENA_PATH/misc/input/icon-theme-map.json");
    return;
  }

  c_string bytes (text);
  QJsonParseError error;
  QJsonDocument doc= QJsonDocument::fromJson (QByteArray (bytes, N(text)),
                                              &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject ()) {
    warn_icon_map ("invalid JSON in icon-theme-map.json");
    return;
  }

  QJsonValue icons_value= doc.object ().value ("icons");
  if (!icons_value.isObject ()) {
    warn_icon_map ("icon-theme-map.json has no icons object");
    return;
  }

  QJsonObject icons= icons_value.toObject ();
  for (auto it= icons.begin (); it != icons.end (); ++it) {
    if (!it.value ().isObject ()) continue;
    QJsonObject obj= it.value ().toObject ();
    QTMIconMapping mapping {
      json_string_list (obj, "theme"),
      json_string_list (obj, "libreoffice")
    };
    if (mapping.theme_names.isEmpty () &&
        mapping.libreoffice_paths.isEmpty ()) continue;
    icon_map[it.key ()]= mapping;
  }

  for (const QString& key: json_string_list (doc.object (),
                                              "prefer_bundled"))
    prefer_bundled_icons.insert (key);
}

static bool
load_theme_icon (const QString& name, QIcon& icon) {
  if (name.isEmpty ()) return false;
  icon= QIcon::fromTheme (name);
  return !icon.isNull ();
}

static bool
load_icon_file (url file_name, QIcon& icon) {
  url res= resolve (file_name);
  if (is_none (res)) return false;
  icon= QIcon (to_qstring (concretize (res)));
  return !icon.isNull ();
}

static bool
load_libreoffice_icon (const QString& rel_path, QIcon& icon) {
  if (rel_path.isEmpty ()) return false;
  string path= string ("$ATHENA_PATH/misc/icons/libreoffice/colibre/") *
               string (rel_path.toUtf8 ().constData ());
  if (!load_icon_file (url (path), icon)) return false;
  icon= adaptive_icon (url (path), icon);
  return true;
}

static bool
load_svg (url file_name, QIcon& icon) {
  url res= file_name;
  QIcon dark;
  if (!is_rooted (file_name)) {
    res= resolve (url ("$ATHENA_PIXMAP_PATH") * url ("light") * file_name |
		  url ("$ATHENA_PIXMAP_PATH") * file_name);
    if (is_none (res)) return false;
    load_icon_file (url ("$ATHENA_PIXMAP_PATH") * url ("dark") * file_name, dark);
  }
  icon= adaptive_icon (file_name, QIcon (to_qstring (concretize (res))), dark);
  return !icon.isNull ();
}

static bool
load_pixmap (url file_name, QIcon& icon, double dpr) {
  url res= file_name;
  url dark_res= url_none ();
  int possible_dpr= ceil (dpr);
  if (!is_rooted (file_name)) {
    string tag= "";
    string suf= suffix (file_name);
    url name= N(suf) == 0 ? file_name : unglue (file_name, N(suf)+1);
    if (possible_dpr == 2 || possible_dpr == 4)
      tag= "_x" * as_string (possible_dpr);
    url name_png= glue (name, tag * ".png");
    res= resolve (url ("$ATHENA_PIXMAP_PATH") * url ("light") * name_png |
		  url ("$ATHENA_PIXMAP_PATH") * name_png);
    if (is_none (res)) return false;
    dark_res= resolve (url ("$ATHENA_PIXMAP_PATH") * url ("dark") * name_png);
  }
  QPixmap pm= QPixmap (to_qstring (concretize (res)));
  pm.setDevicePixelRatio (possible_dpr);
  QIcon dark;
  if (!is_none (dark_res)) {
    QPixmap dark_pm (to_qstring (concretize (dark_res)));
    dark_pm.setDevicePixelRatio (possible_dpr);
    dark= QIcon (dark_pm);
  }
  icon= adaptive_icon (file_name, QIcon (pm), dark);
  return !icon.isNull ();
}

static bool
load_pixmap (url file_name, QIcon& icon) {
  return load_pixmap (file_name, icon, 4.0) ||
         load_pixmap (file_name, icon, 2.0) ||
         load_pixmap (file_name, icon, 1.0);
}

static bool
load_bundled_icon (url file_name, const QString& key, QIcon& icon) {
  if (!key.startsWith ("tm_")) return false;
  string suf= suffix (file_name);
  url name= N(suf) == 0 ? file_name : unglue (file_name, N(suf)+1);
  return load_svg (glue (name, ".svg"), icon) ||
         load_pixmap (file_name, icon);
}

void
QTMIconManager::prewarmMathIcons (
  const std::function<void (int, int)>& progress) {
  Q_ASSERT (QThread::currentThread () == qApp->thread ());
  QTMCommandRegistry& registry= QTMCommandRegistry::instance ();
  if (!registry.initialize ()) {
    std_warning << "math icon cache: command registry not available" << LF;
    return;
  }

  QSet<QString> symbols;
  auto consider= [&symbols] (const QString& icon) {
    if (icon.startsWith (QStringLiteral ("math:")))
      symbols.insert (icon.mid (5));
  };
  for (const QTMCommandDefinition& command: registry.commands ())
    consider (command.icon);
  std::function<void (const QVector<QTMCommandMenuItem>&)> collect;
  collect= [&collect, &consider] (const QVector<QTMCommandMenuItem>& items) {
    for (const QTMCommandMenuItem& item: items) {
      consider (item.icon);
      collect (item.items);
    }
  };
  for (const QTMCommandMenuDefinition& menu: registry.menus ())
    collect (menu.items);
  for (const QTMCommandToolbarDefinition& toolbar: registry.toolbars ())
    collect (toolbar.items);

  QStringList ordered= symbols.values ();
  ordered.sort ();
  QHash<QString, QImage>& images= math_icon_images ();
  images.clear ();
  if (ordered.isEmpty ()) return;

  const QString folder= qEnvironmentVariable ("ATHENA_HOME_PATH") +
                         QStringLiteral ("/system/cache/math-icons-v2");
  const QString signature= math_icon_cache_signature (ordered);
  QDir ().mkpath (folder);
  QFile manifest (folder + QStringLiteral ("/manifest.json"));
  bool manifestValid= false;
  if (manifest.open (QIODevice::ReadOnly)) {
    const QJsonDocument document= QJsonDocument::fromJson (manifest.readAll ());
    manifestValid= document.isObject () &&
      document.object ().value ("signature").toString () == signature &&
      document.object ().value ("count").toInt () == ordered.size ();
  }

  bool allSaved= true;
  int regenerated= 0;
  const int total= ordered.size ();
  for (int i=0; i<total; ++i) {
    const QString& symbol= ordered[i];
    const QString filename= folder + "/" + math_icon_digest (symbol) + ".png";
    QImage image;
    if (manifestValid) image.load (filename, "PNG");
    if (image.isNull ()) {
      try {
        image= render_math_icon_at_startup (symbol);
      }
      catch (...) {
        std_warning << "math icon cache: cannot render a symbol" << LF;
      }
      if (image.isNull ()) {
        // Even on a rendering failure, a paint event cannot compile the icon.
        // Record a transparent placeholder instead of retrying in a menu.
        image= QImage (math_icon_raster_size, math_icon_raster_size,
                       QImage::Format_ARGB32_Premultiplied);
        image.fill (Qt::transparent);
      }
      if (!write_cached_image (filename, image)) allSaved= false;
      ++regenerated;
    }
    images.insert (symbol, image);
    if (progress && ((i+1) % 16 == 0 || i+1 == total))
      progress (i+1, total);
  }
  // Mark the cache complete only after every image has been saved. Incomplete
  // or interrupted warmups are reconstructed on the next startup.
  if (allSaved && (regenerated > 0 || !manifestValid)) {
    QSaveFile output (folder + QStringLiteral ("/manifest.json"));
    QJsonObject details;
    details.insert ("signature", signature);
    details.insert ("count", total);
    details.insert ("schema", math_icon_cache_schema);
    if (output.open (QIODevice::WriteOnly)) {
      output.write (QJsonDocument (details).toJson (QJsonDocument::Compact));
      if (!output.commit ())
        std_warning << "math icon cache: could not commit manifest" << LF;
    }
  }
  io_info << "math icon cache: " << total << " loaded, "
          << regenerated << " built during startup" << LF;
}

QIcon
QTMIconManager::getPresentationIcon (const QString& value) {
  if (value.isEmpty ()) return QIcon ();
  if (value.startsWith (QStringLiteral ("math:"))) {
    if (!icon_table.contains (value))
      icon_table.insert (value, QIcon (new MathSymbolIconEngine (value.mid (5))));
    return icon_table.value (value);
  }
  if (value.startsWith ('#')) {
    QColor color (value);
    if (color.isValid ()) {
      QPixmap pixmap (16, 16);
      pixmap.fill (color);
      return QIcon (pixmap);
    }
  }
  return getIcon (url (from_qstring (value)));
}

QIcon
QTMIconManager::getDocumentIcon (const QString& filename) {
  const QString extension= QFileInfo (filename).suffix ().toLower ();
  QString name;
  if (extension == "ath") name= "text-x-athena";
  else if (extension == "ats") name= "text-x-athena-style";
  else if (extension == "avd") name= "application-x-athena-avd";
  else return QIcon ();
  const QString key= "document:" + name;
  if (!icon_table.contains (key)) {
    QIcon icon;
    load_icon_file (url ("$ATHENA_PATH/misc/images/" *
                        from_qstring (name) * ".svg"), icon);
    icon_table.insert (key, icon);
  }
  return icon_table.value (key);
}

QIcon
QTMIconManager::getIcon (url file_name) {
  QIcon icon;
  QString cache_key= to_qstring (as_string (file_name));
  if (icon_cache ().contains (cache_key))
    return icon_cache ()[cache_key];

  if (file_name == url ("ATHENA") &&
      load_icon_file (url ("$ATHENA_PATH/misc/images/ATHENA.svg"), icon)) {
    icon_cache ()[cache_key]= icon;
    return icon;
  }

  load_icon_map ();
  QString key= icon_key (file_name);
  QTMIconMapping mapping= icon_map.value (key);

  if (prefer_bundled_icons.contains (key) &&
      load_bundled_icon (file_name, key, icon)) {
    icon_cache ()[cache_key]= icon;
    return icon;
  }

  for (const QString& path: mapping.libreoffice_paths)
    if (load_libreoffice_icon (path, icon)) {
      icon_cache ()[cache_key]= icon;
      return icon;
    }

  if (load_bundled_icon (file_name, key, icon)) {
    icon_cache ()[cache_key]= icon;
    return icon;
  }

  for (const QString& name: mapping.theme_names)
    if (load_theme_icon (name, icon)) {
      icon_cache ()[cache_key]= icon;
      return icon;
    }

  string suf= suffix (file_name);
  url name= N(suf) == 0 ? file_name : unglue (file_name, N(suf)+1);
  if (load_svg (glue (name, ".svg"), icon) ||
      load_pixmap (file_name, icon)) {
    icon_cache ()[cache_key]= icon;
    return icon;
  }
  if (file_name != url ("ATHENA"))
    std_error << "Icon not found: " << file_name << LF;
  load_svg (url ("$ATHENA_PATH/misc/images/ATHENA.svg"), icon);
  return icon;
}

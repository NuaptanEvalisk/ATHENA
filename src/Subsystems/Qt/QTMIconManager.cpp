
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

QIcon
QTMIconManager::getPresentationIcon (const QString& value) {
  if (value.isEmpty ()) return QIcon ();
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
QTMIconManager::getIcon (url file_name) {
  QIcon icon;
  QString cache_key= to_qstring (as_string (file_name));
  if (icon_cache ().contains (cache_key))
    return icon_cache ()[cache_key];

  if (file_name == url ("ATHENA") &&
      load_icon_file (url ("$ATHENA_PATH/misc/images/ATHENA-512.png"), icon)) {
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

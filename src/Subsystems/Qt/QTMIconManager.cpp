
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

bool may_transform (url file_name, const QImage& pm);

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
  return load_icon_file (url (path), icon);
}

static bool
load_svg (url file_name, QIcon& icon) {
  url sub= QTMIconManager::is_dark_mode () ?
    url ("dark") : url ("light");
  url res= file_name;
  if (!is_rooted (file_name)) {
    res= resolve (url ("$ATHENA_PIXMAP_PATH") * sub * file_name |
		  url ("$ATHENA_PIXMAP_PATH") * file_name);
    if (is_none (res)) return false;
  }
  icon= QIcon (to_qstring (concretize (res)));
  if (QTMIconManager::is_dark_mode () &&
      tail (head (res)) != url (sub)) {
    QImage image= icon.pixmap (512).toImage ();
    if (may_transform (file_name, image)) {
      invert_colors (image);
      saturate (image);
      QPixmap pixmap= QPixmap::fromImage (image);
      icon= QIcon (pixmap);
    }
  }
  return !icon.isNull ();
}

static bool
load_pixmap (url file_name, QIcon& icon, double dpr) {
  url sub= QTMIconManager::is_dark_mode () ?
    url ("dark") : url ("light");
  url res= file_name;
  int possible_dpr= ceil (dpr);
  if (!is_rooted (file_name)) {
    string tag= "";
    string suf= suffix (file_name);
    url name= N(suf) == 0 ? file_name : unglue (file_name, N(suf)+1);
    if (possible_dpr == 2 || possible_dpr == 4)
      tag= "_x" * as_string (possible_dpr);
    url name_png= glue (name, tag * ".png");
    url name_xpm= glue (name, tag * ".xpm");
    res= resolve (url ("$ATHENA_PIXMAP_PATH") * sub * name_png |
		  url ("$ATHENA_PIXMAP_PATH") * sub * name_xpm |
		  url ("$ATHENA_PIXMAP_PATH") * name_png |
		  url ("$ATHENA_PIXMAP_PATH") * name_xpm);
    if (is_none (res)) return false;
  }
  QPixmap pm= QPixmap (to_qstring (concretize (res)));
  pm.setDevicePixelRatio (possible_dpr);
  if (QTMIconManager::is_dark_mode () &&
      tail (head (res)) != url (sub)) {
    QImage image= pm.toImage();
    if (may_transform (file_name, image)) {
      invert_colors (image);
      saturate (image);
      pm= QPixmap::fromImage (image);
    }
  }
  icon= QIcon (pm);
  return !icon.isNull ();
}

static bool
load_pixmap (url file_name, QIcon& icon) {
  return load_pixmap (file_name, icon, 4.0) ||
         load_pixmap (file_name, icon, 2.0) ||
         load_pixmap (file_name, icon, 1.0);
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

  for (const QString& path: mapping.libreoffice_paths)
    if (load_libreoffice_icon (path, icon)) {
      icon_cache ()[cache_key]= icon;
      return icon;
    }

  string suf= suffix (file_name);
  url name= N(suf) == 0 ? file_name : unglue (file_name, N(suf)+1);
  if (key.startsWith ("tm_") &&
      (load_svg (glue (name, ".svg"), icon) ||
       load_pixmap (file_name, icon))) {
    icon_cache ()[cache_key]= icon;
    return icon;
  }

  for (const QString& name: mapping.theme_names)
    if (load_theme_icon (name, icon)) {
      icon_cache ()[cache_key]= icon;
      return icon;
    }

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

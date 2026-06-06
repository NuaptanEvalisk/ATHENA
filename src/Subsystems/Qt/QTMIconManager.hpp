
/******************************************************************************
* MODULE     : QTMIconManager.hpp
* DESCRIPTION: Utility class to manage icons
* COPYRIGHT  : (C) 2024 Liza Belos, 2025 Gregoire Lecerf
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef ATHENA_QTMICONMANAGER_HPP
#define ATHENA_QTMICONMANAGER_HPP

#include <QApplication>

#include <QIcon>
#include <QMap>
#include <QStringList>
#include "url.hpp"
#include "gui.hpp"

struct QTMIconMapping {
  QStringList theme_names;
  QStringList libreoffice_paths;
};

class QTMIconManager {

public:
  QTMIconManager () {};
  
  QIcon getIcon (url file_name);

  static inline bool is_dark_mode () {
    return occurs ("dark", tm_style_sheet); }

private:
  QMap<QString, QIcon> icon_table, dark_icon_table;
  QMap<QString, QTMIconMapping> icon_map;
  bool icon_map_loaded= false;
  bool icon_map_warned= false;
  
  void load_icon_map ();
  void warn_icon_map (const char* message);

  inline QMap<QString, QIcon>& icon_cache () {
    return is_dark_mode () ? dark_icon_table : icon_table; }
};

#endif // ATHENA_QTMICONMANAGER_HPP

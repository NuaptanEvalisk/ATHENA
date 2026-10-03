/******************************************************************************
* MODULE     : desktop_environment.hpp
* DESCRIPTION: Allowlisted desktop presentation settings for sandboxed plugins
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#ifndef ATHENA_PLUGIN_DESKTOP_ENVIRONMENT_HPP
#define ATHENA_PLUGIN_DESKTOP_ENVIRONMENT_HPP

namespace athena::plugins {

// No session bus, loader paths, or general access to the user's home directory.
inline constexpr const char* desktop_environment_keys[]= {
  "DISPLAY", "XAUTHORITY", "WAYLAND_DISPLAY", "XDG_RUNTIME_DIR",
  "XDG_CURRENT_DESKTOP", "XDG_SESSION_DESKTOP", "XDG_SESSION_TYPE",
  "KDE_SESSION_VERSION", "KDE_FULL_SESSION", "XDG_CONFIG_HOME", "XDG_CONFIG_DIRS",
  "QT_QPA_PLATFORM", "QT_QPA_PLATFORMTHEME", "QT_STYLE_OVERRIDE",
  "QT_SCALE_FACTOR", "QT_SCREEN_SCALE_FACTORS", "QT_FONT_DPI",
  "QT_SCALE_FACTOR_ROUNDING_POLICY", "QT_AUTO_SCREEN_SCALE_FACTOR",
  "QT_ENABLE_HIGHDPI_SCALING", "QT_USE_PHYSICAL_DPI"
};

inline constexpr const char* desktop_config_files[]= {
  "kdeglobals", "kcmfonts", "qt6ct/qt6ct.conf", "qt6ct/colors"
};

} // namespace athena::plugins
#endif

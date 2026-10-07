/******************************************************************************
* MODULE     : qt_resvg_fonts.cpp
* DESCRIPTION: Owner-scoped ATHENA font injection for resvg
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "qt_resvg_fonts.hpp"

#include "athena_platform.hpp"
#include "font_database.hpp"

#include <ResvgQt.h>

#include <QFileInfo>

#include <unordered_set>

std::uint64_t
athena_resvg_font_generation () {
#if ATHENA_PLATFORM_IPADOS
  return athena::text::font_database_native_view ()->generation;
#else
  return 1;
#endif
}

void
athena_configure_resvg_fonts (ResvgOptions& options) {
#if ATHENA_PLATFORM_IPADOS
  auto view= athena::text::font_database_native_view ();
  std::unordered_set<std::string> loaded;
  loaded.reserve (view->faces.size ());
  for (const auto& face: view->faces) {
    const std::string& path= face.file.file_utf8;
    if (path.empty () || !loaded.insert (path).second) continue;
    const QString qpath= QString::fromUtf8 (path.data (), (int) path.size ());
    if (QFileInfo::exists (qpath)) options.loadFontFile (qpath);
  }
#else
  options.loadSystemFonts ();
#endif
}

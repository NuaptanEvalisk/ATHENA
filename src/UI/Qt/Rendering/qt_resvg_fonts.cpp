/******************************************************************************
* MODULE     : qt_resvg_fonts.cpp
* DESCRIPTION: Owner-scoped ATHENA font injection for resvg
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "UI/Qt/Rendering/qt_resvg_fonts.hpp"

#include "athena_platform.hpp"
#include "font_database.hpp"
#include "font_domain.hpp"

#include <ResvgQt.h>

#include <QFileInfo>
#include <QXmlStreamReader>

#include <unordered_set>
#include <memory>

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

namespace {
bool svg_needs_fonts (const QString& path) {
  QFile file (path);
  if (!file.open (QIODevice::ReadOnly)) return true;
  QXmlStreamReader xml (&file);
  while (!xml.atEnd ()) {
    xml.readNext ();
    if (!xml.isStartElement ()) continue;
    const auto name= xml.name ();
    // Nested resources may themselves contain text. Keep their normal font
    // configuration; only self-contained non-text drawings can omit it.
    if (name == QLatin1String ("text") || name == QLatin1String ("image") ||
        name == QLatin1String ("use")) return true;
  }
  return xml.hasError ();
}

struct resvg_owner_options_cache {
  std::uint64_t generation= 0;
  std::unique_ptr<ResvgOptions> drawing, text;
};
}

ResvgOptions&
athena_resvg_options_for (const QString& file_path) {
  auto& cache= font_domain_local<resvg_owner_options_cache> ();
  const bool needs_fonts= svg_needs_fonts (file_path);
  auto& options= needs_fonts ? cache.text : cache.drawing;
  const auto generation= athena_resvg_font_generation ();
  if (!options || (needs_fonts && cache.generation != generation)) {
    options= std::make_unique<ResvgOptions> ();
    if (needs_fonts) {
      athena_configure_resvg_fonts (*options);
      cache.generation= generation;
    }
  }
  options->setResourcesDir (QFileInfo (file_path).absolutePath ());
  return *options;
}

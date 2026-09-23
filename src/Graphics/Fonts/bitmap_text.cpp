/******************************************************************************
* MODULE     : bitmap_text.cpp
* DESCRIPTION: FreeType bitmap glyph decoding with explicit layout dimensions
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "bitmap_text.hpp"
#include "Qt/qt_picture.hpp"
#include <QImage>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <tuple>

namespace athena::text {
namespace {
SI coordinate (double value) {
  if (!std::isfinite (value) || value < std::numeric_limits<SI>::min () ||
      value > std::numeric_limits<SI>::max ())
    throw std::overflow_error ("Bitmap font exceeds layout coordinates");
  return static_cast<SI> (std::llround (value));
}
}

bitmap_text_glyph load_bitmap_text_glyph (
    tt_face face, unsigned int glyph, int size, int hdpi, int vdpi) {
  using key_type= std::tuple<std::string, unsigned int, int, int, int>;
  struct cache_state {
    std::map<key_type, bitmap_text_glyph> entries;
    std::size_t bytes= 0;
  };
  auto& state= font_domain_local<cache_state> ();
  auto& cache= state.entries;
  const key_type key {std::string (face->res_name.data (), N(face->res_name)),
                     glyph, size, hdpi, vdpi};
  if (auto found= cache.find (key); found != cache.end ()) return found->second;
  FT_Face ft= face->ft_face;
  if (face->bad_face || size <= 0 || hdpi <= 0 || vdpi <= 0 ||
      ft->num_fixed_sizes <= 0)
    throw std::invalid_argument ("Invalid bitmap font source or size");
  const double target= static_cast<double> (size) * vdpi / 72.0;
  int strike= 0;
  double distance= std::numeric_limits<double>::infinity ();
  for (int i=0; i<ft->num_fixed_sizes; ++i) {
    const auto& candidate= ft->available_sizes[i];
    if (candidate.x_ppem <= 0 || candidate.y_ppem <= 0) continue;
    const double delta= std::abs (target - candidate.y_ppem / 64.0);
    if (delta < distance) { strike= i; distance= delta; }
  }
  if (!std::isfinite (distance) || FT_Select_Size (ft, strike) ||
      FT_Load_Glyph (ft, glyph, FT_LOAD_COLOR) ||
      ft->glyph->format != FT_GLYPH_FORMAT_BITMAP)
    throw std::runtime_error ("Cannot load bitmap font strike");
  const auto slot= ft->glyph;
  const auto& bitmap= slot->bitmap;
  const auto& selected= ft->available_sizes[strike];
  const double sx= static_cast<double> (size) * hdpi * 64 / (72.0 * selected.x_ppem);
  const double sy= static_cast<double> (size) * vdpi * 64 / (72.0 * selected.y_ppem);
  bitmap_text_glyph result;
  result.left= coordinate (slot->bitmap_left * sx * PIXEL);
  result.bottom= coordinate ((static_cast<double> (slot->bitmap_top) - bitmap.rows) * sy * PIXEL);
  result.width= coordinate (bitmap.width * sx * PIXEL);
  result.height= coordinate (bitmap.rows * sy * PIXEL);
  result.intrinsic_color= bitmap.pixel_mode == FT_PIXEL_MODE_BGRA;
  if (bitmap.width && bitmap.rows) {
    if (bitmap.width > 16384 || bitmap.rows > 16384 ||
        static_cast<std::uint64_t> (bitmap.width) * bitmap.rows > 16 * 1024 * 1024)
      throw std::length_error ("Bitmap glyph exceeds pixel budget");
    if (bitmap.pixel_mode != FT_PIXEL_MODE_BGRA &&
        bitmap.pixel_mode != FT_PIXEL_MODE_GRAY && bitmap.pixel_mode != FT_PIXEL_MODE_MONO)
      throw std::runtime_error ("Unsupported FreeType bitmap pixel mode");
    QImage image (bitmap.width, bitmap.rows, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull ()) throw std::bad_alloc ();
    // FreeType BGRA is premultiplied; construct QRgb values explicitly so host
    // byte order never changes the channels. No face-owned memory escapes.
    for (unsigned int y=0; y<bitmap.rows; ++y) {
      const auto row= bitmap.buffer + static_cast<std::ptrdiff_t> (y) * bitmap.pitch;
      auto dest= reinterpret_cast<QRgb*> (image.scanLine (y));
      for (unsigned int x=0; x<bitmap.width; ++x) {
        if (result.intrinsic_color)
          dest[x]= qRgba (row[4*x+2], row[4*x+1], row[4*x], row[4*x+3]);
        else {
          const int alpha= bitmap.pixel_mode == FT_PIXEL_MODE_MONO ?
            ((row[x/8] >> (7-x%8)) & 1) * 255 :
            (bitmap.num_grays > 1 ? row[x] * 255 / (bitmap.num_grays-1) : 0);
          dest[x]= qRgba (0, 0, 0, alpha);
        }
      }
    }
    result.pixels= qt_picture (image.convertToFormat (QImage::Format_ARGB32), 0, 0);
  }
  // Bound the owner-local raster cache; live shaped runs retain their pictures.
  const std::size_t bytes= static_cast<std::size_t> (bitmap.width) * bitmap.rows * 4;
  constexpr std::size_t budget= 32 * 1024 * 1024;
  if (cache.size () >= 1024 || bytes > budget - state.bytes) {
    cache.clear ();
    state.bytes= 0;
  }
  if (bytes <= budget) {
    cache.emplace (key, result);
    state.bytes+= bytes;
  }
  return result;
}

void bitmap_text_glyph::draw (renderer ren, SI x, SI y) const {
  if (is_nil (pixels) || width <= 0 || height <= 0) return;
  int r, g, b, alpha;
  get_rgb_color (ren->get_pencil ()->get_color (), r, g, b, alpha);
  picture image= intrinsic_color ? pixels : recolor (pixels, rgb_color (r, g, b));
  ren->draw_picture_scaled (image, coordinate (static_cast<double> (x) + left),
    coordinate (static_cast<double> (y) + bottom), width, height, alpha);
}
}

/******************************************************************************
* MODULE     : shaped_text.cpp
* DESCRIPTION: HarfBuzz UTF-8 shaping and native positioned-glyph rendering
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "shaped_text.hpp"
#include "unicode_text.hpp"
#include "Freetype/tt_face.hpp"
#include "Freetype/tt_file.hpp"
#include <harfbuzz/hb-ft.h>
#include <harfbuzz/hb-ot.h>
#include <algorithm>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <tuple>

namespace athena::text {
namespace {

// FreeType's existing glyph cache distinguishes Unicode scalars from glyph ids.
constexpr int glyph_index_base= 0x0c000000;

SI checked_si (std::int64_t value) {
  if (value < std::numeric_limits<SI>::min () ||
      value > std::numeric_limits<SI>::max ())
    throw std::overflow_error ("Shaped text exceeds renderer coordinates");
  return static_cast<SI> (value);
}

SI translated (SI origin, SI offset) {
  if ((offset > 0 && origin > std::numeric_limits<SI>::max () - offset) ||
      (offset < 0 && origin < std::numeric_limits<SI>::min () - offset))
    throw std::overflow_error ("Shaped text origin exceeds renderer coordinates");
  return origin + offset;
}

int font_scale (int size, int dpi) {
  if (size <= 0 || dpi <= 0)
    throw std::invalid_argument ("Shaping requires positive font size and dpi");
  if (size > std::numeric_limits<int>::max () / 64)
    throw std::overflow_error ("Shaping font size exceeds FreeType range");
  // Check before multiplication even on platforms with a 64-bit SI.
  const std::int64_t product= static_cast<std::int64_t> (size) * dpi;
  if (product > (static_cast<std::int64_t> (
                  std::numeric_limits<int>::max ()) * 72 - 36) / PIXEL)
    throw std::overflow_error ("Shaping font scale is too large");
  return static_cast<int> ((product * PIXEL + 36) / 72);
}

struct shaping_font final: font_resource {
  std::unique_ptr<hb_face_t, decltype (&hb_face_destroy)> face;
  std::unique_ptr<hb_font_t, decltype (&hb_font_destroy)> font;

  shaping_font (tt_face source, int xscale, int yscale):
    font_resource ("utf8-shaping:" * source->res_name, 0),
    face (hb_ft_face_create_referenced (source->ft_face), hb_face_destroy),
    font (hb_font_create (face.get ()), hb_font_destroy) {
    if (face.get () == hb_face_get_empty () || font.get () == hb_font_get_empty ())
      throw std::bad_alloc ();
    // OpenType funcs use Unicode cmap and design metrics, independent of mutable
    // FreeType charmap/size settings used by neighboring fonts in this domain.
    hb_ot_font_set_funcs (font.get ());
    hb_font_set_scale (font.get (), xscale, yscale);
    hb_font_make_immutable (font.get ());
  }
};

shaping_font& cached_font (string family, int xscale, int yscale) {
  using key_type= std::tuple<std::string, int, int>;
  // Slots only index resources. Phase zero destroys HarfBuzz references before
  // phase three frees the FreeType face and its backing font-file bytes.
  auto& cache= font_domain_local<std::map<key_type, shaping_font*>> ();
  key_type key {std::string (family.data (), N(family)), xscale, yscale};
  auto found= cache.find (key);
  if (found != cache.end ()) return *found->second;
  tt_face source= load_tt_face (family);
  if (source->bad_face)
    throw std::runtime_error ("Cannot shape with an unavailable font");
  shaping_font* fresh= tm_new<shaping_font> (source, xscale, yscale);
  try { cache.emplace (std::move (key), fresh); }
  catch (...) { tm_delete (fresh); throw; }
  return *fresh;
}

} // namespace

shaped_text shape_freetype_utf8 (
  string family, int size, int hdpi, int vdpi, std::string_view text,
  std::size_t begin, std::size_t end, const shaping_options& options) {
  if (text.size () > static_cast<std::size_t> (
                       std::numeric_limits<int>::max ()))
    throw std::length_error ("Text exceeds HarfBuzz input range");
  require_utf8 (text);
  if (begin > end || end > text.size () ||
      !scalar_boundary (text, begin) || !scalar_boundary (text, end))
    throw std::invalid_argument ("Shaping range must end at UTF-8 boundaries");
  const int xscale= font_scale (size, hdpi);
  const int yscale= font_scale (size, vdpi);
  hb_script_t script= HB_SCRIPT_INVALID;
  if (!options.script.empty ()) {
    if (options.script.size () != 4)
      throw std::invalid_argument ("Shaping script must be an ISO 15924 tag");
    script= hb_script_from_string (options.script.data (), 4);
    if (script == HB_SCRIPT_INVALID || script == HB_SCRIPT_UNKNOWN)
      throw std::invalid_argument ("Unknown shaping script");
  }
  if (options.language.size () > 255 ||
      options.language.find ('\0') != std::string::npos)
    throw std::invalid_argument ("Invalid shaping language");

  shaped_text result;
  result.byte_begin= begin;
  result.byte_end= end;
  result.direction= options.direction;
  if (begin == end) return result;
  shaping_font& cached= cached_font (family, xscale, yscale);
  hb_font_t* hbfont= cached.font.get ();
  std::unique_ptr<hb_buffer_t, decltype (&hb_buffer_destroy)> buffer (
    hb_buffer_create (), hb_buffer_destroy);
  if (!hb_buffer_allocation_successful (buffer.get ()))
    throw std::bad_alloc ();
  hb_buffer_set_cluster_level (buffer.get (),
                              HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS);
  hb_buffer_set_direction (buffer.get (),
    options.direction == run_direction::right_to_left ?
      HB_DIRECTION_RTL : HB_DIRECTION_LTR);
  if (script != HB_SCRIPT_INVALID)
    hb_buffer_set_script (buffer.get (), script);
  hb_buffer_set_language (buffer.get (), hb_language_from_string (
    options.language.data (), static_cast<int> (options.language.size ())));
  hb_buffer_flags_t flags= HB_BUFFER_FLAG_DEFAULT;
  if (begin == 0) flags= static_cast<hb_buffer_flags_t> (flags | HB_BUFFER_FLAG_BOT);
  if (end == text.size ())
    flags= static_cast<hb_buffer_flags_t> (flags | HB_BUFFER_FLAG_EOT);
  hb_buffer_set_flags (buffer.get (), flags);
  hb_buffer_add_utf8 (buffer.get (), text.data (), static_cast<int> (text.size ()),
                     static_cast<unsigned int> (begin),
                     static_cast<int> (end - begin));
  hb_buffer_guess_segment_properties (buffer.get ());
  const hb_feature_t features[]= {
    {HB_TAG ('l','i','g','a'), 0, HB_FEATURE_GLOBAL_START, HB_FEATURE_GLOBAL_END},
    {HB_TAG ('c','l','i','g'), 0, HB_FEATURE_GLOBAL_START, HB_FEATURE_GLOBAL_END}};
  if (!hb_shape_full (hbfont, buffer.get (),
                     options.ligatures ? nullptr : features,
                     options.ligatures ? 0 : 2, nullptr))
    throw std::runtime_error ("HarfBuzz could not shape this text run");
  if (!hb_buffer_allocation_successful (buffer.get ()))
    throw std::bad_alloc ();
  unsigned int count= 0;
  const hb_glyph_info_t* info= hb_buffer_get_glyph_infos (buffer.get (), &count);
  const hb_glyph_position_t* positions=
    hb_buffer_get_glyph_positions (buffer.get (), nullptr);
  if (count > options.max_glyphs)
    throw std::length_error ("Shaped glyph budget exceeded");

  result.glyph_source= tt_font_glyphs (family, size, hdpi, vdpi);
  font_metric metrics= tt_font_metric (family, size, hdpi, vdpi);
  if (metrics->bad_font_metric || result.glyph_source->bad_font_glyphs)
    throw std::runtime_error ("Cannot load shaped font metrics or glyphs");
  result.glyphs.reserve (count);
  std::int64_t x= 0, y= 0;
  for (unsigned int i= 0; i < count; ++i) {
    if (info[i].codepoint > static_cast<unsigned int> (
          std::numeric_limits<int>::max () - glyph_index_base) ||
        info[i].cluster < begin || info[i].cluster >= end ||
        !scalar_boundary (text, info[i].cluster))
      throw std::runtime_error ("Invalid glyph or cluster returned by HarfBuzz");
    positioned_glyph glyph {
      info[i].codepoint, info[i].cluster,
      checked_si (x + positions[i].x_offset),
      checked_si (y + positions[i].y_offset),
      positions[i].x_advance, positions[i].y_advance,
      (hb_glyph_info_get_glyph_flags (&info[i]) &
       HB_GLYPH_FLAG_UNSAFE_TO_BREAK) != 0};
    result.glyphs.push_back (glyph);
    result.missing_glyphs= result.missing_glyphs || glyph.index == 0;
    metric& m= metrics->get (glyph_index_base + glyph.index);
    // FreeType may render a zero-outline glyph into a blank 1x1 bitmap.
    // Do not turn that raster allocation into a claim of visible ink.
    hb_glyph_extents_t outline;
    if (hb_font_get_glyph_extents (hbfont, glyph.index, &outline) &&
        outline.width != 0 && outline.height != 0 &&
        m->x3 < m->x4 && m->y3 < m->y4) {
      const SI x1= checked_si (static_cast<std::int64_t> (glyph.x) + m->x3);
      const SI x2= checked_si (static_cast<std::int64_t> (glyph.x) + m->x4);
      const SI y1= checked_si (static_cast<std::int64_t> (glyph.y) + m->y3);
      const SI y2= checked_si (static_cast<std::int64_t> (glyph.y) + m->y4);
      result.ink_x1= result.has_ink ? std::min (result.ink_x1, x1) : x1;
      result.ink_x2= result.has_ink ? std::max (result.ink_x2, x2) : x2;
      result.ink_y1= result.has_ink ? std::min (result.ink_y1, y1) : y1;
      result.ink_y2= result.has_ink ? std::max (result.ink_y2, y2) : y2;
      result.has_ink= true;
    }
    x= checked_si (x + glyph.advance_x);
    y= checked_si (y + glyph.advance_y);
  }
  result.advance_x= checked_si (x);
  result.advance_y= checked_si (y);
  return result;
}

void shaped_text::draw_fixed (renderer ren, SI x, SI y) const {
  if (ren == nullptr) throw std::invalid_argument ("Missing text renderer");
  // Preflight coordinates so a failure cannot leave a partially drawn run.
  for (const auto& glyph: glyphs) {
    translated (x, glyph.x);
    translated (y, glyph.y);
  }
  for (const auto& glyph: glyphs)
    ren->draw (glyph_index_base + glyph.index, glyph_source,
      translated (x, glyph.x), translated (y, glyph.y));
}

} // namespace athena::text

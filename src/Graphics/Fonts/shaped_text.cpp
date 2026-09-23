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
#include "shaped_line.hpp"
#include "bitmap_text.hpp"
#include "unicode_text.hpp"
#include "Freetype/tt_face.hpp"
#include "Freetype/tt_file.hpp"
#include <harfbuzz/hb-ft.h>
#include <harfbuzz/hb-ot.h>
#include <algorithm>
#include <cstdlib>
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
    const auto instance= (source->ft_face->face_index >> 16) & 0x7fff;
    if (instance) hb_font_set_var_named_instance (font.get (), instance - 1);
    if (!source->source.design_coords.empty ()) {
      std::vector<float> coords;
      for (auto coordinate: source->source.design_coords)
        coords.push_back (static_cast<float> (coordinate) / 65536.0f);
      hb_font_set_var_coords_design (font.get (), coords.data (), coords.size ());
    }
    hb_font_set_scale (font.get (), xscale, yscale);
    hb_font_make_immutable (font.get ());
  }
};

shaping_font& cached_font (tt_face source, int xscale, int yscale) {
  using key_type= std::tuple<std::string, int, int>;
  // Slots only index resources. Phase zero destroys HarfBuzz references before
  // phase three frees the FreeType face and its backing font-file bytes.
  auto& cache= font_domain_local<std::map<key_type, shaping_font*>> ();
  key_type key {std::string (source->res_name.data (), N(source->res_name)), xscale, yscale};
  auto found= cache.find (key);
  if (found != cache.end ()) return *found->second;
  if (source->bad_face)
    throw std::runtime_error ("Cannot shape with an unavailable font");
  shaping_font* fresh= tm_new<shaping_font> (source, xscale, yscale);
  try { cache.emplace (std::move (key), fresh); }
  catch (...) { tm_delete (fresh); throw; }
  return *fresh;
}

void build_carets (shaped_text& run, hb_font_t* font, std::string_view text,
                   std::size_t budget, bool fragments) {
  grapheme_cursor boundaries (text);
  const bool start_boundary= boundaries.boundary (run.byte_begin);
  const bool end_boundary= boundaries.boundary (run.byte_end);
  if (!fragments && (!start_boundary || !end_boundary))
    throw std::invalid_argument ("Editable run splits a grapheme cluster");
  for (std::size_t byte= run.byte_begin;;
       byte= std::min (run.byte_end, boundaries.next (byte))) {
    if (run.carets.size () == budget)
      throw std::length_error ("Shaped caret budget exceeded");
    run.carets.push_back ({byte, 0});
    if (byte == run.byte_end) break;
  }
  auto discard_fragment_edges= [&] {
    if (!end_boundary) run.carets.pop_back ();
    if (!start_boundary && !run.carets.empty ())
      run.carets.erase (run.carets.begin ());
  };
  if (run.glyphs.empty ()) { discard_fragment_edges (); return; }

  struct cluster {
    std::size_t byte, first, last;
    SI left, right;
  };
  std::vector<cluster> clusters;
  SI pen= 0;
  for (std::size_t first= 0; first < run.glyphs.size ();) {
    const std::size_t byte= run.glyphs[first].byte;
    const SI left= pen;
    std::size_t last= first;
    do {
      pen= translated (pen, run.glyphs[last].advance_x);
      ++last;
    } while (last < run.glyphs.size () && run.glyphs[last].byte == byte);
    clusters.push_back ({byte, first, last, left, pen});
    first= last;
  }
  const bool rtl= run.direction == run_direction::right_to_left;
  if (rtl) std::reverse (clusters.begin (), clusters.end ());
  // HarfBuzz may keep a mark in its own cluster. ICU, not that shaping
  // distinction, decides whether the cluster introduces an editing stop.
  std::size_t merged= 0;
  for (const auto c: clusters) {
    if (merged > 0 && !boundaries.boundary (c.byte)) {
      auto& previous= clusters[merged - 1];
      previous.first= std::min (previous.first, c.first);
      previous.last= std::max (previous.last, c.last);
      if (rtl) previous.left= c.left;
      else previous.right= c.right;
    }
    else clusters[merged++]= c;
  }
  clusters.resize (merged);
  const auto by_byte= [] (const text_caret& caret, std::size_t byte) {
    return caret.byte < byte;
  };
  for (std::size_t i= 0; i < clusters.size (); ++i) {
    const auto& c= clusters[i];
    const auto end= i + 1 < clusters.size () ? clusters[i + 1].byte : run.byte_end;
    const auto first= std::lower_bound (run.carets.begin (), run.carets.end (),
                                       c.byte, by_byte);
    const auto last= std::lower_bound (first, run.carets.end (), end, by_byte);
    const std::size_t count= last - first;
    if (count == 0) continue; // A mark-only cluster has no editing stop.
    const SI start_x= rtl ? c.right : c.left;
    const SI end_x= rtl ? c.left : c.right;
    std::vector<SI> ligature;
    if (count > 1) {
      // GDEF carets describe one spacing glyph, possibly accompanied by marks.
      std::size_t base= c.last;
      for (std::size_t g= c.first; g < c.last; ++g) {
        if (hb_ot_layout_get_glyph_class (hb_font_get_face (font),
              run.glyphs[g].index) == HB_OT_LAYOUT_GLYPH_CLASS_MARK) continue;
        if (base != c.last) { base= c.last; break; }
        base= g;
      }
      if (base != c.last) {
        const auto direction= rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR;
        const auto total= hb_ot_layout_get_ligature_carets (font, direction,
          run.glyphs[base].index, 0, nullptr, nullptr);
        if (total == count - 1) {
          std::vector<hb_position_t> positions (total);
          unsigned int received= total;
          hb_ot_layout_get_ligature_carets (font, direction,
            run.glyphs[base].index, 0, &received, positions.data ());
          if (received == total) {
            for (auto position: positions) {
              const auto x= static_cast<std::int64_t> (run.glyphs[base].x) + position;
              if (x < std::min (c.left, c.right) || x > std::max (c.left, c.right)) {
                ligature.clear ();
                break;
              }
              ligature.push_back (checked_si (x));
            }
            std::sort (ligature.begin (), ligature.end ());
            if (rtl) std::reverse (ligature.begin (), ligature.end ());
          }
        }
      }
    }
    for (std::size_t j= 0; j < count; ++j) {
      // The established fallback divides a cluster across its graphemes,
      // never across UTF-8 bytes or individual combining codepoints.
      const auto x= static_cast<std::int64_t> (start_x) +
        (static_cast<std::int64_t> (end_x) - start_x) *
          static_cast<std::int64_t> (j) / static_cast<std::int64_t> (count);
      first[j].x= j > 0 && ligature.size () == count - 1 ?
        ligature[j - 1] : checked_si (x);
    }
  }
  run.carets.front ().x= rtl ? run.advance_x : 0;
  run.carets.back ().x= rtl ? 0 : run.advance_x;
  discard_fragment_edges ();
}

} // namespace

static shaped_text shape_freetype_run (
  string family, const font_file_source* file, int size, int hdpi, int vdpi, std::string_view text,
  std::size_t begin, std::size_t end, const shaping_options& options) {
  if (text.size () > static_cast<std::size_t> (
                       std::numeric_limits<int>::max ()))
    throw std::length_error ("Text exceeds HarfBuzz input range");
  require_utf8 (text);
  if (begin > end || end > text.size () ||
      !scalar_boundary (text, begin) || !scalar_boundary (text, end))
    throw std::invalid_argument ("Shaping range must end at UTF-8 boundaries");
  const auto context_begin= options.context_begin;
  const auto context_end= options.context_end == std::string_view::npos ?
    text.size () : options.context_end;
  if (context_begin > begin || context_end < end || context_end > text.size () ||
      !scalar_boundary (text, context_begin) || !scalar_boundary (text, context_end))
    throw std::invalid_argument ("Shaping context does not contain its UTF-8 item");
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
  if (begin == end) {
    if (options.editing_carets)
      build_carets (result, nullptr, text, options.max_carets,
                    options.grapheme_fragments);
    return result;
  }
  const tt_face face= file ? load_tt_face (*file) : load_tt_face (family);
  shaping_font& cached= cached_font (face, xscale, yscale);
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
  if (begin == context_begin) flags= static_cast<hb_buffer_flags_t> (flags | HB_BUFFER_FLAG_BOT);
  if (end == context_end)
    flags= static_cast<hb_buffer_flags_t> (flags | HB_BUFFER_FLAG_EOT);
  hb_buffer_set_flags (buffer.get (), flags);
  hb_buffer_add_utf8 (buffer.get (), text.data () + context_begin,
                     static_cast<int> (context_end - context_begin),
                     static_cast<unsigned int> (begin - context_begin),
                     static_cast<int> (end - begin));
  hb_buffer_guess_segment_properties (buffer.get ());
  if (options.math_variant != math_alphabet::normal) {
    unsigned int length= 0;
    auto* characters= hb_buffer_get_glyph_infos (buffer.get (), &length);
    for (unsigned int i=0; i<length; ++i)
      characters[i].codepoint= math_variant_character (characters[i].codepoint, options.math_variant);
  }
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

  const bool bitmap= !FT_IS_SCALABLE (face->ft_face) && FT_HAS_FIXED_SIZES (face->ft_face);
  font_metric metrics;
  if (bitmap) result.bitmaps.reserve (count);
  else {
    result.glyph_source= tt_font_glyphs (face, size, hdpi, vdpi);
    metrics= tt_font_metric (face, size, hdpi, vdpi);
    if (metrics->bad_font_metric || result.glyph_source->bad_font_glyphs)
      throw std::runtime_error ("Cannot load shaped font metrics or glyphs");
  }
  result.glyphs.reserve (count);
  std::int64_t x= 0, y= 0;
  for (unsigned int i= 0; i < count; ++i) {
    const std::size_t cluster= context_begin + info[i].cluster;
    if (info[i].codepoint > static_cast<unsigned int> (
          std::numeric_limits<int>::max () - glyph_index_base) ||
        cluster < begin || cluster >= end || !scalar_boundary (text, cluster))
      throw std::runtime_error ("Invalid glyph or cluster returned by HarfBuzz");
    positioned_glyph glyph {
      info[i].codepoint, cluster,
      checked_si (x + positions[i].x_offset),
      checked_si (y + positions[i].y_offset),
      positions[i].x_advance, positions[i].y_advance,
      (hb_glyph_info_get_glyph_flags (&info[i]) &
       HB_GLYPH_FLAG_UNSAFE_TO_BREAK) != 0};
    result.glyphs.push_back (glyph);
    result.missing_glyphs= result.missing_glyphs || glyph.index == 0;
    metric bitmap_metric;
    if (bitmap) {
      result.bitmaps.push_back (load_bitmap_text_glyph (face, glyph.index, size, hdpi, vdpi));
      const auto& raster= result.bitmaps.back ();
      bitmap_metric->x3= raster.left;
      bitmap_metric->x4= checked_si (static_cast<std::int64_t> (raster.left) + raster.width);
      bitmap_metric->y3= raster.bottom;
      bitmap_metric->y4= checked_si (static_cast<std::int64_t> (raster.bottom) + raster.height);
    }
    metric& m= bitmap ? bitmap_metric : metrics->get (glyph_index_base + glyph.index);
    // FreeType may render a zero-outline glyph into a blank 1x1 bitmap.
    // Do not turn that raster allocation into a claim of visible ink.
    hb_glyph_extents_t outline;
    if ((bitmap || (hb_font_get_glyph_extents (hbfont, glyph.index, &outline) &&
        outline.width != 0 && outline.height != 0)) &&
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
  if (count == 1 && !result.missing_glyphs && hb_ot_math_has_data (cached.face.get ())) {
    const auto& glyph= result.glyphs.front ();
    result.math= shaped_math_metrics {
      hb_ot_math_get_glyph_italics_correction (hbfont, glyph.index),
      translated (glyph.x, hb_ot_math_get_glyph_top_accent_attachment (hbfont, glyph.index))};
  }
  if (options.editing_carets)
    build_carets (result, hbfont, text, options.max_carets,
                  options.grapheme_fragments);
  return result;
}

shaped_text shape_freetype_utf8 (string family, int size, int hdpi, int vdpi,
  std::string_view text, std::size_t begin, std::size_t end, const shaping_options& options) {
  return shape_freetype_run (family, nullptr, size, hdpi, vdpi, text, begin, end, options);
}

shaped_text shape_freetype_utf8 (const font_file_source& file, int size, int hdpi, int vdpi,
  std::string_view text, std::size_t begin, std::size_t end, const shaping_options& options) {
  return shape_freetype_run ("", &file, size, hdpi, vdpi, text, begin, end, options);
}

SI shaped_text::caret_x (std::size_t byte) const {
  const auto at= std::lower_bound (carets.begin (), carets.end (), byte,
    [] (const text_caret& caret, std::size_t b) { return caret.byte < b; });
  if (at == carets.end () || at->byte != byte)
    throw std::invalid_argument ("Position is not a shaped grapheme boundary");
  return at->x;
}

std::size_t shaped_text::hit_test (SI x, bool prefer_right) const {
  if (carets.empty ()) throw std::logic_error ("Run has no editing carets");
  const text_caret* best= &carets.front ();
  auto distance= [x] (SI at) {
    return std::abs (static_cast<std::int64_t> (at) - x);
  };
  for (const auto& caret: carets) {
    const auto d= distance (caret.x), old= distance (best->x);
    const bool later= caret.x == best->x ?
      (direction == run_direction::left_to_right ? caret.byte > best->byte :
                                                  caret.byte < best->byte) :
      caret.x > best->x;
    if (d < old || (d == old && later == prefer_right)) best= &caret;
  }
  return best->byte;
}

void shaped_text::draw_fixed (renderer ren, std::string_view source,
                              SI x, SI y) const {
  if (ren == nullptr) throw std::invalid_argument ("Missing text renderer");
  if (byte_begin > byte_end || byte_end > source.size ())
    throw std::invalid_argument ("Shaped text source range is invalid");
  const auto text= source.substr (byte_begin, byte_end - byte_begin);
  require_utf8 (text);
  if (!bitmaps.empty () && bitmaps.size () != glyphs.size ())
    throw std::invalid_argument ("Bitmap glyph count does not match shaped run");
  if (!glyphs.empty () && bitmaps.empty () && is_nil (glyph_source))
    throw std::invalid_argument ("Missing shaped glyph source");
  // Preflight coordinates so a failure cannot leave a partially drawn run.
  for (const auto& glyph: glyphs) {
    if (glyph.index > static_cast<unsigned int> (
          std::numeric_limits<int>::max () - glyph_index_base) ||
        glyph.byte < byte_begin || glyph.byte >= byte_end ||
        !scalar_boundary (text, glyph.byte - byte_begin))
      throw std::invalid_argument ("Invalid shaped glyph or UTF-8 cluster");
    translated (x, glyph.x);
    translated (y, glyph.y);
  }
  ren->draw_utf8 (*this, text, x, y);
}

shaped_line shape_line (unicode_paragraph& paragraph,
  std::size_t begin, std::size_t end, const item_shaper& shape,
  const shaping_options& options, const item_splitter& split) {
  if (!shape) throw std::invalid_argument ("Missing line item shaper");
  auto items= paragraph.items (begin, end);
  if (split) {
    std::vector<shaping_item> divided;
    for (const auto& item: items) {
      const auto cuts= split (item);
      auto start= item.run.begin;
      const auto first= divided.size ();
      for (auto cut: cuts) {
        if (cut <= start || cut >= item.run.end ||
            !scalar_boundary (paragraph.source (), cut))
          throw std::invalid_argument ("Invalid font item boundary");
        auto part= item;
        part.run.begin= start;
        part.run.end= cut;
        divided.push_back (part);
        start= cut;
      }
      auto part= item;
      part.run.begin= start;
      divided.push_back (part);
      if (item.run.right_to_left ())
        std::reverse (divided.begin () + first, divided.end ());
    }
    items= std::move (divided);
  }
  shaped_line result;
  result.byte_begin= begin;
  result.byte_end= end;
  if (begin == end) {
    if (options.max_carets == 0)
      throw std::length_error ("Line caret budget exceeded");
    result.carets.push_back ({begin, 0, caret_affinity::both});
    return result;
  }
  const auto source= paragraph.source ();
  grapheme_cursor boundaries (source);
  std::size_t glyph_count= 0;
  for (const auto& item: items) {
    auto selected= options;
    selected.script= item.script;
    selected.direction= item.run.right_to_left () ?
      run_direction::right_to_left : run_direction::left_to_right;
    selected.context_begin= begin;
    selected.context_end= end;
    selected.editing_carets= true;
    selected.grapheme_fragments= true;
    selected.max_glyphs= options.max_glyphs - glyph_count;
    selected.max_carets= options.max_carets - result.carets.size ();
    // Two temporary fragment edges may be needed for shaping interpolation;
    // they are discarded before publication and never become editing stops.
    if (selected.max_carets <= std::numeric_limits<std::size_t>::max () - 2)
      selected.max_carets+= 2;
    auto run= shape (source, item, selected);
    if (run.byte_begin != item.run.begin || run.byte_end != item.run.end ||
        run.direction != selected.direction || run.advance_y != 0)
      throw std::invalid_argument ("Item shaper changed the line contract");
    if (run.glyphs.size () > options.max_glyphs - glyph_count ||
        run.carets.size () > options.max_carets - result.carets.size ())
      throw std::length_error ("Shaped line budget exceeded");
    glyph_count+= run.glyphs.size ();
    for (const auto& caret: run.carets) {
      if (caret.byte < run.byte_begin || caret.byte > run.byte_end ||
          !boundaries.boundary (caret.byte))
        throw std::invalid_argument ("Item shaper exposed an invalid caret");
      const auto affinity= caret.byte == run.byte_begin ? caret_affinity::downstream :
        caret.byte == run.byte_end ? caret_affinity::upstream : caret_affinity::both;
      result.carets.push_back ({caret.byte, translated (result.advance, caret.x), affinity});
    }
    const SI next= translated (result.advance, run.advance_x);
    result.missing_glyphs= result.missing_glyphs || run.missing_glyphs;
    result.runs.push_back ({std::move (run), result.advance});
    result.advance= next;
  }
  std::sort (result.carets.begin (), result.carets.end (),
    [] (const line_caret& a, const line_caret& b) {
      return std::tie (a.byte, a.x, a.affinity) < std::tie (b.byte, b.x, b.affinity);
    });
  std::size_t count= 0;
  for (const auto caret: result.carets) {
    if (count && result.carets[count - 1].byte == caret.byte &&
        result.carets[count - 1].x == caret.x) {
      if (result.carets[count - 1].affinity != caret.affinity)
        result.carets[count - 1].affinity= caret_affinity::both;
    }
    else result.carets[count++]= caret;
  }
  result.carets.resize (count);
  auto at= result.carets.begin ();
  for (std::size_t byte= begin;; byte= boundaries.next (byte)) {
    if (at == result.carets.end () || at->byte != byte)
      throw std::invalid_argument ("Item shaper omitted a line grapheme caret");
    while (at != result.carets.end () && at->byte == byte) ++at;
    if (byte == end) break;
  }
  return result;
}

SI shaped_line::caret_x (std::size_t byte, caret_affinity affinity) const {
  const auto at= std::lower_bound (carets.begin (), carets.end (), byte,
    [] (const line_caret& c, std::size_t b) { return c.byte < b; });
  if (at == carets.end () || at->byte != byte)
    throw std::invalid_argument ("Position is not a line grapheme boundary");
  for (auto c= at; c != carets.end () && c->byte == byte; ++c)
    if (c->affinity == affinity || c->affinity == caret_affinity::both) return c->x;
  // At a line endpoint the other affinity belongs to the neighboring line.
  if (byte == byte_begin || byte == byte_end) return at->x;
  throw std::invalid_argument ("Caret affinity is unavailable in this line");
}

line_caret shaped_line::hit_test (SI x, bool prefer_right) const {
  if (carets.empty ()) throw std::logic_error ("Line has no editing carets");
  const auto distance= [x] (SI position) {
    return std::llabs (static_cast<long long> (position) - x);
  };
  const line_caret* best= &carets.front ();
  for (const auto& caret: carets) {
    const auto d= distance (caret.x), old= distance (best->x);
    if (d < old || (d == old &&
        (prefer_right ? caret.x > best->x : caret.x < best->x))) best= &caret;
  }
  return *best;
}

std::vector<line_selection_span> shaped_line::selection_spans (
    std::size_t begin, std::size_t end) const {
  (void) caret_x (begin, caret_affinity::downstream);
  (void) caret_x (end, caret_affinity::upstream);
  if (begin > end) std::swap (begin, end);
  std::vector<line_selection_span> spans;
  for (const auto& placed: runs) {
    const auto& run= placed.text;
    const auto first= std::max (begin, run.byte_begin);
    const auto last= std::min (end, run.byte_end);
    if (first >= last) continue;
    // Font/script item edges can lie inside a grapheme. The selected global
    // grapheme includes that whole fragment, even though it has no edge caret.
    const bool rtl= run.direction == run_direction::right_to_left;
    const SI a= first == run.byte_begin ? (rtl ? run.advance_x : 0) : run.caret_x (first);
    const SI b= last == run.byte_end ? (rtl ? 0 : run.advance_x) : run.caret_x (last);
    if (a != b) spans.push_back ({translated (placed.x, std::min (a, b)),
                                  translated (placed.x, std::max (a, b))});
  }
  std::sort (spans.begin (), spans.end (),
    [] (const auto& a, const auto& b) { return a.left < b.left; });
  std::size_t count= 0;
  for (auto span: spans) {
    if (count && spans[count - 1].right >= span.left)
      spans[count - 1].right= std::max (spans[count - 1].right, span.right);
    else spans[count++]= span;
  }
  spans.resize (count);
  return spans;
}

void shaped_line::draw_fixed (renderer ren, std::string_view source, SI x, SI y) const {
  if (ren == nullptr || byte_begin > byte_end || byte_end > source.size ())
    throw std::invalid_argument ("Invalid shaped line drawing request");
  for (const auto& run: runs) translated (x, run.x);
  for (const auto& run: runs) run.text.draw_fixed (ren, source, translated (x, run.x), y);
}

void shaped_line::set_space_widths (std::string_view source,
                                    const std::vector<line_space_width>& spaces) {
  struct adjustment { SI left, right, width; std::int64_t before; };
  std::vector<adjustment> changes;
  std::size_t previous= byte_begin;
  if (byte_end > source.size () || spaces.size () > carets.size ())
    throw std::invalid_argument ("Invalid Unicode line spacing request");
  for (const auto& s: spaces) {
    if (s.begin < previous || s.begin >= s.end || s.end > byte_end || s.width < 0 ||
        source.substr (s.begin, s.end-s.begin).find_first_not_of (' ') != std::string_view::npos)
      throw std::invalid_argument ("Glue must address disjoint ASCII-space ranges");
    const auto spans= selection_spans (s.begin, s.end);
    if (spans.size () != 1 || spans[0].left >= spans[0].right)
      throw std::invalid_argument ("Glue has no contiguous visual interval");
    changes.push_back ({spans[0].left, spans[0].right, s.width, 0});
    previous= s.end;
  }
  if (changes.empty ()) return;
  std::sort (changes.begin (), changes.end (),
    [] (const auto& a, const auto& b) { return a.left < b.left; });
  std::int64_t delta= 0;
  for (std::size_t i=0; i<changes.size (); ++i) {
    auto& s= changes[i];
    if (i && changes[i-1].right > s.left)
      throw std::invalid_argument ("Overlapping visual glue intervals");
    s.before= delta;
    delta += static_cast<std::int64_t> (s.width) - (static_cast<std::int64_t> (s.right) - s.left);
    checked_si (static_cast<std::int64_t> (s.right) + delta);
  }
  const auto move= [&] (SI x) {
    const auto after= std::upper_bound (changes.begin (), changes.end (), x,
      [] (SI at, const adjustment& s) { return at < s.left; });
    if (after == changes.begin ()) return x;
    const auto& s= *(after-1);
    const std::int64_t width= static_cast<std::int64_t> (s.right) - s.left;
    if (x >= s.right)
      return checked_si (static_cast<std::int64_t> (x) + s.before + s.width - width);
    const auto inside= (static_cast<std::int64_t> (x) - s.left) * s.width / width;
    return checked_si (s.left + s.before + inside);
  };
  const SI next_advance= move (advance);
  for (auto& placed: runs) {
    auto& run= placed.text;
    const SI origin= placed.x, next_origin= move (origin);
    SI pen= origin;
    SI min_shift= 0, max_shift= 0;
    for (auto& glyph: run.glyphs) {
      const SI next_pen= translated (pen, glyph.advance_x);
      const SI shift= checked_si (static_cast<std::int64_t> (move (pen)) - pen - next_origin + origin);
      glyph.x= translated (glyph.x, shift);
      if (run.math)
        run.math->top_accent_attachment= translated (run.math->top_accent_attachment, shift);
      glyph.advance_x= checked_si (static_cast<std::int64_t> (move (next_pen)) - move (pen));
      min_shift= std::min (min_shift, shift);
      max_shift= std::max (max_shift, shift);
      pen= next_pen;
    }
    for (auto& caret: run.carets)
      caret.x= checked_si (static_cast<std::int64_t> (move (translated (origin, caret.x))) - next_origin);
    run.advance_x= checked_si (static_cast<std::int64_t> (move (translated (origin, run.advance_x))) - next_origin);
    // Conservative ink bounds cover all translated outlines without stretching
    // their shapes or treating a font's whitespace glyph as visible text.
    if (run.has_ink) {
      run.ink_x1= translated (run.ink_x1, min_shift);
      run.ink_x2= translated (run.ink_x2, max_shift);
    }
    placed.x= next_origin;
  }
  for (auto& caret: carets) caret.x= move (caret.x);
  advance= next_advance;
}

} // namespace athena::text

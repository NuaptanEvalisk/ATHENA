/******************************************************************************
* MODULE     : font_selection.cpp
* DESCRIPTION: Pango font itemization feeding native UTF-8 shaping and export
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "font_selection.hpp"
#include "Freetype/tt_file.hpp"
#include "Freetype/tt_face.hpp"
#include <fontconfig/fontconfig.h>
#include <fontconfig/fcfreetype.h>
#include "file.hpp"
#include <pango/pangoft2.h>
#include <pango/pangofc-font.h>
#include <pango/pangofc-fontmap.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace athena::text {
namespace {
template<class T> using object_ptr= std::unique_ptr<T, decltype (&g_object_unref)>;

void require_c_string (std::string_view s) {
  require_utf8 (s);
  if (s.find ('\0') != std::string_view::npos)
    throw std::invalid_argument ("NUL in font configuration");
}

void validate_request (const font_request& request) {
  require_c_string (request.description_utf8);
  require_c_string (request.language);
  if (request.description_utf8.empty () || request.point_size <= 0 ||
      request.point_size > G_MAXINT / PANGO_SCALE ||
      request.horizontal_dpi <= 0 || request.vertical_dpi <= 0)
    throw std::invalid_argument ("Invalid font selection request");
}

using description_ptr= std::unique_ptr<PangoFontDescription, decltype (&pango_font_description_free)>;

description_ptr describe (const font_request& request) {
  description_ptr desc (pango_font_description_from_string (request.description_utf8.c_str ()),
                         pango_font_description_free);
  if (!desc) throw std::bad_alloc ();
  pango_font_description_set_size (desc.get (), request.point_size * PANGO_SCALE);
  return desc;
}

font_file_source physical_font (PangoFont* font) {
  if (!font || !PANGO_IS_FC_FONT (font))
    throw std::runtime_error ("Pango did not select a physical font");
  const auto pattern= pango_fc_font_get_pattern (PANGO_FC_FONT (font));
  FcChar8* file= nullptr;
  int index= 0;
  if (FcPatternGetString (pattern, FC_FILE, 0, &file) != FcResultMatch ||
      FcPatternGetInteger (pattern, FC_INDEX, 0, &index) != FcResultMatch)
    throw std::runtime_error ("Selected font has no file/face identity");
  FcBool embolden= FcFalse;
  FcMatrix* matrix= nullptr;
  FcPatternGetBool (pattern, FC_EMBOLDEN, 0, &embolden);
  FcPatternGetMatrix (pattern, FC_MATRIX, 0, &matrix);
  if (embolden || (matrix && (matrix->xx != 1 || matrix->yy != 1 ||
                             matrix->xy != 0 || matrix->yx != 0)))
    throw std::runtime_error ("Synthetic font transforms require raster integration");
  font_file_source result {reinterpret_cast<const char*> (file), index};
  unsigned int count= 0;
  const auto hb= pango_font_get_hb_font (font);
  if (!hb) throw std::runtime_error ("Selected font has no shaping face");
  const auto coords= hb_font_get_var_coords_design (hb, &count);
  if (count > 0xffff) throw std::length_error ("Too many font variation axes");
  for (unsigned int i= 0; i < count; ++i) {
    const double fixed= std::round (static_cast<double> (coords[i]) * 65536.0);
    if (!std::isfinite (fixed) || fixed < std::numeric_limits<std::int32_t>::min () ||
        fixed > std::numeric_limits<std::int32_t>::max ())
      throw std::runtime_error ("Invalid font variation coordinate");
    result.design_coords.push_back (static_cast<std::int32_t> (fixed));
  }
  return result;
}

void append_directory (url u, std::vector<std::string>& dirs) {
  if (is_none (u)) return;
  if (is_or (u)) {
    append_directory (u[1], dirs);
    append_directory (u[2], dirs);
  }
  else if (is_directory (u)) {
    const auto path= concretize (u);
    dirs.emplace_back (path.data (), N(path));
  }
}
}

struct font_catalog::impl {
  font_domain& owner= current_font_domain ();
  std::unique_ptr<FcConfig, decltype (&FcConfigDestroy)> config;
  object_ptr<PangoFontMap> map;

  impl (bool system, const std::vector<std::string>& files,
        const std::vector<std::string>& directories):
    config (system ? FcInitLoadConfigAndFonts () : FcConfigCreate (), FcConfigDestroy),
    map (pango_ft2_font_map_new (), g_object_unref) {
    if (!config || !map) throw std::bad_alloc ();
    for (const auto& file: files) {
      require_c_string (file);
      if (!FcConfigAppFontAddFile (config.get (),
            reinterpret_cast<const FcChar8*> (file.c_str ())))
        throw std::runtime_error ("Cannot add application font: " + file);
    }
    for (const auto& dir: directories) {
      require_c_string (dir);
      if (!FcConfigAppFontAddDir (config.get (),
            reinterpret_cast<const FcChar8*> (dir.c_str ())))
        throw std::runtime_error ("Cannot add application font directory: " + dir);
    }
    pango_fc_font_map_set_config (PANGO_FC_FONT_MAP (map.get ()), config.get ());
  }
  void check () const {
    owner.check_owner ();
    if (&current_font_domain () != &owner)
      throw std::logic_error ("Font catalog belongs to another font domain");
  }
};

font_catalog::font_catalog (bool system, const std::vector<std::string>& files,
                            const std::vector<std::string>& dirs):
  state_ (std::make_unique<impl> (system, files, dirs)) {}
font_catalog::~font_catalog () { state_->owner.check_owner (); }

std::vector<selected_font_run> font_catalog::select (
    const std::string& source, const font_request& request, std::uint8_t base_level,
    const std::vector<font_style_span>& styles) {
  state_->check ();
  require_utf8 (source);
  validate_request (request);
  if (source.size () > 16 * 1024 * 1024 || base_level > 1)
    throw std::invalid_argument ("Invalid font selection request");
  if (styles.size () > 100000) throw std::length_error ("Too many font style spans");
  std::size_t previous= 0;
  for (const auto& span: styles) {
    validate_request (span.request);
    if (span.begin < previous || span.begin >= span.end || span.end > source.size () ||
        !scalar_boundary (source, span.begin) || !scalar_boundary (source, span.end) ||
        span.request.horizontal_dpi != request.horizontal_dpi ||
        span.request.vertical_dpi != request.vertical_dpi || span.request.direction != request.direction)
      throw std::invalid_argument ("Invalid paragraph font style range");
    previous= span.end;
  }
  std::vector<selected_font_run> result;
  if (source.empty ()) return result;
  std::vector<description_ptr> descriptions;
  descriptions.push_back (describe (request));
  pango_ft2_font_map_set_resolution (PANGO_FT2_FONT_MAP (state_->map.get ()),
                                    request.horizontal_dpi, request.vertical_dpi);
  object_ptr<PangoContext> context (
    pango_font_map_create_context (state_->map.get ()), g_object_unref);
  if (!context) throw std::bad_alloc ();
  pango_context_set_font_description (context.get (), descriptions[0].get ());
  pango_context_set_language (context.get (), pango_language_from_string (request.language.c_str ()));
  std::unique_ptr<PangoAttrList, decltype (&pango_attr_list_unref)> attrs (
    pango_attr_list_new (), pango_attr_list_unref);
  for (const auto& span: styles) {
    descriptions.push_back (describe (span.request));
    for (auto attr: {pango_attr_font_desc_new (descriptions.back ().get ()),
                     pango_attr_language_new (pango_language_from_string (span.request.language.c_str ()))}) {
      if (!attr) throw std::bad_alloc ();
      attr->start_index= span.begin;
      attr->end_index= span.end;
      pango_attr_list_insert (attrs.get (), attr);
    }
  }
  const auto locate_style= [&] (std::size_t byte) {
    return std::lower_bound (styles.begin (), styles.end (), byte,
      [] (const font_style_span& span, std::size_t at) { return span.end <= at; });
  };
  const auto append= [&] (std::size_t begin, std::size_t end, const font_file_source& font) {
    while (begin < end) {
      const auto span= locate_style (begin);
      const bool inside= span != styles.end () && span->begin <= begin;
      const auto& active= inside ? span->request : request;
      const auto next= span == styles.end () ? end : std::min (end, inside ? span->end : span->begin);
      // Identical adjacent declarations must not break joining or ligatures.
      // Keep explicit NUL control runs separate from neighboring text.
      if (!result.empty () && begin > 0 && source[begin - 1] != '\0' && source[begin] != '\0' &&
          result.back ().end == begin && result.back ().font.file_utf8 == font.file_utf8 &&
          result.back ().font.face_index == font.face_index &&
          result.back ().font.design_coords == font.design_coords &&
          result.back ().point_size == active.point_size && result.back ().language == active.language)
        result.back ().end= next;
      else result.push_back ({begin, next, font, active.point_size, active.language});
      begin= next;
    }
  };
  const auto free_items= [] (GList* list) {
    g_list_free_full (list, [] (gpointer item) { pango_item_free (static_cast<PangoItem*> (item)); });
  };
  // Pango's C string interface stops at NUL. Keep those source bytes as their
  // own control runs; never truncate the rest of the paragraph or rewrite it.
  for (std::size_t start= 0; start < source.size ();) {
    if (source[start] == '\0') {
      const auto span= locate_style (start);
      const auto index= span != styles.end () && span->begin <= start ?
        static_cast<std::size_t> (span - styles.begin ()) + 1 : 0;
      object_ptr<PangoFont> font (pango_context_load_font (context.get (), descriptions[index].get ()),
                                 g_object_unref);
      append (start, start + 1, physical_font (font.get ()));
      ++start;
      continue;
    }
    auto end= source.find ('\0', start);
    if (end == std::string::npos) end= source.size ();
    std::unique_ptr<GList, decltype (free_items)> items (
      pango_itemize_with_base_dir (context.get (), base_level ? PANGO_DIRECTION_RTL : PANGO_DIRECTION_LTR,
        source.c_str (), start, end - start, attrs.get (), nullptr), free_items);
    auto covered= start;
    for (auto link= items.get (); link; link= link->next) {
      const auto item= static_cast<PangoItem*> (link->data);
      if (item->offset < 0 || static_cast<std::size_t> (item->offset) != covered ||
          item->length <= 0 || static_cast<std::size_t> (item->length) > end - covered)
        throw std::runtime_error ("Invalid Pango font item coverage");
      const auto next= covered + item->length;
      if (!scalar_boundary (source, covered) || !scalar_boundary (source, next))
        throw std::runtime_error ("Font item splits a UTF-8 scalar");
      append (covered, next, physical_font (item->analysis.font));
      covered= next;
    }
    if (covered != end) throw std::runtime_error ("Font selection omitted source bytes");
    start= end;
  }
  return result;
}

font_catalog& current_font_catalog () {
  struct catalog_slot {
    font_catalog value;
    static std::vector<std::string> directories () {
      std::vector<std::string> result;
      append_directory (tt_private_font_path (), result);
      return result;
    }
    catalog_slot (): value (true, {}, directories ()) {}
  };
  return font_domain_local<catalog_slot> ().value;
}

font_request font_request_from_source (const physical_font_source& source,
                                       std::string language) {
  const auto face= load_tt_face (source.file);
  if (face->bad_face) throw std::runtime_error ("Cannot describe primary text font");
  std::unique_ptr<FcPattern, decltype (&FcPatternDestroy)> pattern (
    FcFreeTypeQueryFace (face->ft_face,
      reinterpret_cast<const FcChar8*> (source.file.file_utf8.c_str ()),
      source.file.face_index, nullptr), FcPatternDestroy);
  if (!pattern) throw std::runtime_error ("Cannot read primary font metadata");
  description_ptr description (
    pango_fc_font_description_from_pattern (pattern.get (), FALSE),
    pango_font_description_free);
  if (!description) throw std::runtime_error ("Cannot describe primary font style");
  std::unique_ptr<char, decltype (&g_free)> name (
    pango_font_description_to_string (description.get ()), g_free);
  if (!name) throw std::bad_alloc ();
  font_request request;
  request.description_utf8= name.get ();
  request.language= std::move (language);
  request.point_size= source.point_size;
  request.horizontal_dpi= source.horizontal_dpi;
  request.vertical_dpi= source.vertical_dpi;
  validate_request (request);
  return request;
}

font_paragraph::font_paragraph (std::string source, font_request request, font_catalog& catalog):
  font_paragraph (std::move (source), std::move (request), {}, catalog) {}

font_paragraph::font_paragraph (std::string source, font_request request,
                                const std::vector<font_style_span>& styles, font_catalog& catalog):
  source_ (std::move (source)), analysis_ (source_, request.direction, request.language),
  request_ (std::move (request)),
  fonts_ (catalog.select (source_, request_, analysis_.base_level (), styles)),
  owner_ (&current_font_domain ()) {}

unicode_paragraph& font_paragraph::analysis () {
  owner_->check_owner ();
  if (owner_ != &current_font_domain ())
    throw std::logic_error ("Paragraph belongs to another font domain");
  return analysis_;
}

shaped_line font_paragraph::line (std::size_t begin, std::size_t end,
                                 const shaping_options& options, double horizontal_scale) {
  const double scaled= std::round (request_.horizontal_dpi * horizontal_scale);
  if (!std::isfinite (horizontal_scale) || horizontal_scale <= 0 ||
      !std::isfinite (scaled) || scaled < 1 || scaled > std::numeric_limits<int>::max ())
    throw std::invalid_argument ("Invalid horizontal font scale");
  const int hdpi= static_cast<int> (scaled);
  auto locate= [&] (std::size_t byte) {
    return std::lower_bound (fonts_.begin (), fonts_.end (), byte,
      [] (const selected_font_run& run, std::size_t at) { return run.end <= at; });
  };
  return shape_line (analysis (), begin, end,
    [&] (std::string_view source, const shaping_item& item, const shaping_options& o) {
      const auto font= locate (item.run.begin);
      if (font == fonts_.end () || font->begin > item.run.begin || font->end < item.run.end)
        throw std::logic_error ("Shaping item crosses selected font boundary");
      auto selected= o;
      if (selected.language.empty () || selected.language == "und") selected.language= font->language;
      return shape_freetype_utf8 (font->font, font->point_size,
        hdpi, request_.vertical_dpi, source, item.run.begin, item.run.end, selected);
    }, options, [&] (const shaping_item& item) {
      std::vector<std::size_t> cuts;
      for (auto font= locate (item.run.begin); font != fonts_.end () && font->end < item.run.end; ++font)
        cuts.push_back (font->end);
      return cuts;
    });
}

} // namespace athena::text

/******************************************************************************
* MODULE     : shaped_text_test.cpp
* DESCRIPTION: UTF-8 shaping, byte clusters, owner isolation and rendered glyphs
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "font.hpp"
#include "data_cache.hpp"
#include "drd_std.hpp"
#include "unicode_text.hpp"
#include "shaped_line.hpp"
#include "font_selection.hpp"
#include "math_font.hpp"
#include "Boxes/construct.hpp"
#include "Boxes/utf8_line.hpp"
#include "Freetype/tt_face.hpp"
#include "Qt/QTMRenderService.hpp"
#include "Qt/qt_renderer.hpp"
#include <harfbuzz/hb-ot.h>
#include <QTemporaryDir>
#include <QPicture>
#include <cstdlib>
#include <cmath>
#include <future>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <tuple>

bool headless_mode= true;
bool is_headless () { return true; }

using namespace athena::text;

static void require (bool value, const char* message) {
  if (!value) throw std::runtime_error (message);
}

template<class Exception, class Action> static void rejects (Action action) {
  try { action (); }
  catch (const Exception&) { return; }
  throw std::runtime_error ("Expected rejection was missing");
}

static font pagella (int size= 12, int dpi= 96) {
  cache_set ("font_cache.scm", "ttf:texgyrepagella-regular",
    string (std::getenv ("ATHENA_PATH")) *
      "/fonts/truetype/texgyre/texgyrepagella-regular.otf");
  return unicode_font ("texgyrepagella-regular", size, dpi);
}

static shaped_text shape (font fn, std::string_view text,
                           shaping_options options= {}) {
  return fn->shape_utf8 (text, 0, text.size (), options);
}

static void check_carets (font fn) {
  shaping_options options;
  options.editing_carets= true;
  const std::string samples[]= {
    "", "literal <alpha>", "ffi", "a\xce\xb1" "b", "e\xcc\x81x\xcc\x81",
    "\xe4\xb8\xad\xe6\x96\x87", "\xf0\x9f\x98\x80",
    "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb",
    "\xf0\x9f\x87\xa8\xf0\x9f\x87\xb3",
    "\xf0\x9f\x91\x8d\xf0\x9f\x8f\xbd",
    "\xd7\x90\xd6\xb0\xd7\x91", "\xe2\x80\x8d"};
  for (const auto& source: samples) {
    for (const auto direction: {run_direction::left_to_right,
                                run_direction::right_to_left}) {
      options.direction= direction;
      const auto run= shape (fn, source, options);
      grapheme_cursor boundaries (source);
      std::size_t expected= 0;
      for (const auto& caret: run.carets) {
        require (caret.byte == expected, "Caret omitted or split an ICU grapheme");
        require (run.caret_x (caret.byte) == caret.x, "Caret lookup disagrees");
        const auto hit= run.hit_test (caret.x);
        require (run.caret_x (hit) == caret.x && boundaries.boundary (hit),
                 "Hit testing does not agree with displayed caret geometry");
        expected= boundaries.next (expected);
      }
      require (!run.carets.empty () && run.carets.back ().byte == source.size (),
               "Missing end caret");
      for (std::size_t byte= 0; byte < source.size (); ++byte)
        if (!scalar_boundary (source, byte) || !boundaries.boundary (byte))
          rejects<std::invalid_argument> ([&] { run.caret_x (byte); });
      (void) run.hit_test (std::numeric_limits<SI>::min ());
      (void) run.hit_test (std::numeric_limits<SI>::max ());
    }
  }
  options.direction= run_direction::left_to_right;
  auto lig= shape (fn, "ffi", options);
  require (lig.carets.size () == 4 && lig.caret_x (1) > 0 &&
           lig.caret_x (1) < lig.caret_x (2) && lig.caret_x (2) < lig.advance_x,
           "Ligature has no internal grapheme carets");
  const std::string context= "xxe\xcc\x81yy";
  auto middle= fn->shape_utf8 (context, 2, 5, options);
  require (middle.carets.size () == 2 && middle.carets[0].byte == 2 &&
           middle.carets[1].byte == 5, "Caret range lost source byte positions");
  rejects<std::invalid_argument> ([&] { fn->shape_utf8 (context, 2, 3, options); });
  rejects<std::invalid_argument> ([&] { fn->shape_utf8 (context, 3, 3, options); });
  options.max_carets= 1;
  rejects<std::length_error> ([&] { shape (fn, "a", options); });
  options.max_carets= 0;
  rejects<std::length_error> ([&] { shape (fn, "", options); });
  rejects<std::logic_error> ([&] { shape (fn, "a").hit_test (0); });
}

static void check_boxes (font fn) {
  const string source= "xxa\xce\xb1" "e\xcc\x81 ffi<z>yy";
  const int begin= 2, end= N(source) - 2;
  shaping_options options;
  options.editing_carets= true;
  for (const auto direction: {run_direction::left_to_right,
                              run_direction::right_to_left}) {
    options.direction= direction;
    auto run= fn->shape_utf8 ({source.data (), static_cast<std::size_t> (N(source))},
                             begin, end, options);
    box b= utf8_text_box (path (0), source, begin, end, fn, pencil (black), options);
    require (b->get_leaf_left_pos () == begin && b->get_leaf_right_pos () == end &&
             b->get_leaf_string () == source (begin, end), "Box lost source range");
    require (b->w () == run.advance_x, "Text box did not use shaped advance");
    for (const auto& caret: run.carets) {
      const path bp (static_cast<int> (caret.byte) - begin);
      require (b->find_cursor (bp)->ox == caret.x, "Box cursor remeasured a prefix");
      bool found= false;
      path hit= b->find_box_path (caret.x, 0, 0, false, found);
      require (found && b->find_cursor (hit)->ox == caret.x, "Box hit missed caret");
      path tree_position= b->find_tree_path (bp);
      require (last_item (tree_position) == static_cast<int> (caret.byte),
               "Tree position is not an absolute byte offset");
      require (b->find_box_path (tree_position, found) == bp && found,
               "Tree and box positions do not round-trip");
    }
    require (b->find_cursor (path (4))->ox == b->find_cursor (path (3))->ox,
             "Cursor entered a combining sequence");
    auto sel= b->find_selection (path (0), path (end - begin));
    require (!is_nil (sel->rs) && sel->rs->item->x1 <= sel->rs->item->x2,
             "RTL selection rectangle is inverted");
    require (b->get_leaf_offset ("<z>") == run.caret_x (end - 3),
             "Literal angle-bracket search used Cork token positions");
  }
  rejects<std::invalid_argument> ([&] {
    utf8_text_box (path (0), source, 4, end, fn, pencil (black));
  });
}

static void check_lines (font fn) {
  item_shaper shaper= [fn] (std::string_view source, const shaping_item& item,
                            const shaping_options& options) {
    return fn->shape_utf8 (source, item.run.begin, item.run.end, options);
  };
  const std::string samples[]= {"", "a \xd7\x90\xd7\x91 b", "a(\xce\xb1)b",
    "a e\xcc\x81 ffi", "\xd8\x80" "a", "\xd7\x90\xd6\xb0 a",
    "a \xe2\x81\xa7\xd7\x90\xd7\x91\xe2\x81\xa9 b"};
  bool saw_fragment= false;
  for (const auto& source: samples) {
    unicode_paragraph paragraph (source);
    grapheme_cursor boundaries (source);
    for (const auto& item: paragraph.items (0, source.size ()))
      saw_fragment= saw_fragment || !boundaries.boundary (item.run.begin) ||
                                     !boundaries.boundary (item.run.end);
    const auto line= shape_line (paragraph, 0, source.size (), shaper);
    SI x= 0;
    bool missing= false;
    for (const auto& run: line.runs) {
      require (run.x == x, "Visual line run origins are not contiguous");
      x+= run.text.advance_x;
      missing= missing || run.text.missing_glyphs;
    }
    require (line.advance == x && line.missing_glyphs == missing,
             "Line lost run metrics or missing-glyph status");
    for (std::size_t byte= 0; byte < source.size (); byte= boundaries.next (byte))
      for (const auto span: line.selection_spans (byte, boundaries.next (byte)))
        require (span.left <= span.right && span.left >= 0 && span.right <= line.advance,
                 "Grapheme selection escaped its visual line");
    for (const auto& caret: line.carets) {
      require (boundaries.boundary (caret.byte), "Line exposed a partial grapheme");
      require (line.caret_x (caret.byte, caret.affinity) == caret.x,
               "Line caret affinity lost its visual coordinate");
      const auto hit= line.hit_test (caret.x);
      require (hit.x == caret.x && boundaries.boundary (hit.byte),
               "Line hit test disagrees with displayed carets");
    }
    for (std::size_t byte= 0;; byte= boundaries.next (byte)) {
      (void) line.caret_x (byte, caret_affinity::upstream);
      (void) line.caret_x (byte, caret_affinity::downstream);
      if (byte == source.size ()) break;
    }
    for (std::size_t byte= 0; byte < source.size (); ++byte)
      if (!scalar_boundary (source, byte) || !boundaries.boundary (byte))
        rejects<std::invalid_argument> ([&] {
          line.caret_x (byte, caret_affinity::downstream);
        });
  }
  require (saw_fragment, "Fixture did not cover item boundaries inside a grapheme");
  const std::string mixed= "a \xd7\x90\xd7\x91 b";
  unicode_paragraph paragraph (mixed);
  const auto line= shape_line (paragraph, 0, mixed.size (), shaper);
  require (line.caret_x (2, caret_affinity::upstream) !=
           line.caret_x (2, caret_affinity::downstream),
           "Bidi boundary collapsed two distinct logical affinities");
  const auto second= shape_line (paragraph, 2, mixed.size (),
    [&] (std::string_view source, const shaping_item& item, const shaping_options& o) {
      require (source.data () == mixed.data () && o.context_begin == 2 &&
               o.context_end == mixed.size (), "Line lost borrowed source or joining limits");
      return shaper (source, item, o);
    });
  require (second.byte_begin == 2, "Wrapped line lost absolute byte positions");
  shaping_options limit;
  limit.max_glyphs= 1;
  rejects<std::length_error> ([&] { shape_line (paragraph, 0, mixed.size (), shaper, limit); });
  limit.max_glyphs= 1000;
  limit.max_carets= 1;
  rejects<std::length_error> ([&] { shape_line (paragraph, 0, mixed.size (), shaper, limit); });
  limit.max_carets= 0;
  rejects<std::length_error> ([&] { shape_line (paragraph, 0, 0, shaper, limit); });
  const std::string three_scripts= "a\xce\xb1" "b";
  unicode_paragraph three (three_scripts);
  limit.max_glyphs= 2;
  limit.max_carets= 100;
  rejects<std::length_error> ([&] {
    shape_line (three, 0, three_scripts.size (), shaper, limit);
  });
  limit.max_glyphs= 100;
  limit.max_carets= 4;
  rejects<std::length_error> ([&] {
    shape_line (three, 0, three_scripts.size (), shaper, limit);
  });
  rejects<std::invalid_argument> ([&] {
    shape_line (paragraph, 0, mixed.size (),
      [&] (std::string_view s, const shaping_item& i, const shaping_options& o) {
        auto invalid= shaper (s, i, o);
        ++invalid.byte_end;
        return invalid;
      });
  });
  for (const auto& cuts: std::vector<std::vector<std::size_t>> {{0}, {1, 1}, {2, 1}, {4}}) {
    unicode_paragraph ascii ("abc");
    rejects<std::invalid_argument> ([&] {
      shape_line (ascii, 0, 3, shaper, {},
        [&] (const shaping_item&) { return cuts; });
    });
  }
  unicode_paragraph scalar ("\xce\xb1\xce\xb2");
  rejects<std::invalid_argument> ([&] {
    shape_line (scalar, 0, 4, shaper, {},
      [] (const shaping_item&) { return std::vector<std::size_t> {1}; });
  });
  shaping_options fragment;
  fragment.grapheme_fragments= true;
  rejects<std::invalid_argument> ([&] {
    utf8_text_box (path (0), "e\xcc\x81", 0, 1, fn, pencil (black), fragment);
  });
}

static void check_physical_faces () {
  const auto file= (std::filesystem::path (__FILE__).parent_path () /
                    "fixtures/two-faces.ttc").string ();
  const font_file_source first {file, 0}, second {file, 1};
  const auto f0= load_tt_face (first), f1= load_tt_face (second);
  require (!f0->bad_face && !f1->bad_face && f0.rep != f1.rep &&
           f0->ft_face->face_index == 0 && f1->ft_face->face_index == 1,
           "Collection face identity was lost");
  require (f0->font_data.get () == f1->font_data.get (),
           "Collection faces copied the same immutable font file");
  require (load_tt_face (first).rep == f0.rep && load_tt_face (second).rep == f1.rep,
           "Physical font cache did not retain face indices");
  shaping_options options;
  options.editing_carets= true;
  const auto a= shape_freetype_utf8 (first, 12, 600, 600, "A", 0, 1, options);
  const auto b= shape_freetype_utf8 (second, 12, 600, 600, "A", 0, 1, options);
  require (a.glyphs.size () == 1 && b.glyphs.size () == 1 &&
           a.glyphs[0].index == 2 && b.glyphs[0].index == 3 &&
           a.advance_x * 9 == b.advance_x * 5,
           "Shaping used the wrong collection cmap or metrics");
  require (a.ink_y2 > b.ink_y2 && a.ink_x2 < b.ink_x2,
           "Collection outlines do not agree with shaping metrics");
  const auto g0= a.glyph_source->get (0x0c000000 + a.glyphs[0].index);
  const auto g1= b.glyph_source->get (0x0c000000 + b.glyphs[0].index);
  require (!is_nil (g0) && !is_nil (g1) && g0->height > g1->height &&
           g0->width < g1->width, "Rasterizer disagrees with selected shaping face");
  physical_font_source metadata;
  require (b.glyph_source->physical_source (metadata) && metadata.file.file_utf8 == file &&
           metadata.file.face_index == 1 && metadata.point_size == 12 &&
           metadata.horizontal_dpi == 600 && metadata.vertical_dpi == 600,
           "Exporter lost explicit physical font metadata");
  require (FT_Select_Charmap (f1->ft_face, FT_ENCODING_APPLE_ROMAN) == 0,
           "Fixture lacks its non-Unicode charmap");
  const auto encoded= tt_font_glyphs (f1, 13, 96, 96)->get (0x0c000001);
  require (!is_nil (encoded) && encoded->index == 1,
           "Explicit glyph index was mistaken for a legacy character code");
  require (FT_Select_Charmap (f1->ft_face, FT_ENCODING_UNICODE) == 0,
           "Could not restore fixture Unicode charmap");
  const auto variable_file= (std::filesystem::path (__FILE__).parent_path () /
                             "fixtures/named-instance.ttf").string ();
  const font_file_source regular {variable_file, 0}, heavy {variable_file, 0x10000};
  const auto plain= shape_freetype_utf8 (regular, 12, 600, 600, "A", 0, 1);
  const auto bold= shape_freetype_utf8 (heavy, 12, 600, 600, "A", 0, 1);
  require (plain.advance_x * 9 == bold.advance_x * 5 &&
           plain.ink_x2 < bold.ink_x2 &&
           load_tt_face (heavy)->ft_face->face_index == 0x10000,
           "HarfBuzz lost the FreeType named instance");
  const auto bold_raster= bold.glyph_source->get (0x0c000000 + bold.glyphs[0].index);
  require (bold_raster->width > g0->width,
           "Rasterization lost the named-instance variation");
  require (shape_freetype_utf8 (regular, 12, 600, 600, "A", 0, 1).advance_x == plain.advance_x,
           "Named instance contaminated the default-instance cache");
  const font_file_source intermediate {variable_file, 0, {650 * 65536}};
  const auto medium= shape_freetype_utf8 (intermediate, 12, 600, 600, "A", 0, 1);
  require (medium.advance_x * 5 == plain.advance_x * 7 &&
           medium.ink_x2 > plain.ink_x2 && medium.ink_x2 < bold.ink_x2,
           "Shaping lost explicit variation coordinates");
  require (medium.glyph_source->physical_source (metadata) &&
           metadata.file.design_coords == intermediate.design_coords,
           "Export lost explicit variation coordinates");
  require (medium.glyph_source->get (0x0c000000 + medium.glyphs[0].index)->width < bold_raster->width,
           "Rasterizer ignored explicit variation coordinates");
  rejects<std::runtime_error> ([&] {
    shape_freetype_utf8 (font_file_source {variable_file, 0, {1, 2}}, 12, 96, 96, "A", 0, 1);
  });
  rejects<std::runtime_error> ([&] {
    shape_freetype_utf8 (font_file_source {file, 2}, 12, 96, 96, "A", 0, 1);
  });
  rejects<std::runtime_error> ([&] {
    shape_freetype_utf8 (font_file_source {file, 0x10000}, 12, 96, 96, "A", 0, 1);
  });
  rejects<std::invalid_argument> ([&] { load_tt_face (font_file_source {"relative.ttf", 0}); });
  rejects<std::invalid_argument> ([&] { load_tt_face (font_file_source {file, -1}); });
  rejects<std::invalid_argument> ([&] {
    load_tt_face (font_file_source {file + std::string (1, '\0'), 0});
  });
}

static void check_font_selection () {
  const auto file= (std::filesystem::path (__FILE__).parent_path () /
                    "fixtures/two-faces.ttc").string ();
  font_catalog catalog (false, {file});
  font_request request {"ATHENA Collection Fixture One,ATHENA Collection Fixture Two"};
  const std::string source= "A \xce\xb1\xce\xb2 \xd7\x90\xd7\x91 A";
  font_paragraph paragraph (source, request, catalog);
  bool fallback= false;
  std::size_t covered= 0;
  for (const auto& run: paragraph.fonts ()) {
    require (run.begin == covered && run.end > covered && run.font.file_utf8 == file,
             "Font itemization lost coverage or used fonts outside its private catalog");
    covered= run.end;
    fallback= fallback || run.font.face_index == 1;
  }
  require (fallback && covered == source.size (), "Fallback did not cover the paragraph");
  const auto line= paragraph.line (0, source.size ());
  require (!line.missing_glyphs, "Font-selected line still has missing glyphs");
  bool rtl_first= false, rtl_second= false;
  for (const auto& run: line.runs) {
    if (run.text.byte_begin == 9) rtl_first= true;
    if (run.text.byte_begin == 7) {
      require (rtl_first, "Font fallback reversed the visual order of RTL subitems");
      rtl_second= true;
    }
  }
  require (rtl_first && rtl_second, "Fixture did not split its RTL font item");
  const auto wrapped= paragraph.line (2, source.size ());
  require (wrapped.byte_begin == 2 && !wrapped.missing_glyphs,
           "Wrapped line failed to reuse selected fonts");
  const std::string with_nul ("A\0A", 3);
  font_paragraph controls (with_nul, request, catalog);
  const auto control_line= controls.line (0, 3);
  require (controls.analysis ().source () == with_nul && control_line.byte_end == 3 &&
           control_line.carets.back ().byte == 3, "NUL truncated or rewrote source text");
  {
    font_domain other;
    font_domain_binding binding (other);
    rejects<std::logic_error> ([&] { paragraph.line (0, source.size ()); });
    rejects<std::logic_error> ([&] { catalog.select (source, request); });
  }
  rejects<std::invalid_argument> ([&] {
    auto invalid= request;
    invalid.description_utf8.push_back ('\0');
    catalog.select (source, invalid);
  });
  const auto variable= (std::filesystem::path (__FILE__).parent_path () /
                        "fixtures/named-instance.ttf").string ();
  font_catalog variable_catalog (false, {variable});
  request.description_utf8= "ATHENA Collection Fixture One @wght=650";
  font_paragraph varied ("A", request, variable_catalog);
  require (varied.fonts ().size () == 1 &&
           varied.fonts ()[0].font.design_coords == std::vector<std::int32_t> {650 * 65536},
           "Pango-selected variation was not transferred to the native font source");
  const auto expected= shape_freetype_utf8 (font_file_source {variable, 0, {650 * 65536}},
                                          12, 96, 96, "A", 0, 1);
  require (varied.line (0, 1).advance == expected.advance_x,
           "Pango-selected variation changed during shaping");
  font_paragraph installed ("A", font_request {"TeX Gyre Pagella"});
  require (!installed.line (0, 1).missing_glyphs,
            "Application/system catalog did not select an installed font");
  font_catalog synthetic_catalog (true, {file});
  font_paragraph synthetic_bold (
    "A", font_request {"ATHENA Collection Fixture One Bold"}, synthetic_catalog);
  require (synthetic_bold.fonts ().size () == 1 &&
           synthetic_bold.fonts ()[0].font.file_utf8 == file &&
           synthetic_bold.fonts ()[0].font.face_index == 0,
           "Synthetic bold fallback did not retain the real physical face");
}

static void check_font_styles (font nominal) {
  const auto file= (std::filesystem::path (__FILE__).parent_path () /
                    "fixtures/two-faces.ttc").string ();
  font_catalog catalog (false, {file});
  font_request base {"ATHENA Collection Fixture One"};
  font_request alternate {"ATHENA Collection Fixture Two", "he", 24};
  std::vector<font_style_span> styles {{1, 3, alternate}};
  auto paragraph= std::make_shared<font_paragraph> ("AAAA", base, styles, catalog);
  styles.clear ();
  require (paragraph->fonts ().size () == 3, "Styled paragraph lost its default font gaps");
  for (std::size_t i= 0; i < paragraph->fonts ().size (); ++i) {
    const auto& selected= paragraph->fonts ()[i];
    require (selected.font.face_index == (i == 1 ? 1 : 0) &&
             selected.point_size == (i == 1 ? 24 : 12) &&
             selected.language == (i == 1 ? "he" : "und"),
             "Font style identity, size or language was lost");
  }
  const auto line= paragraph->line (0, 4);
  const auto plain= shape_freetype_utf8 (font_file_source {file, 0}, 12, 96, 96, "AAAA", 0, 1);
  const auto large= shape_freetype_utf8 (font_file_source {file, 1}, 24, 96, 96, "AAAA", 1, 3);
  require (line.advance == 2 * plain.advance_x + large.advance_x,
           "Styled paragraph used the base font size for every run");
  require (line.runs.size () == 3, "Style boundaries disappeared from shaped output");
  physical_font_source physical;
  require (line.runs[1].text.glyph_source->physical_source (physical) &&
           physical.point_size == 24 && physical.file.face_index == 1,
           "Styled raster/export metadata uses a different physical size");
  auto box= utf8_line_box (path (0), paragraph, 0, 4, nominal, pencil (black));
  require (box->w () == line.advance && box->y4 >= line.runs[1].text.ink_y2,
           "Styled line box clipped or remeasured the larger font");
  auto wrapped= paragraph->line (2, 4);
  require (wrapped.runs.size () == 2 && wrapped.runs.front ().text.byte_begin == 2,
           "Wrapped styled line lost absolute font ranges");
  require (paragraph->line (1, 3, {}, 1.5).advance ==
           shape_freetype_utf8 (font_file_source {file, 1}, 24, 144, 96, "AAAA", 1, 3).advance_x,
           "Horizontal expansion lost a span's font size");

  const std::string rtl= "A \xd7\x90\xd7\x91 A";
  font_request hebrew= base;
  hebrew.language= "he";
  hebrew.point_size= 18;
  font_paragraph bidi (rtl, base, {{2, 4, hebrew}, {4, 6, alternate}}, catalog);
  const auto bidi_line= bidi.line (0, rtl.size ());
  bool saw_bet= false, saw_alef= false;
  for (const auto& run: bidi_line.runs) {
    if (run.text.byte_begin == 4) {
      require (run.text.direction == run_direction::right_to_left &&
               run.text.glyph_source->physical_source (physical) && physical.point_size == 24,
               "RTL style lost its direction or size");
      saw_bet= true;
    }
    if (run.text.byte_begin == 2) {
      require (saw_bet && run.text.glyph_source->physical_source (physical) && physical.point_size == 18,
               "Font styling restarted bidi at the style boundary");
      saw_alef= true;
    }
  }
  require (saw_alef && saw_bet, "Styled RTL fixture was not exercised");

  font_paragraph combining ("A\xcc\x81", base, {{1, 3, alternate}}, catalog);
  for (const auto& caret: combining.line (0, 3).carets)
    require (caret.byte == 0 || caret.byte == 3, "Style boundary split an editing grapheme");
  const std::string nul ("A\0A", 3);
  font_paragraph control (nul, base, {{1, 3, alternate}}, catalog);
  require (control.fonts ().size () == 3 && control.fonts ()[1].point_size == 24 &&
           control.fonts ()[1].font.face_index == 1 && control.fonts ()[2].font.face_index == 1 &&
           control.line (0, 3).byte_end == 3, "NUL handling ignored its font style or following text");

  font_request roman {"TeX Gyre Pagella"};
  font_request bold {"TeX Gyre Pagella Bold"};
  font_request italic {"TeX Gyre Pagella Italic"};
  font_paragraph emphasis ("AAA", roman, {{1, 2, bold}, {2, 3, italic}});
  require (emphasis.fonts ().size () == 3, "Emphasis did not select distinct physical styles");
  require ((load_tt_face (emphasis.fonts ()[1].font)->ft_face->style_flags & FT_STYLE_FLAG_BOLD) &&
           (load_tt_face (emphasis.fonts ()[2].font)->ft_face->style_flags & FT_STYLE_FLAG_ITALIC) &&
           !emphasis.line (0, 3).missing_glyphs,
           "Pagella emphasis did not use its real bold/italic faces");
  font_request bold_italic {"TeX Gyre Pagella Bold Italic", "el", 14, 144, 120};
  const auto upright_bold= font_request_with_italic (bold_italic, false);
  require (upright_bold.point_size == 14 && upright_bold.horizontal_dpi == 144 &&
           upright_bold.vertical_dpi == 120 && upright_bold.language == "el",
           "Symbol slant override changed the effective font size or language");
  font_paragraph upright_symbol ("D", upright_bold);
  const auto flags= load_tt_face (upright_symbol.fonts ()[0].font)->ft_face->style_flags;
  require ((flags & FT_STYLE_FLAG_BOLD) && !(flags & FT_STYLE_FLAG_ITALIC),
           "Upright symbol lost bold or retained inherited italic");
  font_paragraph italic_symbol ("C", font_request_with_italic (roman, true));
  require (load_tt_face (italic_symbol.fonts ()[0].font)->ft_face->style_flags & FT_STYLE_FLAG_ITALIC,
           "Italic symbol did not select an actual italic face");
  font_paragraph ligature ("ffi", roman, {{0, 1, roman}, {1, 3, roman}});
  font_paragraph unstyled ("ffi", roman);
  require (ligature.fonts ().size () == 1 &&
           ligature.line (0, 3).runs[0].text.glyphs.size () == unstyled.line (0, 3).runs[0].text.glyphs.size () &&
           ligature.line (0, 3).advance == unstyled.line (0, 3).advance,
           "Identical adjacent styles broke a ligature");

  for (const auto& invalid: std::vector<std::vector<font_style_span>> {
      {{2, 2, alternate}}, {{0, 5, alternate}}, {{2, 4, alternate}, {1, 2, alternate}},
      {{0, 3, alternate}, {2, 4, alternate}}})
    rejects<std::invalid_argument> ([&] { font_paragraph p ("AAAA", base, invalid, catalog); });
  rejects<std::invalid_argument> ([&] { font_paragraph p ("\xce\xb1", base, {{1, 2, alternate}}, catalog); });
  auto adjusted= base;
  adjusted.horizontal_dpi= 144;
  adjusted.vertical_dpi= 72;
  font_paragraph mixed_scale ("AAAA", base, {{1, 3, adjusted}}, catalog);
  require (mixed_scale.fonts ().size () == 3 &&
           mixed_scale.fonts ()[1].horizontal_dpi == 144 && mixed_scale.fonts ()[1].vertical_dpi == 72,
           "Font-size adjustment merged into neighboring device scales");
  const auto exact= shape_freetype_utf8 (font_file_source {file, 0}, 12, 144, 72, "AAAA", 1, 3);
  const auto adjusted_line= mixed_scale.line (1, 3);
  require (adjusted_line.advance == exact.advance_x &&
           adjusted_line.runs[0].text.glyph_source->physical_source (physical) &&
           physical.horizontal_dpi == 144 && physical.vertical_dpi == 72 && physical.point_size == 12,
           "Styled shaping lost its native device scale");
  require (mixed_scale.line (1, 3, {}, 1.5).advance ==
           shape_freetype_utf8 (font_file_source {file, 0}, 12, 216, 72, "AAAA", 1, 3).advance_x,
           "Expansion used paragraph DPI instead of the styled run's DPI");
  auto invalid_scale= adjusted;
  invalid_scale.vertical_dpi= 0;
  rejects<std::invalid_argument> ([&] { font_paragraph p ("A", base, {{0, 1, invalid_scale}}, catalog); });
  invalid_scale.vertical_dpi= std::numeric_limits<int>::max ();
  rejects<std::invalid_argument> ([&] { font_paragraph p ("A", base, {{0, 1, invalid_scale}}, catalog); });
  auto wrong_direction= alternate;
  wrong_direction.direction= paragraph_direction::rtl;
  rejects<std::invalid_argument> ([&] { font_paragraph p ("A", base, {{0, 1, wrong_direction}}, catalog); });
}

static void check_line_boxes (font nominal) {
  const auto file= (std::filesystem::path (__FILE__).parent_path () /
                    "fixtures/two-faces.ttc").string ();
  font_catalog catalog (false, {file});
  font_request request {"ATHENA Collection Fixture One,ATHENA Collection Fixture Two"};
  const std::string source= "A \xce\xb1\xce\xb2 \xd7\x90\xd7\x91 A";
  auto paragraph= std::make_shared<font_paragraph> (source, request, catalog);
  auto line= paragraph->line (0, source.size ());
  box b= utf8_line_box (path (0), paragraph, 0, source.size (), nominal, pencil (black));
  require (b->w () == line.advance, "Multi-font box lost line advance");
  for (const auto& caret: line.carets) {
    const path bp (caret.byte, static_cast<int> (caret.affinity));
    require (b->find_cursor (bp)->ox == caret.x, "Multi-font box lost caret affinity");
    bool found= false;
    const auto hit= b->find_box_path (caret.x, 0, 0, false, found);
    require (found && b->find_cursor (hit)->ox == caret.x,
             "Multi-font box hit testing changed the displayed cursor");
    require (last_item (b->find_tree_path (bp)) == static_cast<int> (caret.byte),
             "Box affinity leaked into source tree byte positions");
    const auto from_tree= b->find_box_path (b->find_tree_path (bp), found);
    require (found && from_tree->item == static_cast<int> (caret.byte),
             "Source tree position did not return to its logical line position");
  }
  require (b->find_cursor (path (7, static_cast<int> (caret_affinity::upstream)))->ox !=
           b->find_cursor (path (7, static_cast<int> (caret_affinity::downstream)))->ox,
           "Bidi junction collapsed its two cursor positions");
  const path junction= b->find_tree_path (path (7));
  for (const auto side: {caret_affinity::upstream, caret_affinity::downstream}) {
    auto retained= copy (b->find_check_cursor (junction, side));
    require (retained->valid && retained->affinity == side &&
             retained->ox == line.caret_x (7, side),
             "Logical cursor lookup or copy discarded its visual side");
    auto other_side= copy (retained);
    other_side->affinity= side == caret_affinity::upstream ?
      caret_affinity::downstream : caret_affinity::upstream;
    require (other_side != retained, "Cursor comparison ignored affinity-only movement");
    auto rebuilt= move_box (path (0), utf8_line_box (path (0), paragraph, 0,
      source.size (), nominal, pencil (black), {}, brush (false), 1.25), 300, 400);
    auto restored= rebuilt->find_check_cursor (junction, retained->affinity);
    require (restored->valid && restored->affinity == side &&
             restored->ox == 300 + paragraph->line (0, source.size (), {}, 1.25).caret_x (7, side),
             "Retypesetting failed to restore the retained cursor side");
    require (!b->find_check_cursor (path (0, 8), side)->valid,
             "Affinity lookup accepted an invalid scalar position");
  }
  require (b->find_cursor (path (8))->ox == b->find_cursor (path (7))->ox,
           "Stale byte position entered a UTF-8 scalar");
  const auto spans= line.selection_spans (0, 9);
  require (spans.size () == 2 && spans[0].right < spans[1].left,
           "Logical bidi selection included an unselected visual run");
  const auto reversed= line.selection_spans (9, 0);
  require (reversed.size () == spans.size () && reversed[0].left == spans[0].left &&
           reversed[1].right == spans[1].right, "Backward selection changed the visual region");
  auto selected= b->find_selection (path (0), path (9));
  auto regions= selected->rs;
  for (const auto span: spans) {
    require (!is_nil (regions) && regions->item->x1 == span.left &&
             regions->item->x2 == span.right, "Box selection disagrees with line geometry");
    regions= regions->next;
  }
  require (is_nil (regions), "Box selection added a spurious region");
  require (line.selection_spans (0, source.size ()).size () == 1 &&
           line.selection_spans (7, 7).empty (), "Whole/empty line selection is invalid");
  rejects<std::invalid_argument> ([&] { line.selection_spans (0, 8); });
  auto expanded= b->expand_glyphs (0, 0.25);
  require (expanded->w () == paragraph->line (0, source.size (), {}, 1.25).advance,
           "Line expansion did not reshape every selected font");
  rejects<std::invalid_argument> ([&] { b->expand_glyphs (0, -1); });
  rejects<std::invalid_argument> ([&] {
    paragraph->line (0, source.size (), {}, std::numeric_limits<double>::quiet_NaN ());
  });
  auto wrapped= utf8_line_box (path (0), paragraph, 2, source.size (), nominal, pencil (black));
  require (wrapped->get_leaf_left_pos () == 2 &&
           last_item (wrapped->find_tree_path (path (0))) == 2,
           "Wrapped line lost absolute source offsets");
  array<box> fragments;
  fragments << utf8_line_box (path (0), paragraph, 0, 7, nominal, pencil (black));
  fragments << utf8_line_box (path (0), paragraph, 7, source.size (), nominal, pencil (black));
  array<SI> spacing (2);
  spacing[0]= spacing[1]= 100;
  auto stack= stack_box (path (0), fragments, spacing);
  for (const auto side: {caret_affinity::upstream, caret_affinity::downstream}) {
    const int row= side == caret_affinity::upstream ? 0 : 1;
    auto at_break= stack->find_check_cursor (junction, side);
    require (at_break->valid && at_break->affinity == side &&
             at_break->oy == stack->sy (row),
             "Wrapped endpoint did not choose its requested line");
    const int first= row == 0 ? 0 : 7;
    const int last= row == 0 ? 7 : source.size ();
    require (at_break->ox == paragraph->line (first, last).caret_x (7, side),
             "Wrapped endpoint chose the wrong visual caret in its line");
    auto enclosing= move_box (path (0), stack, 250, 450);
    auto moved= enclosing->find_check_cursor (junction, side);
    require (moved->ox == at_break->ox + 250 && moved->oy == at_break->oy + 450,
             "Wrapped affinity did not propagate through a containing box");
  }
  array<box> reversed_fragments;
  reversed_fragments << fragments[1] << fragments[0];
  auto reversed_row= concat_box (path (0), reversed_fragments);
  auto before= reversed_row->find_check_cursor (junction, caret_affinity::upstream);
  auto after= reversed_row->find_check_cursor (junction, caret_affinity::downstream);
  require (before->valid && after->valid &&
           before->ox == reversed_row->sx (1) + paragraph->line (0, 7).caret_x (7, caret_affinity::upstream) &&
           after->ox == reversed_row->sx (0) + paragraph->line (7, source.size ()).caret_x (7, caret_affinity::downstream),
           "Fragment selection confused visual order with logical source order");
  auto only_first= move_box (path (0), fragments[0], 0, 0);
  require (only_first->find_check_cursor (junction, caret_affinity::downstream)->valid,
           "An absent neighboring fragment invalidated the source endpoint");
  auto nested= move_box (path (0), b, 100, 200);
  bool found= false;
  const auto hit= nested->find_box_path (100 + line.caret_x (7, caret_affinity::upstream),
                                        200, 0, false, found);
  require (found && nested->find_cursor (hit)->ox ==
           100 + line.caret_x (7, caret_affinity::upstream),
           "Nested line box lost visual cursor affinity");
  auto retained_hit= nested->find_cursor (hit);
  auto restored_hit= nested->find_check_cursor (nested->find_tree_path (hit),
                                                retained_hit->affinity);
  require (restored_hit == retained_hit && restored_hit->valid,
           "Click-to-tree-to-cursor conversion changed the visual position");
  require (find_scrolled_box_path (nested, path (), retained_hit->ox,
                                   retained_hit->oy, 0) == hit,
           "Scrolled hit testing dropped the leaf affinity suffix");
  require (is_nil (find_innermost_scroll (nested, b->find_tree_path (path (7)))),
           "Scroll traversal interpreted a leaf affinity as a child index");
  auto shorter= shorter_box (path (0), b, 9);
  require (last_item (shorter->find_tree_path (shorter->find_right_box_path ())) == 9,
           "Shorter box clamped affinity instead of the text byte");
  const auto clipped= shorter->find_box_path (b->x2 + 100, 0, 0, true, found);
  require (last_item (shorter->find_tree_path (clipped)) == 9,
           "Shorter box hit testing lost the terminal position suffix");
  require (shorter->cursor_affinities (shorter->find_right_box_path ()) == caret_affinity::upstream,
           "Hyphenation wrapper exposed text beyond its logical endpoint");
  array<box> clipped_fragments;
  clipped_fragments << shorter << utf8_line_box (path (0), paragraph, 9,
                                                source.size (), nominal, pencil (black));
  auto clipped_stack= stack_box (path (0), clipped_fragments, spacing);
  require (clipped_stack->find_check_cursor (b->find_tree_path (path (9)),
             caret_affinity::downstream)->oy == clipped_stack->sy (1),
           "Clipped source endpoint did not advance to its continuation line");
  auto symbolic= symbol_box (path (0), b, source.size ());
  require (last_item (symbolic->find_tree_path (
             symbolic->find_box_path (b->x2 + 100, 0, 0, true, found))) == static_cast<int> (source.size ()),
           "Symbol modifier clamped affinity instead of the text byte");
  auto legacy= text_box (path (0), 0, "abcdef", nominal, pencil (black));
  require (!legacy->cursor_affinities (path (3)),
           "Legacy cursor positions claimed Unicode fragment-side support");
  require (legacy->find_check_cursor (path (0, 3), caret_affinity::upstream) ==
           legacy->find_check_cursor (path (0, 3), caret_affinity::downstream),
           "Affinity changed legacy text cursor behavior");
  auto legacy_shorter= shorter_box (path (0), legacy, 3);
  auto legacy_symbol= symbol_box (path (0), legacy, 6);
  require (last_item (legacy_shorter->find_tree_path (legacy_shorter->find_right_box_path ())) == 3 &&
           last_item (legacy_symbol->find_tree_path (
             legacy_symbol->find_box_path (legacy->x2 + 100, 0, 0, true, found))) == 6,
           "Position suffix support regressed legacy leaf modifiers");
  paragraph.reset ();
  require (b->get_leaf_string () == string (source.data (), source.size ()),
           "Line box did not retain its shared immutable paragraph");
}

static void check_line_spacing () {
  font_request request;
  request.description_utf8= "TeX Gyre Pagella";
  for (const std::string source: {std::string ("ab  cd ef"),
       std::string ("\xd7\x90\xd7\x91  \xd7\x92\xd7\x93 ef")}) {
    font_paragraph paragraph (source, request);
    const auto begin= source.find (' '), end= source.find_first_not_of (' ', begin);
    auto line= paragraph.line (0, source.size ());
    const SI original= line.advance;
    const auto region= line.selection_spans (begin, end).at (0);
    const SI old_width= region.right-region.left;
    line.set_space_widths (source, {{begin, end, old_width + 1700}});
    require (line.advance == original+1700, "Justification changed nonspace advances");
    const auto expanded= line.selection_spans (begin, end).at (0);
    require (expanded.right-expanded.left == old_width+1700, "Space selection missed justification");
    for (const auto& caret: line.carets)
      require (line.hit_test (caret.x).x == caret.x, "Justified hit test disagrees with carets");
    for (const auto& placed: line.runs) {
      SI pen= 0;
      for (const auto& glyph: placed.text.glyphs) pen += glyph.advance_x;
      require (pen == placed.text.advance_x, "Justified run advances disagree with glyphs");
    }
    line.set_space_widths (source, {{begin, end, old_width}});
    require (line.advance == original, "Restoring glue did not restore line width");
    rejects<std::invalid_argument> ([&] {
      line.set_space_widths (source, {{0, 1, 100}});
    });
    const auto next= source.find (' ', end);
    const auto next_end= source.find_first_not_of (' ', next);
    const auto next_region= line.selection_spans (next, next_end).at (0);
    const SI next_width= next_region.right-next_region.left;
    line.set_space_widths (source, {{begin, end, old_width+1700},
                                    {next, next_end, next_width/2}});
    require (line.advance == original+1700+next_width/2-next_width,
             "Logical glue order did not account for visual bidi ordering");
    require (line.selection_spans (begin, end).at (0).right -
             line.selection_spans (begin, end).at (0).left == old_width+1700,
             "A later glue adjustment changed an earlier space width");
    rejects<std::invalid_argument> ([&] {
      line.set_space_widths (source, {{begin, end, -1}});
    });
  }
}

static void record_bitmap_text (QPicture& recording) {
  font_domain owner;
  font_domain_binding binding (owner);
  const std::string source= "\xf0\x9f\x98\x80";
  font_request request {"Noto Color Emoji"};
  request.horizontal_dpi= request.vertical_dpi= 600;
  font_paragraph paragraph (source, request);
  const auto line= paragraph.line (0, source.size ());
  require (line.runs.size () == 1, "Emoji did not select a single font run");
  const auto& run= line.runs[0].text;
  require (run.bitmaps.size () == 1 && run.has_ink && !run.missing_glyphs,
           "Fixed-strike color glyph was lost");
  require (run.carets.size () == 2 && run.carets.back ().byte == source.size (),
           "Bitmap font changed UTF-8 grapheme carets");
  const auto& bitmap= run.bitmaps[0];
  require (bitmap.intrinsic_color && !is_nil (bitmap.pixels), "Emoji lost its color bitmap");
  const auto expanded= paragraph.line (0, source.size (), {}, 1.5);
  require (expanded.runs[0].text.bitmaps[0].width > bitmap.width &&
           expanded.runs[0].text.bitmaps[0].height == bitmap.height,
           "Bitmap glyph lost anisotropic text scaling");
  QPainter painter (&recording);
  qt_renderer_rep renderer (&painter, 1.0, 160, 80);
  renderer.set_zoom_factor (1.0);
  renderer.set_clipping (0, -80*std_shrinkf*PIXEL, 160*std_shrinkf*PIXEL, 0);
  renderer.set_pencil (pencil (black));
  line.draw_fixed (&renderer, source, 20*std_shrinkf*PIXEL, -50*std_shrinkf*PIXEL);
  painter.end ();
}

static void check_bitmap_text () {
  QPicture recording;
  record_bitmap_text (recording);
  // Recorded pixels must survive destruction of the shaping domain and face.
  QImage image (160, 80, QImage::Format_ARGB32);
  image.fill (Qt::white);
  QPainter painter (&image);
  painter.drawPicture (0, 0, recording);
  painter.end ();
  int colored= 0;
  for (int y=0; y<image.height (); ++y)
    for (int x=0; x<image.width (); ++x) {
      const auto pixel= image.pixel (x, y);
      colored+= std::max ({qRed (pixel), qGreen (pixel), qBlue (pixel)}) -
                 std::min ({qRed (pixel), qGreen (pixel), qBlue (pixel)}) > 40;
    }
  require (colored > 40, "Color glyph produced no colored pixels");
}

static void check_math_alphabets () {
  using alphabet= math_alphabet;
  require (math_variant_character (U'h', alphabet::italic) == 0x210e &&
           math_variant_character (U'E', alphabet::script) == 0x2130 &&
           math_variant_character (U'H', alphabet::fraktur) == 0x210c &&
           math_variant_character (U'I', alphabet::fraktur) == 0x2111 &&
           math_variant_character (U'R', alphabet::double_struck) == 0x211d &&
           math_variant_character (U'7', alphabet::double_struck) == 0x1d7df &&
           math_variant_character (0x03b1, alphabet::italic) == 0x1d6fc &&
           math_variant_character (U'A', alphabet::bold_italic) == 0x1d468,
           "UCD math variants lost alphabet holes, Greek, digits or font roles");
  require (math_variant_character (0x1d434, alphabet::bold) == 0x1d434 &&
           math_variant_character (0x301, alphabet::italic) == 0x301 &&
           math_variant_character (0x4e2d, alphabet::italic) == 0x4e2d,
           "Math font style changed explicit alphabet, combining mark or unmapped script");
  require (math_character_alphabet (0x2130) == alphabet::script &&
           math_character_alphabet (0x210c) == alphabet::fraktur &&
           math_character_alphabet (0x2145) == alphabet::normal,
           "Math alphabet classification guessed an unsupported variant");

  const std::string root= std::getenv ("ATHENA_PATH");
  const std::string regular= root + "/fonts/truetype/texgyre/texgyrepagella-regular.otf";
  const std::string math= root + "/fonts/truetype/texgyre/texgyrepagella-math.otf";
  font_catalog catalog (false, {regular, math});
  font_request base {"TeX Gyre Pagella,TeX Gyre Pagella Math"};
  auto italic= base;
  italic.math_variant= alphabet::italic;
  const std::string source= "ahe\xcc\x81\xce\xb1 7";
  const std::vector<font_style_span> spans {{1, 7, italic}};
  font_paragraph paragraph (source, base, spans, catalog);
  require (paragraph.analysis ().source () == source,
           "Math glyph projection rewrote the source atom");
  const auto line= paragraph.line (0, source.size ());
  require (!line.missing_glyphs, "Pango fallback did not cover the math projection");
  for (auto expected: {std::pair<std::size_t, char32_t> {1, 0x210e}, {5, 0x1d6fc}}) {
    bool found= false;
    for (const auto& placed: line.runs) {
      for (const auto& glyph: placed.text.glyphs) {
        if (glyph.byte != expected.first) continue;
        const auto font= std::find_if (paragraph.fonts ().begin (), paragraph.fonts ().end (),
          [&] (const selected_font_run& run) { return run.begin <= glyph.byte && glyph.byte < run.end; });
        require (font != paragraph.fonts ().end () && font->math_variant == alphabet::italic,
                 "Math projection lost its original-byte style range");
        auto face= load_tt_face (font->font);
        require (glyph.index == FT_Get_Char_Index (face->ft_face, expected.second),
                 "Shaper and fallback selected different math characters");
        found= true;
      }
    }
    require (found, "Math glyph lost its original byte cluster");
  }
  require (line.byte_end == source.size () && line.carets.back ().byte == source.size (),
           "Rendered projection offsets escaped into editor positions");
  rejects<std::invalid_argument> ([&] { line.caret_x (3, caret_affinity::downstream); });
  auto scaled= paragraph.line (1, 7, {}, 1.5);
  require (!scaled.missing_glyphs && scaled.byte_begin == 1 && scaled.byte_end == 7,
           "Partial math layout lost projection mapping");
}

static void check_math_metrics () {
  const std::string file= std::string (std::getenv ("ATHENA_PATH")) +
    "/fonts/truetype/texgyre/texgyrepagella-math.otf";
  cache_set ("font_cache.scm", "ttf:texgyrepagella-math", string (file.c_str ()));
  font physical= unicode_font ("texgyrepagella-math", 12, 96);
  // Read the fixture independently of the owner-local shaping cache.
  std::unique_ptr<hb_blob_t, decltype (&hb_blob_destroy)> blob (
    hb_blob_create_from_file_or_fail (file.c_str ()), hb_blob_destroy);
  require (blob != nullptr, "Cannot read math font fixture");
  std::unique_ptr<hb_face_t, decltype (&hb_face_destroy)> face (
    hb_face_create (blob.get (), 0), hb_face_destroy);
  std::unique_ptr<hb_font_t, decltype (&hb_font_destroy)> reference (
    hb_font_create (face.get ()), hb_font_destroy);
  hb_ot_font_set_funcs (reference.get ());
  const int scale= (12 * 96 * PIXEL + 36) / 72;
  hb_font_set_scale (reference.get (), scale, scale);
  require (hb_ot_math_has_data (face.get ()), "Fixture has no OpenType MATH data");

  const auto physical_source= math_font_source (physical, math_alphabet::normal);
  require (physical_source.file.file_utf8 == file,
           "Math layout did not retain the selected physical face");
  const auto constants= math_layout_metrics (physical);
  require (constants.has_value (), "Native math font did not expose MATH constants");
  require (constants->axis_height ==
             hb_ot_math_get_constant (reference.get (), HB_OT_MATH_CONSTANT_AXIS_HEIGHT) &&
           constants->display_operator_min_height ==
             hb_ot_math_get_constant (reference.get (), HB_OT_MATH_CONSTANT_DISPLAY_OPERATOR_MIN_HEIGHT) &&
           constants->fraction_rule_thickness ==
             hb_ot_math_get_constant (reference.get (), HB_OT_MATH_CONSTANT_FRACTION_RULE_THICKNESS) &&
           constants->radical_vertical_gap ==
             hb_ot_math_get_constant (reference.get (), HB_OT_MATH_CONSTANT_RADICAL_VERTICAL_GAP) &&
           constants->radical_degree_bottom_raise_percent ==
             hb_ot_math_get_constant (reference.get (), HB_OT_MATH_CONSTANT_RADICAL_DEGREE_BOTTOM_RAISE_PERCENT),
           "Native MATH constants disagree with HarfBuzz");

  hb_codepoint_t paren= 0;
  require (hb_font_get_nominal_glyph (reference.get (), '(', &paren) && paren != 0,
           "Math fixture has no left parenthesis");
  unsigned int variant_count= 64;
  std::vector<hb_ot_math_glyph_variant_t> reference_variants (variant_count);
  hb_ot_math_get_glyph_variants (reference.get (), paren, HB_DIRECTION_TTB,
                                  0, &variant_count, reference_variants.data ());
  reference_variants.resize (variant_count);
  require (!reference_variants.empty (), "Math fixture has no parenthesis variants");
  const auto& wanted_variant= reference_variants[std::min<std::size_t> (
    1, reference_variants.size () - 1)];
  auto variant= shape_math_stretch (physical, "(", wanted_variant.advance, true);
  require (variant && !variant->assembled && variant->run.glyphs.size () == 1 &&
           variant->run.glyphs.front ().index == wanted_variant.glyph &&
           variant->extent == wanted_variant.advance,
           "Native math stretch did not select HarfBuzz's ready-made variant");
  const SI assembly_target= reference_variants.back ().advance + scale;
  auto assembly= shape_math_stretch (physical, "(", assembly_target, true);
  require (assembly && assembly->assembled && assembly->extent >= assembly_target &&
           assembly->run.glyphs.size () > 1,
           "Native math stretch did not construct an oversized delimiter");
  for (std::size_t i=1; i<assembly->run.glyphs.size (); ++i)
    require (assembly->run.glyphs[i-1].y <= assembly->run.glyphs[i].y,
             "Vertical MATH assembly is not ordered bottom-to-top");
  const string paren_source ("(");
  auto assembly_box= math_glyph_box (
    path (0), paren_source, physical, pencil (black), assembly->run);
  require (assembly_box->get_leaf_string () == paren_source &&
           assembly_box->h () > 0,
           "Math assembly box lost source identity or physical geometry");
  QPicture math_recording;
  {
    QPainter painter (&math_recording);
    qt_renderer_rep renderer (&painter, 1.0, 200, 200);
    renderer.set_pencil (pencil (black));
    assembly_box->display (&renderer);
    painter.end ();
  }
  require (math_recording.size () > 0,
           "Math assembly did not reach the native glyph renderer");

  bool saw_math_kern_table= false;
  const unsigned int glyph_count= hb_face_get_glyph_count (face.get ());
  for (hb_codepoint_t glyph= 1; glyph<glyph_count; ++glyph)
    for (auto [corner, hb_corner]: {
           std::pair {math_kern_corner::top_right, HB_OT_MATH_KERN_TOP_RIGHT},
           std::pair {math_kern_corner::top_left, HB_OT_MATH_KERN_TOP_LEFT},
           std::pair {math_kern_corner::bottom_right, HB_OT_MATH_KERN_BOTTOM_RIGHT},
           std::pair {math_kern_corner::bottom_left, HB_OT_MATH_KERN_BOTTOM_LEFT}}) {
      unsigned int entry_count= 0;
      const unsigned int total= hb_ot_math_get_glyph_kernings (
        reference.get (), glyph, hb_corner, 0, &entry_count, nullptr);
      if (total == 0) continue;
      saw_math_kern_table= true;
      std::vector<hb_ot_math_kern_entry_t> entries (total);
      entry_count= total;
      hb_ot_math_get_glyph_kernings (
        reference.get (), glyph, hb_corner, 0, &entry_count, entries.data ());
      entries.resize (entry_count);
      shaped_math_metrics probe {0, 0, physical_source, glyph};
      for (const auto& entry: entries)
        for (SI height: {entry.max_correction_height - 1,
                         entry.max_correction_height,
                         entry.max_correction_height + 1})
          require (open_type_math_kern (probe, corner, height) ==
                     hb_ot_math_get_glyph_kerning (
                       reference.get (), glyph, hb_corner, height),
                   "Native height-dependent MATH kern disagrees with HarfBuzz");
    }
  if (!saw_math_kern_table) {
    shaped_math_metrics probe {0, 0, physical_source, paren};
    for (auto [corner, hb_corner]: {
           std::pair {math_kern_corner::top_right, HB_OT_MATH_KERN_TOP_RIGHT},
           std::pair {math_kern_corner::top_left, HB_OT_MATH_KERN_TOP_LEFT},
           std::pair {math_kern_corner::bottom_right, HB_OT_MATH_KERN_BOTTOM_RIGHT},
           std::pair {math_kern_corner::bottom_left, HB_OT_MATH_KERN_BOTTOM_LEFT}})
      for (SI height: {-scale, 0, scale})
        require (open_type_math_kern (probe, corner, height) ==
                   hb_ot_math_get_glyph_kerning (
                     reference.get (), paren, hb_corner, height),
                 "Absent MATH kern table did not match HarfBuzz's zero result");
  }

  font_catalog catalog (false, {file});
  font_request request {"TeX Gyre Pagella Math"};
  bool nonzero_correction= false, offcenter_anchor= false;
  for (const std::string source: {std::string ("A"), std::string ("\xf0\x9d\x91\x93"),
                                  std::string ("\xe2\x88\xab")}) {
    const auto run= shape (physical, source);
    require (run.glyphs.size () == 1 && run.math && !run.missing_glyphs,
             "Single Unicode math glyph lost physical metrics");
    const auto glyph= run.glyphs.front ();
    const SI correction= hb_ot_math_get_glyph_italics_correction (reference.get (), glyph.index);
    const SI anchor= glyph.x +
      hb_ot_math_get_glyph_top_accent_attachment (reference.get (), glyph.index);
    require (run.math->italic_correction == correction &&
             run.math->top_accent_attachment == anchor,
             "Native math metrics disagree with the selected physical glyph");
    nonzero_correction |= correction != 0;
    offcenter_anchor |= anchor != run.advance_x / 2;
    const string atom (source.data (), source.size ());
    auto single= utf8_text_box (path (0), atom, 0, N(atom), physical, pencil (black));
    auto paragraph= std::make_shared<font_paragraph> (source, request, catalog);
    // The nominal text font has no MATH table: actual selected glyphs own these metrics.
    auto leaf= utf8_line_box (path (0), paragraph, 0, source.size (), pagella (), pencil (black));
    for (auto b: {single, leaf}) {
      require (b->right_correction () == correction && b->rsup_correction () == correction &&
               b->top_accent_attachment () == anchor && b->wide_correction (0) == 1,
               "Unicode box lost math script or accent metrics");
      require (b->math_script_kern (math_kern_corner::top_right, 0).value_or (MAX_SI) ==
                 hb_ot_math_get_glyph_kerning (
                   reference.get (), glyph.index, HB_OT_MATH_KERN_TOP_RIGHT, 0),
               "Unicode box lost height-dependent MATH kerning");
      auto decorated= macro_box (path (0), direct_link_box (path (0), b, "#target"));
      require (decorated->top_accent_attachment () == anchor &&
               decorated->rsup_correction () == correction,
               "Atomic symbol or link wrapper discarded math metrics");
      auto moved= move_box (path (0), decorated, 37, 0);
      require (moved->top_accent_attachment () == anchor + 37,
               "Moved math glyph retained an untranslated accent anchor");
      array<box> pieces;
      pieces << moved;
      require (concat_box (path (0), pieces)->top_accent_attachment () == anchor + 37,
               "Single-glyph concatenation lost the accent anchor");
      pieces << single;
      require (!concat_box (path (0), pieces)->top_accent_attachment (),
               "Multiple-glyph expression inherited a single-glyph accent anchor");
      auto accented= wide_box (path (0), b, "^", physical, pencil (black), false, true);
      require (accented->subnr () == 2, "Accent box is missing its glyph");
      auto accent= accented->subbox (1);
      require (accented->sx (1) + ((accent->x1 + accent->x2) >> 1) ==
               accented->sx (0) + anchor,
               "Accent layout added legacy corrections to an OpenType anchor");
    }
    auto expanded= leaf->expand_glyphs (0, 1.0);
    hb_font_set_scale (reference.get (), 2 * scale, scale);
    require (expanded->rsup_correction () ==
               hb_ot_math_get_glyph_italics_correction (reference.get (), glyph.index) &&
             expanded->top_accent_attachment () ==
               hb_ot_math_get_glyph_top_accent_attachment (reference.get (), glyph.index),
             "Horizontal glyph expansion did not rescale math metrics");
    hb_font_set_scale (reference.get (), scale, scale);
    if (source.size () > 1)
      require (!shorter_box (path (0), leaf, 1)->top_accent_attachment (),
               "Clipped glyph retained an invalid whole-glyph anchor");
  }
  require (nonzero_correction && offcenter_anchor, "Math test did not exercise nontrivial metrics");
  require (!shape (pagella (), "A").math && !shape (physical, "AB").math &&
           !shape (physical, "").math && !shape (physical, "\xf4\x8f\xbf\xbf").math,
           "Absent MATH, multiple, empty or missing glyphs acquired false metrics");
  auto combining= std::make_shared<font_paragraph> ("e\xcc\x81", request, catalog);
  require (utf8_line_box (path (0), combining, 0, 3, pagella (), pencil (black))->wide_correction (0) == 1,
           "Accent width classification counted UTF-8 bytes instead of graphemes");
  auto word= std::make_shared<font_paragraph> ("AB", request, catalog);
  auto word_box= utf8_line_box (path (0), word, 0, 2, pagella (), pencil (black));
  require (!word_box->top_accent_attachment () && word_box->wide_correction (0) == 0,
           "Multi-grapheme math text was classified as one accent base");
}

static void check_text () {
  font_domain owner;
  font_domain_binding binding (owner);
  font fn= pagella ();
  check_carets (fn);
  check_boxes (fn);
  check_lines (fn);
  check_physical_faces ();
  check_font_selection ();
  check_font_styles (fn);
  check_line_boxes (fn);
  check_line_spacing ();
  check_bitmap_text ();
  check_math_metrics ();
  check_math_alphabets ();
  auto literal= shape (fn, "a<alpha>b");
  require (literal.glyphs.size () == 9 && !literal.missing_glyphs,
           "Literal angle-bracket text was interpreted as Cork");
  for (std::size_t i= 0; i < literal.glyphs.size (); ++i)
    require (literal.glyphs[i].byte == i, "ASCII cluster offset changed");
  auto greek= shape (fn, "a\xce\xb1" "b");
  require (greek.glyphs.size () == 3 && greek.glyphs[1].byte == 1 &&
           greek.glyphs[2].byte == 3 && !greek.missing_glyphs,
           "UTF-8 input was decoded as legacy bytes");
  tt_face face= load_tt_face ("texgyrepagella-regular");
  require (greek.glyphs[1].index == FT_Get_Char_Index (face->ft_face, 0x3b1),
           "Wrong Unicode glyph was selected");

  auto composed= shape (fn, "\xc3\xa9");
  auto decomposed= shape (fn, "e\xcc\x81");
  require (composed.glyphs.size () == 1 && decomposed.glyphs.size () == 1 &&
           composed.glyphs[0].index == decomposed.glyphs[0].index &&
           composed.byte_end == 2 && decomposed.byte_end == 3,
           "Combining shaping changed source bytes or lost canonical shaping");
  const std::string combining= "x\xcc\x81";
  auto marks= shape (fn, combining);
  require (!marks.missing_glyphs && marks.has_ink,
           "Combining mark did not render");
  grapheme_cursor caret (combining);
  require (caret.next (0) == combining.size (),
           "Editing must use ICU, not a shaping cluster as a caret stop");
  for (const auto& glyph: marks.glyphs)
    require (scalar_boundary (combining, glyph.byte), "Cluster splits UTF-8");

  shaping_options no_ligatures;
  no_ligatures.ligatures= false;
  auto lig= shape (fn, "ffi");
  auto separate= shape (fn, "ffi", no_ligatures);
  require (lig.glyphs.size () < separate.glyphs.size () &&
           separate.glyphs.size () == 3, "OpenType ligature control was ignored");
  const std::string context= "a\xce\xb1" "b";
  auto middle= fn->shape_utf8 (context, 1, 3);
  require (middle.glyphs.size () == 1 && middle.glyphs[0].byte == 1 &&
           middle.byte_begin == 1 && middle.byte_end == 3,
           "Item offset is not an absolute byte offset");
  shaping_options line_context;
  line_context.context_begin= 1;
  line_context.context_end= 3;
  line_context.editing_carets= true;
  auto bounded= fn->shape_utf8 (context, 1, 3, line_context);
  auto isolated= shape (fn, std::string_view (context).substr (1, 2));
  require (bounded.glyphs.size () == isolated.glyphs.size () &&
           bounded.glyphs[0].index == isolated.glyphs[0].index &&
           bounded.glyphs[0].byte == 1 && bounded.carets[0].byte == 1 &&
           bounded.advance_x == isolated.advance_x,
           "Line context changed absolute source positions");
  rejects<std::invalid_argument> ([&] { fn->shape_utf8 (context, 0, 3, line_context); });
  line_context.context_end= 2;
  rejects<std::invalid_argument> ([&] { fn->shape_utf8 (context, 1, 1, line_context); });
  line_context.context_end= context.size () + 1;
  rejects<std::invalid_argument> ([&] { fn->shape_utf8 (context, 1, 3, line_context); });
  auto empty= fn->shape_utf8 (context, 3, 3);
  require (empty.glyphs.empty () && empty.advance_x == 0 && !empty.has_ink,
           "Empty run acquired ink or advance");
  auto spaces= shape (fn, "   ");
  require (!spaces.has_ink && spaces.advance_x > 0,
           "Whitespace needs advance but no ink");

  auto first= shape (fn, "AV");
  auto larger= shape (pagella (24, 96), "AV");
  // Force the shared FreeType face through another size and charmap.
  (void) pagella (36, 120)->get_glyph ("X");
  FT_CharMap saved= face->ft_face->charmap;
  (void) FT_Select_Charmap (face->ft_face, FT_ENCODING_ADOBE_CUSTOM);
  auto again= shape (fn, "AV");
  if (saved) FT_Set_Charmap (face->ft_face, saved);
  require (first.advance_x == again.advance_x &&
           std::abs (larger.advance_x - 2 * first.advance_x) <= 2,
           "Shaping depended on another font's mutable size/charmap");
  require (first.advance_x < shape (fn, "A").advance_x +
                             shape (fn, "V").advance_x,
           "OpenType kerning was not applied");

  // Supplementary-plane input remains one scalar even when the chosen font
  // lacks it; the fallback caller receives an explicit missing-glyph result.
  const std::string nonbmp= "\xf0\x9f\x98\x80";
  auto missing= shape (fn, nonbmp);
  require (missing.glyphs.size () == 1 && missing.glyphs[0].byte == 0 &&
           missing.byte_end == 4 && missing.missing_glyphs,
           "Non-BMP input was split or font fallback failure was hidden");
  shaping_options rtl;
  rtl.direction= run_direction::right_to_left;
  rtl.script= "Hebr";
  rtl.language= "he";
  auto reversed= shape (fn, "\xd7\x90\xd7\x91", rtl);
  require (reversed.glyphs.size () == 2 && reversed.glyphs[0].byte == 2 &&
           reversed.glyphs[1].byte == 0 && reversed.direction == rtl.direction,
           "RTL visual order lost logical byte clusters");
  rejects<std::invalid_argument> ([&] { shape (fn, "\xc0\xaf"); });
  rejects<std::invalid_argument> ([&] { fn->shape_utf8 (context, 2, 3); });
  rejects<std::invalid_argument> ([&] { fn->shape_utf8 (context, 3, 2); });
  shaping_options limited;
  limited.max_glyphs= 1;
  rejects<std::length_error> ([&] { shape (fn, "AB", limited); });
  limited.script= "invalid";
  rejects<std::invalid_argument> ([&] { shape (fn, "A", limited); });
  rejects<std::overflow_error> ([&] {
    shape_freetype_utf8 ("texgyrepagella-regular",
      std::numeric_limits<int>::max (), 96, 96, "A", 0, 1);
  });
}

static void check_recording (bool multi_font= false, double zoom= 1.0,
                             double pixel_ratio= 1.0) {
  const int width= 800, height= 320;
  auto connection= QTMRenderConnection::create (2, 64 * 1024);
  require (bool (connection), "No render connection");
  auto recording= connection->beginRecording (
    width, height, pixel_ratio, qRgb (255, 255, 255), 1, 1,
    {0, 0, width, height});
  require (bool (recording), "No recording");
  int expected_left, expected_right, expected_top, expected_bottom;
  {
    font_domain owner;
    font_domain_binding binding (owner);
    const std::string source= multi_font ? "A \xce\xb1\xce\xb2 A" : "<alpha> \xce\xb1 e\xcc\x81";
    auto run= shape (pagella (12, 600), source);
    string atom (source.data (), source.size ());
    box leaf= utf8_text_box (path (0), atom, 0, N(atom),
                             pagella (12, 600), pencil ((color) qRgb (0, 0, 0)));
    atom.set (0, 'X');
    require (leaf->get_leaf_string ()[0] == source[0],
             "Editing an atom changed a retained box's source snapshot");
    if (multi_font) {
      const auto file= (std::filesystem::path (__FILE__).parent_path () /
                        "fixtures/two-faces.ttc").string ();
      font_catalog catalog (false, {file});
      font_request request {"ATHENA Collection Fixture One,ATHENA Collection Fixture Two"};
      request.horizontal_dpi= request.vertical_dpi= 600;
      auto alternate= request;
      alternate.description_utf8= "ATHENA Collection Fixture Two";
      alternate.point_size= 24;
      alternate.language= "el";
      const std::vector<font_style_span> styles {{2, 6, alternate}};
      auto paragraph= std::make_shared<font_paragraph> (source, request, styles, catalog);
      leaf= utf8_line_box (path (0), paragraph, 0, source.size (), pagella (12, 600),
                           pencil ((color) qRgb (0, 0, 0)));
    }
    require (!run.missing_glyphs && run.has_ink, "Run cannot be rendered");
    invalidate_font_configuration ();
    owner.synchronize_configuration ();
    auto refreshed= shape (pagella (12, 600), source);
    require (refreshed.glyph_source.rep != run.glyph_source.rep &&
             refreshed.advance_x == run.advance_x,
             "Shaping cache was not invalidated with its font domain");
    const double pixel= std_shrinkf * PIXEL / (zoom * pixel_ratio);
    expected_left= static_cast<int> (std::floor (20 + leaf->x3 / pixel));
    expected_right= static_cast<int> (std::ceil (20 + leaf->x4 / pixel));
    expected_top= static_cast<int> (std::floor (200 - leaf->y4 / pixel));
    expected_bottom= static_cast<int> (std::ceil (200 - leaf->y3 / pixel));
    QPainter painter (recording->device ());
    qt_renderer_rep renderer (&painter, pixel_ratio, width, height, true);
    renderer.set_zoom_factor (zoom);
    renderer.set_clipping (0, -height*pixel, width*pixel, 0);
    renderer.set_pencil (pencil ((color) qRgb (0, 0, 0)));
    rejects<std::overflow_error> ([&] {
      run.draw_fixed (&renderer, source, std::numeric_limits<SI>::max (), 0);
    });
    rejects<std::invalid_argument> ([&] {
      run.draw_fixed (&renderer, "short", 0, 0);
    });
    // A retained run still uses its original resources after a font refresh.
    renderer.move_origin (20*pixel, -200*pixel);
    const auto state= std::make_tuple (
      renderer.ox, renderer.oy, renderer.cx1, renderer.cy1,
      renderer.cx2, renderer.cy2, renderer.zoomf, renderer.shrinkf,
      renderer.pixel, renderer.retina_pixel, renderer.brushpx, renderer.thicken);
    leaf->display (&renderer);
    require (state == std::make_tuple (
      renderer.ox, renderer.oy, renderer.cx1, renderer.cy1,
      renderer.cx2, renderer.cy2, renderer.zoomf, renderer.shrinkf,
      renderer.pixel, renderer.retina_pixel, renderer.brushpx, renderer.thicken),
      "Shaped drawing changed the caller's renderer coordinates");
    painter.end ();
  }
  require (recording->finish (), "Could not publish recording");
  auto deadline= std::chrono::steady_clock::now () + std::chrono::seconds (1);
  while (std::chrono::steady_clock::now () < deadline) {
    auto frame= connection->acquireLatestFrame ();
    if (frame) {
      unsigned ink= 0;
      int left= width, right= -1, top= height, bottom= -1;
      for (int y= 0; y < frame.image ().height (); ++y)
        for (int x= 0; x < frame.image ().width (); ++x) {
          if (frame.image ().pixel (x, y) == qRgb (255, 255, 255)) continue;
          ++ink;
          left= std::min (left, x); right= std::max (right, x);
          top= std::min (top, y); bottom= std::max (bottom, y);
        }
      connection->retire ();
      require (ink > 50, "UTF-8 glyph recording was blank after domain destruction");
      require (std::abs (left - expected_left) <= 2 &&
               std::abs (right + 1 - expected_right) <= 2 &&
               std::abs (top - expected_top) <= 2 &&
               std::abs (bottom + 1 - expected_bottom) <= 2,
               "Shaped metrics and recorded ink bounds disagree");
      if (const char* output= std::getenv ("ATHENA_SHAPED_TEXT_TEST_IMAGE"))
        require (frame.image ().save (QString::fromUtf8 (output) +
          QString ("-%1-%2%3.png").arg (zoom).arg (pixel_ratio)
            .arg (multi_font ? "-line" : "")),
                 "Could not save shaped text image");
      return;
    }
    std::this_thread::sleep_for (std::chrono::milliseconds (1));
  }
  connection->retire ();
  throw std::runtime_error ("No rendered UTF-8 frame");
}

int main () {
  QTemporaryDir profile;
  if (!profile.isValid () || std::getenv ("ATHENA_PATH") == nullptr) return 1;
  qputenv ("ATHENA_HOME_PATH", profile.path ().toLocal8Bit ());
  init_std_drd ();
  try {
    const tree diagnostics_before= get_debug_messages ("Debugging console", 1000);
    check_text ();
    auto first= std::async (std::launch::async, check_text);
    auto second= std::async (std::launch::async, check_text);
    first.get ();
    second.get ();
    require (get_debug_messages ("Debugging console", 1000) == diagnostics_before,
             "Font workers mutated the GUI debug-message tree");
    check_recording ();
    check_recording (true);
    for (double zoom: {0.75, 1.5, 3.2})
      for (double ratio: {1.0, 2.0}) {
        check_recording (false, zoom, ratio);
        check_recording (true, zoom, ratio);
      }
  }
  catch (const std::exception& error) {
    std::cerr << error.what () << '\n';
    return 1;
  }
  std::cout << "UTF-8 shaping, byte clusters and owner-local rendering passed\n";
  return 0;
}

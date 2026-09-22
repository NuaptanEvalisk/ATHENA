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
#include "Boxes/construct.hpp"
#include "Boxes/utf8_line.hpp"
#include "Freetype/tt_face.hpp"
#include "Qt/QTMRenderService.hpp"
#include "Qt/qt_renderer.hpp"
#include <QTemporaryDir>
#include <cstdlib>
#include <cmath>
#include <future>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

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

static void check_text () {
  font_domain owner;
  font_domain_binding binding (owner);
  font fn= pagella ();
  check_carets (fn);
  check_boxes (fn);
  check_lines (fn);
  check_physical_faces ();
  check_font_selection ();
  check_line_boxes (fn);
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

static void check_recording (bool multi_font= false) {
  auto connection= QTMRenderConnection::create (2, 64 * 1024);
  require (bool (connection), "No render connection");
  auto recording= connection->beginRecording (
    200, 80, 1.0, qRgb (255, 255, 255), 1, 1, {0, 0, 200, 80});
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
      auto paragraph= std::make_shared<font_paragraph> (source, request, catalog);
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
    const double pixel= std_shrinkf * PIXEL;
    expected_left= static_cast<int> (std::floor (10 + leaf->x3 / pixel));
    expected_right= static_cast<int> (std::ceil (10 + leaf->x4 / pixel));
    expected_top= static_cast<int> (std::floor (45 - leaf->y4 / pixel));
    expected_bottom= static_cast<int> (std::ceil (45 - leaf->y3 / pixel));
    QPainter painter (recording->device ());
    qt_renderer_rep renderer (&painter, 1.0, 200, 80, true);
    renderer.set_zoom_factor (1.0);
    renderer.set_clipping (0, -80*std_shrinkf*PIXEL,
                           200*std_shrinkf*PIXEL, 0);
    renderer.set_pencil (pencil ((color) qRgb (0, 0, 0)));
    rejects<std::overflow_error> ([&] {
      run.draw_fixed (&renderer, source, std::numeric_limits<SI>::max (), 0);
    });
    rejects<std::invalid_argument> ([&] {
      run.draw_fixed (&renderer, "short", 0, 0);
    });
    // A retained run still uses its original resources after a font refresh.
    renderer.move_origin (10*std_shrinkf*PIXEL, -45*std_shrinkf*PIXEL);
    leaf->display (&renderer);
    painter.end ();
  }
  require (recording->finish (), "Could not publish recording");
  auto deadline= std::chrono::steady_clock::now () + std::chrono::seconds (1);
  while (std::chrono::steady_clock::now () < deadline) {
    auto frame= connection->acquireLatestFrame ();
    if (frame) {
      unsigned ink= 0;
      int left= 200, right= -1, top= 80, bottom= -1;
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
        require (frame.image ().save (QString::fromUtf8 (output) + (multi_font ? "-line.png" : "")),
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
  }
  catch (const std::exception& error) {
    std::cerr << error.what () << '\n';
    return 1;
  }
  std::cout << "UTF-8 shaping, byte clusters and owner-local rendering passed\n";
  return 0;
}

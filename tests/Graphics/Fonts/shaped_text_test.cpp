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
#include "Freetype/tt_face.hpp"
#include "Qt/QTMRenderService.hpp"
#include "Qt/qt_renderer.hpp"
#include <QTemporaryDir>
#include <cstdlib>
#include <cmath>
#include <future>
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

static void check_text () {
  font_domain owner;
  font_domain_binding binding (owner);
  font fn= pagella ();
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

static void check_recording () {
  auto connection= QTMRenderConnection::create (2, 64 * 1024);
  require (bool (connection), "No render connection");
  auto recording= connection->beginRecording (
    200, 80, 1.0, qRgb (255, 255, 255), 1, 1, {0, 0, 200, 80});
  require (bool (recording), "No recording");
  int expected_left, expected_right, expected_top, expected_bottom;
  {
    font_domain owner;
    font_domain_binding binding (owner);
    auto run= shape (pagella (12, 600), "<alpha> \xce\xb1 e\xcc\x81");
    require (!run.missing_glyphs && run.has_ink, "Run cannot be rendered");
    invalidate_font_configuration ();
    owner.synchronize_configuration ();
    auto refreshed= shape (pagella (12, 600), "<alpha> \xce\xb1 e\xcc\x81");
    require (refreshed.glyph_source.rep != run.glyph_source.rep &&
             refreshed.advance_x == run.advance_x,
             "Shaping cache was not invalidated with its font domain");
    const double pixel= std_shrinkf * PIXEL;
    expected_left= static_cast<int> (std::floor (10 + run.ink_x1 / pixel));
    expected_right= static_cast<int> (std::ceil (10 + run.ink_x2 / pixel));
    expected_top= static_cast<int> (std::floor (45 - run.ink_y2 / pixel));
    expected_bottom= static_cast<int> (std::ceil (45 - run.ink_y1 / pixel));
    QPainter painter (recording->device ());
    qt_renderer_rep renderer (&painter, 1.0, 200, 80, true);
    renderer.set_zoom_factor (1.0);
    renderer.set_clipping (0, -80*std_shrinkf*PIXEL,
                           200*std_shrinkf*PIXEL, 0);
    renderer.set_pencil (pencil ((color) qRgb (0, 0, 0)));
    rejects<std::overflow_error> ([&] {
      run.draw_fixed (&renderer, std::numeric_limits<SI>::max (), 0);
    });
    // A retained run still uses its original resources after a font refresh.
    run.draw_fixed (&renderer, 10*std_shrinkf*PIXEL, -45*std_shrinkf*PIXEL);
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
        require (frame.image ().save (QString::fromUtf8 (output)),
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
    check_text ();
    auto first= std::async (std::launch::async, check_text);
    auto second= std::async (std::launch::async, check_text);
    first.get ();
    second.get ();
    check_recording ();
  }
  catch (const std::exception& error) {
    std::cerr << error.what () << '\n';
    return 1;
  }
  std::cout << "UTF-8 shaping, byte clusters and owner-local rendering passed\n";
  return 0;
}

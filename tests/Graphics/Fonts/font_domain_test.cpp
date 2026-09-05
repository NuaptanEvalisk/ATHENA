/******************************************************************************
* MODULE     : font_domain_test.cpp
* DESCRIPTION: Font owner isolation, publication, rebinding, and destruction
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include <future>
#include <iostream>
#include <thread>
#include <stdexcept>
#include <cstdlib>
#include <QTemporaryDir>
#include "font_domain.hpp"
#include "font.hpp"
#include "charmap.hpp"
#include "translator.hpp"
#include "data_cache.hpp"
#include "drd_std.hpp"
#include "Freetype/free_type.hpp"
#include "Freetype/tt_face.hpp"
#include "Qt/QTMRenderService.hpp"
#include "Qt/qt_renderer.hpp"

bool headless_mode= true;
bool is_headless () { return true; }

FONT_RESOURCE(domain_test_font, 0);
struct domain_test_font_rep: rep<domain_test_font> {
  int value;
  int& destroyed;
  bool hidden_during_construction;
  domain_test_font_rep (int id, int& count):
    rep<domain_test_font> ("same-name"), value (id), destroyed (count),
    hidden_during_construction (!domain_test_font::instances->contains (res_name)) {
    if (id < 0) throw std::runtime_error ("construction failure");
  }
  ~domain_test_font_rep () override { ++destroyed; }
};
FONT_RESOURCE_CODE(domain_test_font);

static bool
check_invalidation () {
  int destroyed= 0;
  {
    font_domain owner;
    font_domain_binding binding (owner);
    try { domain_test_font_rep failing (-1, destroyed); }
    catch (const std::runtime_error&) {}
    if (domain_test_font::instances->contains ("same-name") ||
        domain_test_font::instances.constructing ()->contains ("same-name"))
      return false;
    domain_test_font old= tm_new<domain_test_font_rep> (1, destroyed);
    invalidate_font_configuration ();
    owner.synchronize_configuration ();
    if (domain_test_font::instances->contains ("same-name")) return false;
    domain_test_font fresh= tm_new<domain_test_font_rep> (2, destroyed);
    domain_test_font retained (old.rep);
    if (retained->value != 1 || domain_test_font ("same-name").rep != fresh.rep)
      return false;
  }
  return destroyed == 2;
}

static bool
check_real_fonts () {
  font_domain owner;
  font_domain_binding binding (owner);
  string family= "texgyrepagella-regular";
  string filename= string (std::getenv ("ATHENA_PATH")) *
    "/fonts/truetype/texgyre/texgyrepagella-regular.otf";
  cache_set ("font_cache.scm", "ttf:" * family, filename);
  font small= unicode_font (family, 10, 600);
  font large= unicode_font (family, 20, 600);
  if (small.rep == large.rep || small.rep != unicode_font (family, 10, 600).rep)
    return false;
  metric before, larger, after;
  small->get_extents ("affix", before);
  large->get_extents ("affix", larger);
  small->get_extents ("affix", after);
  if (before->x2 <= 0 || before->x2 != after->x2 || larger->x2 <= before->x2)
    return false;
  if (is_nil (small->get_glyph ("f"))) return false;
  (void) small->get_lsub_correction ("f");
  (void) large->get_rsup_correction ("T");
  (void) small->get_wide_correction ("x", 1);
  charmap map= load_charmap (tuple ("ec"));
  int child;
  string translated;
  map->lookup ("a", child, translated);
  if (child != 0 || translated != "a") return false;
  translator trl= load_virtual ("emu-bracket");
  if (N(trl->virt_def) == 0 || trl.rep != load_virtual ("emu-bracket").rep)
    return false;
  font_metric missing= std_font_metric ("missing", nullptr, 0, -1);
  return missing->get (1)->x2 == 0;
}

static bool
check_retained_frame () {
  auto connection= QTMRenderConnection::create (2, 64 * 1024);
  if (!connection) return false;
  auto recording= connection->beginRecording (
    160, 100, 1.0, qRgb (255, 255, 255), 1, 1, {0, 0, 160, 100});
  if (!recording) return false;
  {
    font_domain owner;
    font_domain_binding binding (owner);
    cache_set ("font_cache.scm", "ttf:texgyrepagella-regular",
      string (std::getenv ("ATHENA_PATH")) *
        "/fonts/truetype/texgyre/texgyrepagella-regular.otf");
    font f= unicode_font ("texgyrepagella-regular", 10, 600);
    font_metric metrics;
    font_glyphs glyphs;
    int index= f->index_glyph ("f", metrics, glyphs);
    if (index < 0 || is_nil (glyphs)) return false;
    QPainter painter (recording->device ());
    qt_renderer_rep renderer (&painter, 1.0, 160, 100, true);
    renderer.set_clipping (0, -100*PIXEL, 160*PIXEL, 0);
    renderer.set_pencil (pencil ((color) qRgb (0, 0, 0)));
    renderer.draw (index, glyphs, 40*PIXEL, -70*PIXEL);
    painter.end ();
  }
  // Do not even submit until the font, face, library and glyph cache are gone.
  if (!recording->finish ()) return false;
  auto deadline= std::chrono::steady_clock::now () + std::chrono::seconds (1);
  while (std::chrono::steady_clock::now () < deadline) {
    auto frame= connection->acquireLatestFrame ();
    if (frame) {
      bool ink= false;
      for (int y= 0; y < frame.image ().height (); ++y)
        for (int x= 0; x < frame.image ().width (); ++x)
          ink= ink || frame.image ().pixel (x, y) != qRgb (255, 255, 255);
      connection->retire ();
      return ink;
    }
    std::this_thread::sleep_for (std::chrono::milliseconds (1));
  }
  connection->retire ();
  return false;
}

static bool
check_lifetime (int id) {
  int destroyed= 0;
  {
    font_domain owner;
    font_domain_binding binding (owner);
    domain_test_font font= tm_new<domain_test_font_rep> (id, destroyed);
    if (!font->hidden_during_construction) return false;
    if (domain_test_font ("same-name").rep != font.rep) return false;
    if (font_domain_local<int> (7) != 7) return false;
    font_domain_local<int> ()= id;
    FT_Library library= current_ft_library ();
    if (library == nullptr) return false;
    {
      font_domain nested;
      font_domain_binding other (nested);
      if (domain_test_font::instances->contains ("same-name")) return false;
      domain_test_font child= tm_new<domain_test_font_rep> (id + 1, destroyed);
      if (child.rep == font.rep || !child->hidden_during_construction) return false;
      if (current_ft_library () == library) return false;
      font_domain_local<int> ()= id + 1;
    }
    if (destroyed != 1 || font_domain_local<int> () != id) return false;
    if (domain_test_font ("same-name").rep != font.rep) return false;
    if (current_ft_library () != library) return false;
  }
  return destroyed == 2;
}

int
main () {
  QTemporaryDir profile;
  if (!profile.isValid ()) return 1;
  qputenv ("ATHENA_HOME_PATH", profile.path ().toLocal8Bit ());
  init_std_drd ();
  if (std::getenv ("ATHENA_PATH") == nullptr || !check_invalidation ()) return 1;
  std::promise<void> ready[2];
  auto first= ready[0].get_future ();
  auto second= ready[1].get_future ();
  bool correct[2]= {true, true};
  auto run= [&] (int id, std::future<void>& other) {
    ready[id].set_value ();
    other.wait ();
    for (int i= 0; i < 8; ++i)
      if (!check_lifetime (id + i * 2) || !check_real_fonts ()) correct[id]= false;
  };
  std::thread a (run, 0, std::ref (second));
  std::thread b (run, 1, std::ref (first));
  a.join ();
  b.join ();
  if (!correct[0] || !correct[1] || !check_retained_frame ()) return 1;
  std::cout << "Font domains isolate, invalidate, and release real fonts and caches\n";
  return 0;
}

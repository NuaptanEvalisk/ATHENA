
/******************************************************************************
* MODULE     : bitmap_font.cpp
* DESCRIPTION: bitmap fonts
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "bitmap_font.hpp"
#include "iterator.hpp"

FONT_RESOURCE_CODE(font_metric);
FONT_RESOURCE_CODE(font_glyphs);

namespace {
struct font_fallbacks {
  metric metrics{};
  glyph glyphs;
};
}

metric& font_error_metric () {
  return font_domain_local<font_fallbacks> ().metrics;
}

glyph& font_error_glyph () {
  return font_domain_local<font_fallbacks> ().glyphs;
}

font_metric_cache::~font_metric_cache () {
  iterator<int> it= iterate (static_cast<hashmap<int,pointer>&> (*this));
  while (it->busy ()) {
    auto* value= static_cast<metric_struct*> ((*this)[it->next ()]);
    if (value != nullptr) tm_delete (value);
  }
}

/******************************************************************************
* font_metrics
******************************************************************************/

font_metric_rep::font_metric_rep (string name):
  rep<font_metric> (name), bad_font_metric (false) {}

font_metric_rep::~font_metric_rep () = default;

bool font_metric_rep::exists (int char_code) {
  (void) char_code; return true; }

SI font_metric_rep::kerning (int left_code, int right_code) {
  (void) left_code; (void) right_code; return 0; }

/******************************************************************************
* Standard bitmap metrics
******************************************************************************/


struct std_font_metric_rep: public font_metric_rep {
  int bc, ec;
  metric* fnm;

  std_font_metric_rep (string name, metric* fnm, int bc, int ec);
  ~std_font_metric_rep () override { if (fnm) tm_delete_array (fnm); }
  metric& get (int char_code) override;
};

std_font_metric_rep::std_font_metric_rep (
  string name, metric* fnm2, int bc2, int ec2):
    font_metric_rep (name), bc (bc2), ec (ec2), fnm (fnm2)
{
}

metric&
std_font_metric_rep::get (int c) {
  if ((c<bc) || (c>ec)) return font_error_metric ();
  return fnm [c-bc];
}

font_metric
std_font_metric (string name, metric* fnm, int bc, int ec) {
  if (font_metric::instances->contains (name)) {
    if (fnm) tm_delete_array (fnm);
    return font_metric (name);
  }
  return make (font_metric, name,
	       tm_new<std_font_metric_rep> (name, fnm, bc, ec));
}

/******************************************************************************
* font_glyphs
******************************************************************************/

font_glyphs_rep::font_glyphs_rep (string name):
  rep<font_glyphs> (name), bad_font_glyphs (false) {}

font_glyphs_rep::~font_glyphs_rep () = default;

/******************************************************************************
* Standard bitmap fonts
******************************************************************************/


struct std_font_glyphs_rep: public font_glyphs_rep {
  int bc, ec;
  glyph* fng; // definitions of the characters

  std_font_glyphs_rep (string name, glyph* fng, int bc, int ec);
  ~std_font_glyphs_rep () override { if (fng) tm_delete_array (fng); }
  glyph& get (int char_code) override;
};

std_font_glyphs_rep::std_font_glyphs_rep (
  string name, glyph* fng2, int bc2, int ec2):
    font_glyphs_rep (name), bc (bc2), ec (ec2), fng (fng2) {}

glyph&
std_font_glyphs_rep::get (int c) {
  if ((c<bc) || (c>ec)) return font_error_glyph ();
  return fng [c-bc];
}

font_glyphs
std_font_glyphs (string name, glyph* fng, int bc, int ec) {
  if (font_glyphs::instances->contains (name)) {
    if (fng) tm_delete_array (fng);
    return font_glyphs (name);
  }
  return make (font_glyphs, name, tm_new<std_font_glyphs_rep> (name, fng, bc, ec));
}

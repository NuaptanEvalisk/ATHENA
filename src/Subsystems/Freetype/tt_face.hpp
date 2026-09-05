
/******************************************************************************
* MODULE     : tt_face.hpp
* DESCRIPTION: resources for true type faces, gliefs and metrics
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef TT_FACE_H
#define TT_FACE_H
#include "bitmap_font.hpp"
#include "Freetype/free_type.hpp"
#include "hashmap.hpp"

FONT_RESOURCE(tt_face, 3);

struct tt_face_rep: rep<tt_face> {
  bool bad_face = true;
  FT_Face ft_face = nullptr;
  FT_Byte *buffer = nullptr;
  tt_face_rep (string name);
  ~tt_face_rep () override;
};

struct tt_font_metric_rep: font_metric_rep {
  bool bad_metric;
  tt_face face;
  int size, hdpi, vdpi;
  font_metric_cache fnm;
  //metric* fnm;
  //bool* done;
  tt_font_metric_rep (string name, string family, int size, int hdpi, int vdpi);
  bool exists (int char_code) override;
  metric& get (int char_code) override;
  SI kerning (int left_code, int right_code) override;
};

struct tt_font_glyphs_rep: font_glyphs_rep {
  bool bad_glyphs;
  tt_face face;
  int size, hdpi, vdpi;
  hashmap<int,glyph> fng;
  //glyph* fng;
  //bool* done;
  tt_font_glyphs_rep (string name, string family, int size, int hdpi, int vdpi);
  glyph& get (int char_code) override;
};

tt_face load_tt_face (string name);
int tt_math_vertical_variant (string family, unsigned int codepoint,
                              unsigned int variant);
font_metric tt_font_metric (string family, int size, int hdpi, int vdpi);
//font_glyphs tt_font_glyphs (string family, int size, int hdpi, int vdpi);

#endif // defined TT_FACE_H

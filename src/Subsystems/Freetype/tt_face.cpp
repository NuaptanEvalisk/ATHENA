
/******************************************************************************
* MODULE     : tt_face.cpp
* DESCRIPTION: resources for true type faces, gliefs and metrics
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "font.hpp"
#include "tt_face.hpp"
#include "tt_file.hpp"
#include "tm_timer.hpp"
#include "sys_utils.hpp"
#include "unicode_text.hpp"

#include <harfbuzz/hb-ft.h>
#include <harfbuzz/hb-ot.h>
#include <vector>
#include <filesystem>
#include <limits>
#include <map>
#include <stdexcept>

FONT_RESOURCE_CODE(tt_face);

/******************************************************************************
* Utilities
******************************************************************************/

inline int tt_round (int l) { return ((l+0x400020) >> 6) - 0x10000; }
inline SI tt_si (int l) { return l<<2; }

inline FT_UInt
decode_index (FT_Face face, int i) {
  if (i < 0xc000000) return ft_get_char_index (face, i);
  return i - 0xc000000;  
}

/******************************************************************************
* Freetype faces
******************************************************************************/

namespace {
std::shared_ptr<const std::vector<FT_Byte>> font_file_bytes (const std::string& path) {
  using bytes= std::shared_ptr<const std::vector<FT_Byte>>;
  auto& cache= font_domain_local<std::map<std::string, bytes>> ();
  const auto at= cache.find (path);
  if (at != cache.end ()) return at->second;
  auto close= [] (FILE* file) { if (file) texmacs_fclose (file); };
  std::unique_ptr<FILE, decltype (close)> file (
    texmacs_fopen (string (path.data (), path.size ()), "rb"), close);
  if (!file) return {};
  const auto size= texmacs_fsize (file.get ());
  if (size <= 0 || static_cast<std::uintmax_t> (size) >
      static_cast<std::uintmax_t> (std::numeric_limits<FT_Long>::max ())) return {};
  auto data= std::make_shared<std::vector<FT_Byte>> (size);
  if (texmacs_fread (reinterpret_cast<char*> (data->data ()), size, file.get ()) != size)
    return {};
  cache.emplace (path, data);
  return data;
}
}

tt_face_rep::tt_face_rep (string name): rep<tt_face> (name) {
  url u= tt_font_find (name);
  if (is_none (u)) return;
  const string file= concretize (u);
  source.file_utf8.assign (file.data (), N(file));
  open_file (false);
}

tt_face_rep::tt_face_rep (string name, const athena::text::font_file_source& file):
  rep<tt_face> (name), source (file) { open_file (true); }

void tt_face_rep::open_file (bool unicode_only) {
  if (ft_initialize ()) return;
  font_data= font_file_bytes (source.file_utf8);
  if (!font_data) {
    debug_fonts << "Can't read font " << res_name << LF;
    return;
  }
  if (ft_new_memory_face (current_ft_library (), font_data->data (),
      font_data->size (), source.face_index, &ft_face)) {
    debug_fonts << "Can't load font " << res_name << LF;
    return;
  }
  if (unicode_only) {
    if (ft_face->face_index != source.face_index) return;
    if (ft_select_charmap (ft_face, FT_ENCODING_UNICODE)) return;
  }
  else ft_select_charmap (ft_face, ft_encoding_adobe_custom);
  bad_face= false;
}

tt_face_rep::~tt_face_rep () {
  if (ft_face) ft_done_face (ft_face);
}

tt_face
load_tt_face (string name) {
  bench_start ("load tt face");
  tt_face face= make (tt_face, name, tm_new<tt_face_rep> (name));
  bench_cumul ("load tt face");
  return face;
}

tt_face load_tt_face (const athena::text::font_file_source& source) {
  athena::text::require_utf8 (source.file_utf8);
  if (source.file_utf8.empty () || source.file_utf8.find ('\0') != std::string::npos ||
      source.file_utf8.size () > static_cast<std::size_t> (MAX_INT - 64) ||
      !std::filesystem::u8path (source.file_utf8).is_absolute () ||
      source.face_index < 0 || source.face_index > 0x7fffffffL)
    throw std::invalid_argument ("Invalid physical font source");
  const string name= "file-face:" * as_string (source.face_index) * ":" *
    string (source.file_utf8.data (), source.file_utf8.size ());
  return make (tt_face, name, tm_new<tt_face_rep> (name, source));
}

int
tt_math_vertical_variant (string family, unsigned int codepoint,
                          unsigned int variant) {
  tt_face face= load_tt_face (family);
  if (face->bad_face) return 0;
  FT_UInt glyph= ft_get_char_index (face->ft_face, codepoint);
  if (glyph == 0) return 0;

  hb_font_t* hb_font= hb_ft_font_create_referenced (face->ft_face);
  unsigned int count= 0;
  unsigned int total= hb_ot_math_get_glyph_variants (
    hb_font, glyph, HB_DIRECTION_TTB, 0, &count, nullptr);
  if (variant >= total) {
    hb_font_destroy (hb_font);
    return 0;
  }

  std::vector<hb_ot_math_glyph_variant_t> variants (total);
  count= total;
  hb_ot_math_get_glyph_variants (
    hb_font, glyph, HB_DIRECTION_TTB, 0, &count, variants.data ());
  hb_font_destroy (hb_font);
  if (variant >= count || variants[variant].glyph == glyph) return 0;
  return (int) variants[variant].glyph;
}

array<int>
tt_math_vertical_variants (string family, unsigned int codepoint) {
  array<int> out;
  tt_face face= load_tt_face (family);
  if (face->bad_face) return out;
  FT_UInt glyph= ft_get_char_index (face->ft_face, codepoint);
  if (glyph == 0) return out;

  hb_font_t* hb_font= hb_ft_font_create_referenced (face->ft_face);
  unsigned int count= 0;
  unsigned int total= hb_ot_math_get_glyph_variants (
    hb_font, glyph, HB_DIRECTION_TTB, 0, &count, nullptr);
  if (total <= 1) {
    hb_font_destroy (hb_font);
    return out;
  }
  std::vector<hb_ot_math_glyph_variant_t> variants (total);
  count= total;
  hb_ot_math_get_glyph_variants (
    hb_font, glyph, HB_DIRECTION_TTB, 0, &count, variants.data ());
  hb_font_destroy (hb_font);
  for (unsigned int i=1; i<count; ++i)
    out << (variants[i].glyph == glyph ? 0 : (int) variants[i].glyph);
  return out;
}

/******************************************************************************
* Font metrics
******************************************************************************/


tt_font_metric_rep::tt_font_metric_rep (
  string name, string family, int size2, int hdpi2, int vdpi2):
  tt_font_metric_rep (name, load_tt_face (family), size2, hdpi2, vdpi2) {}

tt_font_metric_rep::tt_font_metric_rep (
  string name, tt_face source, int size2, int hdpi2, int vdpi2):
  font_metric_rep (name), face (source), size (size2), hdpi (hdpi2), vdpi (vdpi2), fnm (NULL)
{
  bad_font_metric= face->bad_face ||
    ft_set_char_size (face->ft_face, 0, size<<6, hdpi, vdpi);
  if (bad_font_metric) return;

}

bool
tt_font_metric_rep::exists (int i) {
  if (face->bad_face) return false;
  if (fnm->contains (i)) return true;
  FT_UInt glyph_index= decode_index (face->ft_face, i);
  return glyph_index != 0;
}

metric&
tt_font_metric_rep::get (int i) {
  if (bad_font_metric) return font_error_metric ();
  if (!face->bad_face && !fnm->contains(i)) {
    ft_set_char_size (face->ft_face, 0, size<<6, hdpi, vdpi);
    FT_UInt glyph_index= decode_index (face->ft_face, i);
    if (ft_load_glyph (face->ft_face, glyph_index, FT_LOAD_DEFAULT))
      return font_error_metric ();
    FT_GlyphSlot slot= face->ft_face->glyph;
    if (ft_render_glyph (slot, ft_render_mode_mono)) return font_error_metric ();
    metric_struct* M= tm_new<metric_struct> ();
    fnm(i)= (pointer) M;
    int w= slot->bitmap.width;
    int h= slot->bitmap.rows;
    SI ww= w * PIXEL;
    SI hh= h * PIXEL;
    SI xw= tt_si (slot->metrics.width);
    SI xh= tt_si (slot->metrics.height);
    SI dx= tt_si (slot->metrics.horiBearingX);
    SI dy= tt_si (slot->metrics.horiBearingY);
    SI ll= tt_si (slot->metrics.horiAdvance);
    (void) xw;
    M->x1= 0;
    M->y1= dy - xh;
    M->x2= ll;
    M->y2= dy;
    M->x3= dx;
    M->y3= dy - hh;
    M->x4= dx + ww;
    M->y4= dy;
    //cout << "Glyph " << i << " of " << res_name << "\n";
    //cout << "Logical : " << M->x1/PIXEL << ", " << M->y1/PIXEL
    //     << "; " << M->x2/PIXEL << ", " << M->y2/PIXEL << "\n";
    //cout << "Physical: " << M->x3/PIXEL << ", " << M->y3/PIXEL
    //     << "; " << M->x4/PIXEL << ", " << M->y4/PIXEL << "\n";
  }
  return *((metric*) ((void*) fnm [i]));
}

SI
tt_font_metric_rep::kerning (int left, int right) {
  if (face->bad_face || !FT_HAS_KERNING (face->ft_face)) return 0;
  FT_Vector k;
  FT_UInt l= decode_index (face->ft_face, left);
  FT_UInt r= decode_index (face->ft_face, right);
  ft_set_char_size (face->ft_face, 0, size<<6, hdpi, vdpi);
  if (ft_get_kerning (face->ft_face, l, r, FT_KERNING_DEFAULT, &k)) return 0;
  return tt_si (k.x);
}

font_metric
tt_font_metric (string family, int size, int hdpi, int vdpi) {
  string name= family * as_string (size) * "@" * as_string (hdpi);
  if (vdpi != hdpi) name << "x" << as_string (vdpi);
  return make (font_metric, name,
	       tm_new<tt_font_metric_rep> (name, family, size, hdpi, vdpi));
}

font_metric tt_font_metric (tt_face face, int size, int hdpi, int vdpi) {
  const string name= face->res_name * ":metric:" * as_string (size) * ":" *
    as_string (hdpi) * ":" * as_string (vdpi);
  return make (font_metric, name, tm_new<tt_font_metric_rep> (name, face, size, hdpi, vdpi));
}

/******************************************************************************
* Font glyphs
******************************************************************************/


tt_font_glyphs_rep::tt_font_glyphs_rep (
  string name, string family, int size2, int hdpi2, int vdpi2):
  tt_font_glyphs_rep (name, load_tt_face (family), size2, hdpi2, vdpi2) {}

tt_font_glyphs_rep::tt_font_glyphs_rep (
  string name, tt_face source, int size2, int hdpi2, int vdpi2):
  font_glyphs_rep (name), face (source), size (size2),
  hdpi (hdpi2), vdpi (vdpi2), fng (glyph ())
{
  bad_font_glyphs= face->bad_face ||
    ft_set_char_size (face->ft_face, 0, size<<6, hdpi, vdpi);
  if (bad_font_glyphs) return;
}

bool tt_font_glyphs_rep::physical_source (athena::text::physical_font_source& out) const {
  if (face->bad_face) return false;
  out= {face->source, size, hdpi, vdpi};
  return true;
}

glyph&
tt_font_glyphs_rep::get (int i) {
  if (bad_font_glyphs) return font_error_glyph ();
  if (!face->bad_face && !fng->contains(i)) {
    ft_set_char_size (face->ft_face, 0, size<<6, hdpi, vdpi);
    FT_UInt glyph_index= decode_index (face->ft_face, i);
    if (ft_load_glyph (face->ft_face, glyph_index, FT_LOAD_DEFAULT))
      return font_error_glyph ();
    FT_GlyphSlot slot= face->ft_face->glyph;
    if (ft_render_glyph (slot, ft_render_mode_mono)) return font_error_glyph ();

    int w= slot->bitmap.width;
    int h= slot->bitmap.rows;
    int ox= tt_round (slot->metrics.horiBearingX);
    int oy= tt_round (slot->metrics.horiBearingY);
    int pitch= slot->bitmap.pitch;
    unsigned char *buf= slot->bitmap.buffer;
    if (pitch<0) buf -= pitch*h;
    int x, y;
    glyph G (w, h, -ox, oy);
    // mg:
    // the index variable is used by code who need the glyph_index for unicode characters
    // to locate the right glyph in the font file
    G->index = (i >= 0x0c000000 || (face->ft_face->charmap &&
                face->ft_face->charmap->encoding == FT_ENCODING_UNICODE)) ?
                  glyph_index : i;
    G->lwidth= (tt_si (slot->metrics.horiAdvance)+(PIXEL>>1))/PIXEL;

    for (y=0; y<h; y++) {
      for (x=0; x<w; x++) {
	unsigned char c= buf[x>>3];
	G->set_1 (x, y, (c >> (7-(x&7))) & 1);
      }
      buf += pitch;
    }
    //cout << "Glyph " << i << " of " << res_name << "\n";
    //cout << G << "\n";
    if (G->width * G->height == 0) G= font_error_glyph ();
    fng(i)= G;
  }
  return fng(i);
}

font_glyphs
tt_font_glyphs (string family, int size, int hdpi, int vdpi) {
  string name=
    family * ":" * as_string (size) * "." * as_string (hdpi);
  if (vdpi != hdpi) name << "x" << as_string (vdpi);
  name << "tt";
  return make (font_glyphs, name,
	       tm_new<tt_font_glyphs_rep> (name, family, size, hdpi, vdpi));
}

font_glyphs tt_font_glyphs (tt_face face, int size, int hdpi, int vdpi) {
  const string name= face->res_name * ":glyphs:" * as_string (size) * ":" *
    as_string (hdpi) * ":" * as_string (vdpi);
  return make (font_glyphs, name, tm_new<tt_font_glyphs_rep> (name, face, size, hdpi, vdpi));
}

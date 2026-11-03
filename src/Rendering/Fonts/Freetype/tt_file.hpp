
/******************************************************************************
* MODULE     : tt_file.hpp
* DESCRIPTION: Finding a True Type font
* COPYRIGHT  : (C) 2003  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef TT_FILE_H
#define TT_FILE_H

#include "url.hpp"
#include "bitmap_font.hpp"
#include "tree.hpp"
#include "font_source.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct tt_font_coverage_page {
  std::uint32_t first= 0;
  std::array<std::uint32_t, 8> bits {};
};

struct tt_font_catalog_record {
  athena::text::font_file_source file;
  std::vector<std::string> families;
  std::string style;
  int weight= 0;
  int width= 0;
  int slant= 0;
  int spacing= 0;
  bool scalable= true;
  bool color= false;
  std::vector<tt_font_coverage_page> coverage;
};

url    tt_font_path ();
url    tt_private_font_path ();
tree   tt_font_catalog (bool refresh= false);
std::vector<tt_font_catalog_record> tt_font_catalog_records (bool refresh= false);
string tt_font_catalog_signature ();
string tt_font_match_family (string family);
void   tt_font_cache_set_warmup_disabled (bool disabled);
void   tt_font_cache_warmup ();
void   tt_extend_font_path (url u);
bool   tt_font_exists (string name);
url    tt_font_find (string name);
string tt_find_name (string name, int size);
font_glyphs tt_font_glyphs (string family, int size, int hdpi, int vdpi);

#endif // TT_FILE_H

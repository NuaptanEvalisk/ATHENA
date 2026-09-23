/******************************************************************************
* MODULE     : bitmap_text.hpp
* DESCRIPTION: Owner-local FreeType bitmap strikes for shaped Unicode text
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include "shaped_text.hpp"
#include "Freetype/tt_face.hpp"

namespace athena::text {
bitmap_text_glyph load_bitmap_text_glyph (
  tt_face face, unsigned int glyph, int size, int hdpi, int vdpi);
}

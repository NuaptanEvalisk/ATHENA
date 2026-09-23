/******************************************************************************
* MODULE     : math_font.hpp
* DESCRIPTION: Native Unicode math font requests from document font profiles
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include "font_selection.hpp"

class font;
namespace athena::text {
math_alphabet default_math_alphabet (font source, bool variable);
physical_font_source math_font_source (font source, math_alphabet alphabet);
font_request math_font_request (font source, math_alphabet alphabet);
std::optional<math_font_metrics> math_layout_metrics (font source);
std::optional<math_stretch_result> shape_math_stretch (
  font source, std::string_view scalar, SI target_extent, bool vertical= true);
}

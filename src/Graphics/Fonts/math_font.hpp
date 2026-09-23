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
font_request math_font_request (font source, math_alphabet alphabet);
}

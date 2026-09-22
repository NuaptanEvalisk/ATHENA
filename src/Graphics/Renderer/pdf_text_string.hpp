/******************************************************************************
* MODULE     : pdf_text_string.hpp
* DESCRIPTION: Explicit UTF-8 text strings for PDF and PostScript pdfmark
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once

#include <string>
#include <string_view>

namespace athena::text {

// A complete hexadecimal PDF text-string literal, with a UTF-16BE BOM.
// Unlike the legacy converter, input is UTF-8, not Cork or named-glyph syntax.
std::string pdf_text_string (std::string_view utf8);

} // namespace athena::text

/******************************************************************************
* MODULE     : math_alphabet.hpp
* DESCRIPTION: Unicode mathematical font variants without changing source text
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once

namespace athena::text {

enum class math_alphabet {
  normal, bold, italic, bold_italic, script, bold_script, fraktur, bold_fraktur,
  double_struck, sans, bold_sans, italic_sans, bold_italic_sans, monospace
};

// Font-variant substitution is rendering data, not Unicode normalization.
// Already styled characters, unsupported alphabets and combining marks retain
// their identity. Tables come from ICU's UCD, including Letterlike Symbols holes.
char32_t math_variant_character (char32_t character, math_alphabet alphabet);
math_alphabet math_character_alphabet (char32_t character);

} // namespace athena::text

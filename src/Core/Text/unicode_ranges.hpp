/******************************************************************************
* MODULE     : unicode_ranges.hpp
* DESCRIPTION: Shared Unicode range classification
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
*******************************************************************************/

#ifndef UNICODE_RANGES_H
#define UNICODE_RANGES_H

#include <cstdint>

// Unicode 17.0 CJK unified and compatibility ideograph blocks.
inline bool
unicode_is_cjk_ideograph (std::uint32_t code) {
  return (code >= 0x3400  && code <= 0x4DBF ) ||
         (code >= 0x4E00  && code <= 0x9FFF ) ||
         (code >= 0xF900  && code <= 0xFAFF ) ||
         (code >= 0x20000 && code <= 0x2A6DF) ||
         (code >= 0x2A700 && code <= 0x2B73F) ||
         (code >= 0x2B740 && code <= 0x2B81F) ||
         (code >= 0x2B820 && code <= 0x2CEAF) ||
         (code >= 0x2CEB0 && code <= 0x2EBEF) ||
         (code >= 0x2EBF0 && code <= 0x2EE5F) ||
         (code >= 0x2F800 && code <= 0x2FA1F) ||
         (code >= 0x30000 && code <= 0x3134F) ||
         (code >= 0x31350 && code <= 0x323AF) ||
         (code >= 0x323B0 && code <= 0x3347F);
}

inline bool
unicode_is_cjk_radical_or_stroke (std::uint32_t code) {
  return (code >= 0x2E80 && code <= 0x2EFF) ||
         (code >= 0x2F00 && code <= 0x2FDF) ||
         (code >= 0x31C0 && code <= 0x31EF);
}

inline bool
unicode_is_hiragana (std::uint32_t code) {
  return (code >= 0x3040  && code <= 0x309F ) ||
         (code >= 0x1B000 && code <= 0x1B0FF);
}

inline bool
unicode_is_katakana (std::uint32_t code) {
  return (code >= 0x30A0 && code <= 0x30FF) ||
         (code >= 0x31F0 && code <= 0x31FF) ||
         (code >= 0x3200 && code <= 0x32FF);
}

inline bool
unicode_is_hangul (std::uint32_t code) {
  return (code >= 0x1100 && code <= 0x11FF) ||
         (code >= 0x3130 && code <= 0x318F) ||
         (code >= 0xA960 && code <= 0xA97F) ||
         (code >= 0xAC00 && code <= 0xD7FF);
}

inline bool
unicode_is_cjk_punctuation (std::uint32_t code) {
  return (code >= 0x3000 && code <= 0x303F) ||
         (code >= 0xFF00 && code <= 0xFFEF);
}

inline bool
unicode_is_east_asian_letter (std::uint32_t code) {
  return unicode_is_cjk_ideograph (code) ||
         unicode_is_cjk_radical_or_stroke (code) ||
         unicode_is_hiragana (code) || unicode_is_katakana (code) ||
         unicode_is_hangul (code) ||
         (code >= 0x3100 && code <= 0x312F) ||
         (code >= 0x31A0 && code <= 0x31BF);
}

#endif // defined UNICODE_RANGES_H

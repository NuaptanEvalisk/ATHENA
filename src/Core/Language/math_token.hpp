/******************************************************************************
* MODULE     : math_token.hpp
* DESCRIPTION: Shared mathematical word boundaries
* COPYRIGHT  : (C) 1999 Joris van der Hoeven, 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef MATH_TOKEN_H
#define MATH_TOKEN_H

#include "analyze.hpp"
#include "utf8_edit.hpp"

inline int
math_word_end (const string& s, int pos) {
  ASSERT (pos >= 0 && pos <= N(s), "invalid mathematical text position");
  if (pos >= N(s)) return pos;
  if (is_digit (s[pos])) {
    while (pos < N(s) && is_numeric (s[pos])) ++pos;
    while (s[pos-1] == '.') --pos;
    return utf8_grapheme_snap (s, pos, true);
  }
  if (is_alpha (s[pos])) {
    while (pos < N(s) && is_alpha (s[pos])) ++pos;
    return utf8_grapheme_snap (s, pos, true);
  }
  return utf8_grapheme_next (s, pos);
}

#endif

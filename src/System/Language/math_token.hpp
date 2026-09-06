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

inline int
math_word_end (const string& s, int pos) {
  if (pos >= N(s)) return pos;
  if (is_digit (s[pos])) {
    while (pos < N(s) && is_numeric (s[pos])) ++pos;
    while (s[pos-1] == '.') --pos;
    return pos;
  }
  if (is_alpha (s[pos])) {
    while (pos < N(s) && is_alpha (s[pos])) ++pos;
    return pos;
  }
  if (s[pos] == '<') {
    while (pos < N(s) && s[pos] != '>') ++pos;
    return pos < N(s) ? pos + 1 : pos;
  }
  return pos + 1;
}

#endif

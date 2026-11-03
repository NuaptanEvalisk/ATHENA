/******************************************************************************
* MODULE     : fuzzy_rank.hpp
* DESCRIPTION: Token-aware fuzzy ranking for short strings
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef FUZZY_RANK_H
#define FUZZY_RANK_H

#include "array.hpp"
#include "string.hpp"

struct fuzzy_rank_field {
  fuzzy_rank_field ();
  fuzzy_rank_field (string text2, int weight2);

  string text;
  int    weight;
};

struct fuzzy_rank_result {
  fuzzy_rank_result ();
  fuzzy_rank_result (bool matched2, int score2);

  bool matched;
  int  score;
};

fuzzy_rank_result fuzzy_rank (string query, array<fuzzy_rank_field> fields);

#endif // defined FUZZY_RANK_H

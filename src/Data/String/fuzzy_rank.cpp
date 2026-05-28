/******************************************************************************
* MODULE     : fuzzy_rank.cpp
* DESCRIPTION: Token-aware fuzzy ranking for short strings
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "fuzzy_rank.hpp"
#include "analyze.hpp"

fuzzy_rank_field::fuzzy_rank_field (): text (""), weight (0) {}
fuzzy_rank_field::fuzzy_rank_field (string text2, int weight2):
  text (text2), weight (weight2) {}

fuzzy_rank_result::fuzzy_rank_result (): matched (false), score (-1) {}
fuzzy_rank_result::fuzzy_rank_result (bool matched2, int score2):
  matched (matched2), score (score2) {}

static bool
fuzzy_is_separator (char c) {
  return is_space (c) || c == '-' || c == '_' || c == '/' || c == '\\' ||
         c == '.' || c == ':' || c == '(' || c == ')' || c == '[' ||
         c == ']' || c == '{' || c == '}';
}

static bool
fuzzy_word_boundary (string s, int i) {
  if (i <= 0) return true;
  if (i >= N(s)) return false;
  char prev= s[i - 1];
  char cur = s[i];
  if (fuzzy_is_separator (prev)) return true;
  return is_locase (prev) && is_upcase (cur);
}

static string
fuzzy_lower (string s) {
  string r;
  for (int i=0; i<N(s); i++) r << string ((char) locase (s[i]));
  return r;
}

static array<string>
fuzzy_tokens (string query) {
  array<string> tokens;
  string token;
  for (int i=0; i<N(query); i++) {
    if (fuzzy_is_separator (query[i])) {
      if (N(token) != 0) {
        tokens << token;
        token= "";
      }
    }
    else token << string ((char) query[i]);
  }
  if (N(token) != 0) tokens << token;
  return tokens;
}

static int
fuzzy_find_at (string text, string token, int start) {
  if (N(token) == 0) return start;
  for (int i=start; i+N(token)<=N(text); i++) {
    bool ok= true;
    for (int j=0; j<N(token); j++)
      if (text[i+j] != token[j]) {
        ok= false;
        break;
      }
    if (ok) return i;
  }
  return -1;
}

static int
fuzzy_subsequence_score (string text, string token) {
  int qi= 0;
  int first= -1;
  int last= -1;
  int boundary_bonus= 0;
  int consecutive= 0;
  int best_consecutive= 0;
  int prev= -2;

  for (int i=0; i<N(text) && qi<N(token); i++) {
    if (text[i] == token[qi]) {
      if (first < 0) {
        first= i;
        if (fuzzy_word_boundary (text, i)) boundary_bonus= 400;
      }
      if (i == prev + 1) consecutive++;
      else consecutive= 1;
      if (consecutive > best_consecutive) best_consecutive= consecutive;
      prev= i;
      last= i;
      qi++;
    }
  }
  if (qi != N(token)) return -1;

  int spread= last - first + 1;
  return 1200 + boundary_bonus + 40 * best_consecutive -
         8 * spread - 2 * first - N(text);
}

static int
fuzzy_token_score (string text, string token) {
  if (N(token) == 0) return 0;
  if (text == token) return 10000 - N(text);

  int best= -1;
  for (int pos= fuzzy_find_at (text, token, 0);
       pos >= 0;
       pos= fuzzy_find_at (text, token, pos + 1)) {
    bool boundary= fuzzy_word_boundary (text, pos);
    int score;
    if (pos == 0 && N(token) == N(text))
      score= 10000 - N(text);
    else if (boundary && N(token) + pos <= N(text))
      score= 8200 - 3 * pos - N(text);
    else
      score= 5400 - 6 * pos - N(text);
    if (score > best) best= score;
  }
  if (best >= 0) return best;

  return fuzzy_subsequence_score (text, token);
}

static int
fuzzy_field_score (string text, array<string> tokens) {
  if (N(tokens) == 0) return 0;
  int total= 0;
  for (int i=0; i<N(tokens); i++) {
    int score= fuzzy_token_score (text, tokens[i]);
    if (score < 0) return -1;
    total += score;
  }
  return total;
}

fuzzy_rank_result
fuzzy_rank (string query, array<fuzzy_rank_field> fields) {
  query= fuzzy_lower (query);
  array<string> tokens= fuzzy_tokens (query);
  if (N(tokens) == 0) return fuzzy_rank_result (true, 0);

  int best= -1;
  for (int i=0; i<N(fields); i++) {
    if (fields[i].weight <= 0) continue;
    string text= fuzzy_lower (fields[i].text);
    int score= fuzzy_field_score (text, tokens);
    if (score < 0) continue;
    score= score * fields[i].weight - N(text);
    if (score > best) best= score;
  }

  if (best < 0) return fuzzy_rank_result (false, -1);
  return fuzzy_rank_result (true, best);
}

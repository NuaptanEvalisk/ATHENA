/******************************************************************************
* MODULE     : QTMVaultSearch.cpp
* DESCRIPTION: Vault link search helpers
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMVaultSearch.hpp"
#include "fuzzy_rank.hpp"

static int
fuzzy_subsequence_score (const QString& text, const QString& query) {
  int qi= 0;
  int spread= 0;
  int first= -1;
  for (int i=0; i<text.length () && qi<query.length (); i++) {
    if (text[i] == query[qi]) {
      if (first < 0) first= i;
      spread= i - first;
      qi++;
    }
  }
  if (qi != query.length ()) return -1;
  return 50000 - (10 * spread) - text.length ();
}

int
fuzzy_score (const QString& text, const QString& query) {
  QString normalized= text.toLower ();
  if (query.isEmpty ()) return 0;
  if (normalized == query) return 100000;
  if (normalized.startsWith (query)) return 90000 - normalized.length ();
  if (normalized.contains (query)) return 80000 - normalized.length ();
  return fuzzy_subsequence_score (normalized, query);
}

int
fuzzy_file_score (const WikilinkFileEntry& file, string query) {
  array<fuzzy_rank_field> fields;
  fields << fuzzy_rank_field (file.searchStem, 100);
  fields << fuzzy_rank_field (file.searchPath, 35);
  fuzzy_rank_result result= fuzzy_rank (query, fields);
  return result.matched ? result.score : -1;
}

void
append_search_hits (std::vector<range_set>& out, tree t, tree query,
                    path base, int limit) {
  if (limit <= 0) return;
  range_set sels= search (t, query, base, limit);
  if (N(sels) > 0) out.push_back (sels);
}

void
collect_enunciation_hits (std::vector<range_set>& out, tree t, tree query,
                          const string& tag, path base, int limit) {
  if (limit <= 0 || is_atomic (t)) return;

  if (is_compound (t, tag)) {
    append_search_hits (out, t, query, base, limit);
    return;
  }

  for (int i=0; i<N(t); i++) {
    int found= 0;
    for (const range_set& sels: out) found += N(sels) / 2;
    if (found >= limit) return;
    collect_enunciation_hits (out, t[i], query, tag, base * i,
                              limit - found);
  }
}

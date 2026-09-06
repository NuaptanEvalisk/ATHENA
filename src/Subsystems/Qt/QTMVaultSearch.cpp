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
#include "math_token.hpp"
#include "QTMVaultAnchorModel.hpp"
#include "analyze.hpp"
#include "drd_mode.hpp"
#include "fuzzy_rank.hpp"
#include "qt_utilities.hpp"
#include <QFile>
#include <QRegularExpression>
#include <rapidfuzz/distance/Levenshtein.hpp>
#include <rapidfuzz/fuzz.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>

static QByteArray
longest_ascii_word (const QString& query) {
  QByteArray best;
  QByteArray current;
  for (QChar ch: query) {
    ushort code= ch.unicode ();
    bool word= (code >= '0' && code <= '9') ||
               (code >= 'A' && code <= 'Z') ||
               (code >= 'a' && code <= 'z') || code == '_';
    if (word) current.append ((char) code);
    else {
      if (current.size () > best.size ()) best= current;
      current.clear ();
    }
  }
  if (current.size () > best.size ()) best= current;
  return best;
}

VaultRawSearchPrefilter::VaultRawSearchPrefilter (
  const QString& query, bool case_insensitive, bool fuzzy)
  : caseInsensitive (case_insensitive)
{
  // Approximate structural matches need not preserve any particular query
  // token.  Parsing every file in fuzzy mode is therefore the only
  // false-negative-free policy for this raw-source prefilter.
  if (!fuzzy) needle= longest_ascii_word (query);
  if (caseInsensitive) needle= needle.toLower ();
}

bool
VaultRawSearchPrefilter::isEffective () const {
  return !needle.isEmpty ();
}

bool
VaultRawSearchPrefilter::fileMayMatch (url file) const {
  if (!isEffective ()) return true;
  QFile input (to_qstring (concretize (file)));
  if (!input.open (QIODevice::ReadOnly)) return true;
  QByteArray source= input.readAll ();
  if (input.error () != QFileDevice::NoError) return true;
  if (caseInsensitive) source= source.toLower ();
  return source.contains (needle);
}

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
list_filter_score (const QString& text, const QString& query,
                   bool caseInsensitive, bool fuzzy) {
  QString haystack= caseInsensitive ? text.toCaseFolded () : text;
  QString needle= caseInsensitive ? query.toCaseFolded () : query;
  if (needle.isEmpty ()) return 0;
  if (haystack == needle) return 100000;
  if (haystack.startsWith (needle)) return 90000 - haystack.length ();
  if (haystack.contains (needle)) return 80000 - haystack.length ();
  if (!fuzzy) return -1;
  return fuzzy_subsequence_score (haystack, needle);
}

int
fuzzy_file_score (const WikilinkFileEntry& file, string query) {
  array<fuzzy_rank_field> fields;
  fields << fuzzy_rank_field (file.searchStem, 100);
  fields << fuzzy_rank_field (file.searchPath, 35);
  fuzzy_rank_result result= fuzzy_rank (query, fields);
  return result.matched ? result.score : -1;
}

namespace {

struct FuzzyAtomicText {
  std::u32string text;
  std::vector<int> starts;
  std::vector<int> ends;
};

static void
append_normalized_character (FuzzyAtomicText& out, const QString& value,
                             int byteStart, int byteEnd,
                             bool caseInsensitive) {
  QString normalized= caseInsensitive ? value.toCaseFolded () : value;
  QList<uint> codepoints= normalized.toUcs4 ();
  for (uint codepoint: codepoints) {
    out.text.push_back ((char32_t) codepoint);
    out.starts.push_back (byteStart);
    out.ends.push_back (byteEnd);
  }
}

static FuzzyAtomicText
normalize_atomic_text (const string& source, bool caseInsensitive) {
  FuzzyAtomicText out;
  for (int start=0; start<N(source); ) {
    int end= tm_char_next (source, start);
    append_normalized_character (
      out, to_qstring (source (start, end)), start, end, caseInsensitive);
    start= end;
  }
  return out;
}

static std::u32string
normalize_query (const string& source, bool caseInsensitive) {
  return normalize_atomic_text (source, caseInsensitive).text;
}

static bool
same_atomic_path (path position, path atom) {
  return !is_nil (position) && path_up (position) == atom;
}

static std::vector<std::pair<size_t,size_t>>
unblocked_regions (const FuzzyAtomicText& text, path atom,
                   const std::vector<VaultContentMatch>& exact) {
  std::vector<std::pair<size_t,size_t>> blocked;
  for (const VaultContentMatch& match: exact) {
    if (!match.exact || !same_atomic_path (match.start, atom) ||
        !same_atomic_path (match.end, atom))
      continue;
    int byteStart= last_item (match.start);
    int byteEnd= last_item (match.end);
    size_t first= 0;
    while (first < text.ends.size () && text.ends[first] <= byteStart) first++;
    size_t last= first;
    while (last < text.starts.size () && text.starts[last] < byteEnd) last++;
    if (first < last) blocked.push_back ({first, last});
  }
  std::sort (blocked.begin (), blocked.end ());

  std::vector<std::pair<size_t,size_t>> regions;
  size_t cursor= 0;
  for (const auto& interval: blocked) {
    size_t first= std::max (cursor, interval.first);
    size_t last= std::max (first, interval.second);
    if (cursor < first) regions.push_back ({cursor, first});
    cursor= std::max (cursor, last);
  }
  if (cursor < text.text.size ()) regions.push_back ({cursor, text.text.size ()});
  return regions;
}

static void
append_fuzzy_atomic_matches (
  std::vector<VaultContentMatch>& out, const FuzzyAtomicText& text,
  const std::u32string& query, path atom, int limit, double cutoff,
  const rapidfuzz::fuzz::CachedPartialRatio<char32_t>& scorer,
  const std::vector<VaultContentMatch>& exact)
{
  if (limit <= 0 || text.text.empty ()) return;
  std::deque<std::pair<size_t,size_t>> pending;
  for (const auto& region: unblocked_regions (text, atom, exact))
    pending.push_back (region);

  while (!pending.empty () && limit > 0) {
    auto region= pending.front ();
    pending.pop_front ();
    if (region.second <= region.first) continue;

    auto first= text.text.begin () + (std::ptrdiff_t) region.first;
    auto last= text.text.begin () + (std::ptrdiff_t) region.second;
    double quick= scorer.similarity (first, last, cutoff);
    if (quick < cutoff) continue;
    rapidfuzz::ScoreAlignment<double> alignment=
      rapidfuzz::fuzz::partial_ratio_alignment (
        query.begin (), query.end (), first, last, cutoff);
    size_t alignedStart= region.first + alignment.dest_start;
    size_t radius= std::max<size_t> (
      1, (query.size () * (size_t) (100.0 - cutoff) + 99) / 100);
    size_t startFirst= alignedStart > radius ? alignedStart - radius : region.first;
    startFirst= std::max (startFirst, region.first);
    size_t startLast= std::min (region.second, alignedStart + radius + 1);
    size_t lengthFirst= query.size () > radius ? query.size () - radius : 1;
    size_t lengthLast= query.size () + radius;

    size_t matchStart= 0;
    size_t matchEnd= 0;
    double matchScore= 0.0;
    size_t bestLengthDifference= (size_t) -1;
    for (size_t candidateStart=startFirst; candidateStart<startLast;
         candidateStart++) {
      for (size_t length=lengthFirst; length<=lengthLast; length++) {
        size_t candidateEnd= candidateStart + length;
        if (candidateEnd > region.second) break;
        double score= 100.0 * rapidfuzz::levenshtein_normalized_similarity (
          query.begin (), query.end (),
          text.text.begin () + (std::ptrdiff_t) candidateStart,
          text.text.begin () + (std::ptrdiff_t) candidateEnd,
          rapidfuzz::LevenshteinWeightTable {1, 1, 1}, cutoff / 100.0);
        size_t difference= length > query.size () ? length - query.size () :
          query.size () - length;
        if (score > matchScore ||
            (score == matchScore && difference < bestLengthDifference)) {
          matchScore= score;
          matchStart= candidateStart;
          matchEnd= candidateEnd;
          bestLengthDifference= difference;
        }
      }
    }
    if (matchScore < cutoff || matchStart >= matchEnd ||
        matchEnd > text.text.size ())
      continue;

    VaultContentMatch match;
    match.start= atom * text.starts[matchStart];
    match.end= atom * text.ends[matchEnd - 1];
    match.exact= false;
    match.score= matchScore;
    out.push_back (match);
    limit--;

    if (region.first < matchStart)
      pending.push_back ({region.first, matchStart});
    if (matchEnd < region.second)
      pending.push_back ({matchEnd, region.second});
  }
}

static void
collect_fuzzy_atomic_matches (
  std::vector<VaultContentMatch>& out, tree t, path base,
  const std::u32string& query, int& remaining, double cutoff,
  bool caseInsensitive,
  const rapidfuzz::fuzz::CachedPartialRatio<char32_t>& scorer,
  const std::vector<VaultContentMatch>& exact)
{
  if (remaining <= 0) return;
  if (is_atomic (t)) {
    size_t before= out.size ();
    append_fuzzy_atomic_matches (
      out, normalize_atomic_text (t->label, caseInsensitive), query, base,
      remaining, cutoff, scorer, exact);
    remaining -= (int) (out.size () - before);
    return;
  }
  if (is_func (t, RAW_DATA)) return;
  for (int i=0; i<N(t) && remaining>0; i++)
    collect_fuzzy_atomic_matches (out, t[i], base * i, query, remaining,
                                  cutoff, caseInsensitive, scorer, exact);
}

} // namespace

static bool
math_match_boundary (tree t, path base, path position) {
  path relative= position / base;
  if (is_nil (relative)) return true;
  tree value= subtree (t, path_up (relative));
  if (!is_atomic (value)) return true;
  int wanted= last_item (relative), pos= 0;
  while (pos < wanted && pos < N(value->label))
    pos= math_word_end (value->label, pos);
  return pos == wanted;
}

void
append_content_matches (std::vector<VaultContentMatch>& out, tree t,
                        tree query, path base, int limit,
                        bool caseInsensitive, bool fuzzy) {
  if (limit <= 0) return;
  // Keep the mathematical context but select the expression inside it,
  // rather than requiring the enclosing formula to equal the whole query.
  tree pattern= query;
  if (is_compound (query, "math", 1))
    pattern= compound ("math", compound ("select-region", query[0]));
  range_set exactRanges= search (t, pattern, base, caseInsensitive, limit);
  std::vector<VaultContentMatch> exact;
  for (int i=0; i+1<N(exactRanges); i+=2) {
    if (is_compound (query, "math", 1) &&
        (!math_match_boundary (t, base, exactRanges[i]) ||
         !math_match_boundary (t, base, exactRanges[i+1]))) continue;
    VaultContentMatch match;
    match.start= exactRanges[i];
    match.end= exactRanges[i + 1];
    match.exact= true;
    match.score= 100.0;
    exact.push_back (match);
    out.push_back (match);
  }
  if (!fuzzy || !is_atomic (query) || (int) exact.size () >= limit) return;

  std::u32string normalizedQuery=
    normalize_query (query->label, caseInsensitive);
  if (normalizedQuery.size () < 4) return;
  double cutoff= normalizedQuery.size () <= 5 ? 75.0 : 80.0;
  int remaining= limit - (int) exact.size ();
  rapidfuzz::fuzz::CachedPartialRatio<char32_t> scorer (normalizedQuery);
  collect_fuzzy_atomic_matches (out, t, base, normalizedQuery, remaining,
                                cutoff, caseInsensitive, scorer, exact);
}

void
collect_enunciation_matches (std::vector<VaultContentMatch>& out, tree t,
                             tree query, const string& tag, path base,
                             int limit, bool caseInsensitive, bool fuzzy) {
  if (limit <= 0 || is_atomic (t)) return;
  if (is_compound (t, tag)) {
    append_content_matches (out, t, query, base, limit,
                            caseInsensitive, fuzzy);
    return;
  }
  size_t initialSize= out.size ();
  for (int i=0; i<N(t); i++) {
    int found= (int) (out.size () - initialSize);
    int remaining= limit - found;
    if (remaining <= 0) return;
    collect_enunciation_matches (out, t[i], query, tag, base * i, remaining,
                                  caseInsensitive, fuzzy);
  }
}

namespace {

static tree
subtree_at_path (tree t, path where) {
  while (!is_nil (where)) {
    int index= where->item;
    if (is_atomic (t) || index < 0 || index >= N(t)) return tree ();
    t= t[index];
    where= where->next;
  }
  return t;
}

static void
append_atomic_descendant_matches (
  std::vector<VaultContentMatch>& out, tree t, tree query, path base,
  int& remaining, bool caseInsensitive, bool fuzzy)
{
  if (remaining <= 0) return;
  if (is_atomic (t)) {
    size_t before= out.size ();
    append_content_matches (out, t, query, base, remaining,
                            caseInsensitive, fuzzy);
    remaining -= (int) (out.size () - before);
    return;
  }
  if (is_func (t, RAW_DATA)) return;
  for (int i=0; i<N(t) && remaining>0; i++)
    append_atomic_descendant_matches (out, t[i], query, base * i, remaining,
                                      caseInsensitive, fuzzy);
}

static bool
same_match_range (const VaultContentMatch& a, const VaultContentMatch& b) {
  return a.start == b.start && a.end == b.end;
}

static bool
path_starts_with (path value, path prefix) {
  if (is_nil (prefix)) return true;
  if (is_nil (value) || value->item != prefix->item) return false;
  return path_starts_with (value->next, prefix->next);
}

static bool
formatting_wrapper (tree t) {
  return is_compound (t, "with") || is_compound (t, "style-with");
}

static bool
bold_wrapper (tree t) {
  if (is_compound (t, "strong")) return N(t) >= 1;
  if (!formatting_wrapper (t) || N(t) < 3) return false;
  for (int i=0; i+1<N(t)-1; i+=2)
    if (is_atomic (t[i]) && is_atomic (t[i+1]) &&
        (t[i]->label == "font-series" || t[i]->label == "fontseries") &&
        (t[i+1]->label == "bold" || t[i+1]->label == "bold-series"))
      return true;
  return false;
}

static bool
enunciation_node (tree t) {
  if (!is_compound (t)) return false;
  string tag= as_string (L(t));
  for (const WikilinkEnunciationFilterEntry& entry:
       wikilink_enunciation_filters)
    if (normalized_enunciation_tag (to_qstring (tag)) ==
        normalized_enunciation_tag (entry.tag))
      return true;
  return false;
}

static bool
heading_node (tree t) {
  return is_compound (t, "section") || is_compound (t, "section*") ||
         is_compound (t, "subsection") || is_compound (t, "subsection*") ||
         is_compound (t, "subsubsection") ||
         is_compound (t, "subsubsection*") ||
         is_compound (t, "paragraph") || is_compound (t, "paragraph*") ||
         is_compound (t, "subparagraph") ||
         is_compound (t, "subparagraph*");
}

static bool
tree_has_visible_text (tree t) {
  if (is_atomic (t)) return !to_qstring (t->label).trimmed ().isEmpty ();
  if (is_func (t, LABEL) || is_func (t, RAW_DATA)) return false;
  for (int i=0; i<N(t); i++)
    if (tree_has_visible_text (t[i])) return true;
  return false;
}

static QString
visible_tree_text (tree t) {
  if (is_atomic (t)) return to_qstring (t->label);
  if (is_func (t, LABEL) || is_func (t, RAW_DATA)) return QString ();
  QStringList parts;
  for (int i=0; i<N(t); i++) {
    QString part= visible_tree_text (t[i]).trimmed ();
    if (!part.isEmpty ()) parts << part;
  }
  return parts.join (" ").simplified ();
}

static bool
leading_bold_scope (tree t, path base, path& scope, tree& titleTree) {
  if (is_atomic (t)) return false;
  if (bold_wrapper (t)) {
    scope= base;
    titleTree= t;
    return true;
  }
  if (formatting_wrapper (t) && N(t) >= 1)
    return leading_bold_scope (t[N(t)-1], base * (N(t)-1), scope,
                               titleTree);
  for (int i=0; i<N(t); i++) {
    if (!tree_has_visible_text (t[i])) continue;
    return leading_bold_scope (t[i], base * i, scope, titleTree);
  }
  return false;
}

static bool
preferred_anchor_title (tree t, QString& title) {
  if (!is_func (t, LABEL, 1)) return false;
  QString anchor= to_qstring (tree_as_string (t[0])).trimmed ();
  QRegularExpressionMatch heading=
    QRegularExpression ("^H[1-6]\\s+(.+)$").match (anchor);
  if (heading.hasMatch ()) {
    title= heading.captured (1).trimmed ();
    return true;
  }
  if (!is_upper_anchor (anchor)) return false;
  QString tag= anchor_pair_tag (anchor);
  for (const WikilinkEnunciationFilterEntry& entry:
       wikilink_enunciation_filters)
    if (tag == normalized_enunciation_tag (entry.tag)) {
      QString key= clean_anchor_display (anchor);
      int colon= key.indexOf (':');
      title= (colon < 0 ? key : key.mid (colon + 1)).trimmed ();
      return !title.isEmpty ();
    }
  return false;
}

struct PreferredSearchScope {
  path where;
  QString title;
};

static int
rapidfuzz_title_score (QString title, QString query, bool caseInsensitive) {
  title= title.simplified ();
  query= query.simplified ();
  if (caseInsensitive) {
    title= title.toCaseFolded ();
    query= query.toCaseFolded ();
  }
  if (title.isEmpty () || query.isEmpty ()) return -1;
  if (title == query) return 100000;
  std::u32string titleText= title.toStdU32String ();
  std::u32string queryText= query.toStdU32String ();
  return (int) std::lround (
    1000.0 * rapidfuzz::fuzz::WRatio (titleText, queryText));
}

static void
collect_preferred_scopes (tree t, path base,
                          std::vector<PreferredSearchScope>& scopes) {
  if (is_atomic (t)) return;
  QString anchorTitle;
  if (preferred_anchor_title (t, anchorTitle))
    scopes.push_back ({base, anchorTitle});
  if (heading_node (t))
    scopes.push_back ({base, visible_tree_text (t)});
  if (enunciation_node (t)) {
    path title;
    tree titleTree;
    if (leading_bold_scope (t, base, title, titleTree))
      scopes.push_back ({title, visible_tree_text (titleTree)});
  }
  for (int i=0; i<N(t); i++)
    collect_preferred_scopes (t[i], base * i, scopes);
}

} // namespace

void
append_heading_matches (std::vector<VaultContentMatch>& out, tree t,
                        tree query, path base, int limit,
                        bool caseInsensitive, bool fuzzy) {
  if (limit <= 0) return;
  std::vector<TransclusionAnchorPair> headings=
    collect_heading_anchor_targets (t, base);
  std::vector<VaultContentMatch> additions;
  int remaining= limit;
  for (const TransclusionAnchorPair& heading: headings) {
    if (remaining <= 0) break;
    path relative= heading.lowerWhere;
    for (int i=0; i<N(base) && !is_nil (relative); i++)
      relative= relative->next;
    tree node= subtree_at_path (t, relative);
    append_atomic_descendant_matches (additions, node, query,
                                      heading.lowerWhere, remaining,
                                      caseInsensitive, fuzzy);
  }
  for (const VaultContentMatch& addition: additions) {
    bool duplicate= false;
    for (const VaultContentMatch& current: out)
      if (same_match_range (current, addition)) {
        duplicate= true;
        break;
      }
    if (!duplicate) out.push_back (addition);
  }
}

void
score_search_match_titles (
  tree t, tree query, path base, std::vector<VaultContentMatch>& matches,
  bool caseInsensitive, bool) {
  std::vector<PreferredSearchScope> scopes;
  collect_preferred_scopes (t, base, scopes);
  QString queryText= to_qstring (tree_as_string (query)).simplified ();
  for (VaultContentMatch& match: matches)
    for (const PreferredSearchScope& scope: scopes)
      if (path_starts_with (match.start, scope.where)) {
        int score= rapidfuzz_title_score (scope.title, queryText,
                                          caseInsensitive);
        match.titleMatchScore= std::max (match.titleMatchScore, score);
      }
}

bool
vault_search_match_precedes (const VaultContentMatch& a,
                             const VaultContentMatch& b) {
  if (a.titleMatchScore != b.titleMatchScore)
    return a.titleMatchScore > b.titleMatchScore;
  if (a.exact != b.exact) return a.exact;
  if (!a.exact && a.score != b.score) return a.score > b.score;
  return path_less (a.start, b.start);
}

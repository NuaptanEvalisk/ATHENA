/******************************************************************************
* MODULE     : QTMVaultAnchorModel.cpp
* DESCRIPTION: Vault anchor model helpers
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMVaultAnchorModel.hpp"
#include "convert.hpp"
#include "qt_utilities.hpp"
#include <QRegularExpression>

const std::vector<WikilinkEnunciationFilterEntry> wikilink_enunciation_filters= {
  { "Theorem", "theorem" },
  { "Proposition", "proposition" },
  { "Lemma", "lemma" },
  { "Corollary", "corollary" },
  { "Axiom", "axiom" },
  { "Definition", "definition" },
  { "Conjecture", "conjecture" },
  { "Remark", "remark" },
  { "Note", "note" },
  { "Example", "example" },
  { "Warning", "warning" },
  { "Disambiguation", "disambiguation" },
  { "Question", "question" },
  { "Solution", "solution" },
  { "Solution*", "solution*" },
  { "Proof", "proof" },
  { "Alternative proof", "proof-alternative" },
  { "Standard proof", "proof-standard" }
};

QString
clean_anchor_display (QString anchor) {
  anchor.replace ("{", "");
  anchor.replace ("}", "");
  return anchor.trimmed ();
}

bool
is_wikilink_anchor (const QString& anchor) {
  static const QRegularExpression heading ("^H[1-6]\\s+.+$");
  return anchor.contains ("{") || heading.match (anchor.trimmed ()).hasMatch ();
}

bool
is_upper_anchor (const QString& anchor) {
  return anchor.contains ("{");
}

bool
is_lower_anchor (const QString& anchor) {
  return anchor.contains ("}");
}

QString
anchor_pair_key (QString anchor) {
  anchor.replace ("{", "");
  anchor.replace ("}", "");
  return anchor.trimmed ();
}

QString
anchor_pair_tag (QString anchor) {
  QString key= anchor_pair_key (anchor).toLower ();
  int pos= key.indexOf (":");
  return pos < 0 ? key : key.left (pos);
}

QString
normalized_enunciation_tag (QString tag) {
  tag= tag.trimmed ().toLower ();
  if (tag == "solution*") return "solution";
  if (tag == "proof-alternative" || tag == "proof-standard") return "proof";
  return tag;
}

bool
anchor_pair_matches_enunciation (const TransclusionAnchorPair& pair,
                                 const QString& tag) {
  QString normalized= normalized_enunciation_tag (tag);
  if (normalized.isEmpty ()) return false;
  return anchor_pair_tag (pair.upper) == normalized;
}

bool
anchor_pair_is_enunciation (const TransclusionAnchorPair& pair) {
  QString tag= anchor_pair_tag (pair.upper);
  for (const WikilinkEnunciationFilterEntry& entry:
       wikilink_enunciation_filters)
    if (tag == normalized_enunciation_tag (entry.tag)) return true;
  return false;
}

void
collect_anchors (tree t, path base, std::vector<WikilinkAnchorEntry>& out) {
  if (is_atomic (t)) return;
  if (is_func (t, LABEL, 1)) {
    WikilinkAnchorEntry e;
    e.anchor= to_qstring (tree_as_string (t[0]));
    e.where= base;
    out.push_back (e);
  }
  for (int i=0; i<N(t); i++)
    collect_anchors (t[i], base * i, out);
}

std::vector<TransclusionAnchorPair>
collect_transclusion_pairs (const std::vector<WikilinkAnchorEntry>& anchors) {
  std::vector<TransclusionAnchorPair> pairs;
  for (int i=0; i<(int) anchors.size (); i++) {
    if (!is_upper_anchor (anchors[i].anchor)) continue;
    QString key= anchor_pair_key (anchors[i].anchor);
    if (key.isEmpty ()) continue;
    for (int j=i+1; j<(int) anchors.size (); j++) {
      if (!is_lower_anchor (anchors[j].anchor)) continue;
      if (anchor_pair_key (anchors[j].anchor) != key) continue;
      if (!path_less (anchors[i].where, anchors[j].where)) continue;
      TransclusionAnchorPair pair;
      pair.upper= anchors[i].anchor;
      pair.lower= anchors[j].anchor;
      pair.upperWhere= anchors[i].where;
      pair.lowerWhere= anchors[j].where;
      pair.upperIndex= i;
      pair.lowerIndex= j;
      pairs.push_back (pair);
      break;
    }
  }
  return pairs;
}

namespace {

static int
heading_anchor_level (const QString& anchor) {
  static const QRegularExpression heading ("^H([1-6])\\s+.+$");
  QRegularExpressionMatch match= heading.match (anchor.trimmed ());
  return match.hasMatch () ? match.captured (1).toInt () : 0;
}

static int
heading_node_level (tree t) {
  if (is_compound (t, "section") || is_compound (t, "section*")) return 1;
  if (is_compound (t, "subsection") || is_compound (t, "subsection*"))
    return 2;
  if (is_compound (t, "subsubsection") ||
      is_compound (t, "subsubsection*"))
    return 3;
  if (is_compound (t, "paragraph") || is_compound (t, "paragraph*"))
    return 4;
  if (is_compound (t, "subparagraph") ||
      is_compound (t, "subparagraph*"))
    return 5;
  return 0;
}

static bool
empty_document_child (tree t) {
  return is_atomic (t) && to_qstring (t->label).trimmed ().isEmpty ();
}

static void
collect_heading_anchor_targets_impl (
  tree t, path base, std::vector<TransclusionAnchorPair>& out)
{
  if (is_atomic (t)) return;
  if (is_func (t, DOCUMENT)) {
    for (int i=0; i<N(t); i++) {
      if (!is_func (t[i], LABEL, 1)) continue;
      QString anchor= to_qstring (tree_as_string (t[i][0]));
      int level= heading_anchor_level (anchor);
      if (level == 0) continue;
      int headingIndex= i + 1;
      while (headingIndex < N(t) && empty_document_child (t[headingIndex]))
        headingIndex++;
      if (headingIndex >= N(t) ||
          heading_node_level (t[headingIndex]) != level)
        continue;
      TransclusionAnchorPair target;
      target.upper= anchor;
      target.lower= anchor;
      target.upperWhere= base * i;
      target.lowerWhere= base * headingIndex;
      target.upperIndex= -1;
      target.lowerIndex= -1;
      out.push_back (target);
    }
  }
  for (int i=0; i<N(t); i++)
    collect_heading_anchor_targets_impl (t[i], base * i, out);
}

static bool
path_starts_with (path value, path prefix) {
  if (is_nil (prefix)) return true;
  if (is_nil (value) || value->item != prefix->item) return false;
  return path_starts_with (value->next, prefix->next);
}

} // namespace

std::vector<TransclusionAnchorPair>
collect_heading_anchor_targets (tree body, path base) {
  std::vector<TransclusionAnchorPair> targets;
  collect_heading_anchor_targets_impl (body, base, targets);
  return targets;
}

int
enclosing_anchor_pair_index (
  const std::vector<TransclusionAnchorPair>& pairs, path where) {
  int best= -1;
  for (int i=0; i<(int) pairs.size (); i++) {
    if (!path_less (pairs[i].upperWhere, where) ||
        !path_less (where, pairs[i].lowerWhere))
      continue;
    if (best < 0 || path_less (pairs[best].upperWhere, pairs[i].upperWhere))
      best= i;
  }
  return best;
}

int
heading_anchor_target_index (
  const std::vector<TransclusionAnchorPair>& targets, path where) {
  for (int i=0; i<(int) targets.size (); i++)
    if (path_starts_with (where, targets[i].lowerWhere)) return i;
  return -1;
}

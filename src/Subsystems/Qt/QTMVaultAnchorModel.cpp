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
  return anchor.contains ("{");
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

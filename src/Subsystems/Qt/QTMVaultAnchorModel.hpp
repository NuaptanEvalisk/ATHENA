/******************************************************************************
* MODULE     : QTMVaultAnchorModel.hpp
* DESCRIPTION: Vault anchor model helpers
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMVAULTANCHORMODEL_HPP
#define QTMVAULTANCHORMODEL_HPP

#include "path.hpp"
#include "tree.hpp"
#include <QString>
#include <vector>

struct WikilinkAnchorEntry {
  QString anchor;
  path    where;
};

struct TransclusionAnchorPair {
  QString upper;
  QString lower;
  path    upperWhere;
  path    lowerWhere;
  int     upperIndex;
  int     lowerIndex;
};

struct WikilinkEnunciationFilterEntry {
  const char* label;
  const char* tag;
};

extern const std::vector<WikilinkEnunciationFilterEntry> wikilink_enunciation_filters;

QString clean_anchor_display (QString anchor);
bool is_wikilink_anchor (const QString& anchor);
bool is_upper_anchor (const QString& anchor);
bool is_lower_anchor (const QString& anchor);
QString anchor_pair_key (QString anchor);
QString anchor_pair_tag (QString anchor);
QString normalized_enunciation_tag (QString tag);
bool anchor_pair_matches_enunciation (const TransclusionAnchorPair& pair,
                                       const QString& tag);
bool anchor_pair_is_enunciation (const TransclusionAnchorPair& pair);
void collect_anchors (tree t, path base, std::vector<WikilinkAnchorEntry>& out);
std::vector<TransclusionAnchorPair> collect_transclusion_pairs (
  const std::vector<WikilinkAnchorEntry>& anchors);
std::vector<TransclusionAnchorPair> collect_heading_anchor_targets (
  tree body, path base);
int enclosing_anchor_pair_index (
  const std::vector<TransclusionAnchorPair>& pairs, path where);
int heading_anchor_target_index (
  const std::vector<TransclusionAnchorPair>& targets, path where);

#endif // QTMVAULTANCHORMODEL_HPP

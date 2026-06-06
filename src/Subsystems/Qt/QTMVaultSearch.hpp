/******************************************************************************
* MODULE     : QTMVaultSearch.hpp
* DESCRIPTION: Vault link search helpers
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMVAULTSEARCH_HPP
#define QTMVAULTSEARCH_HPP

#include "QTMVaultLinkModel.hpp"
#include "path.hpp"
#include "tree.hpp"
#include "tree_search.hpp"
#include "url.hpp"
#include <QString>
#include <vector>

struct WikilinkSearchResult {
  QString relPath;
  url     file;
  int     occurrence;
  int     fileHits;
  path    hitStart;
  path    hitEnd;
};

struct TransclusionSearchResult {
  QString relPath;
  url     file;
  QString upper;
  QString lower;
  path    upperWhere;
  path    lowerWhere;
  int     occurrence;
  int     fileHits;
};

int fuzzy_score (const QString& text, const QString& query);
int fuzzy_file_score (const WikilinkFileEntry& file, string query);
void append_search_hits (std::vector<range_set>& out, tree t, tree query,
                         path base, int limit);
void collect_enunciation_hits (std::vector<range_set>& out, tree t, tree query,
                               const string& tag, path base, int limit);

#endif // QTMVAULTSEARCH_HPP

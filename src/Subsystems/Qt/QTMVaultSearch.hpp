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
#include <QByteArray>
#include <QString>
#include <vector>

struct WikilinkSearchResult {
  QString relPath;
  url     file;
  int     occurrence;
  int     fileHits;
  path    hitStart;
  path    hitEnd;
  bool    exact;
  double  score;
  int     titleMatchScore= -1;
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
  bool    exact;
  double  score;
  int     titleMatchScore= -1;
};

struct VaultContentMatch {
  path   start;
  path   end;
  bool   exact;
  double score;
  int    titleMatchScore= -1;
};

class VaultRawSearchPrefilter {
  QByteArray needle;
  bool       caseInsensitive;

public:
  VaultRawSearchPrefilter (const QString& query, bool case_insensitive,
                           bool fuzzy);

  bool isEffective () const;
  bool fileMayMatch (url file) const;
  bool fileMayMatch (const QString& file) const;
};

int fuzzy_score (const QString& text, const QString& query);
int list_filter_score (const QString& text, const QString& query,
                       bool case_insensitive, bool fuzzy);
int fuzzy_file_score (const WikilinkFileEntry& file, string query);
void append_content_matches (std::vector<VaultContentMatch>& out, tree t,
                             tree query, path base, int limit,
                             bool case_insensitive, bool fuzzy);
void collect_enunciation_matches (std::vector<VaultContentMatch>& out, tree t,
                                   tree query, const string& tag, path base,
                                   int limit, bool case_insensitive,
                                   bool fuzzy);
void append_heading_matches (std::vector<VaultContentMatch>& out, tree t,
                             tree query, path base, int limit,
                             bool case_insensitive, bool fuzzy);
void score_search_match_titles (tree t, tree query, path base,
                                std::vector<VaultContentMatch>& matches,
                                bool case_insensitive, bool fuzzy);
bool vault_search_match_precedes (const VaultContentMatch& a,
                                  const VaultContentMatch& b);

#endif // QTMVAULTSEARCH_HPP

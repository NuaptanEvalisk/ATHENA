/******************************************************************************
* MODULE     : QTMGlobalSearch.hpp
* DESCRIPTION: Qt global search pane for ATHENA vault files
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMGLOBALSEARCH_HPP
#define QTMGLOBALSEARCH_HPP

#include "path.hpp"
#include "string.hpp"
#include "tree.hpp"
#include "url.hpp"
#include "widget.hpp"

#include <QSize>
#include <QString>
#include <QWidget>
#include <vector>

class QLabel;
class QListWidget;
class QPushButton;
class QProgressBar;
class QTimer;

class QTMGlobalSearch : public QWidget {
public:
  QTMGlobalSearch (QWidget* parent = nullptr);
  ~QTMGlobalSearch ();

  QSize sizeHint () const override;

private:
  struct Result {
    QString relPath;
    url     file;
    int     hits;
    path    firstHit;
  };

  QWidget* createQueryWidget ();
  tree     currentQuery () const;
  tree     normalizeQuery (tree t) const;
  void     startSearch ();
  void     cancelSearch ();
  void     scanChunk ();
  bool     searchFile (url u, Result& result) const;
  QString  relativePath (url u) const;
  void     addResult (const Result& result);
  void     openCurrentResult ();
  void     setIdleStatus ();
  void     setRunningStatus ();
  void     finishSearch ();

  widget              queryWidget;
  url                 queryUrl;
  tree                queryTree;
  std::vector<url>    scanFiles;
  int                 scanIndex;
  std::vector<Result> results;

  QLabel*       prompt;
  QLabel*       status;
  QPushButton*  searchButton;
  QPushButton*  cancelButton;
  QProgressBar* progress;
  QListWidget*  resultList;
  QTimer*       scanTimer;
};

void global_search_show ();

#endif // QTMGLOBALSEARCH_HPP

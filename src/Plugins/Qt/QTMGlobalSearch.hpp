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
class QEvent;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QSizeGrip;
class QProgressBar;
class QSplitter;
class QTimer;

class QTMGlobalSearch : public QWidget {
public:
  QTMGlobalSearch (QWidget* parent = nullptr);
  ~QTMGlobalSearch ();

  QSize sizeHint () const override;
  bool eventFilter (QObject* watched, QEvent* event) override;
  void setPreviewZoomFactor (double zoom);
  void setFloatingResizeGripVisible (bool visible);

private:
  struct Result {
    QString relPath;
    url     file;
    int     occurrence;
    int     fileHits;
    path    hitStart;
    path    hitEnd;
  };

  QWidget* createQueryWidget ();
  QWidget* createPreviewWidget ();
  tree     currentQuery () const;
  tree     normalizeQuery (tree t) const;
  void     startSearch ();
  void     cancelSearch ();
  void     scanChunk ();
  int      searchFile (url u, std::vector<Result>& hits) const;
  QString  relativePath (url u) const;
  tree     buildPreview (const Result& result) const;
  tree     buildPreviewFromBody (tree body, path hitStart) const;
  void     addResult (const Result& result);
  void     updatePreview (QListWidgetItem* current);
  void     clearPreview ();
  void     openResult (QListWidgetItem* item);
  void     openCurrentResult ();
  void     applyPreviewZoom ();
  void     setIdleStatus ();
  void     setRunningStatus ();
  void     finishSearch ();

  widget              queryWidget;
  widget              previewWidget;
  url                 queryUrl;
  url                 previewUrl;
  tree                queryTree;
  std::vector<url>    scanFiles;
  int                 scanIndex;
  int                 matchedFiles;
  double              previewZoomFactor;
  std::vector<Result> results;

  QLabel*       prompt;
  QLabel*       status;
  QLabel*       previewTitle;
  QPushButton*  searchButton;
  QPushButton*  cancelButton;
  QSizeGrip*    floatingSizeGrip;
  QProgressBar* progress;
  QListWidget*  resultList;
  QSplitter*    splitter;
  QTimer*       scanTimer;
};

void global_search_show ();

#endif // QTMGLOBALSEARCH_HPP

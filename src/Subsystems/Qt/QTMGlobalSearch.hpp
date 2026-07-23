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
class QCheckBox;
class QComboBox;
class QListWidget;
class QListWidgetItem;
class QLineEdit;
class QPushButton;
class QSizeGrip;
class QProgressBar;
class QStringListModel;
class QSplitter;
class QTimer;
class QTMWidget;

class QTMGlobalSearch : public QWidget {
public:
  QTMGlobalSearch (QWidget* parent = nullptr);
  ~QTMGlobalSearch ();

  QSize sizeHint () const override;
  bool eventFilter (QObject* watched, QEvent* event) override;
  void setPreviewZoomFactor (double zoom);
  void setFloatingResizeGripVisible (bool visible);
  void refreshNamespaces ();
  void refreshSearchOptions ();
  void focusQueryEditor ();

private:
  struct Result {
    QString relPath;
    url     file;
    int     occurrence;
    int     fileHits;
    path    hitStart;
    path    hitEnd;
    bool    exact;
    double  score;
  };

  QWidget* createQueryWidget ();
  QWidget* createPreviewWidget ();
  void     destroyPreviewWidget ();
  void     recreatePreviewWidget ();
  tree     currentQuery () const;
  tree     normalizeQuery (tree t) const;
  QString  selectedNamespace () const;
  QString  selectedEnunciation () const;
  QString  selectedPerson () const;
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
  void     installPreviewEventFilter (QWidget* root);
  void     showFallbackPreview ();
  bool     isPreviewWatchedObject (QObject* watched) const;
  void     refreshPreviewLayout ();
  void     refreshPreviewLayoutNow ();
  SI       currentPreviewWidth () const;
  double   currentPreviewZoom () const;
  void     setIdleStatus ();
  void     setRunningStatus ();
  void     finishSearch ();

  widget              queryWidget;
  widget              previewWidget;
  url                 queryUrl;
  tree                queryTree;
  tree                previewBody;
  std::vector<url>    scanFiles;
  int                 scanIndex;
  int                 matchedFiles;
  double              previewZoomFactor;
  SI                  previewWidth;
  double              previewZoom;
  bool                previewRecreating;
  std::vector<Result> results;

  QLabel*       prompt;
  QLabel*       status;
  QLabel*       previewTitle;
  QComboBox*    enunciationCombo;
  QComboBox*    personCombo;
  QCheckBox*    caseInsensitiveCheck;
  QCheckBox*    fuzzyCheck;
  QLineEdit*    namespaceEdit;
  QStringListModel* namespaceModel;
  QPushButton*  searchButton;
  QPushButton*  cancelButton;
  QSizeGrip*    floatingSizeGrip;
  QProgressBar* progress;
  QListWidget*  resultList;
  QTMWidget*    queryTexmacsWidget;
  QWidget*      previewHostWidget;
  QWidget*      previewQtWidget;
  QTMWidget*    previewTexmacsWidget;
  QSplitter*    splitter;
  QTimer*       scanTimer;
};

void global_search_show ();

#endif // QTMGLOBALSEARCH_HPP

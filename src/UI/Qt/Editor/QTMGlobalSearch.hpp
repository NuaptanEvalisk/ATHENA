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
#include <QPointer>
#include <QString>
#include <QWidget>
#include <cstdint>
#include <memory>
#include <vector>

struct VaultSearchControl;
struct VaultSearchOptions;

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
    bool    semantic= false;
    bool    occurrenceAvailable= true;
    double  semanticScore= 0;
    double  relevance= 0;
    QString sourceUuid;
    QString identityKey;
  };

  QWidget* createQueryWidget ();
  QWidget* createPreviewWidget ();
  void     destroyPreviewWidget ();
  void     recreatePreviewWidget ();
  tree     normalizeQuery (tree t) const;
  QString  selectedNamespace () const;
  QString  selectedEnunciation () const;
  void     startSearch ();
  void     cancelSearch ();
  static int searchFile (tree body, url u, const tree& query,
                         const VaultSearchOptions& options,
                         std::vector<Result>& hits);
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
  tree                previewBody;
  std::vector<url>    scanFiles;
  std::shared_ptr<VaultSearchControl> searchTask;
  std::uint64_t       searchGeneration;
  std::uint64_t       previewGeneration= 0;
  QPointer<QObject>   previewRequest;
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
  QComboBox*    modeCombo;
  QCheckBox*    caseInsensitiveCheck;
  QCheckBox*    fuzzyCheck;
  QLineEdit*    namespaceEdit;
  QStringListModel* namespaceModel;
  QPushButton*  searchButton;
  QPushButton*  cancelButton;
  QPushButton*  openButton;
  QSizeGrip*    floatingSizeGrip;
  QProgressBar* progress;
  QListWidget*  resultList;
  QTMWidget*    queryTexmacsWidget;
  QWidget*      previewHostWidget;
  QWidget*      previewQtWidget;
  QTMWidget*    previewTexmacsWidget;
  QSplitter*    splitter;
};

void global_search_show ();
void global_search_show_with_zoom (double previewZoom);

#endif // QTMGLOBALSEARCH_HPP

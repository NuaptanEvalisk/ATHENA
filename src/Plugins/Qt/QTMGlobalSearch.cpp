/******************************************************************************
* MODULE     : QTMGlobalSearch.cpp
* DESCRIPTION: Qt global search pane for ATHENA vault files
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMGlobalSearch.hpp"
#include "QTMMainTabWindow.hpp"
#include "convert.hpp"
#include "drd_mode.hpp"
#include "message.hpp"
#include "new_buffer.hpp"
#include "qt_utilities.hpp"
#include "qt_widget.hpp"
#include "renderer.hpp"
#include "scheme.hpp"
#include "link.hpp"
#include "tm_buffer.hpp"
#include "tm_window.hpp"
#include "tree_search.hpp"
#include "vault.hpp"

#include <DockAreaWidget.h>
#include <DockSplitter.h>
#include <DockWidget.h>
#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSize>
#include <QSizeGrip>
#include <QSizePolicy>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <iostream>

static QTMGlobalSearch* global_search_widget= nullptr;
static ads::CDockWidget* global_search_dock= nullptr;

static QString
qstring_from_tm (string s) {
  return to_qstring (s);
}

static void
set_global_search_area_height (ads::CDockWidget* dock) {
  if (dock == nullptr || dock->isInFloatingContainer ()) return;
  ads::CDockAreaWidget* area= dock->dockAreaWidget ();
  if (area == nullptr || dock->dockContainer () == nullptr) return;
  ads::CDockSplitter* splitter= area->parentSplitter ();
  if (splitter == nullptr) return;
  QList<int> sizes= splitter->sizes ();
  if (sizes.size () < 2) return;
  int total= 0;
  for (int size : sizes) total += size;
  if (total <= 0) return;
  int target= qBound (440, total / 2, 720);
  int areaIndex= splitter->indexOf (area);
  if (areaIndex < 0 || areaIndex >= sizes.size ()) return;
  int delta= target - sizes[areaIndex];
  if (delta == 0) return;
  sizes[areaIndex]= target;
  int other= areaIndex == 0 ? 1 : 0;
  sizes[other]= qMax (180, sizes[other] - delta);
  splitter->setSizes (sizes);
}

QTMGlobalSearch::QTMGlobalSearch (QWidget* parent)
  : QWidget (parent),
    queryUrl (url ("tmfs://aux/global-search")),
    previewUrl (url ("tmfs://aux/global-search-preview")),
    scanIndex (0),
    matchedFiles (0),
    previewZoomFactor (1.0) {
  prompt= new QLabel ("Search the current vault", this);
  status= new QLabel (this);
  floatingSizeGrip= new QSizeGrip (this);

  QWidget* query= createQueryWidget ();
  query->setMinimumHeight (88);
  setFocusProxy (query);

  searchButton= new QPushButton ("Search", this);
  cancelButton= new QPushButton ("Cancel", this);
  cancelButton->setEnabled (false);

  progress= new QProgressBar (this);
  progress->setRange (0, 1);
  progress->setValue (0);

  resultList= new QListWidget (this);
  resultList->setAlternatingRowColors (true);
  resultList->setMinimumWidth (480);
  resultList->setMinimumHeight (320);
  resultList->installEventFilter (this);

  previewTitle= new QLabel ("Select a result to preview it.", this);
  QWidget* preview= createPreviewWidget ();
  preview->setMinimumHeight (220);

  QWidget* leftPane= new QWidget (this);
  leftPane->setMinimumWidth (500);
  QVBoxLayout* leftLayout= new QVBoxLayout (leftPane);
  leftLayout->setContentsMargins (0, 0, 0, 0);
  leftLayout->addWidget (resultList, 1);

  QWidget* rightPane= new QWidget (this);
  QVBoxLayout* rightLayout= new QVBoxLayout (rightPane);
  rightLayout->setContentsMargins (0, 0, 0, 0);
  rightLayout->addWidget (previewTitle);
  rightLayout->addWidget (preview, 1);

  splitter= new QSplitter (Qt::Horizontal, this);
  splitter->addWidget (leftPane);
  splitter->addWidget (rightPane);
  splitter->setStretchFactor (0, 0);
  splitter->setStretchFactor (1, 1);
  splitter->setSizes (QList<int> () << 520 << 820);

  scanTimer= new QTimer (this);
  scanTimer->setInterval (0);

  QHBoxLayout* buttons= new QHBoxLayout ();
  buttons->addWidget (searchButton);
  buttons->addWidget (cancelButton);
  buttons->addStretch ();

  floatingSizeGrip->hide ();
  QHBoxLayout* gripRow= new QHBoxLayout ();
  gripRow->setContentsMargins (0, 0, 0, 0);
  gripRow->addStretch ();
  gripRow->addWidget (floatingSizeGrip, 0, Qt::AlignRight | Qt::AlignBottom);

  QVBoxLayout* layout= new QVBoxLayout (this);
  layout->setContentsMargins (8, 8, 8, 8);
  layout->addWidget (prompt);
  layout->addWidget (query);
  layout->addLayout (buttons);
  layout->addWidget (progress);
  layout->addWidget (status);
  layout->addWidget (splitter, 1);
  layout->addLayout (gripRow);

  connect (searchButton, &QPushButton::clicked,
           this, [this] () { startSearch (); });
  connect (cancelButton, &QPushButton::clicked,
           this, [this] () { cancelSearch (); });
  connect (scanTimer, &QTimer::timeout,
           this, [this] () { scanChunk (); });
  connect (resultList, &QListWidget::itemDoubleClicked,
           this, [this] (QListWidgetItem* item) { openResult (item); });
  connect (resultList, &QListWidget::itemActivated,
           this, [this] (QListWidgetItem* item) { openResult (item); });
  connect (resultList, &QListWidget::currentItemChanged,
           this, [this] (QListWidgetItem* current, QListWidgetItem*) {
             updatePreview (current);
           });

  setIdleStatus ();
}

QTMGlobalSearch::~QTMGlobalSearch () {
  scanTimer->stop ();
  if (!is_nil (queryWidget)) send_destroy (queryWidget);
  if (!is_nil (previewWidget)) send_destroy (previewWidget);
}

QSize
QTMGlobalSearch::sizeHint () const {
  return QSize (1360, 720);
}

void
QTMGlobalSearch::setPreviewZoomFactor (double zoom) {
  if (zoom >= 0.04 && zoom <= 25.0)
    previewZoomFactor= zoom;
  applyPreviewZoom ();
}

void
QTMGlobalSearch::setFloatingResizeGripVisible (bool visible) {
  floatingSizeGrip->setVisible (visible);
}

bool
QTMGlobalSearch::eventFilter (QObject* watched, QEvent* event) {
  if (watched == resultList && event->type () == QEvent::KeyPress) {
    QKeyEvent* key= static_cast<QKeyEvent*> (event);
    if (key->key () == Qt::Key_Return || key->key () == Qt::Key_Enter) {
      openCurrentResult ();
      return true;
    }
  }
  return QWidget::eventFilter (watched, event);
}

QWidget*
QTMGlobalSearch::createQueryWidget () {
  tree doc (DOCUMENT, "");
  tree style= compound ("style", tuple ("generic"));
  queryWidget= texmacs_input_widget (doc, style, queryUrl);
  QWidget* qwid= concrete (queryWidget)->as_qwidget (this);
  qwid->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Fixed);
  return qwid;
}

QWidget*
QTMGlobalSearch::createPreviewWidget () {
  tree doc (DOCUMENT, "");
  tree style= compound ("style", tuple ("generic"));
  previewWidget= texmacs_input_widget (doc, style, previewUrl);
  tm_buffer buf= concrete_buffer (previewUrl);
  if (!is_nil (buf)) buf->buf->read_only= true;
  QWidget* qwid= concrete (previewWidget)->as_qwidget (this);
  qwid->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Expanding);
  applyPreviewZoom ();
  return qwid;
}

void
QTMGlobalSearch::applyPreviewZoom () {
  if (is_nil (previewWidget)) return;
  set_zoom_factor (previewWidget, get_retina_zoom () * previewZoomFactor);
}

tree
QTMGlobalSearch::normalizeQuery (tree t) const {
  while (is_func (t, DOCUMENT, 1) ||
         is_func (t, INACTIVE, 1) ||
         is_func (t, VAR_INACTIVE, 1))
    t= t[0];
  if (is_func (t, WITH) && N(t) > 0)
    t= t[N(t) - 1];
  return t;
}

tree
QTMGlobalSearch::currentQuery () const {
  if (!contains (queryUrl, get_all_buffers ())) return tree ("");
  return normalizeQuery (get_buffer_body (queryUrl));
}

QString
QTMGlobalSearch::relativePath (url u) const {
  url rel= delta (vault_get_root () * url (""), u);
  return qstring_from_tm (as_unix_string (rel));
}

void
QTMGlobalSearch::setIdleStatus () {
  status->setText ("Enter a query and search the current vault.");
  progress->setRange (0, 1);
  progress->setValue (0);
}

void
QTMGlobalSearch::setRunningStatus () {
  status->setText (QString ("Searching %1 vault files...")
                   .arg ((int) scanFiles.size ()));
  progress->setRange (0, (int) scanFiles.size ());
  progress->setValue (0);
}

void
QTMGlobalSearch::startSearch () {
  std::cout << "Global search: Search button clicked\n";
  if (!vault_active ()) {
    QMessageBox::warning (this, "Global search",
                          "No active vault. Please load a vault first.");
    return;
  }

  if (scanTimer->isActive ()) scanTimer->stop ();
  resultList->clear ();
  clearPreview ();
  results.clear ();
  scanFiles.clear ();
  scanIndex= 0;
  matchedFiles= 0;

  queryTree= currentQuery ();
  if (is_empty (queryTree)) {
    status->setText ("Enter a non-empty query.");
    progress->setRange (0, 1);
    progress->setValue (0);
    return;
  }

  array<url> files= vault_get_all_files ();
  for (int i=0; i<N(files); i++)
    if (suffix (files[i]) == "ath")
      scanFiles.push_back (files[i]);

  std::sort (scanFiles.begin (), scanFiles.end (),
             [this] (const url& a, const url& b) {
               return relativePath (a) < relativePath (b);
             });

  std::cout << "Global search: deferred scan starting, "
            << (int) scanFiles.size () << " files\n";
  setRunningStatus ();
  searchButton->setEnabled (false);
  cancelButton->setEnabled (true);
  scanTimer->start ();
}

void
QTMGlobalSearch::cancelSearch () {
  if (!scanTimer->isActive ()) return;
  scanTimer->stop ();
  status->setText (QString ("Search cancelled after %1/%2 files; %3 occurrence(s) in %4 file(s).")
                   .arg (scanIndex)
                   .arg ((int) scanFiles.size ())
                   .arg ((int) results.size ())
                   .arg (matchedFiles));
  searchButton->setEnabled (true);
  cancelButton->setEnabled (false);
}

int
QTMGlobalSearch::searchFile (url u, std::vector<Result>& hits) const {
  try {
    tree t= import_tree (u, "texmacs");
    tree body= extract (t, "body");
    if (is_empty (body)) body= t;

    int oldMode= set_access_mode (DRD_ACCESS_SOURCE);
    range_set sels;
    try {
      sels= search (body, queryTree, path (), 200);
    }
    catch (...) {
      set_access_mode (oldMode);
      throw;
    }
    set_access_mode (oldMode);

    int hitCount= N(sels) / 2;
    if (hitCount <= 0) return 0;

    QString rel= relativePath (u);
    for (int i=0; i<hitCount; i++) {
      Result result;
      result.relPath= rel;
      result.file= u;
      result.occurrence= i + 1;
      result.fileHits= hitCount;
      result.hitStart= sels[2*i];
      result.hitEnd= sels[2*i + 1];
      hits.push_back (result);
    }
    return hitCount;
  }
  catch (...) {
    std::cout << "Global search: skipped "
              << to_qstring (concretize (u)).toStdString () << "\n";
    return 0;
  }
}

tree
QTMGlobalSearch::buildPreviewFromBody (tree body, path hitStart) const {
  if (is_empty (body)) return tree (DOCUMENT, "");
  if (!is_func (body, DOCUMENT)) {
    tree preview (DOCUMENT);
    preview << compound ("marked", copy (body));
    return preview;
  }

  if (N(body) == 0) return tree (DOCUMENT, "");

  int top= 0;
  if (!is_nil (hitStart)) top= hitStart->item;
  if (top < 0 || top >= N(body)) top= 0;

  int first= std::max (0, top - 2);
  int last = std::min (N(body), top + 3);
  tree preview (DOCUMENT);
  for (int i= first; i<last; i++) {
    tree block= copy (body[i]);
    if (i == top) block= compound ("marked", block);
    preview << block;
  }
  return preview;
}

tree
QTMGlobalSearch::buildPreview (const Result& result) const {
  try {
    tree t= import_tree (result.file, "texmacs");
    tree body= extract (t, "body");
    if (is_empty (body)) body= t;
    return buildPreviewFromBody (body, result.hitStart);
  }
  catch (...) {
    return tree (DOCUMENT, "Preview unavailable.");
  }
}

void
QTMGlobalSearch::addResult (const Result& result) {
  results.push_back (result);
  QListWidgetItem* item= new QListWidgetItem (
    QString ("%1 (%2)")
      .arg (result.relPath)
      .arg (result.occurrence));
  item->setToolTip (
    QString ("%1\nOccurrence %2 of %3\nHit path: %4")
      .arg (result.relPath)
      .arg (result.occurrence)
      .arg (result.fileHits)
      .arg (qstring_from_tm (as_string (result.hitStart))));
  item->setData (Qt::UserRole, (int) results.size () - 1);
  resultList->addItem (item);
  if (resultList->count () == 1) resultList->setCurrentRow (0);
}

void
QTMGlobalSearch::updatePreview (QListWidgetItem* current) {
  if (current == nullptr) {
    clearPreview ();
    return;
  }

  int index= current->data (Qt::UserRole).toInt ();
  if (index < 0 || index >= (int) results.size ()) {
    clearPreview ();
    return;
  }

  const Result& result= results[index];
  previewTitle->setText (
    QString ("%1  (%2 of %3)")
      .arg (result.relPath)
      .arg (result.occurrence)
      .arg (result.fileHits));
  set_buffer_body (previewUrl, buildPreview (result));
  tm_buffer buf= concrete_buffer (previewUrl);
  if (!is_nil (buf)) buf->buf->read_only= true;
  applyPreviewZoom ();
}

void
QTMGlobalSearch::clearPreview () {
  if (previewTitle != nullptr)
    previewTitle->setText ("Select a result to preview it.");
  set_buffer_body (previewUrl, tree (DOCUMENT, ""));
}

void
QTMGlobalSearch::scanChunk () {
  const int chunkSize= 8;
  int end= std::min (scanIndex + chunkSize, (int) scanFiles.size ());
  for (; scanIndex < end; scanIndex++) {
    std::vector<Result> hits;
    if (searchFile (scanFiles[scanIndex], hits) > 0) {
      matchedFiles++;
      for (const Result& result : hits)
        addResult (result);
    }
  }

  progress->setValue (scanIndex);
  status->setText (
    QString ("Searching %1/%2 files; %3 occurrence(s) in %4 file(s).")
      .arg (scanIndex)
      .arg ((int) scanFiles.size ())
      .arg ((int) results.size ())
      .arg (matchedFiles));

  if (scanIndex >= (int) scanFiles.size ())
    finishSearch ();
}

void
QTMGlobalSearch::finishSearch () {
  scanTimer->stop ();
  searchButton->setEnabled (true);
  cancelButton->setEnabled (false);
  status->setText (
    QString ("%1 occurrence(s) in %2 file(s), out of %3 scanned files.")
      .arg ((int) results.size ())
      .arg (matchedFiles)
      .arg ((int) scanFiles.size ()));
  std::cout << "Global search: deferred scan finished with "
            << (int) results.size () << " occurrences in "
            << matchedFiles << " files\n";
}

void
QTMGlobalSearch::openResult (QListWidgetItem* item) {
  if (item == nullptr) return;
  int index= item->data (Qt::UserRole).toInt ();
  if (index < 0 || index >= (int) results.size ()) return;

  const Result result= results[index];
  array<object> cmd;
  cmd << symbol_object ("global-search-open-occurrence");
  cmd << object (result.file);
  cmd << list_object (symbol_object ("quote"), object (result.hitStart));
  cmd << list_object (symbol_object ("quote"), object (result.hitEnd));
  exec_delayed (scheme_cmd (as_list_object (cmd)));
}

void
QTMGlobalSearch::openCurrentResult () {
  QListWidgetItem* item= resultList->currentItem ();
  if (item == nullptr && resultList->count () > 0) item= resultList->item (0);
  openResult (item);
}

void
global_search_show () {
  if (!vault_active ()) {
    QMessageBox::warning (QApplication::activeWindow (), "Global search",
                          "No active vault. Please load a vault first.");
    return;
  }

  QTMMainTabWindow* win= QTMMainTabWindow::topTabWindow ();
  if (win == nullptr || win->dockManager () == nullptr) {
    QMessageBox::warning (QApplication::activeWindow (), "Global search",
                          "No active ATHENA window.");
    return;
  }

  if (global_search_widget == nullptr) {
    global_search_widget= new QTMGlobalSearch ();
    global_search_widget->resize (1360, 720);
    QObject::connect (global_search_widget, &QObject::destroyed, [] () {
      global_search_widget= nullptr;
      global_search_dock= nullptr;
    });
  }
  global_search_widget->setPreviewZoomFactor (
    get_server ()->get_window_zoom_factor ());

  QString title= "Global search";
  if (global_search_dock == nullptr) {
    global_search_dock= new ads::CDockWidget (title);
    global_search_dock->setObjectName ("athena-global-search");
    global_search_dock->resize (1360, 720);
    global_search_dock->setWidget (global_search_widget);
    global_search_dock->setFeature (
      ads::CDockWidget::DockWidgetDeleteOnClose, false);
    QTMGlobalSearch* pane= global_search_widget;
    ads::CDockWidget* dock= global_search_dock;
    QObject::connect (dock, &ads::CDockWidget::topLevelChanged,
                      pane, [pane, dock] (bool) {
                        pane->setFloatingResizeGripVisible (
                          dock->isInFloatingContainer ());
                      });
    QObject::connect (global_search_dock, &QObject::destroyed, [] () {
      global_search_dock= nullptr;
    });
  }

  if (global_search_dock->dockAreaWidget () == nullptr ||
      global_search_dock->dockContainer () == nullptr) {
    win->dockManager ()->addDockWidget (ads::BottomDockWidgetArea,
                                        global_search_dock);
  }

  global_search_dock->setWindowTitle (title);
  global_search_widget->setFloatingResizeGripVisible (
    global_search_dock->isInFloatingContainer ());
  global_search_dock->show ();
  global_search_dock->raise ();
  set_global_search_area_height (global_search_dock);
  QTimer::singleShot (0, win, [] () {
    set_global_search_area_height (global_search_dock);
  });
  global_search_widget->setFocus ();
}

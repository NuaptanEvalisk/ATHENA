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
#include "new_buffer.hpp"
#include "qt_utilities.hpp"
#include "qt_widget.hpp"
#include "scheme.hpp"
#include "tm_window.hpp"
#include "tree_search.hpp"
#include "vault.hpp"

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
#include <QSizePolicy>
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

QTMGlobalSearch::QTMGlobalSearch (QWidget* parent)
  : QWidget (parent),
    queryUrl (url ("tmfs://aux/global-search")),
    scanIndex (0) {
  prompt= new QLabel ("Search the current vault", this);
  status= new QLabel (this);

  QWidget* query= createQueryWidget ();
  query->setMinimumHeight (120);
  setFocusProxy (query);

  searchButton= new QPushButton ("Search", this);
  cancelButton= new QPushButton ("Cancel", this);
  cancelButton->setEnabled (false);

  progress= new QProgressBar (this);
  progress->setRange (0, 1);
  progress->setValue (0);

  resultList= new QListWidget (this);
  resultList->setAlternatingRowColors (true);
  resultList->installEventFilter (this);

  scanTimer= new QTimer (this);
  scanTimer->setInterval (0);

  QHBoxLayout* buttons= new QHBoxLayout ();
  buttons->addWidget (searchButton);
  buttons->addWidget (cancelButton);
  buttons->addStretch ();

  QVBoxLayout* layout= new QVBoxLayout (this);
  layout->setContentsMargins (8, 8, 8, 8);
  layout->addWidget (prompt);
  layout->addWidget (query);
  layout->addLayout (buttons);
  layout->addWidget (progress);
  layout->addWidget (status);
  layout->addWidget (resultList, 1);

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

  setIdleStatus ();
}

QTMGlobalSearch::~QTMGlobalSearch () {
  scanTimer->stop ();
  if (!is_nil (queryWidget)) send_destroy (queryWidget);
}

QSize
QTMGlobalSearch::sizeHint () const {
  return QSize (1100, 560);
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
  results.clear ();
  scanFiles.clear ();
  scanIndex= 0;

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
  status->setText (QString ("Search cancelled after %1/%2 files; %3 matching files.")
                   .arg (scanIndex)
                   .arg ((int) scanFiles.size ())
                   .arg ((int) results.size ()));
  searchButton->setEnabled (true);
  cancelButton->setEnabled (false);
}

bool
QTMGlobalSearch::searchFile (url u, Result& result) const {
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

    int hits= N(sels) / 2;
    if (hits <= 0) return false;

    result.relPath= relativePath (u);
    result.file= u;
    result.hits= hits;
    result.firstHit= sels[0];
    return true;
  }
  catch (...) {
    std::cout << "Global search: skipped "
              << to_qstring (concretize (u)).toStdString () << "\n";
    return false;
  }
}

void
QTMGlobalSearch::addResult (const Result& result) {
  results.push_back (result);
  QListWidgetItem* item= new QListWidgetItem (
    QString ("%1    %2 hit%3")
      .arg (result.relPath)
      .arg (result.hits)
      .arg (result.hits == 1 ? "" : "s"));
  item->setData (Qt::UserRole, (int) results.size () - 1);
  resultList->addItem (item);
  if (resultList->count () == 1) resultList->setCurrentRow (0);
}

void
QTMGlobalSearch::scanChunk () {
  const int chunkSize= 8;
  int end= std::min (scanIndex + chunkSize, (int) scanFiles.size ());
  for (; scanIndex < end; scanIndex++) {
    Result result;
    if (searchFile (scanFiles[scanIndex], result))
      addResult (result);
  }

  progress->setValue (scanIndex);
  status->setText (
    QString ("Searching %1/%2 files; %3 matching files.")
      .arg (scanIndex)
      .arg ((int) scanFiles.size ())
      .arg ((int) results.size ()));

  if (scanIndex >= (int) scanFiles.size ())
    finishSearch ();
}

void
QTMGlobalSearch::finishSearch () {
  scanTimer->stop ();
  searchButton->setEnabled (true);
  cancelButton->setEnabled (false);
  status->setText (
    QString ("%1 matching files out of %2.")
      .arg ((int) results.size ())
      .arg ((int) scanFiles.size ()));
  std::cout << "Global search: deferred scan finished with "
            << (int) results.size () << " result files\n";
}

void
QTMGlobalSearch::openResult (QListWidgetItem* item) {
  if (item == nullptr) return;
  int index= item->data (Qt::UserRole).toInt ();
  if (index < 0 || index >= (int) results.size ()) return;

  const Result result= results[index];
  exec_delayed (scheme_cmd (
    list_object (symbol_object ("global-search-open-result"),
                 object (result.file),
                 list_object (symbol_object ("quote"),
                              object (result.firstHit)))));
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
    QObject::connect (global_search_widget, &QObject::destroyed, [] () {
      global_search_widget= nullptr;
      global_search_dock= nullptr;
    });
  }

  QString title= "Global search";
  if (global_search_dock == nullptr) {
    global_search_dock= new ads::CDockWidget (title);
    global_search_dock->resize (1100, 560);
    global_search_dock->setWidget (global_search_widget);
    global_search_dock->setFeature (
      ads::CDockWidget::DockWidgetDeleteOnClose, false);
    QObject::connect (global_search_dock, &QObject::destroyed, [] () {
      global_search_dock= nullptr;
    });
    win->dockManager ()->addDockWidget (
      ads::BottomDockWidgetArea, global_search_dock);
  }

  global_search_dock->setWindowTitle (title);
  global_search_dock->show ();
  global_search_dock->raise ();
  global_search_widget->setFocus ();
}

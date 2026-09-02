/******************************************************************************
* MODULE     : QTMNeighborhoodsPane.cpp
* DESCRIPTION: Native ADS pane for ATHENA document neighborhoods
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMNeighborhoodsPane.hpp"

#include "QTMMainTabWindow.hpp"
#include "QTMToast.hpp"
#include "new_buffer.hpp"
#include "qt_utilities.hpp"
#include "scheme.hpp"
#include "vault.hpp"

#include <DockManager.h>
#include <DockWidget.h>
#include <QAbstractItemView>
#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QFont>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

static QTMNeighborhoodsPane* neighborhoods_pane_widget= nullptr;
static ads::CDockWidget* neighborhoods_pane_dock= nullptr;

enum NeighborhoodRoles {
  FilePathRole= Qt::UserRole,
  RowKeyRole
};

static QString
qs (string s) {
  return QString::fromUtf8 (as_charp (s), N(s));
}

static string
tm_string (const QString& s) {
  return from_qstring (s);
}

static void
neighborhoods_load_file (url file) {
  if (is_none (file)) return;
  exec_delayed (scheme_cmd (list_object (symbol_object ("load-buffer"),
                            object (file))));
  QTimer::singleShot (120, [] () { neighborhoods_pane_refresh (); });
}

QTMNeighborhoodsPane::QTMNeighborhoodsPane (QWidget* parent)
  : QWidget (parent),
    table (new QTableWidget (this)),
    message (new QLabel (this)),
    timer (new QTimer (this)),
    refreshing (false) {
  buildUi ();
  timer->setInterval (900);
  connect (timer, &QTimer::timeout, this, [this] () {
    refreshFromCurrentBuffer ();
  });
  timer->start ();
  refreshFromCurrentBuffer (true);
}

QSize
QTMNeighborhoodsPane::sizeHint () const {
  return QSize (760, 260);
}

void
QTMNeighborhoodsPane::buildUi () {
  QVBoxLayout* layout= new QVBoxLayout (this);
  layout->setContentsMargins (0, 0, 0, 0);
  layout->setSpacing (0);

  QToolBar* toolbar= new QToolBar (this);
  toolbar->setIconSize (QSize (16, 16));
  QAction* refreshAction= toolbar->addAction (
    QApplication::style ()->standardIcon (QStyle::SP_BrowserReload),
    "Refresh", this, [this] () { refreshFromCurrentBuffer (true); });
  refreshAction->setToolTip ("Refresh neighborhoods");
  layout->addWidget (toolbar);

  message->setWordWrap (true);
  message->setMargin (10);
  message->hide ();
  layout->addWidget (message);

  table->setEditTriggers (QAbstractItemView::NoEditTriggers);
  table->setSelectionBehavior (QAbstractItemView::SelectRows);
  table->setSelectionMode (QAbstractItemView::SingleSelection);
  table->setAlternatingRowColors (true);
  table->setShowGrid (true);
  table->horizontalHeader ()->hide ();
  table->verticalHeader ()->hide ();
  table->horizontalHeader ()->setSectionResizeMode (
    QHeaderView::ResizeToContents);
  table->verticalHeader ()->setSectionResizeMode (QHeaderView::ResizeToContents);
  connect (table, &QTableWidget::itemDoubleClicked, this,
           [this] (QTableWidgetItem* item) { activateItem (item); });
  connect (table, &QTableWidget::itemSelectionChanged, this,
           [this] () { selectionChanged (); });
  layout->addWidget (table, 1);
}

void
QTMNeighborhoodsPane::refreshFromCurrentBuffer (bool force) {
  url current= get_current_buffer_safe ();
  QString currentSignature= qs (as_string (current));
  if (!force && currentSignature == lastCurrentPath) return;

  athena_neighborhood_set set= athena_neighborhoods_for_file (current);
  lastCurrentPath= currentSignature;
  targetBuffer= current;
  renderSet (set);
}

void
QTMNeighborhoodsPane::renderSet (const athena_neighborhood_set& set) {
  refreshing= true;
  table->clear ();
  table->setRowCount (0);
  table->setColumnCount (0);

  if (!set.valid) {
    table->hide ();
    message->setText (set.error == ""
      ? "Select a vault .ath document to view neighborhoods."
      : qs (set.error));
    message->show ();
    refreshing= false;
    return;
  }

  message->hide ();
  table->show ();

  int maxLeft= 0;
  int maxRight= 0;
  for (const athena_neighborhood_row& row: set.rows) {
    maxLeft= std::max (maxLeft, row.current_index);
    maxRight= std::max (maxRight,
                        (int) row.files.size () - row.current_index - 1);
  }
  int currentColumn= 1 + maxLeft;
  int columns= 1 + maxLeft + 1 + maxRight;
  table->setRowCount ((int) set.rows.size ());
  table->setColumnCount (columns);

  QBrush currentBrush (QColor (116, 214, 185, 90));
  QBrush selectedBrush (QColor (116, 214, 185, 55));
  QBrush rowHeaderBrush (QColor (245, 242, 250));

  for (int r=0; r<(int) set.rows.size (); r++) {
    const athena_neighborhood_row& row= set.rows[r];
    QTableWidgetItem* nameItem= new QTableWidgetItem (qs (row.name));
    nameItem->setData (RowKeyRole, qs (row.key));
    nameItem->setFlags (Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    nameItem->setBackground (r == set.selected_row ? selectedBrush
                                                   : rowHeaderBrush);
    QFont font= nameItem->font ();
    font.setBold (r == set.selected_row);
    nameItem->setFont (font);
    if (row.warning != "") nameItem->setToolTip (qs (row.warning));
    table->setItem (r, 0, nameItem);

    int startColumn= currentColumn - row.current_index;
    for (int i=0; i<(int) row.files.size (); i++) {
      int c= startColumn + i;
      if (c < 1 || c >= columns) continue;
      const athena_neighborhood_entry& entry= row.files[i];
      QTableWidgetItem* item= new QTableWidgetItem (qs (entry.display));
      item->setData (FilePathRole, qs (entry.canonical_path));
      item->setData (RowKeyRole, qs (row.key));
      item->setToolTip (qs (entry.canonical_path));
      item->setFlags (Qt::ItemIsEnabled | Qt::ItemIsSelectable);
      if (c == currentColumn) {
        item->setBackground (currentBrush);
        QFont f= item->font ();
        f.setBold (true);
        item->setFont (f);
      }
      else if (r == set.selected_row) item->setBackground (selectedBrush);
      table->setItem (r, c, item);
    }
  }

  table->resizeColumnsToContents ();
  table->resizeRowsToContents ();
  if (set.selected_row >= 0)
    table->selectRow (set.selected_row);
  refreshing= false;
}

void
QTMNeighborhoodsPane::activateItem (QTableWidgetItem* item) {
  if (item == nullptr) return;
  QString path= item->data (FilePathRole).toString ();
  if (path.isEmpty ()) return;
  QString key= item->data (RowKeyRole).toString ();
  if (!key.isEmpty ()) athena_neighborhood_select (targetBuffer,
                                                   tm_string (key));
  neighborhoods_load_file (url_system (tm_string (path)));
}

void
QTMNeighborhoodsPane::selectionChanged () {
  if (refreshing) return;
  QList<QTableWidgetItem*> selection= table->selectedItems ();
  if (selection.isEmpty ()) return;

  QString key;
  for (QTableWidgetItem* item: selection) {
    key= item->data (RowKeyRole).toString ();
    if (!key.isEmpty ()) break;
  }
  if (key.isEmpty ()) return;
  if (athena_neighborhood_select (targetBuffer, tm_string (key))) {
    lastCurrentPath.clear ();
    QTimer::singleShot (0, this, [this] () {
      refreshFromCurrentBuffer (true);
    });
  }
}

void
neighborhoods_pane_show () {
  QTMMainTabWindow* win= QTMMainTabWindow::topTabWindow ();
  if (win == nullptr || win->dockManager () == nullptr) {
    QMessageBox::warning (QApplication::activeWindow (), "Neighborhoods",
                          "No active ATHENA window.");
    return;
  }

  if (neighborhoods_pane_widget == nullptr) {
    neighborhoods_pane_widget= new QTMNeighborhoodsPane ();
    QObject::connect (neighborhoods_pane_widget, &QObject::destroyed, [] () {
      neighborhoods_pane_widget= nullptr;
      neighborhoods_pane_dock= nullptr;
    });
  }

  if (neighborhoods_pane_dock == nullptr) {
    neighborhoods_pane_dock= new ads::CDockWidget ("Neighborhoods");
    neighborhoods_pane_dock->setObjectName ("athena-neighborhoods-pane");
    neighborhoods_pane_dock->resize (760, 260);
    neighborhoods_pane_dock->setWidget (neighborhoods_pane_widget);
    neighborhoods_pane_dock->setFeature (
      ads::CDockWidget::DockWidgetDeleteOnClose, false);
    QObject::connect (neighborhoods_pane_dock, &QObject::destroyed, [] () {
      neighborhoods_pane_dock= nullptr;
    });
    win->dockManager ()->addDockWidgetFloating (neighborhoods_pane_dock);
    win->scheduleAdsLayoutRestore (neighborhoods_pane_dock);
  }
  else if (neighborhoods_pane_dock->dockAreaWidget () == nullptr ||
           neighborhoods_pane_dock->dockContainer () == nullptr)
    win->dockManager ()->addDockWidgetFloating (neighborhoods_pane_dock);

  neighborhoods_pane_dock->toggleView (true);
  neighborhoods_pane_dock->show ();
  neighborhoods_pane_dock->raise ();
  neighborhoods_pane_widget->refreshFromCurrentBuffer (true);
  neighborhoods_pane_widget->setFocus ();
}

void
neighborhoods_pane_refresh () {
  if (neighborhoods_pane_widget != nullptr)
    neighborhoods_pane_widget->refreshFromCurrentBuffer (true);
}

bool
neighborhoods_open_neighbor (int direction) {
  url target;
  string message;
  if (!athena_neighborhood_current_neighbor (direction, target, message))
    return false;
  neighborhoods_load_file (target);
  return true;
}

bool
neighborhoods_cycle_selected () {
  string message;
  if (!athena_neighborhood_cycle_current (message)) return false;
  neighborhoods_pane_refresh ();
  qtm_show_toast ("", message);
  return true;
}

/******************************************************************************
* MODULE     : QTMOutlinePane.cpp
* DESCRIPTION: Live document outline pane
* COPYRIGHT  : (C) 2026  Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMOutlinePane.hpp"
#include "QTMMainTabWindow.hpp"
#include "heading_word_count.hpp"
#include "editor.hpp"
#include "qt_utilities.hpp"

#include <DockWidget.h>
#include <QApplication>
#include <QHeaderView>
#include <QMessageBox>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <algorithm>

static QTMOutlinePane* outline_pane_widget= nullptr;
static ads::CDockWidget* outline_pane_dock= nullptr;

QTMOutlinePane::QTMOutlinePane (QWidget* parent)
  : QWidget (parent), tree (new QTreeWidget (this)), timer (new QTimer (this)) {
  tree->setColumnCount (2);
  tree->setHeaderLabels (QStringList () << "Outline" << "Words");
  tree->setAlternatingRowColors (true);
  tree->setUniformRowHeights (true);
  tree->header ()->setSectionResizeMode (0, QHeaderView::Stretch);
  tree->header ()->setSectionResizeMode (1, QHeaderView::ResizeToContents);

  QVBoxLayout* layout= new QVBoxLayout (this);
  layout->setContentsMargins (0, 0, 0, 0);
  layout->addWidget (tree);

  timer->setInterval (500);
  connect (timer, &QTimer::timeout, this, [this] () { refresh (); });
  connect (tree, &QTreeWidget::itemActivated,
           this, [this] (QTreeWidgetItem* item) { activateItem (item); });
  connect (tree, &QTreeWidget::itemDoubleClicked,
           this, [this] (QTreeWidgetItem* item) { activateItem (item); });
  timer->start ();
  refresh ();
}

QSize
QTMOutlinePane::sizeHint () const {
  return QSize (320, 600);
}

void
QTMOutlinePane::refresh () {
  editor ed= get_current_editor ();
  if (is_nil (ed)) return;

  class tree doc= ed->the_buffer ();
  QString signature= to_qstring (as_string (hash (doc))) + ":" +
                     to_qstring (as_string (N(doc)));
  if (signature == lastSignature) return;
  lastSignature= signature;

  entries.clear ();
  array<heading_word_count_entry> outline_entries=
    athena_heading_word_count_entries (doc, ed->the_buffer_path ());
  for (int i=0; i<N(outline_entries); i++) {
    QTMOutlinePane::Entry entry;
    entry.level= outline_entries[i].level;
    entry.title= to_qstring (outline_entries[i].title);
    entry.words= outline_entries[i].words;
    entry.treePath= outline_entries[i].tree_path;
    entries.append (entry);
  }

  tree->clear ();
  QVector<QTreeWidgetItem*> parents;
  for (int i=0; i<entries.size (); i++) {
    const Entry& entry= entries[i];
    QTreeWidgetItem* item= new QTreeWidgetItem ();
    item->setText (0, entry.title);
    item->setText (1, entry.words > 0 ? QString::number (entry.words) : "");
    item->setData (0, Qt::UserRole, i);

    int level= std::max (0, entry.level);
    while (parents.size () > level) parents.pop_back ();

    QTreeWidgetItem* parent= nullptr;
    for (int j=parents.size () - 1; j >= 0; j--)
      if (parents[j] != nullptr) {
        parent= parents[j];
        break;
      }

    if (parent == nullptr) tree->addTopLevelItem (item);
    else parent->addChild (item);

    if (parents.size () <= level) parents.resize (level + 1);
    parents[level]= item;
  }
  tree->expandAll ();
}

void
QTMOutlinePane::activateItem (QTreeWidgetItem* item) {
  if (item == nullptr) return;
  int index= item->data (0, Qt::UserRole).toInt ();
  if (index < 0 || index >= entries.size ()) return;

  editor ed= get_current_editor ();
  if (is_nil (ed)) return;
  ed->focus_on_this_editor ();
  ed->go_to_start (entries[index].treePath);
}

void
outline_pane_show () {
  QTMMainTabWindow* win= QTMMainTabWindow::topTabWindow ();
  if (win == nullptr || win->dockManager () == nullptr) {
    QMessageBox::warning (QApplication::activeWindow (), "Outline",
                          "No active ATHENA window.");
    return;
  }

  if (outline_pane_widget == nullptr) {
    outline_pane_widget= new QTMOutlinePane ();
    QObject::connect (outline_pane_widget, &QObject::destroyed, [] () {
      outline_pane_widget= nullptr;
      outline_pane_dock= nullptr;
    });
  }

  if (outline_pane_dock == nullptr) {
    outline_pane_dock= new ads::CDockWidget ("Outline");
    outline_pane_dock->setObjectName ("athena-outline-pane");
    outline_pane_dock->resize (320, 600);
    outline_pane_dock->setWidget (outline_pane_widget);
    outline_pane_dock->setFeature (
      ads::CDockWidget::DockWidgetDeleteOnClose, false);
    QObject::connect (outline_pane_dock, &QObject::destroyed, [] () {
      outline_pane_dock= nullptr;
    });
    win->showAdsDockWidget (outline_pane_dock, ads::RightDockWidgetArea);
  }

  win->showAdsDockWidget (outline_pane_dock, ads::RightDockWidgetArea);
  outline_pane_widget->setFocus ();
}

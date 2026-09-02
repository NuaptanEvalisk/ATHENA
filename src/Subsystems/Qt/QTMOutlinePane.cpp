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
#include "buffer_actor.hpp"
#include "new_view.hpp"
#include "outline_snapshot.hpp"
#include "qt_utilities.hpp"
#include "tm_buffer.hpp"
#include "tm_window.hpp"

#include <DockWidget.h>
#include <QApplication>
#include <QHeaderView>
#include <QMessageBox>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <algorithm>
#include <cstring>
#include <limits>

static QTMOutlinePane* outline_pane_widget= nullptr;
static ads::CDockWidget* outline_pane_dock= nullptr;

QTMOutlinePane::QTMOutlinePane (QWidget* parent)
  : QWidget (parent), tree (new QTreeWidget (this)), timer (new QTimer (this)),
    lastActorId (ATHENA_NO_ACTOR), lastViewId (ATHENA_NO_VIEW),
    lastSignature (0), hasSignature (false) {
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
  tm_view view= concrete_view (get_current_view_safe ());
  if (view == nullptr || view->buf == nullptr || view->buf->actor == nullptr)
    return;

  athena_actor_id actorId= view->buf->actor->id ();
  athena_view_id viewId= view->runtime_id;
  if (actorId != lastActorId || viewId != lastViewId) {
    lastActorId= actorId;
    lastViewId= viewId;
    lastSignature= 0;
    hasSignature= false;
    entries.clear ();
    tree->clear ();
  }

  (void) buffer_actor::try_submit_coalesced_to (
    actorId, actor_command_kind::request_outline, viewId,
    lastSignature, hasSignature ? 1 : 0);
}

void
QTMOutlinePane::applySnapshot (
  athena_view_id viewId, owned_actor_blob snapshot,
  std::uint64_t signature) {
  if (!snapshot || viewId != lastViewId ||
      snapshot.size () < sizeof (actor_outline_snapshot_header))
    return;

  const std::byte* data= snapshot.data ();
  actor_outline_snapshot_header header;
  std::memcpy (&header, data, sizeof (header));
  if (header.version != ATHENA_OUTLINE_SNAPSHOT_VERSION ||
      header.signature != signature ||
      header.entry_count >
        (snapshot.size () - sizeof (header)) /
          sizeof (actor_outline_snapshot_entry))
    return;

  QVector<Entry> decoded;
  decoded.reserve (static_cast<int> (header.entry_count));
  for (std::uint32_t i= 0; i < header.entry_count; ++i) {
    actor_outline_snapshot_entry record;
    std::memcpy (
      &record,
      data + sizeof (header) +
        static_cast<std::size_t> (i) * sizeof (record),
      sizeof (record));
    std::size_t titleOffset= record.title_offset;
    std::size_t titleSize= record.title_size;
    std::size_t pathOffset= record.path_offset;
    std::size_t pathCount= record.path_count;
    if (titleOffset > snapshot.size () ||
        titleSize > snapshot.size () - titleOffset ||
        pathOffset > snapshot.size () ||
        pathCount > (snapshot.size () - pathOffset) / sizeof (std::int32_t))
      return;

    Entry entry;
    entry.level= record.level;
    entry.words= record.words;
    if (titleSize > static_cast<std::size_t> (std::numeric_limits<int>::max ()) ||
        pathCount > static_cast<std::size_t> (std::numeric_limits<int>::max ()))
      return;
    entry.title= QString::fromUtf8 (
      reinterpret_cast<const char*> (data + titleOffset),
      static_cast<int> (titleSize));
    entry.treePath.reserve (static_cast<int> (pathCount));
    for (std::size_t j= 0; j < pathCount; ++j) {
      std::int32_t item;
      std::memcpy (&item, data + pathOffset + j * sizeof (item),
                   sizeof (item));
      entry.treePath.append (static_cast<int> (item));
    }
    decoded.append (std::move (entry));
  }

  entries= std::move (decoded);
  lastSignature= signature;
  hasSignature= true;

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

  tm_view view= concrete_view (get_current_view_safe ());
  if (view == nullptr || view->runtime_id != lastViewId ||
      view->buf == nullptr || view->buf->actor == nullptr ||
      view->buf->actor->id () != lastActorId)
    return;

  const QVector<int>& path= entries[index].treePath;
  std::size_t bytes= static_cast<std::size_t> (path.size ()) *
                     sizeof (std::int32_t);
  actor_blob_reservation reservation=
    actor_blob_registry::instance ().allocate (bytes);
  for (int i= 0; i < path.size (); ++i) {
    std::int32_t value= static_cast<std::int32_t> (path[i]);
    std::memcpy (reservation.data () +
                   static_cast<std::size_t> (i) * sizeof (value),
                 &value, sizeof (value));
  }
  athena_blob_id payload= reservation.publish ();
  actor_command_ticket ticket= buffer_actor::submit_to (
    lastActorId, actor_command_kind::activate_outline_entry, lastViewId,
    payload, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER,
    static_cast<std::uint64_t> (path.size ()));
  if (!ticket) (void) actor_blob_registry::instance ().discard (payload);
}

void
outline_pane_show () {
  if (qt_defer_to_main_thread (outline_pane_show)) return;

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
  }

  win->showAdsDockWidget (outline_pane_dock, ads::RightDockWidgetArea);
  outline_pane_widget->setFocus ();
}

void
outline_pane_accept_snapshot (
  athena_view_id viewId, athena_blob_id payload,
  std::uint64_t signature) {
  owned_actor_blob snapshot= actor_blob_registry::instance ().take (payload);
  if (outline_pane_widget != nullptr)
    outline_pane_widget->applySnapshot (
      viewId, std::move (snapshot), signature);
}

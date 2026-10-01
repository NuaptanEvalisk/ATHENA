/******************************************************************************
* MODULE     : QTMDocumentHistoryPane.cpp
* DESCRIPTION: Qt document history pane
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include "QTMDocumentHistoryPane.hpp"

#include "ATHENA/Data/document_history_store.hpp"
#include "ATHENA/Data/new_buffer.hpp"
#include "QTMDocumentIdentity.hpp"
#include "QTMDocumentHistory.hpp"
#include "QTMMainTabWindow.hpp"
#include "file.hpp"
#include "qt_utilities.hpp"
#include "vault.hpp"

#include <DockWidget.h>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QHeaderView>
#include <QIcon>
#include <QMenu>
#include <QMessageBox>
#include <QSize>
#include <QStyle>
#include <QThread>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {
namespace fs= std::filesystem;

QTMDocumentHistoryPane* document_history_widget= nullptr;
ads::CDockWidget* document_history_dock= nullptr;

std::string
std_text (string value) {
  return std::string (value.data (), static_cast<std::size_t> (N(value)));
}

QIcon
history_icon (const QString& name, QStyle::StandardPixmap fallback) {
  QIcon icon= QIcon::fromTheme (name);
  if (icon.isNull ()) icon= QApplication::style ()->standardIcon (fallback);
  return icon;
}

QString
display_size (qint64 bytes) {
  if (bytes < 0) return QString ();
  const char* units[]= { "B", "KiB", "MiB", "GiB" };
  double value= static_cast<double> (bytes);
  int unit= 0;
  while (value >= 1024.0 && unit < 3) {
    value /= 1024.0;
    ++unit;
  }
  return unit == 0 ? QString ("%1 %2").arg (bytes).arg (units[unit])
                   : QString ("%1 %2").arg (value, 0, 'f', 1).arg (units[unit]);
}

struct HistoryLocation {
  fs::path root;
  std::string relative;
};

std::optional<HistoryLocation>
history_location (url document) {
  auto context= vault_capture_context ();
  if (!context || is_none (document) || is_rooted_tmfs (document) ||
      is_rooted_web (document) || is_scratch (document))
    return std::nullopt;

  std::error_code ec;
  fs::path path= fs::weakly_canonical (
    fs::path (std_text (concretize (document))), ec);
  if (ec)
    path= fs::absolute (
      fs::path (std_text (concretize (document))), ec).lexically_normal ();
  fs::path root= fs::weakly_canonical (context->root, ec);
  if (ec) root= context->root.lexically_normal ();

  fs::path relative= path.lexically_relative (root);
  if (relative.empty () || relative.is_absolute ()) return std::nullopt;
  for (const auto& part: relative)
    if (part == "..") return std::nullopt;

  std::string encoded= relative.generic_string ();
  if (!athena::history::valid_relative_document_path (encoded))
    return std::nullopt;
  return HistoryLocation {root, std::move (encoded)};
}

QString
document_label (url document) {
  if (is_none (document)) return QString ();
  auto location= history_location (document);
  if (location)
    return QString::fromUtf8 (
      location->relative.data (), static_cast<int> (location->relative.size ()));
  return to_qstring (as_string (tail (document)));
}

url
last_active_document_url () {
  QTMDocumentIdentity identity= qtm_last_active_document_identity (
    QTMMainTabWindow::topTabWindow ());
  if (!identity.has_buffer_name ()) return url_none ();
  return url (string (
    identity.native_url_name.data (),
    static_cast<int> (identity.native_url_name.size ())));
}

} // namespace

QTMDocumentHistoryPane::QTMDocumentHistoryPane (QWidget* parent)
  : QWidget (parent), tree (new QTreeWidget (this)),
    document (url_none ()), followCurrent (true) {
  tree->setColumnCount (5);
  tree->setHeaderLabels (
    QStringList () << "Time" << "Trigger" << "Content" << "Stored" << "Encoding");
  tree->setAlternatingRowColors (true);
  tree->setContextMenuPolicy (Qt::CustomContextMenu);
  tree->setSelectionMode (QAbstractItemView::SingleSelection);
  tree->setUniformRowHeights (true);
  tree->header ()->setSectionResizeMode (0, QHeaderView::ResizeToContents);
  tree->header ()->setSectionResizeMode (1, QHeaderView::Stretch);
  tree->header ()->setSectionResizeMode (2, QHeaderView::ResizeToContents);
  tree->header ()->setSectionResizeMode (3, QHeaderView::ResizeToContents);
  tree->header ()->setSectionResizeMode (4, QHeaderView::ResizeToContents);

  QToolBar* toolbar= new QToolBar (this);
  toolbar->setIconSize (QSize (16, 16));
  toolbar->setToolButtonStyle (Qt::ToolButtonIconOnly);

  QAction* refreshAction= toolbar->addAction (
    history_icon ("view-refresh", QStyle::SP_BrowserReload),
    "Refresh", this, [this] () { refresh (); });
  refreshAction->setToolTip ("Refresh");

  QAction* openAction= toolbar->addAction (
    history_icon ("document-open", QStyle::SP_DialogOpenButton),
    "Open version", this, [this] () { openSelectedVersion (); });
  openAction->setToolTip ("Open selected historical version");

  QAction* restoreAction= toolbar->addAction (
    history_icon ("edit-undo", QStyle::SP_ArrowBack),
    "Restore version", this, [this] () { restoreSelectedVersion (); });
  restoreAction->setToolTip ("Restore selected version into the live document");

  QVBoxLayout* layout= new QVBoxLayout (this);
  layout->setContentsMargins (0, 0, 0, 0);
  layout->addWidget (toolbar);
  layout->addWidget (tree);

  connect (tree, &QTreeWidget::itemDoubleClicked,
           this, [this] (QTreeWidgetItem*) { openSelectedVersion (); });
  connect (tree, &QTreeWidget::customContextMenuRequested,
           this, [this] (const QPoint& pos) { showContextMenu (pos); });
}

QSize
QTMDocumentHistoryPane::sizeHint () const {
  return QSize (620, 600);
}

void
QTMDocumentHistoryPane::setDocument (url document2, bool followCurrent2) {
  document= document2;
  followCurrent= followCurrent2;
  refresh ();
}

void
QTMDocumentHistoryPane::followCurrentDocument () {
  if (!followCurrent) return;
  url current= last_active_document_url ();
  if (!is_none (current)) document= current;
  else document= url_none ();
}

void
QTMDocumentHistoryPane::refresh () {
  tree->clear ();
  followCurrentDocument ();

  auto location= history_location (document);
  if (!location) return;

  athena::history::document_history_store store;
  std::string error;
  if (!store.open (location->root, error)) {
    std_warning << "Could not open Document History: "
                << string (error.c_str ()) << LF;
    return;
  }

  std::vector<athena::history::version_entry> versions;
  if (!store.list (location->relative, versions, error)) {
    std_warning << "Could not list Document History: "
                << string (error.c_str ()) << LF;
    return;
  }

  for (const auto& version: versions) {
    QTreeWidgetItem* item= new QTreeWidgetItem ();
    item->setText (
      0, QDateTime::fromMSecsSinceEpoch (version.created_at_ms)
           .toString ("yyyy-MM-dd HH:mm:ss"));
    item->setText (
      1, QString::fromUtf8 (
           version.trigger.data (), static_cast<int> (version.trigger.size ())));
    item->setText (2, display_size (version.content_size));
    item->setText (3, display_size (version.stored_size));
    item->setText (4, version.delta ?
                      QString ("Fossil delta (%1)").arg (version.chain_depth) :
                      QString ("Full"));
    item->setData (0, Qt::UserRole, QVariant::fromValue<qlonglong> (version.id));
    tree->addTopLevelItem (item);
  }
}

std::int64_t
QTMDocumentHistoryPane::selectedVersionId () const {
  QTreeWidgetItem* item= tree->currentItem ();
  if (item == nullptr) return 0;
  return item->data (0, Qt::UserRole).toLongLong ();
}

void
QTMDocumentHistoryPane::openSelectedVersion () {
  const std::int64_t version= selectedVersionId ();
  if (version <= 0 || is_none (document)) return;
  (void) qtm_document_history_open_version (document, version);
}

void
QTMDocumentHistoryPane::restoreSelectedVersion () {
  const std::int64_t version= selectedVersionId ();
  if (version <= 0 || is_none (document)) return;

  QString label= document_label (document);
  if (QMessageBox::question (
        this, "Restore Document History",
        QString ("Restore this historical version into %1?\n\n"
                 "ATHENA will preserve the current state in Document History "
                 "before restoring.").arg (label),
        QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
    return;

  if (!qtm_document_history_restore_version (document, version)) {
    QMessageBox::warning (
      this, "Restore Document History",
      "The historical version could not be restored.");
    return;
  }
  refresh ();
}

void
QTMDocumentHistoryPane::showContextMenu (const QPoint& pos) {
  QTreeWidgetItem* item= tree->itemAt (pos);
  if (item != nullptr) tree->setCurrentItem (item);
  const bool selected= selectedVersionId () > 0;

  QMenu menu (this);
  menu.addAction ("Open version", this, [this] { openSelectedVersion (); })
      ->setEnabled (selected);
  menu.addAction ("Restore version", this, [this] { restoreSelectedVersion (); })
      ->setEnabled (selected);
  menu.addSeparator ();
  menu.addAction ("Refresh", this, [this] { refresh (); });
  menu.exec (tree->viewport ()->mapToGlobal (pos));
}

static void
document_history_pane_show_impl (url document, bool follow) {
  if (qApp != nullptr && QThread::currentThread () != qApp->thread ()) {
    string encoded= as_string (document);
    qt_post_to_main_thread ([encoded= std::move (encoded), follow] {
      document_history_pane_show_impl (url (encoded), follow);
    });
    return;
  }

  if (!vault_active ()) {
    QMessageBox::warning (
      QApplication::activeWindow (), "Document History",
      "No active vault. Please load a vault first.");
    return;
  }

  if (follow) document= last_active_document_url ();

  QTMMainTabWindow* win= QTMMainTabWindow::topTabWindow ();
  if (win == nullptr || win->dockManager () == nullptr) {
    QMessageBox::warning (
      QApplication::activeWindow (), "Document History",
      "No active ATHENA window.");
    return;
  }

  if (document_history_widget == nullptr) {
    document_history_widget= new QTMDocumentHistoryPane ();
    QObject::connect (document_history_widget, &QObject::destroyed, [] {
      document_history_widget= nullptr;
      document_history_dock= nullptr;
    });
  }

  document_history_widget->setDocument (document, follow);
  QString title= "Document History";
  QString label= document_label (document);
  if (!label.isEmpty ()) title += " - " + label;

  if (document_history_dock == nullptr) {
    document_history_dock= new ads::CDockWidget (title);
    document_history_dock->setObjectName ("athena-document-history");
    document_history_dock->resize (620, 600);
    document_history_dock->setWidget (document_history_widget);
    document_history_dock->setFeature (
      ads::CDockWidget::DockWidgetDeleteOnClose, false);
    QObject::connect (document_history_dock, &QObject::destroyed, [] {
      document_history_dock= nullptr;
    });
    win->showAdsDockWidget (document_history_dock, ads::RightDockWidgetArea);
  }

  document_history_dock->setWindowTitle (title);
  win->showAdsDockWidget (document_history_dock, ads::RightDockWidgetArea);
  document_history_widget->setFocus ();
}

void
document_history_pane_show_document (url document) {
  document_history_pane_show_impl (document, is_none (document));
}

void
document_history_pane_show_frozen (url document) {
  document_history_pane_show_impl (document, false);
}

void
document_history_pane_show () {
  document_history_pane_show_document (url_none ());
}

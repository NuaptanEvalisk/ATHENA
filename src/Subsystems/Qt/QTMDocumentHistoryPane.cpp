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
#include "confined_filesystem.hpp"
#include "buffer_name_catalog.hpp"

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
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QPlainTextEdit>
#include <QJsonDocument>

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
    scope (new QComboBox (this)),
    document (url_none ()), followCurrent (true) {
  tree->setColumnCount (6);
  tree->setHeaderLabels (
    QStringList () << "Time" << "Source" << "Trigger" << "Content" << "Stored" << "Encoding");
  tree->setAlternatingRowColors (true);
  tree->setContextMenuPolicy (Qt::CustomContextMenu);
  tree->setSelectionMode (QAbstractItemView::SingleSelection);
  tree->setUniformRowHeights (true);
  tree->header ()->setSectionResizeMode (0, QHeaderView::ResizeToContents);
  tree->header ()->setSectionResizeMode (1, QHeaderView::Stretch);
  tree->header ()->setSectionResizeMode (2, QHeaderView::ResizeToContents);
  tree->header ()->setSectionResizeMode (3, QHeaderView::ResizeToContents);
  tree->header ()->setSectionResizeMode (4, QHeaderView::ResizeToContents);
  tree->header ()->setSectionResizeMode (5, QHeaderView::ResizeToContents);

  QToolBar* toolbar= new QToolBar (this);
  toolbar->setIconSize (QSize (16, 16));
  toolbar->setToolButtonStyle (Qt::ToolButtonIconOnly);
  scope->addItems ({tr ("Current document"), tr ("Protected vault history")});
  toolbar->addWidget (scope);
  connect (scope, &QComboBox::currentIndexChanged, this, [this] { refresh (); });

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
  restoreAction->setToolTip ("Restore selected historical version");
  toolbar->addAction (history_icon ("document-save-as", QStyle::SP_DialogSaveButton),
    "Export copy", this, [this] { exportSelectedVersion (); });
  auto* older= toolbar->addAction (history_icon ("go-down", QStyle::SP_ArrowDown),
    "Older protected snapshots", this, [this] { refresh (true); });
  older->setEnabled (false);
  connect (scope, &QComboBox::currentIndexChanged, this,
    [older] (int index) { older->setEnabled (index == 1); });

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
  if (is_none (document) && scope->currentIndex () != 1) {
    scope->setCurrentIndex (1); return;
  }
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
QTMDocumentHistoryPane::refresh (bool older) {
  followCurrentDocument ();
  auto location= history_location (document);
  if (scope->currentIndex () == 1) {
    const auto context= vault_capture_context ();
    location= context ? std::optional<HistoryLocation> (HistoryLocation {context->root, {}}) : std::nullopt;
  }
  if (!older || scope->currentIndex () != 1 || !location ||
      historyRoot != QString::fromStdString (location->root.string ())) {
    tree->clear (); pageCursor= 0;
  }
  if (!location) return;
  historyRoot= QString::fromStdString (location->root.string ());

  athena::history::document_history_store store;
  std::string error;
  if (!store.open (location->root, error)) {
    std_warning << "Could not open Document History: "
                << string (error.c_str ()) << LF;
    return;
  }

  std::vector<athena::history::version_entry> versions;
  const bool listed= scope->currentIndex () == 1 ?
    store.list_protected (pageCursor, 256, versions, error) :
    store.list (location->relative, versions, error);
  if (!listed) {
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
      2, QString::fromUtf8 (
           version.trigger.data (), static_cast<int> (version.trigger.size ())));
    item->setText (1, QString::fromStdString (version.relative_path));
    item->setToolTip (1, item->text (1));
    item->setText (3, display_size (version.content_size));
    item->setText (4, display_size (version.stored_size));
    item->setText (5, version.delta ?
                      QString ("Fossil delta (%1)").arg (version.chain_depth) :
                      (version.protected_snapshot ? tr ("Full (protected)") : QString ("Full")));
    item->setData (0, Qt::UserRole, QVariant::fromValue<qlonglong> (version.id));
    item->setData (0, Qt::UserRole + 1, QString::fromStdString (version.relative_path));
    item->setData (0, Qt::UserRole + 2, version.protected_snapshot);
    tree->addTopLevelItem (item);
    pageCursor= version.id;
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
  auto selected= selectedDocument ();
  if (version <= 0 || is_none (selected)) return;
  if (suffix (selected) == "ath") {
    (void) qtm_document_history_open_version (selected, version); return;
  }
  std::string bytes;
  if (!readSelectedVersion (bytes)) return;
  const auto display= QByteArray (bytes.data (), int (std::min<std::size_t> (bytes.size (), 64 * 1024)));
  const auto text= QString::fromUtf8 (display);
  auto* dialog= new QDialog (this);
  dialog->setAttribute (Qt::WA_DeleteOnClose);
  dialog->setWindowTitle (tr ("History snapshot %1").arg (version));
  dialog->resize (800, 550);
  auto* layout= new QVBoxLayout (dialog);
  auto* preview= new QPlainTextEdit (dialog);
  preview->setReadOnly (true);
  const auto json= QJsonDocument::fromJson (display);
  preview->setPlainText (!json.isNull () ? QString::fromUtf8 (json.toJson ()) :
    (text.toUtf8 () == display && !display.contains ('\0') ? text : QString::fromLatin1 (display.toHex (' '))));
  if (bytes.size () > 64 * 1024) dialog->setWindowTitle (dialog->windowTitle () + tr (" (first 64 KiB)"));
  layout->addWidget (preview);
  auto* buttons= new QDialogButtonBox (QDialogButtonBox::Close, dialog);
  connect (buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  layout->addWidget (buttons);
  dialog->show ();
}

url QTMDocumentHistoryPane::selectedDocument () const {
  const auto context= vault_capture_context ();
  auto* item= tree->currentItem ();
  if (!item || !context || QString::fromStdString (context->root.string ()) != historyRoot) return url_none ();
  const auto path= context->root / item->data (0, Qt::UserRole + 1).toString ().toStdString ();
  return url_system (string (path.string ().c_str ()));
}

bool QTMDocumentHistoryPane::readSelectedVersion (std::string& bytes) {
  if (is_none (selectedDocument ())) return false;
  athena::history::document_history_store store;
  std::string error;
  if (store.open (fs::path (historyRoot.toStdString ()), error) &&
      store.reconstruct (selectedVersionId (), bytes, error)) return true;
  std_warning << "Could not read recovery snapshot: " << string (error.c_str ()) << LF;
  QMessageBox::warning (this, tr ("File History"), QString::fromStdString (error));
  return false;
}

void QTMDocumentHistoryPane::exportSelectedVersion () {
  std::string bytes;
  if (!readSelectedVersion (bytes)) return;
  const auto chosen= QFileDialog::getSaveFileName (this, tr ("Export history copy"),
    QFileInfo (to_qstring (as_string (selectedDocument ()))).fileName ());
  if (chosen.isEmpty ()) return;
  try {
    const auto destination= fs::absolute (fs::path (chosen.toStdString ()));
    athena::filesystem::confined_root parent (destination.parent_path ());
    std::unique_lock<std::recursive_mutex> publication (document_publication_mutex ());
    const auto written= parent.create (destination.filename (), bytes, [&] {
      if (!published_file_buffers (destination.generic_string ()).empty ())
        throw std::runtime_error ("An open buffer already owns the export destination");
    });
    if (!written.directory_synced) throw std::runtime_error ("Exported copy could not be directory-synchronized");
  }
  catch (const std::exception& error) {
    std_warning << "Could not export history copy: " << string (error.what ()) << LF;
    QMessageBox::warning (this, tr ("Export history copy"),
      tr ("Choose a new filename. Existing files are not overwritten.\n%1").arg (QString::fromUtf8 (error.what ())));
  }
}

void
QTMDocumentHistoryPane::restoreSelectedVersion () {
  const std::int64_t version= selectedVersionId ();
  auto selected= selectedDocument ();
  if (version <= 0 || is_none (selected)) return;

  QString label= tree->currentItem ()->text (1);
  if (QMessageBox::question (
        this, "Restore Document History",
        QString ("Restore this historical version into %1?\n\n"
                 "ATHENA will preserve the current state in Document History "
                 "before restoring.").arg (label),
        QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
    return;

  std::string diagnostic;
  const bool restored= tree->currentItem ()->data (0, Qt::UserRole + 2).toBool () ?
    qtm_document_history_restore_protected (fs::path (historyRoot.toStdString ()), version, &diagnostic) :
    qtm_document_history_restore_version (selected, version);
  if (!restored) {
    QMessageBox::warning (
      this, "Restore Document History",
      diagnostic.empty () ? tr ("The historical version could not be restored.") : QString::fromStdString (diagnostic));
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
  menu.addAction ("Export copy", this, [this] { exportSelectedVersion (); })
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

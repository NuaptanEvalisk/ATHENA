/******************************************************************************
* MODULE     : QTMVaultBackupViewer.cpp
* DESCRIPTION: Qt vault backup viewer pane
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMVaultBackupViewer.hpp"
#include "QTMMainTabWindow.hpp"
#include "qt_utilities.hpp"
#include "vault.hpp"

#include <DockWidget.h>
#include <QAction>
#include <QAbstractItemView>
#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QIcon>
#include <QMenu>
#include <QMessageBox>
#include <QSize>
#include <QStyle>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

static QTMVaultBackupViewer* vault_backup_viewer_widget= nullptr;
static ads::CDockWidget* vault_backup_viewer_dock= nullptr;

static QIcon
backup_viewer_icon (const QString& name, QStyle::StandardPixmap fallback) {
  QIcon icon= QIcon::fromTheme (name);
  if (icon.isNull ()) icon= QApplication::style ()->standardIcon (fallback);
  return icon;
}

static QString
display_size (qint64 bytes) {
  if (bytes < 0) return QString ();
  const char* units[]= { "B", "KiB", "MiB", "GiB" };
  double value= (double) bytes;
  int unit= 0;
  while (value >= 1024.0 && unit < 3) {
    value /= 1024.0;
    unit++;
  }
  return unit == 0 ? QString ("%1 %2").arg (bytes).arg (units[unit])
                   : QString ("%1 %2").arg (value, 0, 'f', 1).arg (units[unit]);
}

static qint64
path_size (const QString& path) {
  QFileInfo info (path);
  if (!info.exists ()) return -1;
  if (info.isFile ()) return info.size ();

  qint64 total= 0;
  QDirIterator it (path, QDir::Files | QDir::NoDotAndDotDot,
                   QDirIterator::Subdirectories);
  while (it.hasNext ()) {
    it.next ();
    total += it.fileInfo ().size ();
  }
  return total;
}

QTMVaultBackupViewer::QTMVaultBackupViewer (QWidget* parent)
  : QWidget (parent), tree (new QTreeWidget (this)) {
  tree->setColumnCount (4);
  tree->setHeaderLabels (QStringList () << "Backup" << "Kind" << "Modified"
                                        << "Size");
  tree->setAlternatingRowColors (true);
  tree->setContextMenuPolicy (Qt::CustomContextMenu);
  tree->setSelectionMode (QAbstractItemView::ExtendedSelection);
  tree->setUniformRowHeights (true);
  tree->header ()->setSectionResizeMode (0, QHeaderView::Stretch);
  for (int i=1; i<4; i++)
    tree->header ()->setSectionResizeMode (i, QHeaderView::ResizeToContents);

  QToolBar* toolbar= new QToolBar (this);
  toolbar->setIconSize (QSize (16, 16));
  toolbar->setToolButtonStyle (Qt::ToolButtonIconOnly);
  QAction* refreshAction= toolbar->addAction (
    backup_viewer_icon ("view-refresh", QStyle::SP_BrowserReload),
    "Refresh", this, [this] () { refresh (); });
  refreshAction->setToolTip ("Refresh");
  QAction* openAction= toolbar->addAction (
    backup_viewer_icon ("document-open-folder", QStyle::SP_DirOpenIcon),
    "Open backup", this, [this] () { openSelectedBackup (); });
  openAction->setToolTip ("Open backup");
  QAction* removeAction= toolbar->addAction (
    backup_viewer_icon ("user-trash", QStyle::SP_TrashIcon),
    "Remove backup", this, [this] () { removeSelectedBackups (); });
  removeAction->setToolTip ("Remove backup");

  QVBoxLayout* layout= new QVBoxLayout (this);
  layout->setContentsMargins (0, 0, 0, 0);
  layout->addWidget (toolbar);
  layout->addWidget (tree);

  connect (tree, &QTreeWidget::itemDoubleClicked,
           this, [this] (QTreeWidgetItem*) { openSelectedBackup (); });
  connect (tree, &QTreeWidget::customContextMenuRequested,
           this, [this] (const QPoint& pos) { showContextMenu (pos); });
}

QSize
QTMVaultBackupViewer::sizeHint () const {
  return QSize (520, 600);
}

void
QTMVaultBackupViewer::setVault (const QString& rootPath2,
                                const QString& vaultName2) {
  rootPath= QFileInfo (rootPath2).canonicalFilePath ();
  if (rootPath.isEmpty ()) rootPath= QDir::cleanPath (rootPath2);
  vaultName= vaultName2;
  refresh ();
}

void
QTMVaultBackupViewer::addBackupItem (const QString& kind, const QString& path,
                                     const QString& displayPath) {
  QFileInfo info (path);
  QTreeWidgetItem* item= new QTreeWidgetItem ();
  item->setText (0, displayPath);
  item->setText (1, kind);
  item->setText (2, info.lastModified ().toString ("yyyy-MM-dd HH:mm:ss"));
  item->setText (3, display_size (path_size (path)));
  item->setData (0, Qt::UserRole, path);
  tree->addTopLevelItem (item);
}

void
QTMVaultBackupViewer::refresh () {
  tree->clear ();
  if (rootPath.isEmpty ()) return;

  QDir backupRoot (QDir (rootPath).filePath (".backup"));
  if (!backupRoot.exists ()) return;

  QFileInfoList entries= backupRoot.entryInfoList (
    QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
    QDir::Time | QDir::Reversed);
  for (const QFileInfo& entry: entries) {
    QString name= entry.fileName ();
    if (name == "manual-save" && entry.isDir ()) {
      QDirIterator it (entry.absoluteFilePath (), QStringList () << "*.zst",
                       QDir::Files | QDir::NoDotAndDotDot,
                       QDirIterator::Subdirectories);
      while (it.hasNext ()) {
        QString file= it.next ();
        QString rel= backupRoot.relativeFilePath (file);
        addBackupItem ("Manual save", file, rel);
      }
    }
    else if (entry.isDir ()) {
      addBackupItem ("Vault maintenance", entry.absoluteFilePath (), name);
    }
    else {
      addBackupItem ("Backup file", entry.absoluteFilePath (), name);
    }
  }

  tree->sortItems (2, Qt::DescendingOrder);
}

QList<QTreeWidgetItem*>
QTMVaultBackupViewer::selectedBackupItems () const {
  QList<QTreeWidgetItem*> items= tree->selectedItems ();
  if (items.isEmpty () && tree->currentItem () != nullptr)
    items << tree->currentItem ();
  return items;
}

QString
QTMVaultBackupViewer::backupPath (QTreeWidgetItem* item) const {
  if (item == nullptr) return QString ();
  return item->data (0, Qt::UserRole).toString ();
}

void
QTMVaultBackupViewer::openSelectedBackup () {
  QList<QTreeWidgetItem*> items= selectedBackupItems ();
  if (items.isEmpty ()) return;
  QString path= backupPath (items.first ());
  if (path.isEmpty ()) return;

  QFileInfo info (path);
  QString dir= info.isDir () ? path : info.absolutePath ();
  QDesktopServices::openUrl (QUrl::fromLocalFile (dir));
}

void
QTMVaultBackupViewer::removeSelectedBackups () {
  QList<QTreeWidgetItem*> items= selectedBackupItems ();
  if (items.isEmpty ()) return;

  QString text= items.size () == 1
    ? QString ("Move selected backup to system trash?")
    : QString ("Move %1 selected backups to system trash?").arg (items.size ());
  if (QMessageBox::question (this, "Remove Backup", text,
                             QMessageBox::Yes | QMessageBox::No) !=
      QMessageBox::Yes)
    return;

  QStringList failures;
  for (QTreeWidgetItem* item: items) {
    QString path= backupPath (item);
    if (path.isEmpty ()) continue;
    if (!QFile::moveToTrash (path)) failures << path;
  }
  refresh ();
  if (!failures.isEmpty ())
    QMessageBox::warning (this, "Remove Backup",
                          "Some backups could not be moved to system trash.");
}

void
QTMVaultBackupViewer::showContextMenu (const QPoint& pos) {
  QTreeWidgetItem* item= tree->itemAt (pos);
  if (item != nullptr && !item->isSelected ()) {
    tree->clearSelection ();
    item->setSelected (true);
    tree->setCurrentItem (item);
  }

  bool hasSelection= !selectedBackupItems ().isEmpty ();
  QMenu menu (this);
  menu.addAction ("Open backup", this, [this] () { openSelectedBackup (); })
      ->setEnabled (hasSelection);
  menu.addAction ("Remove backup", this, [this] () { removeSelectedBackups (); })
      ->setEnabled (hasSelection);
  menu.addSeparator ();
  menu.addAction ("Refresh", this, [this] () { refresh (); });
  menu.exec (tree->viewport ()->mapToGlobal (pos));
}

void
vault_backup_viewer_show () {
  if (!vault_active ()) {
    QMessageBox::warning (QApplication::activeWindow (), "Vault Backup Viewer",
                          "No active vault. Please load a vault first.");
    return;
  }

  QTMMainTabWindow* win= QTMMainTabWindow::topTabWindow ();
  if (win == nullptr || win->dockManager () == nullptr) {
    QMessageBox::warning (QApplication::activeWindow (), "Vault Backup Viewer",
                          "No active ATHENA window.");
    return;
  }

  if (vault_backup_viewer_widget == nullptr) {
    vault_backup_viewer_widget= new QTMVaultBackupViewer ();
    QObject::connect (vault_backup_viewer_widget, &QObject::destroyed, [] () {
      vault_backup_viewer_widget= nullptr;
      vault_backup_viewer_dock= nullptr;
    });
  }

  QString root= to_qstring (concretize (vault_get_root ()));
  QString name= to_qstring (vault_get_name ());
  vault_backup_viewer_widget->setVault (root, name);

  QString title= name.isEmpty () ? "Vault Backup Viewer" :
                 QString ("Vault Backup Viewer - ") + name;
  if (vault_backup_viewer_dock == nullptr) {
    vault_backup_viewer_dock= new ads::CDockWidget (title);
    vault_backup_viewer_dock->setObjectName ("athena-vault-backup-viewer");
    vault_backup_viewer_dock->resize (520, 600);
    vault_backup_viewer_dock->setWidget (vault_backup_viewer_widget);
    vault_backup_viewer_dock->setFeature (
      ads::CDockWidget::DockWidgetDeleteOnClose, false);
    QObject::connect (vault_backup_viewer_dock, &QObject::destroyed, [] () {
      vault_backup_viewer_dock= nullptr;
    });
    win->showAdsDockWidget (vault_backup_viewer_dock, ads::RightDockWidgetArea);
  }

  vault_backup_viewer_dock->setWindowTitle (title);
  win->showAdsDockWidget (vault_backup_viewer_dock, ads::RightDockWidgetArea);
  vault_backup_viewer_widget->setFocus ();
}

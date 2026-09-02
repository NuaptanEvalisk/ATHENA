/******************************************************************************
* MODULE     : QTMVaultExplorer.cpp
* DESCRIPTION: Qt vault explorer pane for ATHENA vault files
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMVaultExplorer.hpp"
#include "QTMVaultSafeRename.hpp"
#include "QTMMainTabWindow.hpp"
#include "editor.hpp"
#include "boot.hpp"
#include "scheme_execution_context.hpp"
#include "scheme.hpp"
#include "qt_utilities.hpp"
#include "vault.hpp"

#include <DockAreaWidget.h>
#include <DockSplitter.h>
#include <DockWidget.h>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QModelIndex>
#include <QSize>
#include <QSizeGrip>
#include <QStyle>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

static QString vault_explorer_clipboard_path;
static QTMVaultExplorer* vault_explorer_widget= nullptr;
static ads::CDockWidget* vault_explorer_dock= nullptr;

static bool
vault_explorer_use_system_trash () {
  return get_preference ("vault explorer use system trash", "off") == "on";
}

static bool
vault_explorer_move_to_trash (const QString& path) {
  return QFile::moveToTrash (path);
}

static QIcon
vault_explorer_icon (const QString& name, QStyle::StandardPixmap fallback) {
  QIcon icon= QIcon::fromTheme (name);
  if (icon.isNull ()) icon= QApplication::style ()->standardIcon (fallback);
  return icon;
}

static void
set_vault_explorer_area_width (ads::CDockManager* manager,
                               ads::CDockWidget* dock) {
  if (manager == nullptr || dock == nullptr || dock->isInFloatingContainer ())
    return;

  ads::CDockAreaWidget* area= dock->dockAreaWidget ();
  if (area == nullptr || dock->dockContainer () == nullptr) return;
  ads::CDockSplitter* splitter= area->parentSplitter ();
  if (splitter == nullptr || splitter->orientation () != Qt::Horizontal) return;

  QList<int> sizes= manager->splitterSizes (area);
  int index= splitter->indexOf (area);
  if (index < 0 || index >= sizes.size () || sizes.size () < 2) return;

  int total= 0;
  for (int size: sizes) total += size;
  if (total <= 0) total= splitter->width ();
  if (total <= 0) return;

  const int target= qMin (300, qMax (220, total / 3));
  int remaining= qMax (120, total - target);
  int otherTotal= 0;
  for (int i=0; i<sizes.size (); i++)
    if (i != index) otherTotal += sizes[i];

  sizes[index]= target;
  int assigned= target;
  int lastOther= -1;
  for (int i=0; i<sizes.size (); i++) {
    if (i == index) continue;
    lastOther= i;
    sizes[i]= otherTotal > 0 ? (sizes[i] * remaining) / otherTotal
                             : remaining / (sizes.size () - 1);
    assigned += sizes[i];
  }
  if (lastOther >= 0) sizes[lastOther] += total - assigned;
  manager->setSplitterSizes (area, sizes);
}

QTMVaultExplorer::QTMVaultExplorer (QWidget* parent)
  : QWidget (parent), model (new QFileSystemModel (this)),
    tree (new QTreeView (this)),
    floatingSizeGrip (new QSizeGrip (this)) {
  model->setReadOnly (false);
  model->setFilter (QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);

  tree->setModel (model);
  tree->setContextMenuPolicy (Qt::CustomContextMenu);
  tree->setEditTriggers (QAbstractItemView::NoEditTriggers);
  tree->setExpandsOnDoubleClick (false);
  tree->setSelectionMode (QAbstractItemView::SingleSelection);
  tree->setUniformRowHeights (true);
  tree->header ()->setSectionResizeMode (0, QHeaderView::Stretch);
  tree->header ()->setStretchLastSection (false);

  QToolBar* toolbar= new QToolBar (this);
  toolbar->setIconSize (QSize (16, 16));
  toolbar->setToolButtonStyle (Qt::ToolButtonIconOnly);
  QAction* newFileAction= toolbar->addAction (
    vault_explorer_icon ("document-new", QStyle::SP_FileIcon),
    "New File", this, [this] () { newFile (); });
  newFileAction->setToolTip ("New File");
  QAction* newFolderAction= toolbar->addAction (
    vault_explorer_icon ("folder-new", QStyle::SP_DirIcon),
    "New Folder", this, [this] () { newFolder (); });
  newFolderAction->setToolTip ("New Folder");
  QAction* refreshAction= toolbar->addAction (
    vault_explorer_icon ("view-refresh", QStyle::SP_BrowserReload),
    "Refresh", this, [this] () { refresh (); });
  refreshAction->setToolTip ("Refresh");

  floatingSizeGrip->hide ();
  QHBoxLayout* gripRow= new QHBoxLayout ();
  gripRow->setContentsMargins (0, 0, 0, 0);
  gripRow->addStretch ();
  gripRow->addWidget (floatingSizeGrip, 0, Qt::AlignRight | Qt::AlignBottom);

  QVBoxLayout* layout= new QVBoxLayout (this);
  layout->setContentsMargins (0, 0, 0, 0);
  layout->addWidget (toolbar);
  layout->addWidget (tree, 1);
  layout->addLayout (gripRow);

  connect (tree, &QTreeView::doubleClicked,
           this, [this] (const QModelIndex& index) { loadIndex (index); });
  connect (tree, &QTreeView::customContextMenuRequested,
           this, [this] (const QPoint& pos) { showContextMenu (pos); });
}

QSize
QTMVaultExplorer::sizeHint () const {
  return QSize (280, 600);
}

void
QTMVaultExplorer::setFloatingResizeGripVisible (bool visible) {
  floatingSizeGrip->setVisible (visible);
}

void
QTMVaultExplorer::setVault (const QString& rootPath2, const QString& vaultName2) {
  rootPath = QFileInfo (rootPath2).canonicalFilePath ();
  if (rootPath.isEmpty ()) rootPath= QDir::cleanPath (rootPath2);
  vaultName= vaultName2;

  QModelIndex root= model->setRootPath (rootPath);
  tree->setRootIndex (root);
  for (int i=1; i<model->columnCount (); i++) tree->hideColumn (i);
  tree->header ()->setSectionResizeMode (0, QHeaderView::Stretch);
}

void
QTMVaultExplorer::revealPath (const QString& path) {
  if (path.isEmpty () || !pathInVault (path) || !QFileInfo::exists (path))
    return;

  QModelIndex index= model->index (path);
  if (!index.isValid ()) return;

  QModelIndex parent= index.parent ();
  while (parent.isValid () && parent != tree->rootIndex ()) {
    tree->expand (parent);
    parent= parent.parent ();
  }
  tree->expand (index.parent ());
  tree->setCurrentIndex (index);
  tree->scrollTo (index, QAbstractItemView::PositionAtCenter);
}

QModelIndex
QTMVaultExplorer::selectedIndex () const {
  QModelIndex index= tree->currentIndex ();
  if (!index.isValid ()) index= tree->rootIndex ();
  if (index.column () != 0) index= index.sibling (index.row (), 0);
  return index;
}

QString
QTMVaultExplorer::selectedPath () const {
  QModelIndex index= selectedIndex ();
  if (!index.isValid ()) return rootPath;
  return model->filePath (index);
}

QString
QTMVaultExplorer::selectedDirectory () const {
  QString path= selectedPath ();
  QFileInfo info (path);
  if (info.isDir ()) return path;
  return info.absolutePath ();
}

bool
QTMVaultExplorer::pathInVault (const QString& path) const {
  QString canonicalRoot= QFileInfo (rootPath).canonicalFilePath ();
  QString canonicalPath= QFileInfo (path).canonicalFilePath ();
  if (canonicalRoot.isEmpty ()) canonicalRoot= QDir::cleanPath (rootPath);
  if (canonicalPath.isEmpty ()) canonicalPath= QDir::cleanPath (path);
  canonicalRoot= QDir::cleanPath (canonicalRoot);
  canonicalPath= QDir::cleanPath (canonicalPath);
  return canonicalPath == canonicalRoot ||
         canonicalPath.startsWith (canonicalRoot + QDir::separator ());
}

void
QTMVaultExplorer::showError (const QString& message) const {
  QMessageBox::warning (const_cast<QTMVaultExplorer*> (this),
                        "Vault Explorer", message);
}

void
QTMVaultExplorer::refresh () {
  if (rootPath.isEmpty ()) return;
  model->setRootPath ("");
  QModelIndex root= model->setRootPath (rootPath);
  tree->setRootIndex (root);
}

void
QTMVaultExplorer::loadSelected () {
  loadIndex (selectedIndex ());
}

void
QTMVaultExplorer::loadIndex (const QModelIndex& index) {
  if (!index.isValid ()) return;
  QModelIndex nameIndex= index.column () == 0 ? index : index.sibling (index.row (), 0);
  tree->setCurrentIndex (nameIndex);

  QString path= model->filePath (nameIndex);
  if (path.isEmpty () || !pathInVault (path)) return;
  QFileInfo info (path);
  if (!info.exists ()) return;
  if (info.isDir ()) {
    tree->setExpanded (nameIndex, !tree->isExpanded (nameIndex));
    return;
  }

  QString suffix= info.suffix ().toLower ();
  if (suffix == "ath" || suffix == "tm") {
    exec_delayed (scheme_cmd (list_object (symbol_object ("load-buffer"),
                              object (url_system (from_qstring (path))))));
  }
  else {
    QDesktopServices::openUrl (QUrl::fromLocalFile (path));
  }
}

bool
QTMVaultExplorer::writeNewFile (const QString& path) {
  QFileInfo info (path);
  QDir ().mkpath (info.absolutePath ());
  QFile file (path);
  if (!file.open (QIODevice::WriteOnly | QIODevice::NewOnly | QIODevice::Text))
    return false;

  QString suffix= info.suffix ().toLower ();
  if (suffix == "ath" || suffix == "tm") {
    QTextStream out (&file);
    out << "<TeXmacs|2.1.4>\n\n"
        << "<style|generic>\n\n"
        << "<\\body>\n"
        << "  \n"
        << "</body>\n";
  }
  return true;
}

void
QTMVaultExplorer::newFile () {
  QString dir= selectedDirectory ();
  bool ok= false;
  QString name= QInputDialog::getText (this, "New File", "File name:",
                                       QLineEdit::Normal, "", &ok).trimmed ();
  if (!ok || name.isEmpty ()) return;
  if (name.contains ('/') || name.contains ('\\')) {
    showError ("File name must not contain path separators.");
    return;
  }

  QString path= QDir (dir).filePath (name);
  if (!pathInVault (dir) || QFileInfo::exists (path)) {
    showError ("Cannot create file at this location.");
    return;
  }
  if (!writeNewFile (path)) showError ("Could not create file.");
}

void
QTMVaultExplorer::newFolder () {
  QString dir= selectedDirectory ();
  bool ok= false;
  QString name= QInputDialog::getText (this, "New Folder", "Folder name:",
                                       QLineEdit::Normal, "", &ok).trimmed ();
  if (!ok || name.isEmpty ()) return;
  if (name.contains ('/') || name.contains ('\\')) {
    showError ("Folder name must not contain path separators.");
    return;
  }

  QString path= QDir (dir).filePath (name);
  if (!pathInVault (dir) || QFileInfo::exists (path) || !QDir ().mkpath (path))
    showError ("Could not create folder.");
}

void
QTMVaultExplorer::renameSelected () {
  QString path= selectedPath ();
  if (path == rootPath || !pathInVault (path)) return;
  QFileInfo info (path);
  bool ok= false;
  QString name= QInputDialog::getText (this, "Rename", "New name:",
                                       QLineEdit::Normal, info.fileName (),
                                       &ok).trimmed ();
  if (!ok || name.isEmpty () || name == info.fileName ()) return;
  if (name.contains ('/') || name.contains ('\\')) {
    showError ("Name must not contain path separators.");
    return;
  }

  QString target= QDir (info.absolutePath ()).filePath (name);
  if (QFileInfo::exists (target)) {
    showError ("Could not rename item: destination already exists.");
    return;
  }
  if (qtm_safe_rename_vault_item (this, path, target)) refresh ();
}

void
QTMVaultExplorer::copySelected () {
  QString path= selectedPath ();
  if (!pathInVault (path)) return;
  vault_explorer_clipboard_path= path;
}

bool
QTMVaultExplorer::copyRecursively (const QString& src, const QString& dst) {
  QFileInfo info (src);
  if (info.isDir ()) {
    QDir sourceDir (src);
    if (!QDir ().mkpath (dst)) return false;
    QFileInfoList entries= sourceDir.entryInfoList (
      QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name);
    for (const QFileInfo& entry: entries) {
      QString childDst= QDir (dst).filePath (entry.fileName ());
      if (!copyRecursively (entry.absoluteFilePath (), childDst)) return false;
    }
    return true;
  }
  return QFile::copy (src, dst);
}

void
QTMVaultExplorer::pasteIntoSelected () {
  if (vault_explorer_clipboard_path.isEmpty () ||
      !QFileInfo::exists (vault_explorer_clipboard_path)) return;

  QString dir= selectedDirectory ();
  QFileInfo source (vault_explorer_clipboard_path);
  QString target= QDir (dir).filePath (source.fileName ());
  QString cleanSource= QDir::cleanPath (source.absoluteFilePath ());
  QString cleanTarget= QDir::cleanPath (target);
  if (!pathInVault (dir) || QFileInfo::exists (target)) {
    showError ("Cannot paste item at this location.");
    return;
  }
  if (source.isDir () &&
      cleanTarget.startsWith (cleanSource + QDir::separator ())) {
    showError ("Cannot paste a folder inside itself.");
    return;
  }
  if (!copyRecursively (vault_explorer_clipboard_path, target))
    showError ("Could not paste item.");
}

void
QTMVaultExplorer::deleteSelected () {
  QString path= selectedPath ();
  if (path == rootPath || !pathInVault (path)) return;
  QFileInfo info (path);
  bool useTrash= vault_explorer_use_system_trash ();
  QString action= useTrash ? "Move to Trash" : "Delete";
  QString text= action + " '" + info.fileName () + "'?";
  if (QMessageBox::question (this, "Delete", text,
                             QMessageBox::Yes | QMessageBox::No) !=
      QMessageBox::Yes)
    return;

  if (useTrash) {
    if (!vault_explorer_move_to_trash (path))
      showError ("Could not move item to system trash.");
    return;
  }

  bool ok= info.isDir () ? QDir (path).removeRecursively () : QFile::remove (path);
  if (!ok) showError ("Could not delete item.");
}

void
QTMVaultExplorer::openInFileManager () {
  QString path= selectedPath ();
  if (!pathInVault (path)) return;
  QFileInfo info (path);
  QString dir= info.isDir () ? path : info.absolutePath ();
  QDesktopServices::openUrl (QUrl::fromLocalFile (dir));
}

void
QTMVaultExplorer::showContextMenu (const QPoint& pos) {
  QModelIndex index= tree->indexAt (pos);
  if (index.isValid ()) tree->setCurrentIndex (index);

  bool hasSelection= selectedIndex ().isValid ();
  bool canPaste= !vault_explorer_clipboard_path.isEmpty ();
  QMenu menu (this);
  menu.addAction ("Load file", this, [this] () { loadSelected (); })
      ->setEnabled (hasSelection);
  menu.addSeparator ();
  menu.addAction ("New file", this, [this] () { newFile (); });
  menu.addAction ("New folder", this, [this] () { newFolder (); });
  menu.addSeparator ();
  menu.addAction ("Rename", this, [this] () { renameSelected (); })
      ->setEnabled (selectedPath () != rootPath);
  menu.addAction ("Copy", this, [this] () { copySelected (); })
      ->setEnabled (hasSelection);
  menu.addAction ("Paste", this, [this] () { pasteIntoSelected (); })
      ->setEnabled (canPaste);
  menu.addAction ("Delete", this, [this] () { deleteSelected (); })
      ->setEnabled (selectedPath () != rootPath);
  menu.addSeparator ();
  menu.addAction ("Open in system file manager",
                  this, [this] () { openInFileManager (); });
  menu.addAction ("Refresh", this, [this] () { refresh (); });
  menu.exec (tree->viewport ()->mapToGlobal (pos));
}

void
vault_show_explorer () {
  if (qt_defer_to_main_thread (vault_show_explorer)) return;

  if (!vault_active ()) {
    QMessageBox::warning (QApplication::activeWindow (), "Vault Explorer",
                          "No active vault. Please load a vault first.");
    return;
  }

  QTMMainTabWindow* win= QTMMainTabWindow::topTabWindow ();
  if (win == nullptr) {
    QMessageBox::warning (QApplication::activeWindow (), "Vault Explorer",
                          "No active ATHENA window.");
    return;
  }

  if (vault_explorer_widget == nullptr) {
    vault_explorer_widget= new QTMVaultExplorer ();
    vault_explorer_widget->resize (280, 600);
    QObject::connect (vault_explorer_widget, &QObject::destroyed, [] () {
      vault_explorer_widget= nullptr;
      vault_explorer_dock= nullptr;
    });
  }

  QString root= to_qstring (concretize (vault_get_root ()));
  QString name= to_qstring (vault_get_name ());
  vault_explorer_widget->setVault (root, name);

  QString title= name.isEmpty () ? "Vault Explorer" :
                 QString ("Vault Explorer - ") + name;

  if (vault_explorer_dock == nullptr) {
    vault_explorer_dock= new ads::CDockWidget (title);
    vault_explorer_dock->setObjectName ("athena-vault-explorer");
    vault_explorer_dock->resize (300, 600);
    vault_explorer_dock->setWidget (vault_explorer_widget);
    vault_explorer_dock->setFeature (
      ads::CDockWidget::DockWidgetDeleteOnClose, false);
    QTMVaultExplorer* pane= vault_explorer_widget;
    ads::CDockWidget* dock= vault_explorer_dock;
    QObject::connect (dock, &ads::CDockWidget::topLevelChanged,
                      pane, [pane, dock] (bool) {
                        pane->setFloatingResizeGripVisible (
                          dock->isInFloatingContainer ());
                      });
    QObject::connect (vault_explorer_dock, &QObject::destroyed, [] () {
      vault_explorer_dock= nullptr;
    });
    win->showAdsDockWidget (vault_explorer_dock, ads::LeftDockWidgetArea);
  }

  win->showAdsDockWidget (vault_explorer_dock, ads::LeftDockWidgetArea);

  vault_explorer_dock->setWindowTitle (title);
  vault_explorer_widget->setFloatingResizeGripVisible (
    vault_explorer_dock->isInFloatingContainer ());
  set_vault_explorer_area_width (win->dockManager (), vault_explorer_dock);
  QTimer::singleShot (0, win, [win] () {
    set_vault_explorer_area_width (win->dockManager (), vault_explorer_dock);
  });
  vault_explorer_widget->setFocus ();
}

void
vault_explorer_show_path (const QString& path) {
  vault_show_explorer ();
  if (vault_explorer_widget == nullptr) return;
  vault_explorer_widget->revealPath (path);
  QTimer::singleShot (0, vault_explorer_widget, [path] () {
    if (vault_explorer_widget != nullptr)
      vault_explorer_widget->revealPath (path);
  });
}

void
vault_explorer_track_file (url file) {
  if (get_preference ("vault explorer track current file", "off") != "on")
    return;
  if (is_none (file)) return;
  if (!is_rooted (file, "default") && !is_rooted (file, "file")) return;

  QCoreApplication* app= QCoreApplication::instance ();
  if (app != nullptr && QThread::currentThread () != app->thread ()) {
    const SchemeExecutionContext* context= current_scheme_execution_context ();
    if (context != nullptr && context->editor != nullptr &&
        context->view_id != ATHENA_NO_VIEW)
      (void) context->editor->publish_ui_text (
        actor_command_kind::ui_vault_explorer_track_file, as_string (file));
    return;
  }

  if (!vault_active () || vault_explorer_widget == nullptr) return;

  QString path= to_qstring (concretize (file));
  vault_explorer_widget->revealPath (path);
}

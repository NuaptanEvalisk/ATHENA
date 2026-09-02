/******************************************************************************
* MODULE     : QTMNamespaceExplorer.cpp
* DESCRIPTION: Qt namespace explorer pane for ATHENA vault files
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMNamespaceExplorer.hpp"
#include "QTMVaultSafeRename.hpp"

#include "QTMMainTabWindow.hpp"
#include "QTMNamespaceManager.hpp"
#include "QTMReverseHierarchyGraph.hpp"
#include "QTMVaultExplorer.hpp"
#include "QTMVaultInfoModel.hpp"
#include "boot.hpp"
#include "namespace_ontology.hpp"
#include "namespaces.hpp"
#include "qt_utilities.hpp"
#include "scheme.hpp"
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
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QSize>
#include <QSizeGrip>
#include <QSignalBlocker>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>
#include <QTextStream>

#include <algorithm>

static QTMNamespaceExplorer* namespace_explorer_widget= nullptr;
static ads::CDockWidget* namespace_explorer_dock= nullptr;
static QString namespace_explorer_clipboard_path;

namespace {
enum NamespaceExplorerItemType {
  NamespaceItem= 0,
  FileItem= 1,
  PlaceholderItem= 2,
  StatusItem= 3
};

enum NamespaceExplorerRoles {
  TypeRole= Qt::UserRole,
  NamespaceNameRole,
  NamespacePathRole,
  FilePathRole,
  PopulatedRole
};
}

static bool
namespace_explorer_use_system_trash () {
  return get_preference ("vault explorer use system trash", "off") == "on";
}

static bool
namespace_explorer_leaf_matches_only () {
  return get_preference ("vault namespace explorer leaf matches only", "off")
         == "on";
}

static bool
namespace_explorer_from_root_namespace () {
  return get_preference ("vault namespace explorer from root namespace", "off")
         == "on";
}

static bool
namespace_explorer_simplify_hierarchy () {
  return get_preference ("vault namespace explorer simplify hierarchy", "off")
         == "on";
}

static QString
namespace_explorer_root_namespace () {
  QTMVaultfileInfo info;
  return qtm_vaultfile_read (info) ? info.rootNamespace.trimmed ()
                                   : QString ();
}

static QIcon
namespace_explorer_icon (const QString& name, QStyle::StandardPixmap fallback) {
  QIcon icon= QIcon::fromTheme (name);
  if (icon.isNull ()) icon= QApplication::style ()->standardIcon (fallback);
  return icon;
}

static QString
namespace_explorer_relative_path (const QString& root, const QString& path) {
  QString rel= QDir (root).relativeFilePath (path);
  return rel == "." ? QFileInfo (path).fileName () : rel;
}

static string
namespace_explorer_scheme_quote (const QString& s) {
  QByteArray bytes= s.toUtf8 ();
  std::string out= "\"";
  for (char ch: bytes) {
    unsigned char c= (unsigned char) ch;
    if (c == '\\') out += "\\\\";
    else if (c == '"') out += "\\\"";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if (c == '\t') out += "\\t";
    else out.push_back ((char) c);
  }
  out += "\"";
  return string (out.c_str ());
}

static QString
namespace_explorer_namespace_url (const QStringList& path, bool technical) {
  QStringList parts= path;
  if (parts.isEmpty ()) return QString ();
  if (technical) parts.last ()= "!" + parts.last ();
  return QString ("tmfs://ns/") + parts.join ("/");
}

static void
namespace_explorer_load_url (const QString& tmfs) {
  if (tmfs.isEmpty ()) return;
  exec_delayed (scheme_cmd ("(load-buffer (string->url " *
                            namespace_explorer_scheme_quote (tmfs) * "))"));
}

static void
set_namespace_explorer_area_width (ads::CDockManager* manager,
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

  const int target= qMin (340, qMax (240, total / 3));
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

QTMNamespaceExplorer::QTMNamespaceExplorer (QWidget* parent)
  : QWidget (parent),
    tree (new QTreeWidget (this)),
    leafMatchesOnlyAction (nullptr),
    fromRootNamespaceAction (nullptr),
    simplifyHierarchyAction (nullptr),
    floatingSizeGrip (new QSizeGrip (this)),
    ontologyPollTimer (new QTimer (this)) {
  tree->setColumnCount (1);
  tree->setHeaderHidden (true);
  tree->setContextMenuPolicy (Qt::CustomContextMenu);
  tree->setEditTriggers (QAbstractItemView::NoEditTriggers);
  tree->setExpandsOnDoubleClick (false);
  tree->setSelectionMode (QAbstractItemView::SingleSelection);
  tree->setUniformRowHeights (true);
  tree->header ()->setSectionResizeMode (0, QHeaderView::Stretch);

  QToolBar* toolbar= new QToolBar (this);
  toolbar->setIconSize (QSize (16, 16));
  toolbar->setToolButtonStyle (Qt::ToolButtonIconOnly);
  QAction* refreshAction= toolbar->addAction (
    namespace_explorer_icon ("view-refresh", QStyle::SP_BrowserReload),
    "Refresh", this, [this] () { refresh (true); });
  refreshAction->setToolTip ("Refresh");
  leafMatchesOnlyAction= toolbar->addAction (
    namespace_explorer_icon ("view-filter", QStyle::SP_FileDialogDetailedView),
    "Leaf matches only");
  leafMatchesOnlyAction->setCheckable (true);
  leafMatchesOnlyAction->setChecked (namespace_explorer_leaf_matches_only ());
  leafMatchesOnlyAction->setToolTip (
    "Only show file matches for namespaces without child namespaces");
  connect (leafMatchesOnlyAction, &QAction::toggled, this,
           [this] (bool checked) {
             set_preference ("vault namespace explorer leaf matches only",
                             checked ? "on" : "off");
             refresh ();
           });
  fromRootNamespaceAction= toolbar->addAction (
    namespace_explorer_icon ("go-home", QStyle::SP_DirHomeIcon),
    "From root namespace");
  fromRootNamespaceAction->setCheckable (true);
  fromRootNamespaceAction->setChecked (namespace_explorer_from_root_namespace ());
  fromRootNamespaceAction->setToolTip (
    "Show only the configured root namespace at the explorer root level");
  connect (fromRootNamespaceAction, &QAction::toggled, this,
           [this] (bool checked) {
             set_preference ("vault namespace explorer from root namespace",
                             checked ? "on" : "off");
             refresh ();
           });
  simplifyHierarchyAction= toolbar->addAction (
    namespace_explorer_icon ("view-list-tree",
                             QStyle::SP_FileDialogDetailedView),
    "Simplify hierarchy");
  simplifyHierarchyAction->setCheckable (true);
  simplifyHierarchyAction->setChecked (namespace_explorer_simplify_hierarchy ());
  simplifyHierarchyAction->setToolTip (
    "Fold redundant namespace children into a separate ellipsis branch");
  connect (simplifyHierarchyAction, &QAction::toggled, this,
           [this] (bool checked) {
             set_preference ("vault namespace explorer simplify hierarchy",
                             checked ? "on" : "off");
             refresh ();
           });

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

  connect (tree, &QTreeWidget::itemExpanded,
           this, [this] (QTreeWidgetItem* item) {
             populateNamespaceItem (item);
           });
  connect (tree, &QTreeWidget::itemDoubleClicked,
           this, [this] (QTreeWidgetItem* item, int) { loadItem (item); });
  connect (tree, &QTreeWidget::customContextMenuRequested,
           this, [this] (const QPoint& pos) { showContextMenu (pos); });
  ontologyPollTimer->setInterval (50);
  connect (ontologyPollTimer, &QTimer::timeout, this, [this] () {
    string error;
    athena_namespace_ontology_status status=
      athena_namespace_ontology_get_status (error);
    if (status == athena_namespace_ontology_ready ||
        status == athena_namespace_ontology_failed ||
        status == athena_namespace_ontology_inactive)
      refresh (false);
  });
}

QSize
QTMNamespaceExplorer::sizeHint () const {
  return QSize (320, 600);
}

void
QTMNamespaceExplorer::setFloatingResizeGripVisible (bool visible) {
  floatingSizeGrip->setVisible (visible);
}

bool
QTMNamespaceExplorer::pathInVault (const QString& path) const {
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
QTMNamespaceExplorer::showError (const QString& message) const {
  QMessageBox::warning (const_cast<QTMNamespaceExplorer*> (this),
                        "Namespace Explorer", message);
}

void
QTMNamespaceExplorer::refresh (bool invalidateCache) {
  rootPath= to_qstring (concretize (vault_get_root ()));
  namespaces.clear ();
  tree->clear ();
  if (leafMatchesOnlyAction != nullptr) {
    QSignalBlocker blocker (leafMatchesOnlyAction);
    leafMatchesOnlyAction->setChecked (namespace_explorer_leaf_matches_only ());
  }
  if (fromRootNamespaceAction != nullptr) {
    QSignalBlocker blocker (fromRootNamespaceAction);
    fromRootNamespaceAction->setChecked (
      namespace_explorer_from_root_namespace ());
  }
  if (simplifyHierarchyAction != nullptr) {
    QSignalBlocker blocker (simplifyHierarchyAction);
    simplifyHierarchyAction->setChecked (
      namespace_explorer_simplify_hierarchy ());
  }

  string error;
  if (invalidateCache) athena_namespace_ontology_invalidate (true);
  athena_namespace_ontology_status ontologyStatus=
    athena_namespace_ontology_get_status (error);
  if (ontologyStatus != athena_namespace_ontology_ready) {
    QTreeWidgetItem* statusItem= new QTreeWidgetItem (tree);
    statusItem->setData (0, TypeRole, StatusItem);
    statusItem->setFlags (statusItem->flags () & ~Qt::ItemIsSelectable);
    if (ontologyStatus == athena_namespace_ontology_building) {
      statusItem->setText (0, "Indexing namespaces...");
      ontologyPollTimer->start ();
    }
    else {
      ontologyPollTimer->stop ();
      statusItem->setText (
        0, ontologyStatus == athena_namespace_ontology_failed && error != "" ?
          "Namespace indexing failed: " + to_qstring (error) :
          "Namespace index is unavailable.");
    }
    return;
  }
  ontologyPollTimer->stop ();

  QStringList names;
  for (const athena_namespace_definition& ns: athena_namespaces_list ()) {
    QString name= to_qstring (ns.name);
    namespaces.insert (name, ns);
    names << name;
  }
  names.sort ();

  if (namespace_explorer_from_root_namespace ()) {
    QString rootNamespace= namespace_explorer_root_namespace ();
    if (!rootNamespace.isEmpty ()) {
      if (names.contains (rootNamespace)) names= QStringList () << rootNamespace;
      else {
        names.clear ();
        showError ("Root namespace in Vaultfile.json is not a valid namespace: " +
                   rootNamespace);
      }
    }
  }

  for (const QString& name: names)
    addNamespaceItem (nullptr, name, QStringList () << name);
}

bool
QTMNamespaceExplorer::selectNamespace (const QString& name) {
  if (name.trimmed ().isEmpty ()) return false;
  refresh ();
  for (int i=0; i<tree->topLevelItemCount (); i++)
    if (selectNamespaceInItem (tree->topLevelItem (i), name)) {
      tree->setFocus ();
      return true;
    }
  return false;
}

bool
QTMNamespaceExplorer::selectNamespaceInItem (QTreeWidgetItem* item,
                                            const QString& name) {
  if (item == nullptr) return false;
  int type= item->data (0, TypeRole).toInt ();
  if (type != NamespaceItem && type != PlaceholderItem) return false;

  if (type == PlaceholderItem) {
    for (int i=0; i<item->childCount (); i++)
      if (selectNamespaceInItem (item->child (i), name)) {
        item->setExpanded (true);
        return true;
      }
    return false;
  }

  if (item->data (0, NamespaceNameRole).toString () == name) {
    tree->setCurrentItem (item);
    tree->scrollToItem (item, QAbstractItemView::PositionAtCenter);
    return true;
  }

  populateNamespaceItem (item);
  for (int i=0; i<item->childCount (); i++)
    if (selectNamespaceInItem (item->child (i), name)) {
      item->setExpanded (true);
      return true;
    }
  return false;
}

QStringList
QTMNamespaceExplorer::directChildNames (const QString& name,
                                        const QStringList& path) const {
  QStringList childNames;
  strings visible;
  strings folded;
  string error;
  if (!athena_namespace_ontology_children (
        from_qstring (name), false, visible, folded, error))
    return childNames;
  for (int i=0; i<N(visible); ++i) {
    QString child= to_qstring (visible[i]);
    if (!path.contains (child)) childNames << child;
  }
  return childNames;
}

void
QTMNamespaceExplorer::simplifyChildNames (const QString& parent,
                                          const QStringList& path,
                                          const QStringList& childNames,
                                          QStringList& visibleNames,
                                          QStringList& foldedNames) const {
  (void) parent;
  (void) path;
  visibleNames.clear ();
  foldedNames.clear ();

  if (!namespace_explorer_simplify_hierarchy ()) {
    visibleNames= childNames;
    return;
  }
  strings cachedVisible;
  strings cachedFolded;
  string error;
  if (!athena_namespace_ontology_children (
        from_qstring (parent), true, cachedVisible, cachedFolded, error)) {
    visibleNames= childNames;
    return;
  }
  for (int i=0; i<N(cachedVisible); ++i) {
    QString child= to_qstring (cachedVisible[i]);
    if (childNames.contains (child) && !path.contains (child))
      visibleNames << child;
  }
  for (int i=0; i<N(cachedFolded); ++i) {
    QString child= to_qstring (cachedFolded[i]);
    if (childNames.contains (child) && !path.contains (child))
      foldedNames << child;
  }
}

void
QTMNamespaceExplorer::addNamespaceItem (QTreeWidgetItem* parent,
                                        const QString& name,
                                        const QStringList& path) {
  QTreeWidgetItem* item= parent == nullptr
    ? new QTreeWidgetItem (tree) : new QTreeWidgetItem (parent);
  item->setText (0, name);
  item->setIcon (0, namespace_explorer_icon ("folder", QStyle::SP_DirIcon));
  item->setData (0, TypeRole, NamespaceItem);
  item->setData (0, NamespaceNameRole, name);
  item->setData (0, NamespacePathRole, path);
  item->setData (0, PopulatedRole, false);
  item->setChildIndicatorPolicy (QTreeWidgetItem::ShowIndicator);
  item->setToolTip (0, "Namespace: " + name);
}

void
QTMNamespaceExplorer::addFoldedNamespaceItem (QTreeWidgetItem* parent,
                                             const QStringList& names,
                                             const QStringList& path) {
  if (parent == nullptr || names.isEmpty ()) return;

  QTreeWidgetItem* item= new QTreeWidgetItem (parent);
  item->setText (0, "...");
  item->setIcon (0, namespace_explorer_icon ("view-more-horizontal",
                                             QStyle::SP_ArrowRight));
  item->setData (0, TypeRole, PlaceholderItem);
  item->setData (0, PopulatedRole, true);
  item->setToolTip (0, "Namespaces also reachable through another child");

  for (const QString& child: names) {
    QStringList childPath= path;
    childPath << child;
    addNamespaceItem (item, child, childPath);
  }
}

void
QTMNamespaceExplorer::addFileItem (QTreeWidgetItem* parent,
                                   const QString& display,
                                   const QString& path,
                                   const QString& tooltip) {
  QTreeWidgetItem* item= new QTreeWidgetItem (parent);
  item->setText (0, display);
  item->setIcon (0, namespace_explorer_icon ("text-x-generic",
                                             QStyle::SP_FileIcon));
  item->setData (0, TypeRole, FileItem);
  item->setData (0, FilePathRole, path);
  item->setToolTip (0, tooltip);
}

void
QTMNamespaceExplorer::populateNamespaceItem (QTreeWidgetItem* item) {
  if (item == nullptr ||
      item->data (0, TypeRole).toInt () != NamespaceItem ||
      item->data (0, PopulatedRole).toBool ())
    return;

  qDeleteAll (item->takeChildren ());
  QString name= item->data (0, NamespaceNameRole).toString ();
  QStringList path= item->data (0, NamespacePathRole).toStringList ();

  QStringList childNames= directChildNames (name, path);
  QStringList visibleNames;
  QStringList foldedNames;
  simplifyChildNames (name, path, childNames, visibleNames, foldedNames);

  for (const QString& child: visibleNames) {
    QStringList childPath= path;
    childPath << child;
    addNamespaceItem (item, child, childPath);
  }
  addFoldedNamespaceItem (item, foldedNames, path);

  if (!namespace_explorer_leaf_matches_only () || childNames.isEmpty ()) {
    string error;
    std::vector<athena_namespace_match> members=
      athena_namespace_members (from_qstring (name), error);
    if (error != "")
      showError ("Namespace sorter warning: " + to_qstring (error));
    for (const athena_namespace_match& m: members) {
      QString path= to_qstring (concretize (m.file));
      QFileInfo info (path);
      QString display= info.fileName ();
      if (display.isEmpty ()) display= to_qstring (m.stem) + ".ath";
      QString tooltip= namespace_explorer_relative_path (rootPath, path);
      addFileItem (item, display, path, tooltip);
    }
  }

  item->setData (0, PopulatedRole, true);
}

void
QTMNamespaceExplorer::loadItem (QTreeWidgetItem* item) {
  if (item == nullptr) return;
  int type= item->data (0, TypeRole).toInt ();
  if (type == NamespaceItem) {
    item->setExpanded (!item->isExpanded ());
    return;
  }
  if (type == PlaceholderItem) {
    item->setExpanded (!item->isExpanded ());
    return;
  }
  if (type == FileItem) openFile (item);
}

void
QTMNamespaceExplorer::openFile (QTreeWidgetItem* item) {
  QString path= item == nullptr ? QString () :
    item->data (0, FilePathRole).toString ();
  if (path.isEmpty () || !pathInVault (path) || !QFileInfo::exists (path))
    return;

  exec_delayed (scheme_cmd (list_object (symbol_object ("load-buffer"),
                            object (url_system (from_qstring (path))))));
}

void
QTMNamespaceExplorer::openNamespaceHomepage (QTreeWidgetItem* item) {
  if (item == nullptr || item->data (0, TypeRole).toInt () != NamespaceItem)
    return;

  QStringList path= item->data (0, NamespacePathRole).toStringList ();
  namespace_explorer_load_url (namespace_explorer_namespace_url (path, false));
}

void
QTMNamespaceExplorer::openNamespaceTechnicalSummary (QTreeWidgetItem* item) {
  if (item == nullptr || item->data (0, TypeRole).toInt () != NamespaceItem)
    return;

  QStringList path= item->data (0, NamespacePathRole).toStringList ();
  namespace_explorer_load_url (namespace_explorer_namespace_url (path, true));
}

QString
QTMNamespaceExplorer::selectedFilePath () const {
  QTreeWidgetItem* item= tree->currentItem ();
  if (item == nullptr || item->data (0, TypeRole).toInt () != FileItem)
    return QString ();
  return item->data (0, FilePathRole).toString ();
}

QString
QTMNamespaceExplorer::selectedDirectory () const {
  QString path= selectedFilePath ();
  if (path.isEmpty ()) return rootPath;
  return QFileInfo (path).absolutePath ();
}

bool
QTMNamespaceExplorer::writeNewFile (const QString& path) {
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
QTMNamespaceExplorer::newFileNearSelected () {
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
  else refresh (true);
}

void
QTMNamespaceExplorer::newFolderNearSelected () {
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
  else refresh (true);
}

void
QTMNamespaceExplorer::renameSelectedFile () {
  QString path= selectedFilePath ();
  if (path.isEmpty () || !pathInVault (path)) return;

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
    showError ("Could not rename file: destination already exists.");
    return;
  }
  if (qtm_safe_rename_vault_item (this, path, target)) refresh (true);
}

void
QTMNamespaceExplorer::copySelectedFile () {
  QString path= selectedFilePath ();
  if (!path.isEmpty () && pathInVault (path))
    namespace_explorer_clipboard_path= path;
}

bool
QTMNamespaceExplorer::copyRecursively (const QString& src,
                                       const QString& dst) {
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
QTMNamespaceExplorer::pasteNearSelected () {
  if (namespace_explorer_clipboard_path.isEmpty () ||
      !QFileInfo::exists (namespace_explorer_clipboard_path))
    return;

  QString dir= selectedDirectory ();
  QFileInfo source (namespace_explorer_clipboard_path);
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
  if (!copyRecursively (namespace_explorer_clipboard_path, target))
    showError ("Could not paste item.");
  else refresh (true);
}

void
QTMNamespaceExplorer::deleteSelectedFile () {
  QString path= selectedFilePath ();
  if (path.isEmpty () || !pathInVault (path)) return;

  QFileInfo info (path);
  bool useTrash= namespace_explorer_use_system_trash ();
  QString action= useTrash ? "Move to Trash" : "Delete";
  QString text= action + " '" + info.fileName () + "'?";
  if (QMessageBox::question (this, "Delete", text,
                             QMessageBox::Yes | QMessageBox::No) !=
      QMessageBox::Yes)
    return;

  bool ok= false;
  if (useTrash) {
    ok= QFile::moveToTrash (path);
  }
  else ok= QFile::remove (path);

  if (!ok) showError ("Could not delete file.");
  else refresh (true);
}

void
QTMNamespaceExplorer::openSelectedFileInFileManager () {
  QString path= selectedFilePath ();
  if (path.isEmpty () || !pathInVault (path)) return;
  QDesktopServices::openUrl (QUrl::fromLocalFile (QFileInfo (path).absolutePath ()));
}

void
QTMNamespaceExplorer::showContextMenu (const QPoint& pos) {
  QTreeWidgetItem* item= tree->itemAt (pos);
  if (item != nullptr) tree->setCurrentItem (item);
  if (item == nullptr) return;

  int type= item->data (0, TypeRole).toInt ();
  QMenu menu (this);
  if (type == NamespaceItem) {
    menu.addAction ("Open homepage", this,
                    [this, item] () { openNamespaceHomepage (item); });
    menu.addAction ("Technical summary", this,
                    [this, item] () {
                      openNamespaceTechnicalSummary (item);
                    });
    menu.addAction ("Direct hierarchy graph", this, [item] () {
      QString name= item->data (0, NamespaceNameRole).toString ();
      direct_hierarchy_graph_show_namespace (from_qstring (name));
    });
    menu.addSeparator ();
    menu.addAction ("Show in namespace manager", this, [item] () {
      QString name= item->data (0, NamespaceNameRole).toString ();
      namespace_manager_show_namespace (from_qstring (name));
    });
  }
  else if (type == FileItem) {
    menu.addAction ("Load file", this, [this, item] () { openFile (item); });
    menu.addSeparator ();
    menu.addAction ("Copy", this, [this] () { copySelectedFile (); });
    menu.addAction ("Paste", this, [this] () { pasteNearSelected (); })
        ->setEnabled (!namespace_explorer_clipboard_path.isEmpty ());
    menu.addAction ("Delete", this, [this] () { deleteSelectedFile (); });
    menu.addSeparator ();
    menu.addAction ("Show in vault explorer", this, [this] () {
      vault_explorer_show_path (selectedFilePath ());
    });
    menu.addAction ("Open in system file manager", this,
                    [this] () { openSelectedFileInFileManager (); });
    menu.addAction ("Refresh", this, [this] () { refresh (true); });
  }
  if (!menu.actions ().isEmpty ())
    menu.exec (tree->viewport ()->mapToGlobal (pos));
}

void
namespace_explorer_show () {
  if (!vault_active ()) {
    QMessageBox::warning (QApplication::activeWindow (), "Namespace Explorer",
                          "No active vault. Please load a vault first.");
    return;
  }

  QTMMainTabWindow* win= QTMMainTabWindow::topTabWindow ();
  if (win == nullptr || win->dockManager () == nullptr) {
    QMessageBox::warning (QApplication::activeWindow (), "Namespace Explorer",
                          "No active ATHENA window.");
    return;
  }

  if (namespace_explorer_widget == nullptr) {
    namespace_explorer_widget= new QTMNamespaceExplorer ();
    namespace_explorer_widget->resize (320, 600);
    QObject::connect (namespace_explorer_widget, &QObject::destroyed, [] () {
      namespace_explorer_widget= nullptr;
      namespace_explorer_dock= nullptr;
    });
  }
  namespace_explorer_widget->refresh ();

  QString title= QString ("Namespace Explorer - ") + to_qstring (vault_get_name ());
  if (namespace_explorer_dock == nullptr) {
    namespace_explorer_dock= new ads::CDockWidget (title);
    namespace_explorer_dock->setObjectName ("athena-namespace-explorer");
    namespace_explorer_dock->resize (340, 600);
    namespace_explorer_dock->setWidget (namespace_explorer_widget);
    namespace_explorer_dock->setFeature (
      ads::CDockWidget::DockWidgetDeleteOnClose, false);
    QTMNamespaceExplorer* pane= namespace_explorer_widget;
    ads::CDockWidget* dock= namespace_explorer_dock;
    QObject::connect (dock, &ads::CDockWidget::topLevelChanged,
                      pane, [pane, dock] (bool) {
                        pane->setFloatingResizeGripVisible (
                          dock->isInFloatingContainer ());
                      });
    QObject::connect (namespace_explorer_dock, &QObject::destroyed, [] () {
      namespace_explorer_dock= nullptr;
    });
    win->showAdsDockWidget (namespace_explorer_dock, ads::LeftDockWidgetArea);
  }

  win->showAdsDockWidget (namespace_explorer_dock, ads::LeftDockWidgetArea);

  namespace_explorer_dock->setWindowTitle (title);
  namespace_explorer_widget->setFloatingResizeGripVisible (
    namespace_explorer_dock->isInFloatingContainer ());
  set_namespace_explorer_area_width (win->dockManager (),
                                     namespace_explorer_dock);
  QTimer::singleShot (0, win, [win] () {
    set_namespace_explorer_area_width (win->dockManager (),
                                       namespace_explorer_dock);
  });
  namespace_explorer_widget->setFocus ();
}

void
namespace_explorer_show_namespace (string name) {
  namespace_explorer_show ();
  if (namespace_explorer_widget == nullptr) return;

  QString qname= to_qstring (name);
  if (!namespace_explorer_widget->selectNamespace (qname)) {
    QMessageBox::warning (
      QApplication::activeWindow (), "Namespace Explorer",
      QString ("Namespace \"%1\" is not visible in the namespace explorer.")
        .arg (qname));
  }
}

/******************************************************************************
* MODULE     : QTMNamespaceExplorer.hpp
* DESCRIPTION: Qt namespace explorer pane for ATHENA vault files
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMNAMESPACEEXPLORER_HPP
#define QTMNAMESPACEEXPLORER_HPP

#include "namespaces.hpp"

#include <QMap>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QWidget>

class QPoint;
class QAction;
class QSizeGrip;
class QTreeWidget;
class QTreeWidgetItem;

class QTMNamespaceExplorer : public QWidget {
public:
  QTMNamespaceExplorer (QWidget* parent = nullptr);

  QSize sizeHint () const override;
  void refresh ();
  void setFloatingResizeGripVisible (bool visible);

private:
  void populateNamespaceItem (QTreeWidgetItem* item);
  void addNamespaceItem (QTreeWidgetItem* parent, const QString& name,
                         const QStringList& path);
  void addFileItem (QTreeWidgetItem* parent, const QString& display,
                    const QString& path, const QString& tooltip);
  void loadItem (QTreeWidgetItem* item);
  void openFile (QTreeWidgetItem* item);
  void openNamespaceHomepage (QTreeWidgetItem* item);
  void openNamespaceTechnicalSummary (QTreeWidgetItem* item);
  void renameSelectedFile ();
  void newFileNearSelected ();
  void newFolderNearSelected ();
  void copySelectedFile ();
  void pasteNearSelected ();
  void deleteSelectedFile ();
  void openSelectedFileInFileManager ();
  void showContextMenu (const QPoint& pos);
  void showError (const QString& message) const;
  bool pathInVault (const QString& path) const;
  QString selectedFilePath () const;
  QString selectedDirectory () const;
  bool writeNewFile (const QString& path);
  bool copyRecursively (const QString& src, const QString& dst);

  QTreeWidget* tree;
  QAction*     leafMatchesOnlyAction;
  QSizeGrip*   floatingSizeGrip;
  QString      rootPath;
  QMap<QString, athena_namespace_definition> namespaces;
};

void namespace_explorer_show ();

#endif // QTMNAMESPACEEXPLORER_HPP

/******************************************************************************
* MODULE     : QTMVaultExplorer.hpp
* DESCRIPTION: Qt vault explorer pane for ATHENA vault files
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMVAULTEXPLORER_HPP
#define QTMVAULTEXPLORER_HPP

#include "string.hpp"
#include "url.hpp"
#include <QSize>
#include <QString>
#include <QWidget>

class QFileSystemModel;
class QModelIndex;
class QPoint;
class QSizeGrip;
class QTreeView;

class QTMVaultExplorer : public QWidget {
public:
  QTMVaultExplorer (QWidget* parent = nullptr);

  void setVault (const QString& rootPath, const QString& vaultName);
  void revealPath (const QString& path);
  QSize sizeHint () const override;
  void setFloatingResizeGripVisible (bool visible);

private:
  void    loadIndex (const QModelIndex& index);
  QModelIndex selectedIndex () const;
  QString selectedPath () const;
  QString selectedDirectory () const;
  bool    pathInVault (const QString& path) const;
  bool    copyRecursively (const QString& src, const QString& dst);
  void    showError (const QString& message) const;
  void    refresh ();
  void    loadSelected ();
  void    newFile ();
  void    newFolder ();
  void    renameSelected ();
  void    copySelected ();
  void    pasteIntoSelected ();
  void    deleteSelected ();
  void    openInFileManager ();
  void    showContextMenu (const QPoint& pos);
  bool    writeNewFile (const QString& path);

  QFileSystemModel* model;
  QTreeView*        tree;
  QSizeGrip*        floatingSizeGrip;
  QString           rootPath;
  QString           vaultName;
};

void vault_show_explorer ();
void vault_explorer_show_path (const QString& path);
void vault_explorer_track_file (url file);

#endif // QTMVAULTEXPLORER_HPP

/******************************************************************************
* MODULE     : QTMVaultBackupViewer.hpp
* DESCRIPTION: Qt vault backup viewer pane
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMVAULTBACKUPVIEWER_HPP
#define QTMVAULTBACKUPVIEWER_HPP

#include <QSize>
#include <QString>
#include <QWidget>

class QPoint;
class QTreeWidget;
class QTreeWidgetItem;

class QTMVaultBackupViewer : public QWidget {
public:
  QTMVaultBackupViewer (QWidget* parent = nullptr);

  void setVault (const QString& rootPath, const QString& vaultName);
  QSize sizeHint () const override;

private:
  void refresh ();
  void addBackupItem (const QString& kind, const QString& path,
                      const QString& displayPath);
  QList<QTreeWidgetItem*> selectedBackupItems () const;
  QString backupPath (QTreeWidgetItem* item) const;
  void openSelectedBackup ();
  void removeSelectedBackups ();
  void showContextMenu (const QPoint& pos);

  QTreeWidget* tree;
  QString      rootPath;
  QString      vaultName;
};

void vault_backup_viewer_show ();

#endif // QTMVAULTBACKUPVIEWER_HPP

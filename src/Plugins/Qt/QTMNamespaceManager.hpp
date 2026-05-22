/******************************************************************************
* MODULE     : QTMNamespaceManager.hpp
* DESCRIPTION: Qt namespace manager pane
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMNAMESPACEMANAGER_HPP
#define QTMNAMESPACEMANAGER_HPP

#include <QSize>
#include <QString>
#include <QWidget>

class QComboBox;
class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QAction;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class QTMNamespaceManager : public QWidget {
public:
  QTMNamespaceManager (QWidget* parent = nullptr);
  QSize sizeHint () const override;

  void refreshAll ();

private:
  void refreshNamespaces ();
  void refreshMembers ();
  void refreshRelations ();
  void loadNamespace (QListWidgetItem* item);
  void newNamespace ();
  void saveNamespace ();
  void deleteNamespace ();
  void updateModeUi ();
  void chooseSorterPath ();
  void chooseStylePath ();
  void addExplicitParent ();
  void removeSelectedExplicitParents ();
  void saveRelation ();
  void deleteSelectedRelation ();
  void setSelectedRelationDecision (const QString& decision);
  QStringList selectedRelationKeys () const;

  QListWidget* namespaceList;
  QLineEdit*   nameEdit;
  QComboBox*   kindCombo;
  QLineEdit*   templateEdit;
  QCheckBox*   trivialSorterCheck;
  QLineEdit*   sorterEdit;
  QPushButton* sorterBrowseButton;
  QLineEdit*   styleEdit;
  QListWidget* explicitParentsList;
  QComboBox*   explicitParentCombo;
  QListWidget* derivedParentsList;
  QAction*     saveNamespaceAction;
  QAction*     deleteNamespaceAction;
  QLabel*      modeLabel;
  QTreeWidget* membersTree;
  QTreeWidget* relationsTree;
  QLineEdit*   relationParentEdit;
  QLineEdit*   relationChildEdit;
  QComboBox*   relationDecisionCombo;
  QLabel*      statusLabel;
  QString      loadedName;
};

void namespace_manager_show ();

#endif // QTMNAMESPACEMANAGER_HPP

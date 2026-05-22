/******************************************************************************
* MODULE     : QTMNamespaceManager.cpp
* DESCRIPTION: Qt namespace manager pane
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMNamespaceManager.hpp"

#include "QTMMainTabWindow.hpp"
#include "namespaces.hpp"
#include "qt_utilities.hpp"
#include "vault.hpp"

#include <DockWidget.h>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStyle>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

static QTMNamespaceManager* namespace_manager_widget= nullptr;
static ads::CDockWidget* namespace_manager_dock= nullptr;

static strings
qline_to_strings (QLineEdit* edit) {
  strings out;
  QStringList parts= edit->text ().split (',', Qt::SkipEmptyParts);
  for (QString part: parts) {
    QString item= part.trimmed ();
    if (!item.isEmpty ()) out << from_qstring (item);
  }
  return out;
}

static QString
strings_to_qline (const strings& xs) {
  QStringList parts;
  for (int i=0; i<N(xs); i++) parts << to_qstring (xs[i]);
  return parts.join (", ");
}

static QIcon
namespace_icon (const QString& name, QStyle::StandardPixmap fallback) {
  QIcon icon= QIcon::fromTheme (name);
  if (icon.isNull ()) icon= QApplication::style ()->standardIcon (fallback);
  return icon;
}

QTMNamespaceManager::QTMNamespaceManager (QWidget* parent)
  : QWidget (parent),
    namespaceList (new QListWidget (this)),
    nameEdit (new QLineEdit (this)),
    kindCombo (new QComboBox (this)),
    templateEdit (new QLineEdit (this)),
    trivialSorterCheck (new QCheckBox ("Use trivial sorting algorithm", this)),
    sorterEdit (new QLineEdit (this)),
    styleEdit (new QLineEdit (this)),
    parentsEdit (new QLineEdit (this)),
    derivedParentsEdit (new QLineEdit (this)),
    saveNamespaceAction (nullptr),
    deleteNamespaceAction (nullptr),
    modeLabel (new QLabel (this)),
    membersTree (new QTreeWidget (this)),
    relationsTree (new QTreeWidget (this)),
    relationParentEdit (new QLineEdit (this)),
    relationChildEdit (new QLineEdit (this)),
    relationDecisionCombo (new QComboBox (this)),
    statusLabel (new QLabel (this)) {
  kindCombo->addItems (QStringList () << "concrete" << "semi-concrete"
                                      << "abstract");
  relationDecisionCombo->addItems (QStringList () << "allow" << "deny");

  namespaceList->setSelectionMode (QAbstractItemView::SingleSelection);
  namespaceList->setUniformItemSizes (true);

  membersTree->setColumnCount (4);
  membersTree->setHeaderLabels (QStringList () << "File" << "Captures"
                                               << "Ambiguous" << "Path");
  membersTree->setAlternatingRowColors (true);
  membersTree->setUniformRowHeights (true);
  membersTree->header ()->setSectionResizeMode (0, QHeaderView::ResizeToContents);
  membersTree->header ()->setSectionResizeMode (1, QHeaderView::Stretch);
  membersTree->header ()->setSectionResizeMode (2, QHeaderView::ResizeToContents);
  membersTree->header ()->setSectionResizeMode (3, QHeaderView::Stretch);

  relationsTree->setColumnCount (4);
  relationsTree->setHeaderLabels (QStringList () << "Parent" << "Child"
                                                 << "Decision" << "Source");
  relationsTree->setAlternatingRowColors (true);
  relationsTree->setSelectionMode (QAbstractItemView::ExtendedSelection);
  relationsTree->setUniformRowHeights (true);
  relationsTree->header ()->setSectionResizeMode (0, QHeaderView::ResizeToContents);
  relationsTree->header ()->setSectionResizeMode (1, QHeaderView::ResizeToContents);
  relationsTree->header ()->setSectionResizeMode (2, QHeaderView::ResizeToContents);
  relationsTree->header ()->setSectionResizeMode (3, QHeaderView::Stretch);

  QToolBar* toolbar= new QToolBar (this);
  toolbar->setIconSize (QSize (16, 16));
  toolbar->setToolButtonStyle (Qt::ToolButtonTextBesideIcon);
  toolbar->addAction (namespace_icon ("document-new", QStyle::SP_FileIcon),
                      "Start new", this, [this] () { newNamespace (); })
          ->setToolTip ("Clear the form and start creating a new namespace");
  saveNamespaceAction=
    toolbar->addAction (namespace_icon ("document-save",
                                        QStyle::SP_DialogSaveButton),
                        "Create namespace", this,
                        [this] () { saveNamespace (); });
  deleteNamespaceAction=
    toolbar->addAction (namespace_icon ("edit-delete", QStyle::SP_TrashIcon),
                        "Delete selected", this,
                        [this] () { deleteNamespace (); });
  deleteNamespaceAction->setToolTip ("Delete the selected namespace");
  toolbar->addSeparator ();
  toolbar->addAction (namespace_icon ("view-refresh", QStyle::SP_BrowserReload),
                      "Refresh", this, [this] () { refreshAll (); })
          ->setToolTip ("Refresh");

  QFormLayout* form= new QFormLayout ();
  form->setFieldGrowthPolicy (QFormLayout::ExpandingFieldsGrow);
  form->addRow ("Name", nameEdit);
  form->addRow ("Kind", kindCombo);
  form->addRow ("Template", templateEdit);
  QWidget* sorterWidget= new QWidget (this);
  QVBoxLayout* sorterLayout= new QVBoxLayout (sorterWidget);
  sorterLayout->setContentsMargins (0, 0, 0, 0);
  sorterLayout->setSpacing (4);
  sorterLayout->addWidget (sorterEdit);
  sorterLayout->addWidget (trivialSorterCheck);
  form->addRow ("Sorter .c path", sorterWidget);
  form->addRow ("Style path", styleEdit);
  form->addRow ("Parents", parentsEdit);
  form->addRow ("Derived parents", derivedParentsEdit);

  QHBoxLayout* relationEditLayout= new QHBoxLayout ();
  relationEditLayout->addWidget (new QLabel ("Parent", this));
  relationEditLayout->addWidget (relationParentEdit, 2);
  relationEditLayout->addWidget (new QLabel ("Child", this));
  relationEditLayout->addWidget (relationChildEdit, 2);
  relationEditLayout->addWidget (relationDecisionCombo);
  QPushButton* saveRel= new QPushButton ("Save relation", this);
  QPushButton* allowRel= new QPushButton ("Allow selected", this);
  QPushButton* denyRel= new QPushButton ("Deny selected", this);
  QPushButton* delRel= new QPushButton ("Delete selected", this);
  relationEditLayout->addWidget (saveRel);
  relationEditLayout->addWidget (allowRel);
  relationEditLayout->addWidget (denyRel);
  relationEditLayout->addWidget (delRel);

  QWidget* editor= new QWidget (this);
  QVBoxLayout* editorLayout= new QVBoxLayout (editor);
  editorLayout->setContentsMargins (8, 0, 0, 0);
  modeLabel->setTextFormat (Qt::PlainText);
  editorLayout->addWidget (modeLabel);
  editorLayout->addLayout (form);
  editorLayout->addWidget (new QLabel ("Matched files", this));
  editorLayout->addWidget (membersTree, 3);
  editorLayout->addWidget (new QLabel ("Cached hierarchy decisions", this));
  editorLayout->addWidget (relationsTree, 2);
  editorLayout->addLayout (relationEditLayout);

  QSplitter* splitter= new QSplitter (Qt::Horizontal, this);
  splitter->addWidget (namespaceList);
  splitter->addWidget (editor);
  splitter->setStretchFactor (0, 1);
  splitter->setStretchFactor (1, 4);
  splitter->setSizes (QList<int> () << 260 << 900);

  statusLabel->setTextFormat (Qt::PlainText);
  statusLabel->setWordWrap (true);

  QVBoxLayout* layout= new QVBoxLayout (this);
  layout->setContentsMargins (0, 0, 0, 0);
  layout->addWidget (toolbar);
  layout->addWidget (splitter);
  layout->addWidget (statusLabel);

  connect (namespaceList, &QListWidget::itemSelectionChanged, this, [this] () {
    QListWidgetItem* item= namespaceList->currentItem ();
    if (item != nullptr) loadNamespace (item);
  });
  connect (relationsTree, &QTreeWidget::itemSelectionChanged, this, [this] () {
    QTreeWidgetItem* item= relationsTree->currentItem ();
    if (item == nullptr) return;
    relationParentEdit->setText (item->text (0));
    relationChildEdit->setText (item->text (1));
    relationDecisionCombo->setCurrentText (item->text (2));
  });
  connect (saveRel, &QPushButton::clicked, this, [this] () { saveRelation (); });
  connect (allowRel, &QPushButton::clicked, this,
           [this] () { setSelectedRelationDecision ("allow"); });
  connect (denyRel, &QPushButton::clicked, this,
           [this] () { setSelectedRelationDecision ("deny"); });
  connect (delRel, &QPushButton::clicked, this,
           [this] () { deleteSelectedRelation (); });
  connect (trivialSorterCheck, &QCheckBox::toggled, this,
           [this] (bool on) {
             sorterEdit->setEnabled (!on);
             sorterEdit->setPlaceholderText (
               on ? "Built-in sorter returns 0 for every comparison" : "");
           });
  updateModeUi ();
}

QSize
QTMNamespaceManager::sizeHint () const {
  return QSize (1180, 760);
}

void
QTMNamespaceManager::refreshAll () {
  refreshNamespaces ();
  refreshRelations ();
  refreshMembers ();
}

void
QTMNamespaceManager::refreshNamespaces () {
  QString selected= namespaceList->currentItem () == nullptr
    ? loadedName : namespaceList->currentItem ()->text ();
  namespaceList->clear ();
  for (const athena_namespace_definition& ns: athena_namespaces_list ())
    namespaceList->addItem (to_qstring (ns.name));

  QList<QListWidgetItem*> matches=
    namespaceList->findItems (selected, Qt::MatchExactly);
  if (!matches.isEmpty ()) namespaceList->setCurrentItem (matches.first ());
  else if (namespaceList->count () > 0) namespaceList->setCurrentRow (0);
}

void
QTMNamespaceManager::refreshMembers () {
  membersTree->clear ();
  QString name= nameEdit->text ().trimmed ();
  if (name.isEmpty ()) return;

  string error;
  std::vector<athena_namespace_match> members=
    athena_namespace_members (from_qstring (name), error);
  for (const athena_namespace_match& m: members) {
    QStringList caps;
    for (int i=0; i<N(m.captures); i++)
      caps << QString ("%1=%2").arg (to_qstring (m.capture_types[i]),
                                     to_qstring (m.captures[i]));
    QTreeWidgetItem* item= new QTreeWidgetItem ();
    item->setText (0, to_qstring (m.stem));
    item->setText (1, caps.join (", "));
    item->setText (2, m.ambiguous ? "yes" : "");
    item->setText (3, to_qstring (concretize (m.file)));
    membersTree->addTopLevelItem (item);
  }
  if (error == "")
    statusLabel->setText (QString ("%1 matched file(s).").arg (members.size ()));
  else
    statusLabel->setText (QString ("%1 matched file(s). Sorter/template warning: %2")
                          .arg (members.size ())
                          .arg (to_qstring (error)));
}

void
QTMNamespaceManager::refreshRelations () {
  relationsTree->clear ();
  for (const athena_namespace_relation& rel: athena_namespace_relations_list ()) {
    QTreeWidgetItem* item= new QTreeWidgetItem ();
    item->setText (0, to_qstring (rel.parent));
    item->setText (1, to_qstring (rel.child));
    item->setText (2, to_qstring (rel.decision));
    item->setText (3, to_qstring (rel.source));
    relationsTree->addTopLevelItem (item);
  }
}

void
QTMNamespaceManager::loadNamespace (QListWidgetItem* item) {
  if (item == nullptr) return;
  athena_namespace_definition ns;
  if (!athena_namespace_get (from_qstring (item->text ()), ns)) return;

  loadedName= item->text ();
  nameEdit->setText (to_qstring (ns.name));
  kindCombo->setCurrentText (to_qstring (ns.kind));
  templateEdit->setText (to_qstring (ns.templ));
  trivialSorterCheck->setChecked (ns.sorter_trivial);
  sorterEdit->setText (to_qstring (ns.sorter_path));
  sorterEdit->setEnabled (!ns.sorter_trivial);
  styleEdit->setText (to_qstring (ns.style_path));
  parentsEdit->setText (strings_to_qline (ns.parents));
  derivedParentsEdit->setText (strings_to_qline (ns.derived_parents));
  updateModeUi ();
  refreshMembers ();
}

void
QTMNamespaceManager::newNamespace () {
  loadedName.clear ();
  namespaceList->clearSelection ();
  nameEdit->clear ();
  kindCombo->setCurrentText ("concrete");
  templateEdit->clear ();
  trivialSorterCheck->setChecked (false);
  sorterEdit->clear ();
  sorterEdit->setEnabled (true);
  styleEdit->clear ();
  parentsEdit->clear ();
  derivedParentsEdit->clear ();
  membersTree->clear ();
  statusLabel->setText ("Fill the form, then click Create namespace.");
  updateModeUi ();
  nameEdit->setFocus ();
}

void
QTMNamespaceManager::saveNamespace () {
  bool creating= loadedName.isEmpty ();
  athena_namespace_definition ns;
  ns.name= from_qstring (nameEdit->text ().trimmed ());
  ns.kind= from_qstring (kindCombo->currentText ());
  ns.templ= from_qstring (templateEdit->text ().trimmed ());
  ns.sorter_trivial= trivialSorterCheck->isChecked ();
  ns.sorter_path= from_qstring (sorterEdit->text ().trimmed ());
  ns.style_path= from_qstring (styleEdit->text ().trimmed ());
  ns.parents= qline_to_strings (parentsEdit);
  ns.derived_parents= qline_to_strings (derivedParentsEdit);

  if (ns.name == "") {
    QMessageBox::warning (this, "Namespace Manager",
                          "Namespace name cannot be empty.");
    return;
  }

  athena_namespace_definition existing;
  bool targetExists= athena_namespace_get (ns.name, existing);
  QString targetName= to_qstring (ns.name);
  if (creating && targetExists) {
    if (QMessageBox::question (
          this, "Update Namespace",
          QString ("Namespace \"%1\" already exists. Update it instead?")
            .arg (targetName),
          QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
      return;
  }
  else if (!creating && loadedName != targetName) {
    if (targetExists) {
      QMessageBox::warning (
        this, "Rename Namespace",
        QString ("Cannot rename to \"%1\" because that namespace already "
                 "exists. Select it from the list to edit it.")
          .arg (targetName));
      return;
    }
    if (QMessageBox::question (
          this, "Rename Namespace",
          QString ("Rename namespace \"%1\" to \"%2\"?")
            .arg (loadedName, targetName),
          QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
      return;
  }

  string error;
  if (!athena_namespace_save (ns, error)) {
    QMessageBox::warning (this, "Namespace Manager", to_qstring (error));
    return;
  }
  if (!loadedName.isEmpty () && loadedName != to_qstring (ns.name)) {
    string ignored;
    athena_namespace_remove (from_qstring (loadedName), ignored);
  }
  loadedName= to_qstring (ns.name);
  refreshNamespaces ();
  refreshRelations ();
  refreshMembers ();
  updateModeUi ();
  statusLabel->setText (creating ? "Namespace created." : "Namespace updated.");
}

void
QTMNamespaceManager::deleteNamespace () {
  QString name= nameEdit->text ().trimmed ();
  if (name.isEmpty ()) return;
  if (QMessageBox::question (this, "Delete Namespace",
                             QString ("Delete namespace \"%1\"?").arg (name),
                             QMessageBox::Yes | QMessageBox::No) !=
      QMessageBox::Yes)
    return;

  string error;
  if (!athena_namespace_remove (from_qstring (name), error)) {
    QMessageBox::warning (this, "Namespace Manager", to_qstring (error));
    return;
  }
  newNamespace ();
  refreshAll ();
  statusLabel->setText ("Namespace deleted.");
}

void
QTMNamespaceManager::updateModeUi () {
  bool creating= loadedName.isEmpty ();
  if (creating) {
    modeLabel->setText ("Mode: creating a new namespace");
    saveNamespaceAction->setText ("Create namespace");
    saveNamespaceAction->setToolTip (
      "Create a namespace from the current form fields");
  }
  else {
    modeLabel->setText (QString ("Mode: editing namespace \"%1\"")
                        .arg (loadedName));
    saveNamespaceAction->setText ("Update namespace");
    saveNamespaceAction->setToolTip (
      "Update the selected namespace using the current form fields");
  }
  deleteNamespaceAction->setEnabled (!creating);
}

void
QTMNamespaceManager::saveRelation () {
  string error;
  if (!athena_namespace_relation_set (
        from_qstring (relationParentEdit->text ().trimmed ()),
        from_qstring (relationChildEdit->text ().trimmed ()),
        from_qstring (relationDecisionCombo->currentText ()), "user", error)) {
    QMessageBox::warning (this, "Namespace Manager", to_qstring (error));
    return;
  }
  refreshRelations ();
}

QStringList
QTMNamespaceManager::selectedRelationKeys () const {
  QStringList keys;
  QList<QTreeWidgetItem*> items= relationsTree->selectedItems ();
  if (items.isEmpty () && relationsTree->currentItem () != nullptr)
    items << relationsTree->currentItem ();
  for (QTreeWidgetItem* item: items)
    keys << item->text (0) + "\n" + item->text (1);
  return keys;
}

void
QTMNamespaceManager::deleteSelectedRelation () {
  for (const QString& key: selectedRelationKeys ()) {
    QStringList parts= key.split ('\n');
    if (parts.size () != 2) continue;
    string error;
    athena_namespace_relation_remove (from_qstring (parts[0]),
                                      from_qstring (parts[1]), error);
  }
  refreshRelations ();
}

void
QTMNamespaceManager::setSelectedRelationDecision (const QString& decision) {
  for (const QString& key: selectedRelationKeys ()) {
    QStringList parts= key.split ('\n');
    if (parts.size () != 2) continue;
    string error;
    athena_namespace_relation_set (from_qstring (parts[0]),
                                   from_qstring (parts[1]),
                                   from_qstring (decision), "user", error);
  }
  refreshRelations ();
}

void
namespace_manager_show () {
  if (!vault_active ()) {
    QMessageBox::warning (QApplication::activeWindow (), "Namespace Manager",
                          "No active vault. Please load a vault first.");
    return;
  }

  QTMMainTabWindow* win= QTMMainTabWindow::topTabWindow ();
  if (win == nullptr || win->dockManager () == nullptr) {
    QMessageBox::warning (QApplication::activeWindow (), "Namespace Manager",
                          "No active ATHENA window.");
    return;
  }

  if (namespace_manager_widget == nullptr) {
    namespace_manager_widget= new QTMNamespaceManager ();
    QObject::connect (namespace_manager_widget, &QObject::destroyed, [] () {
      namespace_manager_widget= nullptr;
      namespace_manager_dock= nullptr;
    });
  }
  namespace_manager_widget->refreshAll ();

  QString title= QString ("Namespace Manager - ") + to_qstring (vault_get_name ());
  if (namespace_manager_dock == nullptr) {
    namespace_manager_dock= new ads::CDockWidget (title);
    namespace_manager_dock->setObjectName ("athena-namespace-manager");
    namespace_manager_dock->resize (1180, 760);
    namespace_manager_dock->setWidget (namespace_manager_widget);
    namespace_manager_dock->setFeature (
      ads::CDockWidget::DockWidgetDeleteOnClose, false);
    QObject::connect (namespace_manager_dock, &QObject::destroyed, [] () {
      namespace_manager_dock= nullptr;
    });
  }

  if (namespace_manager_dock->dockAreaWidget () == nullptr ||
      namespace_manager_dock->dockContainer () == nullptr) {
    win->dockManager ()->addDockWidget (ads::RightDockWidgetArea,
                                        namespace_manager_dock);
  }

  namespace_manager_dock->setWindowTitle (title);
  namespace_manager_dock->show ();
  namespace_manager_dock->raise ();
  namespace_manager_widget->setFocus ();
}

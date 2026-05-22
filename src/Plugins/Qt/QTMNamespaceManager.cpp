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
#include "scheme.hpp"
#include "vault.hpp"

#include <DockWidget.h>
#include <KIOFileWidgets/KFileCustomDialog>
#include <KIOFileWidgets/KFileWidget>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStyle>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

static QTMNamespaceManager* namespace_manager_widget= nullptr;
static ads::CDockWidget* namespace_manager_dock= nullptr;

static QIcon
namespace_icon (const QString& name, QStyle::StandardPixmap fallback) {
  QIcon icon= QIcon::fromTheme (name);
  if (icon.isNull ()) icon= QApplication::style ()->standardIcon (fallback);
  return icon;
}

static strings
qlist_to_strings (QListWidget* list) {
  strings out;
  for (int i=0; i<list->count (); i++)
    out << from_qstring (list->item (i)->text ());
  return out;
}

static void
set_qlist_strings (QListWidget* list, const strings& xs) {
  list->clear ();
  for (int i=0; i<N(xs); i++) list->addItem (to_qstring (xs[i]));
}

static bool
qlist_contains (QListWidget* list, const QString& text) {
  return !list->findItems (text, Qt::MatchExactly).isEmpty ();
}

static QString
namespace_selected_local_file (KFileWidget* file_widget) {
  QString selected= file_widget->selectedFile ();
  if (!selected.isEmpty ()) return selected;
  QUrl selected_url= file_widget->selectedUrl ();
  return selected_url.isLocalFile () ? selected_url.toLocalFile () : QString ();
}

static QString
namespace_vault_root_path () {
  return QFileInfo (to_qstring (concretize (vault_get_root ())))
    .absoluteFilePath ();
}

static QString
namespace_existing_path_for_edit (QLineEdit* edit) {
  QString text= edit->text ().trimmed ();
  QString root= namespace_vault_root_path ();
  if (text.isEmpty ()) return root;
  QFileInfo info (text);
  if (info.isAbsolute ()) return info.absoluteFilePath ();
  return QDir (root).absoluteFilePath (text);
}

static QString
namespace_relative_path_if_possible (const QString& path) {
  QString root= namespace_vault_root_path ();
  QString rel= QDir (root).relativeFilePath (path);
  if (rel == "." || rel.startsWith ("../") || rel == ".." ||
      QDir::isAbsolutePath (rel))
    return path;
  return rel;
}

static QString
namespace_choose_file (QWidget* parent, QLineEdit* edit,
                       const QString& title, const QString& filter) {
  QString start_path= namespace_existing_path_for_edit (edit);
  QFileInfo start_info (start_path);
  if (!start_info.exists ()) start_path= start_info.absolutePath ();

  KFileCustomDialog dialog (QUrl::fromLocalFile (start_path), parent);
  dialog.setWindowTitle (title);
  dialog.setOperationMode (KFileWidget::Opening);

  KFileWidget* file_widget= dialog.fileWidget ();
  file_widget->setMode (KFile::File | KFile::ExistingOnly | KFile::LocalOnly);
  if (!filter.isEmpty ()) file_widget->setFilter (filter);

  QRect r;
  QSize dialog_size= dialog.sizeHint ();
  if (dialog_size.width () > 860) dialog_size.setWidth (860);
  r.setSize (dialog_size);
  QWidget* anchor= parent == nullptr ? QApplication::activeWindow () : parent;
  if (anchor != nullptr) r.moveCenter (anchor->geometry ().center ());
  dialog.setGeometry (r);

  if (dialog.exec () != QDialog::Accepted) return QString ();
  QString selected= namespace_selected_local_file (file_widget);
  return selected.isEmpty () ? QString () :
    namespace_relative_path_if_possible (selected);
}

static bool
namespace_at_least_semi_concrete (const athena_namespace_definition& ns) {
  return ns.kind != "abstract" && ns.templ != "" &&
         (ns.sorter_trivial || ns.sorter_path != "");
}

static QStringList
namespace_lines (const QString& text) {
  QStringList out;
  for (const QString& line: text.split ('\n')) {
    QString trimmed= line.trimmed ();
    if (!trimmed.isEmpty () && !out.contains (trimmed)) out << trimmed;
  }
  return out;
}

static bool
namespace_confirm_sorter_compatibility (
  QWidget* parent, const athena_namespace_definition& first,
  const athena_namespace_definition& second) {
  string error;
  string first_source, second_source;
  if (!athena_namespace_sorter_source (first, first_source, error)) {
    QMessageBox::warning (parent, "Generate Sub-products", to_qstring (error));
    return false;
  }
  if (!athena_namespace_sorter_source (second, second_source, error)) {
    QMessageBox::warning (parent, "Generate Sub-products", to_qstring (error));
    return false;
  }

  QDialog dialog (parent);
  dialog.setWindowTitle ("Confirm Sorter Compatibility");
  QVBoxLayout* layout= new QVBoxLayout (&dialog);
  QLabel* label= new QLabel (
    "Confirm that these two namespace sorting algorithms are compatible.",
    &dialog);
  label->setWordWrap (true);
  layout->addWidget (label);

  QHBoxLayout* sources= new QHBoxLayout ();
  auto add_source= [&] (const QString& title, const QString& source) {
    QWidget* pane= new QWidget (&dialog);
    QVBoxLayout* pane_layout= new QVBoxLayout (pane);
    pane_layout->setContentsMargins (0, 0, 0, 0);
    pane_layout->addWidget (new QLabel (title, pane));
    QPlainTextEdit* edit= new QPlainTextEdit (pane);
    edit->setReadOnly (true);
    edit->setLineWrapMode (QPlainTextEdit::NoWrap);
    edit->setPlainText (source);
    edit->setMinimumSize (420, 300);
    pane_layout->addWidget (edit);
    sources->addWidget (pane);
  };
  add_source (to_qstring (first.name), utf8_to_qstring (first_source));
  add_source (to_qstring (second.name), utf8_to_qstring (second_source));
  layout->addLayout (sources);

  QDialogButtonBox* buttons= new QDialogButtonBox (
    QDialogButtonBox::Yes | QDialogButtonBox::No, &dialog);
  QObject::connect (buttons, &QDialogButtonBox::accepted, &dialog,
                    &QDialog::accept);
  QObject::connect (buttons, &QDialogButtonBox::rejected, &dialog,
                    &QDialog::reject);
  layout->addWidget (buttons);
  return dialog.exec () == QDialog::Accepted;
}

static QStringList
namespace_ask_subproduct_names (QWidget* parent, const QString& first,
                                const QString& second) {
  QDialog dialog (parent);
  dialog.setWindowTitle ("Sub-product Names");
  QVBoxLayout* layout= new QVBoxLayout (&dialog);
  QLabel* label= new QLabel (
    "Enter one namespace name per line. Each name will be created with the "
    "same sub-product template and product sorter.",
    &dialog);
  label->setWordWrap (true);
  layout->addWidget (label);
  QPlainTextEdit* edit= new QPlainTextEdit (&dialog);
  edit->setPlainText (first + " - " + second + "\n" +
                      second + " - " + first);
  edit->setMinimumSize (520, 180);
  layout->addWidget (edit);
  QDialogButtonBox* buttons= new QDialogButtonBox (
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  QObject::connect (buttons, &QDialogButtonBox::accepted, &dialog,
                    &QDialog::accept);
  QObject::connect (buttons, &QDialogButtonBox::rejected, &dialog,
                    &QDialog::reject);
  layout->addWidget (buttons);
  if (dialog.exec () != QDialog::Accepted) return QStringList ();
  return namespace_lines (edit->toPlainText ());
}

static bool
namespace_ask_subproduct_template (QWidget* parent,
                                   const athena_namespace_definition& first,
                                   const athena_namespace_definition& second,
                                   QString& templ) {
  while (true) {
    QDialog dialog (parent);
    dialog.setWindowTitle ("Sub-product Naming Template");
    QVBoxLayout* layout= new QVBoxLayout (&dialog);
    QLabel* label= new QLabel (
      "Confirm or edit the naming template for the generated sub-product "
      "namespaces. It must derive from both selected namespace templates.",
      &dialog);
    label->setWordWrap (true);
    layout->addWidget (label);
    QLineEdit* edit= new QLineEdit (templ, &dialog);
    edit->setMinimumWidth (560);
    layout->addWidget (edit);
    QDialogButtonBox* buttons= new QDialogButtonBox (
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect (buttons, &QDialogButtonBox::accepted, &dialog,
                      &QDialog::accept);
    QObject::connect (buttons, &QDialogButtonBox::rejected, &dialog,
                      &QDialog::reject);
    layout->addWidget (buttons);
    if (dialog.exec () != QDialog::Accepted) return false;

    QString candidate= edit->text ().trimmed ();
    if (candidate.isEmpty ()) {
      QMessageBox::warning (parent, "Sub-product Template",
                            "The template cannot be empty.");
      continue;
    }
    string error;
    bool d1= false, d2= false;
    if (!athena_namespace_template_derives (from_qstring (candidate),
                                            first.templ, d1, error) ||
        !athena_namespace_template_derives (from_qstring (candidate),
                                            second.templ, d2, error)) {
      QMessageBox::warning (parent, "Sub-product Template",
                            to_qstring (error));
      continue;
    }
    if (!d1 || !d2) {
      QMessageBox::warning (
        parent, "Sub-product Template",
        "The template does not derive from both selected namespace templates.");
      continue;
    }
    templ= candidate;
    return true;
  }
}

QTMNamespaceManager::QTMNamespaceManager (QWidget* parent)
  : QWidget (parent),
    namespaceList (new QListWidget (this)),
    nameEdit (new QLineEdit (this)),
    kindCombo (new QComboBox (this)),
    templateEdit (new QLineEdit (this)),
    trivialSorterCheck (new QCheckBox ("Use trivial sorting algorithm", this)),
    sorterEdit (new QLineEdit (this)),
    sorterBrowseButton (new QPushButton ("Browse...", this)),
    styleEdit (new QLineEdit (this)),
    explicitParentsList (new QListWidget (this)),
    explicitParentCombo (new QComboBox (this)),
    derivedParentsList (new QListWidget (this)),
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

  namespaceList->setSelectionMode (QAbstractItemView::ExtendedSelection);
  namespaceList->setUniformItemSizes (true);
  explicitParentsList->setSelectionMode (QAbstractItemView::ExtendedSelection);
  explicitParentsList->setUniformItemSizes (true);
  explicitParentsList->setMinimumHeight (84);
  explicitParentCombo->setEditable (true);
  derivedParentsList->setSelectionMode (QAbstractItemView::NoSelection);
  derivedParentsList->setUniformItemSizes (true);
  derivedParentsList->setMinimumHeight (72);
  derivedParentsList->setFocusPolicy (Qt::NoFocus);

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
  toolbar->addAction (namespace_icon ("list-add", QStyle::SP_FileDialogNewFolder),
                      "Generate sub-products", this,
                      [this] () { generateSubproducts (); })
          ->setToolTip ("Generate sub-product namespaces from two selected namespaces");
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
  QHBoxLayout* sorterPathLayout= new QHBoxLayout ();
  sorterPathLayout->setContentsMargins (0, 0, 0, 0);
  sorterPathLayout->setSpacing (4);
  sorterPathLayout->addWidget (sorterEdit, 1);
  sorterPathLayout->addWidget (sorterBrowseButton);
  sorterLayout->addLayout (sorterPathLayout);
  sorterLayout->addWidget (trivialSorterCheck);
  form->addRow ("Sorter .c path", sorterWidget);
  QWidget* styleWidget= new QWidget (this);
  QHBoxLayout* styleLayout= new QHBoxLayout (styleWidget);
  styleLayout->setContentsMargins (0, 0, 0, 0);
  styleLayout->setSpacing (4);
  QPushButton* styleBrowse= new QPushButton ("Browse...", this);
  styleLayout->addWidget (styleEdit, 1);
  styleLayout->addWidget (styleBrowse);
  form->addRow ("Style path", styleWidget);
  QWidget* explicitParentsWidget= new QWidget (this);
  QVBoxLayout* explicitParentsLayout= new QVBoxLayout (explicitParentsWidget);
  explicitParentsLayout->setContentsMargins (0, 0, 0, 0);
  explicitParentsLayout->setSpacing (4);
  explicitParentsLayout->addWidget (explicitParentsList);
  QHBoxLayout* explicitParentControls= new QHBoxLayout ();
  explicitParentControls->setContentsMargins (0, 0, 0, 0);
  explicitParentControls->setSpacing (4);
  QPushButton* addParent= new QPushButton ("Add", this);
  QPushButton* removeParent= new QPushButton ("Remove selected", this);
  explicitParentControls->addWidget (explicitParentCombo, 1);
  explicitParentControls->addWidget (addParent);
  explicitParentControls->addWidget (removeParent);
  explicitParentsLayout->addLayout (explicitParentControls);
  form->addRow ("Explicit parents", explicitParentsWidget);
  form->addRow ("Derived parents", derivedParentsList);

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
  connect (sorterBrowseButton, &QPushButton::clicked, this,
           [this] () { chooseSorterPath (); });
  connect (styleBrowse, &QPushButton::clicked, this,
           [this] () { chooseStylePath (); });
  connect (addParent, &QPushButton::clicked, this,
           [this] () { addExplicitParent (); });
  connect (removeParent, &QPushButton::clicked, this,
           [this] () { removeSelectedExplicitParents (); });
  connect (explicitParentCombo->lineEdit (), &QLineEdit::returnPressed, this,
           [this] () { addExplicitParent (); });
  connect (trivialSorterCheck, &QCheckBox::toggled, this,
           [this] (bool on) {
             sorterEdit->setEnabled (!on);
             sorterBrowseButton->setEnabled (!on);
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
  string error;
  if (!athena_namespace_refresh_derived (error) && error != "")
    statusLabel->setText ("Derived parent refresh failed: " +
                          to_qstring (error));

  QString selected= namespaceList->currentItem () == nullptr
    ? loadedName : namespaceList->currentItem ()->text ();
  namespaceList->clear ();
  QString comboSelected= explicitParentCombo->currentText ();
  explicitParentCombo->clear ();
  for (const athena_namespace_definition& ns: athena_namespaces_list ()) {
    QString name= to_qstring (ns.name);
    namespaceList->addItem (to_qstring (ns.name));
    explicitParentCombo->addItem (name);
  }
  explicitParentCombo->setCurrentText (comboSelected);

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
  sorterBrowseButton->setEnabled (!ns.sorter_trivial);
  styleEdit->setText (to_qstring (ns.style_path));
  set_qlist_strings (explicitParentsList, ns.parents);
  set_qlist_strings (derivedParentsList, ns.derived_parents);
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
  sorterBrowseButton->setEnabled (true);
  styleEdit->clear ();
  explicitParentsList->clear ();
  derivedParentsList->clear ();
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
  ns.parents= qlist_to_strings (explicitParentsList);
  ns.derived_parents= strings ();

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

QStringList
QTMNamespaceManager::selectedNamespaceNames () const {
  QStringList names;
  QList<QListWidgetItem*> items= namespaceList->selectedItems ();
  for (QListWidgetItem* item: items)
    if (!names.contains (item->text ())) names << item->text ();
  if (names.isEmpty () && namespaceList->currentItem () != nullptr)
    names << namespaceList->currentItem ()->text ();
  return names;
}

void
QTMNamespaceManager::generateSubproducts () {
  QStringList names= selectedNamespaceNames ();
  if (names.size () != 2) {
    QMessageBox::warning (
      this, "Generate Sub-products",
      "Select exactly two namespaces in the namespace list first.");
    return;
  }

  athena_namespace_definition first, second;
  if (!athena_namespace_get (from_qstring (names[0]), first) ||
      !athena_namespace_get (from_qstring (names[1]), second)) {
    QMessageBox::warning (this, "Generate Sub-products",
                          "Could not load the selected namespaces.");
    return;
  }
  if (!namespace_at_least_semi_concrete (first) ||
      !namespace_at_least_semi_concrete (second)) {
    QMessageBox::warning (
      this, "Generate Sub-products",
      "Both selected namespaces must be at least semi-concrete and have a "
      "sorting algorithm.");
    return;
  }

  if (!namespace_confirm_sorter_compatibility (this, first, second)) return;

  QStringList productNames=
    namespace_ask_subproduct_names (this, names[0], names[1]);
  if (productNames.isEmpty ()) return;

  bool aggressive=
    get_preference ("vault subproduct consume string aggressively", "on") ==
    "on";
  string suggested;
  string error;
  if (!athena_namespace_suggest_subproduct_template (
        first.templ, second.templ, aggressive, suggested, error)) {
    suggested= "";
    if (error != "")
      QMessageBox::information (this, "Sub-product Template",
                                to_qstring (error));
  }
  QString templ= to_qstring (suggested);
  if (!namespace_ask_subproduct_template (this, first, second, templ)) return;

  string sorterPath;
  if (!athena_namespace_generate_product_sorter (
        first, second, from_qstring (templ), sorterPath, error)) {
    QMessageBox::warning (this, "Generate Sub-products", to_qstring (error));
    return;
  }

  QStringList existing;
  for (const QString& name: productNames) {
    athena_namespace_definition ignored;
    if (athena_namespace_get (from_qstring (name), ignored))
      existing << name;
  }
  if (!existing.isEmpty ()) {
    if (QMessageBox::question (
          this, "Update Existing Namespaces",
          QString ("These namespaces already exist and will be updated:\n%1")
            .arg (existing.join ("\n")),
          QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
      return;
  }

  for (const QString& name: productNames) {
    athena_namespace_definition ns;
    ns.name= from_qstring (name);
    ns.kind= "semi-concrete";
    ns.templ= from_qstring (templ);
    ns.sorter_trivial= false;
    ns.sorter_path= sorterPath;
    ns.style_path= "";
    ns.parents= strings ();
    ns.parents << first.name << second.name;
    ns.derived_parents= strings ();
    if (!athena_namespace_save (ns, error)) {
      QMessageBox::warning (this, "Generate Sub-products", to_qstring (error));
      return;
    }
  }

  refreshAll ();
  statusLabel->setText (
    QString ("Generated %1 sub-product namespace(s).")
      .arg (productNames.size ()));
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
QTMNamespaceManager::chooseSorterPath () {
  QString selected= namespace_choose_file (
    this, sorterEdit, "Choose Namespace Sorter", "*.c|C source files");
  if (!selected.isEmpty ()) sorterEdit->setText (selected);
}

void
QTMNamespaceManager::chooseStylePath () {
  QString selected= namespace_choose_file (
    this, styleEdit, "Choose Namespace Style", "*.ts *.scm|ATHENA style files");
  if (!selected.isEmpty ()) styleEdit->setText (selected);
}

void
QTMNamespaceManager::addExplicitParent () {
  QString parent= explicitParentCombo->currentText ().trimmed ();
  if (parent.isEmpty ()) return;
  if (parent == nameEdit->text ().trimmed ()) {
    QMessageBox::warning (this, "Namespace Manager",
                          "A namespace cannot be its own parent.");
    return;
  }
  if (qlist_contains (explicitParentsList, parent)) return;
  explicitParentsList->addItem (parent);
}

void
QTMNamespaceManager::removeSelectedExplicitParents () {
  QList<QListWidgetItem*> items= explicitParentsList->selectedItems ();
  for (QListWidgetItem* item: items)
    delete explicitParentsList->takeItem (explicitParentsList->row (item));
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

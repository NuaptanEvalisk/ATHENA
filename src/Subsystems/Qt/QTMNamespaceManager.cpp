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
#include "QTMNamespaceExplorer.hpp"
#include "QTMReverseHierarchyGraph.hpp"
#include "boot.hpp"
#include "namespaces.hpp"
#include "qt_utilities.hpp"
#include "scheme.hpp"
#include "vault.hpp"

#include <DockWidget.h>
#ifdef USE_KF6
#include <KIOFileWidgets/KFileCustomDialog>
#include <KIOFileWidgets/KFileWidget>
#endif
#ifdef USE_KF6_SYNTAX_HIGHLIGHTING
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/SyntaxHighlighter>
#include <KSyntaxHighlighting/Theme>
#endif
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyle>
#include <QTabWidget>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>
#include <QWizard>
#include <QWizardPage>

static QTMNamespaceManager* namespace_manager_widget= nullptr;
static ads::CDockWidget* namespace_manager_dock= nullptr;

#ifdef USE_KF6_SYNTAX_HIGHLIGHTING
static KSyntaxHighlighting::Repository&
namespace_syntax_repository () {
  static KSyntaxHighlighting::Repository repository;
  return repository;
}

static void
namespace_highlight_c_source (QPlainTextEdit* edit) {
  KSyntaxHighlighting::Repository& repository= namespace_syntax_repository ();
  edit->ensurePolished ();
  KSyntaxHighlighting::SyntaxHighlighter* highlighter=
    new KSyntaxHighlighting::SyntaxHighlighter (edit->document ());
  highlighter->setTheme (repository.themeForPalette (edit->palette ()));
  highlighter->setDefinition (repository.definitionForName ("C"));
}
#endif

static QIcon
namespace_icon (const QString& name, QStyle::StandardPixmap fallback) {
  QIcon icon= QIcon::fromTheme (name);
  if (icon.isNull ()) icon= QApplication::style ()->standardIcon (fallback);
  return icon;
}

static array<string>
qlist_to_strings (QListWidget* list) {
  array<string> out;
  for (int i=0; i<list->count (); i++)
    out << from_qstring (list->item (i)->text ());
  return out;
}

static void
set_qlist_strings (QListWidget* list, const array<string>& xs) {
  list->clear ();
  for (int i=0; i<N(xs); i++) list->addItem (to_qstring (xs[i]));
}

static QString
namespace_join_strings (const array<string>& xs) {
  QStringList parts;
  for (int i=0; i<N(xs); i++) parts << to_qstring (xs[i]);
  return parts.join (", ");
}

static bool
qlist_contains (QListWidget* list, const QString& text) {
  return !list->findItems (text, Qt::MatchExactly).isEmpty ();
}

#ifdef USE_KF6
static QString
namespace_selected_local_file (KFileWidget* file_widget) {
  QString selected= file_widget->selectedFile ();
  if (!selected.isEmpty ()) return selected;
  QUrl selected_url= file_widget->selectedUrl ();
  return selected_url.isLocalFile () ? selected_url.toLocalFile () : QString ();
}

static void
namespace_set_file_filter (KFileWidget* file_widget, const QString& filter) {
  if (file_widget == nullptr || filter.isEmpty ()) return;
  QList<KFileFilter> filters;
  for (const QString& entry : filter.split ('\n', Qt::SkipEmptyParts)) {
    QStringList parts= entry.split ('|');
    QString patterns= parts.value (0).trimmed ();
    QString label= parts.value (1, patterns).trimmed ();
    filters << KFileFilter (label, patterns.split (' ', Qt::SkipEmptyParts),
                            QStringList ());
  }
  file_widget->setFilters (filters);
}
#endif

static QString
namespace_qt_file_filter (const QString& filter) {
  if (filter.isEmpty ()) return QString ();
  QStringList out;
  for (const QString& entry: filter.split ('\n', Qt::SkipEmptyParts)) {
    QStringList parts= entry.split ('|');
    QString patterns= parts.value (0).trimmed ();
    QString label= parts.value (1, patterns).trimmed ();
    if (patterns.isEmpty ()) continue;
    out << label + " (" + patterns + ")";
  }
  return out.join (";;");
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

#ifdef USE_KF6
  KFileCustomDialog dialog (QUrl::fromLocalFile (start_path), parent);
  dialog.setWindowTitle (title);
  dialog.setOperationMode (KFileWidget::Opening);

  KFileWidget* file_widget= dialog.fileWidget ();
  file_widget->setMode (KFile::File | KFile::ExistingOnly | KFile::LocalOnly);
  namespace_set_file_filter (file_widget, filter);

  QRect r;
  QSize dialog_size= dialog.sizeHint ();
  if (dialog_size.width () > 860) dialog_size.setWidth (860);
  r.setSize (dialog_size);
  QWidget* anchor= parent == nullptr ? QApplication::activeWindow () : parent;
  if (anchor != nullptr) r.moveCenter (anchor->geometry ().center ());
  dialog.setGeometry (r);

  if (dialog.exec () != QDialog::Accepted) return QString ();
  QString selected= namespace_selected_local_file (file_widget);
#else
  QString selected= QFileDialog::getOpenFileName (
    parent, title, start_path, namespace_qt_file_filter (filter));
#endif
  return selected.isEmpty () ? QString () :
    namespace_relative_path_if_possible (selected);
}

static QString
namespace_choose_homepage_target (QWidget* parent, QLineEdit* edit) {
  QString start_path= namespace_existing_path_for_edit (edit);
  QFileInfo start_info (start_path);
  if (!start_info.exists ()) start_path= start_info.absolutePath ();

#ifdef USE_KF6
  KFileCustomDialog dialog (QUrl::fromLocalFile (start_path), parent);
  dialog.setWindowTitle ("Create Namespace Homepage");
  dialog.setOperationMode (KFileWidget::Saving);

  KFileWidget* file_widget= dialog.fileWidget ();
  file_widget->setMode (KFile::File | KFile::LocalOnly);
  namespace_set_file_filter (file_widget, "*.ath|ATHENA documents");

  QRect r;
  QSize dialog_size= dialog.sizeHint ();
  if (dialog_size.width () > 860) dialog_size.setWidth (860);
  r.setSize (dialog_size);
  QWidget* anchor= parent == nullptr ? QApplication::activeWindow () : parent;
  if (anchor != nullptr) r.moveCenter (anchor->geometry ().center ());
  dialog.setGeometry (r);

  if (dialog.exec () != QDialog::Accepted) return QString ();
  QString selected= namespace_selected_local_file (file_widget);
#else
  QString selected= QFileDialog::getSaveFileName (
    parent, "Create Namespace Homepage", start_path,
    namespace_qt_file_filter ("*.ath|ATHENA documents"));
#endif
  if (selected.isEmpty ()) return QString ();
  if (QFileInfo (selected).suffix ().isEmpty ()) selected += ".ath";
  return namespace_relative_path_if_possible (selected);
}

static bool
namespace_create_homepage_file (const QString& rel_or_abs, QString& error) {
  QString root= namespace_vault_root_path ();
  QString path= QFileInfo (rel_or_abs).isAbsolute ()
    ? rel_or_abs : QDir (root).absoluteFilePath (rel_or_abs);
  QFileInfo info (path);
  QDir dir= info.dir ();
  if (!dir.exists () && !dir.mkpath (".")) {
    error= "Cannot create homepage directory.";
    return false;
  }
  if (info.exists ()) return true;
  QFile file (path);
  if (!file.open (QIODevice::WriteOnly | QIODevice::Text)) {
    error= "Cannot create homepage file.";
    return false;
  }
  file.write ("(document (TeXmacs \"2.1\") (style \"generic\") "
              "(body (document \"\")))\n");
  return true;
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

class NamespaceKindPage : public QWizardPage {
public:
  NamespaceKindPage (QWidget* parent = nullptr)
    : QWizardPage (parent),
      abstractButton (new QRadioButton ("Abstract", this)),
      semiConcreteButton (new QRadioButton ("Semi-concrete", this)),
      concreteButton (new QRadioButton ("Concrete", this)) {
    setTitle ("Namespace Type");
    setSubTitle ("Choose what kind of namespace to create.");

    concreteButton->setChecked (true);

    QLabel* abstractHelp= new QLabel (
      "Groups other namespaces and does not match files directly.", this);
    QLabel* semiHelp= new QLabel (
      "Matches files using a template and sorter.", this);
    QLabel* concreteHelp= new QLabel (
      "Matches files and can provide style and initial content for new files.",
      this);
    for (QLabel* label: { abstractHelp, semiHelp, concreteHelp }) {
      label->setWordWrap (true);
      label->setStyleSheet ("color: palette(mid);");
    }

    QVBoxLayout* layout= new QVBoxLayout (this);
    layout->addWidget (concreteButton);
    layout->addWidget (concreteHelp);
    layout->addSpacing (8);
    layout->addWidget (semiConcreteButton);
    layout->addWidget (semiHelp);
    layout->addSpacing (8);
    layout->addWidget (abstractButton);
    layout->addWidget (abstractHelp);
    layout->addStretch (1);
  }

  QString kind () const {
    if (abstractButton->isChecked ()) return "abstract";
    if (semiConcreteButton->isChecked ()) return "semi-concrete";
    return "concrete";
  }

private:
  QRadioButton* abstractButton;
  QRadioButton* semiConcreteButton;
  QRadioButton* concreteButton;
};

class NamespaceDetailsPage : public QWizardPage {
public:
  NamespaceDetailsPage (NamespaceKindPage* kindPage, QWidget* parent = nullptr)
    : QWizardPage (parent),
      kindPage (kindPage),
      nameEdit (new QLineEdit (this)),
      templateEdit (new QLineEdit (this)),
      trivialSorterCheck (new QCheckBox ("Use trivial sorting algorithm", this)),
      sorterEdit (new QLineEdit (this)),
      sorterBrowseButton (new QPushButton ("Browse...", this)),
      styleEdit (new QLineEdit (this)),
      styleBrowseButton (new QPushButton ("Browse...", this)),
      initialContentEdit (new QLineEdit (this)),
      initialContentBrowseButton (new QPushButton ("Browse...", this)),
      parentList (new QListWidget (this)),
      parentCombo (new QComboBox (this)),
      templateLabel (new QLabel ("Template", this)),
      sorterLabel (new QLabel ("Sorter .c path", this)),
      styleLabel (new QLabel ("Style path", this)),
      initialContentLabel (new QLabel ("Initial content", this)) {
    setTitle ("Namespace Details");
    setSubTitle ("Fill in the fields for the selected namespace type.");

    parentList->setSelectionMode (QAbstractItemView::ExtendedSelection);
    parentList->setMinimumHeight (92);
    parentCombo->setEditable (true);
    for (const athena_namespace_definition& ns: athena_namespaces_list ())
      parentCombo->addItem (to_qstring (ns.name));

    QFormLayout* form= new QFormLayout ();
    form->setFieldGrowthPolicy (QFormLayout::ExpandingFieldsGrow);
    form->addRow ("Name", nameEdit);
    form->addRow (templateLabel, templateEdit);

    sorterWidget= new QWidget (this);
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
    form->addRow (sorterLabel, sorterWidget);

    styleWidget= new QWidget (this);
    QHBoxLayout* styleLayout= new QHBoxLayout (styleWidget);
    styleLayout->setContentsMargins (0, 0, 0, 0);
    styleLayout->setSpacing (4);
    styleLayout->addWidget (styleEdit, 1);
    styleLayout->addWidget (styleBrowseButton);
    form->addRow (styleLabel, styleWidget);

    initialContentWidget= new QWidget (this);
    QHBoxLayout* initialContentLayout= new QHBoxLayout (initialContentWidget);
    initialContentLayout->setContentsMargins (0, 0, 0, 0);
    initialContentLayout->setSpacing (4);
    initialContentLayout->addWidget (initialContentEdit, 1);
    initialContentLayout->addWidget (initialContentBrowseButton);
    form->addRow (initialContentLabel, initialContentWidget);

    QWidget* parentsWidget= new QWidget (this);
    QVBoxLayout* parentsLayout= new QVBoxLayout (parentsWidget);
    parentsLayout->setContentsMargins (0, 0, 0, 0);
    parentsLayout->setSpacing (4);
    parentsLayout->addWidget (parentList);
    QHBoxLayout* parentControls= new QHBoxLayout ();
    parentControls->setContentsMargins (0, 0, 0, 0);
    parentControls->setSpacing (4);
    QPushButton* addParent= new QPushButton ("Add", this);
    QPushButton* removeParent= new QPushButton ("Remove selected", this);
    parentControls->addWidget (parentCombo, 1);
    parentControls->addWidget (addParent);
    parentControls->addWidget (removeParent);
    parentsLayout->addLayout (parentControls);
    form->addRow ("Explicit parents", parentsWidget);

    QVBoxLayout* layout= new QVBoxLayout (this);
    layout->addLayout (form);

    connect (trivialSorterCheck, &QCheckBox::toggled, this,
             [this] () { updateKindUi (); });
    connect (sorterBrowseButton, &QPushButton::clicked, this, [this] () {
      QString selected= namespace_choose_file (
        this, sorterEdit, "Choose Namespace Sorter", "*.c|C source files");
      if (!selected.isEmpty ()) sorterEdit->setText (selected);
    });
    connect (styleBrowseButton, &QPushButton::clicked, this, [this] () {
      QString selected= namespace_choose_file (
        this, styleEdit, "Choose Namespace Style", "*.ts *.scm|ATHENA style files");
      if (!selected.isEmpty ()) styleEdit->setText (selected);
    });
    connect (initialContentBrowseButton, &QPushButton::clicked, this, [this] () {
      QString selected= namespace_choose_file (
        this, initialContentEdit, "Choose Initial Content",
        "*.ath|ATHENA documents");
      if (!selected.isEmpty ()) initialContentEdit->setText (selected);
    });
    connect (addParent, &QPushButton::clicked, this, [this] () {
      QString parent= parentCombo->currentText ().trimmed ();
      if (parent.isEmpty ()) return;
      if (parent == nameEdit->text ().trimmed ()) {
        QMessageBox::warning (this, "Namespace Wizard",
                              "A namespace cannot be its own parent.");
        return;
      }
      if (!qlist_contains (parentList, parent)) parentList->addItem (parent);
    });
    connect (removeParent, &QPushButton::clicked, this, [this] () {
      QList<QListWidgetItem*> items= parentList->selectedItems ();
      for (QListWidgetItem* item: items)
        delete parentList->takeItem (parentList->row (item));
    });

    updateKindUi ();
  }

  void initializePage () override { updateKindUi (); }

  void updateKindUi () {
    QString kind= kindPage->kind ();
    bool abstract= kind == "abstract";
    bool concrete= kind == "concrete";
    templateLabel->setVisible (!abstract);
    templateEdit->setVisible (!abstract);
    sorterLabel->setVisible (!abstract);
    sorterWidget->setVisible (!abstract);
    styleLabel->setVisible (concrete);
    styleWidget->setVisible (concrete);
    initialContentLabel->setVisible (concrete);
    initialContentWidget->setVisible (concrete);
    templateEdit->setEnabled (!abstract);
    styleEdit->setEnabled (concrete);
    styleBrowseButton->setEnabled (concrete);
    initialContentEdit->setEnabled (concrete);
    initialContentBrowseButton->setEnabled (concrete);
    trivialSorterCheck->setEnabled (!abstract);
    bool sorterEnabled= !abstract && !trivialSorterCheck->isChecked ();
    sorterEdit->setEnabled (sorterEnabled);
    sorterBrowseButton->setEnabled (sorterEnabled);
    sorterEdit->setPlaceholderText (
      abstract ? "Abstract namespaces do not match files directly" :
      trivialSorterCheck->isChecked ()
        ? "Built-in sorter returns 0 for every comparison" : "");
    templateEdit->setPlaceholderText (
      abstract ? "" : "%w Lecture Notes %R");
    initialContentEdit->setPlaceholderText ("Optional .ath template document");
  }

  bool validatePage () override {
    QString name= nameEdit->text ().trimmed ();
    QString kind= kindPage->kind ();
    if (name.isEmpty ()) {
      QMessageBox::warning (this, "Namespace Wizard",
                            "Namespace name cannot be empty.");
      return false;
    }
    if (name.contains ("!")) {
      QMessageBox::warning (this, "Namespace Wizard",
                            "Namespace name cannot contain '!'.");
      return false;
    }
    if (kind != "abstract" && templateEdit->text ().trimmed ().isEmpty ()) {
      QMessageBox::warning (this, "Namespace Wizard",
                            "Concrete and semi-concrete namespaces need a "
                            "filename template.");
      return false;
    }
    return true;
  }

  athena_namespace_definition definition () const {
    athena_namespace_definition ns;
    ns.name= from_qstring (nameEdit->text ().trimmed ());
    ns.kind= from_qstring (kindPage->kind ());
    bool abstract= kindPage->kind () == "abstract";
    bool concrete= kindPage->kind () == "concrete";
    ns.templ= abstract ? string ("") :
      from_qstring (templateEdit->text ().trimmed ());
    ns.sorter_trivial= !abstract && trivialSorterCheck->isChecked ();
    ns.sorter_path= (!abstract && !ns.sorter_trivial) ?
      from_qstring (sorterEdit->text ().trimmed ()) : string ("");
    ns.style_path= concrete ?
      from_qstring (styleEdit->text ().trimmed ()) : string ("");
    ns.initial_content_path= concrete ?
      from_qstring (initialContentEdit->text ().trimmed ()) : string ("");
    ns.homepage_path= "";
    ns.parents= qlist_to_strings (parentList);
    ns.derived_parents= array<string> ();
    return ns;
  }

private:
  NamespaceKindPage* kindPage;
  QLineEdit*   nameEdit;
  QLineEdit*   templateEdit;
  QCheckBox*   trivialSorterCheck;
  QLineEdit*   sorterEdit;
  QPushButton* sorterBrowseButton;
  QWidget*     sorterWidget;
  QLineEdit*   styleEdit;
  QPushButton* styleBrowseButton;
  QWidget*     styleWidget;
  QLineEdit*   initialContentEdit;
  QPushButton* initialContentBrowseButton;
  QWidget*     initialContentWidget;
  QListWidget* parentList;
  QComboBox*   parentCombo;
  QLabel*      templateLabel;
  QLabel*      sorterLabel;
  QLabel*      styleLabel;
  QLabel*      initialContentLabel;
};

class NamespaceHomepagePage : public QWizardPage {
public:
  NamespaceHomepagePage (QWidget* parent = nullptr)
    : QWizardPage (parent),
      useHomepage (new QCheckBox ("Use a homepage for this namespace", this)),
      homepageEdit (new QLineEdit (this)),
      browseButton (new QPushButton ("Browse existing...", this)),
      createButton (new QPushButton ("Create new...", this)) {
    setTitle ("Namespace Homepage");
    setSubTitle ("Optionally attach a .ath document as the user-facing "
                 "homepage for tmfs://ns/name.");

    QHBoxLayout* row= new QHBoxLayout ();
    row->setContentsMargins (0, 0, 0, 0);
    row->setSpacing (4);
    row->addWidget (homepageEdit, 1);
    row->addWidget (browseButton);
    row->addWidget (createButton);

    QVBoxLayout* layout= new QVBoxLayout (this);
    layout->addWidget (useHomepage);
    layout->addLayout (row);

    connect (useHomepage, &QCheckBox::toggled, this,
             [this] () { updateUi (); });
    connect (browseButton, &QPushButton::clicked, this, [this] () {
      QString selected= namespace_choose_file (
        this, homepageEdit, "Choose Namespace Homepage",
        "*.ath|ATHENA documents");
      if (!selected.isEmpty ()) {
        homepageEdit->setText (selected);
        useHomepage->setChecked (true);
      }
    });
    connect (createButton, &QPushButton::clicked, this, [this] () {
      QString selected= namespace_choose_homepage_target (this, homepageEdit);
      if (selected.isEmpty ()) return;
      QString error;
      if (!namespace_create_homepage_file (selected, error)) {
        QMessageBox::warning (this, "Namespace Wizard", error);
        return;
      }
      homepageEdit->setText (selected);
      useHomepage->setChecked (true);
    });
    updateUi ();
  }

  QString homepagePath () const {
    return useHomepage->isChecked () ? homepageEdit->text ().trimmed () :
                                      QString ();
  }

  bool validatePage () override {
    if (!useHomepage->isChecked ()) return true;
    if (homepageEdit->text ().trimmed ().isEmpty ()) {
      QMessageBox::warning (this, "Namespace Wizard",
                            "Choose or create a homepage file, or disable "
                            "the homepage toggle.");
      return false;
    }
    return true;
  }

private:
  void updateUi () {
    bool enabled= useHomepage->isChecked ();
    homepageEdit->setEnabled (enabled);
    browseButton->setEnabled (enabled);
    createButton->setEnabled (enabled);
  }

  QCheckBox*  useHomepage;
  QLineEdit*  homepageEdit;
  QPushButton* browseButton;
  QPushButton* createButton;
};

class NamespaceCreationSummaryPage : public QWizardPage {
public:
  NamespaceCreationSummaryPage (NamespaceDetailsPage* details,
                                NamespaceHomepagePage* homepage,
                                QWidget* parent = nullptr)
    : QWizardPage (parent), details (details), homepage (homepage),
      summary (new QLabel (this)) {
    setTitle ("Confirm Namespace");
    setSubTitle ("Review the namespace before creating it.");
    summary->setWordWrap (true);
    summary->setTextFormat (Qt::PlainText);
    QVBoxLayout* layout= new QVBoxLayout (this);
    layout->addWidget (summary);
  }

  void initializePage () override {
    athena_namespace_definition ns= details->definition ();
    ns.homepage_path= from_qstring (homepage->homepagePath ());
    QStringList lines;
    lines << "Name: " + to_qstring (ns.name)
          << "Kind: " + to_qstring (ns.kind)
          << "Template: " + (ns.templ == "" ? QString ("<none>") :
                              to_qstring (ns.templ))
          << "Sorter: " +
             (ns.sorter_trivial ? QString ("<trivial>") :
              ns.sorter_path == "" ? QString ("<none>") :
              to_qstring (ns.sorter_path))
          << "Style: " + (ns.style_path == "" ? QString ("<none>") :
                          to_qstring (ns.style_path))
          << "Initial content: " +
             (ns.initial_content_path == "" ? QString ("<none>") :
              to_qstring (ns.initial_content_path))
          << "Homepage: " + (ns.homepage_path == "" ? QString ("<none>") :
                             to_qstring (ns.homepage_path))
          << "Explicit parents: " +
             (N(ns.parents) == 0 ? QString ("<none>") :
              namespace_join_strings (ns.parents));
    summary->setText (lines.join ("\n"));
  }

private:
  NamespaceDetailsPage* details;
  NamespaceHomepagePage* homepage;
  QLabel* summary;
};

class SorterCompatibilityWizardPage : public QWizardPage {
public:
  SorterCompatibilityWizardPage (const athena_namespace_definition& first,
                                 const athena_namespace_definition& second,
                                 const QString& firstSource,
                                 const QString& secondSource,
                                 QWidget* parent = nullptr)
    : QWizardPage (parent),
      confirmCheck (new QCheckBox ("These sorters are compatible", this)) {
    setTitle ("Sorter Compatibility");
    setSubTitle ("Read both sorting algorithms and confirm that their orderings "
                 "are compatible for a sub-product namespace.");

    QVBoxLayout* layout= new QVBoxLayout (this);
    QHBoxLayout* sources= new QHBoxLayout ();
    auto add_source= [&] (const QString& title, const QString& source) {
      QWidget* pane= new QWidget (this);
      QVBoxLayout* paneLayout= new QVBoxLayout (pane);
      paneLayout->setContentsMargins (0, 0, 0, 0);
      paneLayout->addWidget (new QLabel (title, pane));
      QPlainTextEdit* edit= new QPlainTextEdit (pane);
      edit->setReadOnly (true);
      edit->setLineWrapMode (QPlainTextEdit::NoWrap);
      edit->setFont (QFontDatabase::systemFont (QFontDatabase::FixedFont));
      edit->setPlainText (source);
#ifdef USE_KF6_SYNTAX_HIGHLIGHTING
      namespace_highlight_c_source (edit);
#endif
      edit->setMinimumSize (420, 280);
      paneLayout->addWidget (edit);
      sources->addWidget (pane);
    };
    add_source (to_qstring (first.name), firstSource);
    add_source (to_qstring (second.name), secondSource);
    layout->addLayout (sources);
    layout->addWidget (confirmCheck);
  }

  bool validatePage () override {
    if (confirmCheck->isChecked ()) return true;
    QMessageBox::warning (this, "Generate Sub-products",
                          "Confirm sorter compatibility before continuing.");
    return false;
  }

private:
  QCheckBox* confirmCheck;
};

class SubproductNamesWizardPage : public QWizardPage {
public:
  SubproductNamesWizardPage (const QString& first, const QString& second,
                             QWidget* parent = nullptr)
    : QWizardPage (parent), edit (new QPlainTextEdit (this)) {
    setTitle ("Sub-product Names");
    setSubTitle ("Enter one namespace name per line. Each name will be created "
                 "with the same sub-product template and product sorter.");
    edit->setPlainText (first + " - " + second + "\n" +
                        second + " - " + first);
    edit->setMinimumSize (560, 180);
    QVBoxLayout* layout= new QVBoxLayout (this);
    layout->addWidget (edit);
  }

  QStringList names () const {
    return namespace_lines (edit->toPlainText ());
  }

  bool validatePage () override {
    if (!names ().isEmpty ()) return true;
    QMessageBox::warning (this, "Generate Sub-products",
                          "Enter at least one sub-product namespace name.");
    return false;
  }

private:
  QPlainTextEdit* edit;
};

class SubproductTemplateWizardPage : public QWizardPage {
public:
  SubproductTemplateWizardPage (const athena_namespace_definition& first,
                                const athena_namespace_definition& second,
                                const QString& suggested,
                                QWidget* parent = nullptr)
    : QWizardPage (parent), first (first), second (second),
      edit (new QLineEdit (suggested, this)) {
    setTitle ("Sub-product Template");
    setSubTitle ("Confirm or edit the naming template. It must derive from both "
                 "selected namespace templates.");
    edit->setMinimumWidth (620);
    QVBoxLayout* layout= new QVBoxLayout (this);
    layout->addWidget (edit);
  }

  QString templ () const {
    return edit->text ().trimmed ();
  }

  bool validatePage () override {
    QString candidate= templ ();
    if (candidate.isEmpty ()) {
      QMessageBox::warning (this, "Sub-product Template",
                            "The template cannot be empty.");
      return false;
    }
    string error;
    bool d1= false, d2= false;
    if (!athena_namespace_template_derives (from_qstring (candidate),
                                            first.templ, d1, error) ||
        !athena_namespace_template_derives (from_qstring (candidate),
                                            second.templ, d2, error)) {
      QMessageBox::warning (this, "Sub-product Template", to_qstring (error));
      return false;
    }
    if (!d1 || !d2) {
      QMessageBox::warning (
        this, "Sub-product Template",
        "The template does not derive from both selected namespace templates.");
      return false;
    }
    return true;
  }

private:
  athena_namespace_definition first;
  athena_namespace_definition second;
  QLineEdit* edit;
};

class SingleParentSubproductTemplateWizardPage : public QWizardPage {
public:
  SingleParentSubproductTemplateWizardPage (
    const athena_namespace_definition& parent,
    const QString& suggested, QWidget* wizardParent = nullptr)
    : QWizardPage (wizardParent), parent (parent),
      edit (new QLineEdit (suggested, this)) {
    setTitle ("Sub-product Template");
    setSubTitle ("Confirm or edit the naming template. It must derive from the "
                 "selected semi-concrete namespace template.");
    edit->setMinimumWidth (620);
    QVBoxLayout* layout= new QVBoxLayout (this);
    layout->addWidget (edit);
  }

  QString templ () const {
    return edit->text ().trimmed ();
  }

  bool validatePage () override {
    QString candidate= templ ();
    if (candidate.isEmpty ()) {
      QMessageBox::warning (this, "Sub-product Template",
                            "The template cannot be empty.");
      return false;
    }
    string error;
    bool derives= false;
    if (!athena_namespace_template_derives (from_qstring (candidate),
                                            parent.templ, derives, error)) {
      QMessageBox::warning (this, "Sub-product Template", to_qstring (error));
      return false;
    }
    if (!derives) {
      QMessageBox::warning (
        this, "Sub-product Template",
        "The template does not derive from the selected semi-concrete "
        "namespace template.");
      return false;
    }
    return true;
  }

private:
  athena_namespace_definition parent;
  QLineEdit* edit;
};

class SubproductSummaryWizardPage : public QWizardPage {
public:
  SubproductSummaryWizardPage (SubproductNamesWizardPage* namesPage,
                               SubproductTemplateWizardPage* templatePage,
                               QWidget* parent = nullptr)
    : QWizardPage (parent), namesPage (namesPage),
      templatePage (templatePage), summary (new QLabel (this)) {
    setTitle ("Confirm Sub-products");
    setSubTitle ("Review the generated namespaces before creating them.");
    summary->setWordWrap (true);
    summary->setTextFormat (Qt::PlainText);
    QVBoxLayout* layout= new QVBoxLayout (this);
    layout->addWidget (summary);
  }

  void initializePage () override {
    summary->setText ("Namespaces:\n" + namesPage->names ().join ("\n") +
                      "\n\nTemplate:\n" + templatePage->templ ());
  }

private:
  SubproductNamesWizardPage* namesPage;
  SubproductTemplateWizardPage* templatePage;
  QLabel* summary;
};

class SingleParentSubproductSummaryWizardPage : public QWizardPage {
public:
  SingleParentSubproductSummaryWizardPage (
    SubproductNamesWizardPage* namesPage,
    SingleParentSubproductTemplateWizardPage* templatePage,
    QWidget* parent = nullptr)
    : QWizardPage (parent), namesPage (namesPage),
      templatePage (templatePage), summary (new QLabel (this)) {
    setTitle ("Confirm Sub-products");
    setSubTitle ("Review the generated namespaces before creating them.");
    summary->setWordWrap (true);
    summary->setTextFormat (Qt::PlainText);
    QVBoxLayout* layout= new QVBoxLayout (this);
    layout->addWidget (summary);
  }

  void initializePage () override {
    summary->setText ("Namespaces:\n" + namesPage->names ().join ("\n") +
                      "\n\nKind:\nsemi-concrete" +
                      "\n\nTemplate:\n" + templatePage->templ ());
  }

private:
  SubproductNamesWizardPage* namesPage;
  SingleParentSubproductTemplateWizardPage* templatePage;
  QLabel* summary;
};

class AbstractSubproductSummaryWizardPage : public QWizardPage {
public:
  AbstractSubproductSummaryWizardPage (SubproductNamesWizardPage* namesPage,
                                       QWidget* parent = nullptr)
    : QWizardPage (parent), namesPage (namesPage), summary (new QLabel (this)) {
    setTitle ("Confirm Sub-products");
    setSubTitle ("Review the generated abstract namespaces before creating them.");
    summary->setWordWrap (true);
    summary->setTextFormat (Qt::PlainText);
    QVBoxLayout* layout= new QVBoxLayout (this);
    layout->addWidget (summary);
  }

  void initializePage () override {
    summary->setText ("Namespaces:\n" + namesPage->names ().join ("\n") +
                      "\n\nKind:\nabstract" +
                      "\n\nTemplate:\n<none>" +
                      "\n\nSorter:\n<none>");
  }

private:
  SubproductNamesWizardPage* namesPage;
  QLabel* summary;
};

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
    styleBrowseButton (new QPushButton ("Browse...", this)),
    initialContentEdit (new QLineEdit (this)),
    initialContentBrowseButton (new QPushButton ("Browse...", this)),
    homepageEdit (new QLineEdit (this)),
    homepageBrowseButton (new QPushButton ("Browse...", this)),
    homepageCreateButton (new QPushButton ("Create...", this)),
    homepageEditButton (new QPushButton ("Edit homepage", this)),
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
    statusLabel (new QLabel (this)),
    editorTabs (new QTabWidget (this)),
    definitionTab (nullptr),
    documentsTab (nullptr),
    hierarchyTab (nullptr),
    matchedFilesTab (nullptr),
    relationDecisionsTab (nullptr),
    loadingUi (false),
    dirty (false) {
  kindCombo->addItems (QStringList () << "concrete" << "semi-concrete"
                                      << "abstract");
  relationDecisionCombo->addItems (QStringList () << "allow" << "deny");

  namespaceList->setSelectionMode (QAbstractItemView::ExtendedSelection);
  namespaceList->setUniformItemSizes (true);
  namespaceList->setContextMenuPolicy (Qt::CustomContextMenu);
  namespaceList->setMinimumWidth (260);
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
                      "New namespace...", this, [this] () { newNamespace (); })
          ->setToolTip ("Create a namespace using the wizard");
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

  auto scrollableTab= [this] (QWidget* content) {
    QScrollArea* area= new QScrollArea (this);
    area->setWidgetResizable (true);
    area->setFrameShape (QFrame::NoFrame);
    area->setWidget (content);
    return area;
  };

  QWidget* definitionContent= new QWidget (this);
  QFormLayout* definitionForm= new QFormLayout (definitionContent);
  definitionForm->setContentsMargins (12, 12, 12, 12);
  definitionForm->setFieldGrowthPolicy (QFormLayout::ExpandingFieldsGrow);
  definitionForm->addRow ("Name", nameEdit);
  definitionForm->addRow ("Kind", kindCombo);
  definitionForm->addRow ("Filename template", templateEdit);
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
  definitionForm->addRow ("Sorter .c path", sorterWidget);
  definitionForm->addItem (new QSpacerItem (0, 0, QSizePolicy::Minimum,
                                             QSizePolicy::Expanding));
  definitionTab= scrollableTab (definitionContent);

  QWidget* documentsContent= new QWidget (this);
  QFormLayout* documentsForm= new QFormLayout (documentsContent);
  documentsForm->setContentsMargins (12, 12, 12, 12);
  documentsForm->setFieldGrowthPolicy (QFormLayout::ExpandingFieldsGrow);
  QWidget* styleWidget= new QWidget (this);
  QHBoxLayout* styleLayout= new QHBoxLayout (styleWidget);
  styleLayout->setContentsMargins (0, 0, 0, 0);
  styleLayout->setSpacing (4);
  styleLayout->addWidget (styleEdit, 1);
  styleLayout->addWidget (styleBrowseButton);
  documentsForm->addRow ("Style path", styleWidget);
  QWidget* initialContentWidget= new QWidget (this);
  QHBoxLayout* initialContentLayout= new QHBoxLayout (initialContentWidget);
  initialContentLayout->setContentsMargins (0, 0, 0, 0);
  initialContentLayout->setSpacing (4);
  initialContentLayout->addWidget (initialContentEdit, 1);
  initialContentLayout->addWidget (initialContentBrowseButton);
  documentsForm->addRow ("Initial content", initialContentWidget);
  QWidget* homepageWidget= new QWidget (this);
  QHBoxLayout* homepageLayout= new QHBoxLayout (homepageWidget);
  homepageLayout->setContentsMargins (0, 0, 0, 0);
  homepageLayout->setSpacing (4);
  homepageLayout->addWidget (homepageEdit, 1);
  homepageLayout->addWidget (homepageBrowseButton);
  homepageLayout->addWidget (homepageCreateButton);
  homepageLayout->addWidget (homepageEditButton);
  documentsForm->addRow ("Homepage", homepageWidget);
  documentsForm->addItem (new QSpacerItem (0, 0, QSizePolicy::Minimum,
                                            QSizePolicy::Expanding));
  documentsTab= scrollableTab (documentsContent);

  hierarchyTab= new QWidget (this);
  QHBoxLayout* hierarchyLayout= new QHBoxLayout (hierarchyTab);
  hierarchyLayout->setContentsMargins (12, 12, 12, 12);
  QSplitter* hierarchySplitter= new QSplitter (Qt::Horizontal, hierarchyTab);
  QWidget* explicitParentsPane= new QWidget (hierarchySplitter);
  QVBoxLayout* explicitPaneLayout= new QVBoxLayout (explicitParentsPane);
  explicitPaneLayout->setContentsMargins (0, 0, 6, 0);
  explicitPaneLayout->addWidget (new QLabel ("Explicit parents",
                                             explicitParentsPane));
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
  explicitPaneLayout->addWidget (explicitParentsWidget, 1);
  QWidget* derivedParentsPane= new QWidget (hierarchySplitter);
  QVBoxLayout* derivedPaneLayout= new QVBoxLayout (derivedParentsPane);
  derivedPaneLayout->setContentsMargins (6, 0, 0, 0);
  derivedPaneLayout->addWidget (new QLabel ("Derived parents",
                                            derivedParentsPane));
  derivedPaneLayout->addWidget (derivedParentsList, 1);
  hierarchySplitter->addWidget (explicitParentsPane);
  hierarchySplitter->addWidget (derivedParentsPane);
  hierarchySplitter->setStretchFactor (0, 1);
  hierarchySplitter->setStretchFactor (1, 1);
  hierarchyLayout->addWidget (hierarchySplitter);

  matchedFilesTab= new QWidget (this);
  QVBoxLayout* matchedFilesLayout= new QVBoxLayout (matchedFilesTab);
  matchedFilesLayout->setContentsMargins (8, 8, 8, 8);
  matchedFilesLayout->addWidget (membersTree);

  relationDecisionsTab= new QWidget (this);
  QVBoxLayout* relationLayout= new QVBoxLayout (relationDecisionsTab);
  relationLayout->setContentsMargins (8, 8, 8, 8);
  relationLayout->addWidget (relationsTree, 1);
  QFormLayout* relationForm= new QFormLayout ();
  relationForm->setFieldGrowthPolicy (QFormLayout::ExpandingFieldsGrow);
  relationForm->addRow ("Parent", relationParentEdit);
  relationForm->addRow ("Child", relationChildEdit);
  relationForm->addRow ("Decision", relationDecisionCombo);
  relationLayout->addLayout (relationForm);
  QHBoxLayout* relationCommands= new QHBoxLayout ();
  QPushButton* saveRel= new QPushButton ("Save relation", this);
  QPushButton* allowRel= new QPushButton ("Allow selected", this);
  QPushButton* denyRel= new QPushButton ("Deny selected", this);
  QPushButton* delRel= new QPushButton ("Delete selected", this);
  relationCommands->addWidget (saveRel);
  relationCommands->addWidget (allowRel);
  relationCommands->addWidget (denyRel);
  relationCommands->addWidget (delRel);
  relationCommands->addStretch (1);
  relationLayout->addLayout (relationCommands);

  editorTabs->addTab (definitionTab, "Definition");
  editorTabs->addTab (documentsTab, "Documents");
  editorTabs->addTab (hierarchyTab, "Hierarchy");
  editorTabs->addTab (matchedFilesTab, "Matched Files");
  editorTabs->addTab (relationDecisionsTab, "Relation Decisions");

  QWidget* editor= new QWidget (this);
  QVBoxLayout* editorLayout= new QVBoxLayout (editor);
  editorLayout->setContentsMargins (8, 0, 0, 0);
  modeLabel->setTextFormat (Qt::PlainText);
  editorLayout->addWidget (modeLabel);
  editorLayout->addWidget (editorTabs, 1);

  QSplitter* splitter= new QSplitter (Qt::Horizontal, this);
  splitter->addWidget (namespaceList);
  splitter->addWidget (editor);
  splitter->setCollapsible (0, false);
  splitter->setStretchFactor (0, 0);
  splitter->setStretchFactor (1, 1);
  splitter->setSizes (QList<int> () << 300 << 880);

  statusLabel->setTextFormat (Qt::PlainText);
  statusLabel->setWordWrap (true);
  statusLabel->setSizePolicy (QSizePolicy::Preferred, QSizePolicy::Maximum);
  statusLabel->setAlignment (Qt::AlignLeft | Qt::AlignVCenter);

  QVBoxLayout* layout= new QVBoxLayout (this);
  layout->setContentsMargins (0, 0, 0, 0);
  layout->addWidget (toolbar);
  layout->addWidget (splitter, 1);
  layout->addWidget (statusLabel);

  connect (namespaceList, &QListWidget::itemSelectionChanged, this, [this] () {
    if (loadingUi) return;
    QListWidgetItem* item= namespaceList->currentItem ();
    if (item != nullptr) switchToNamespace (item->text ());
  });
  connect (namespaceList, &QListWidget::customContextMenuRequested,
           this, [this] (const QPoint& pos) {
             showNamespaceContextMenu (pos);
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
  connect (styleBrowseButton, &QPushButton::clicked, this,
           [this] () { chooseStylePath (); });
  connect (initialContentBrowseButton, &QPushButton::clicked, this,
           [this] () { chooseInitialContentPath (); });
  connect (homepageBrowseButton, &QPushButton::clicked, this,
           [this] () { chooseHomepagePath (); });
  connect (homepageCreateButton, &QPushButton::clicked, this,
           [this] () { createHomepagePath (); });
  connect (homepageEditButton, &QPushButton::clicked, this,
           [this] () { editHomepage (); });
  connect (kindCombo, &QComboBox::currentTextChanged, this,
           [this] () { updateModeUi (); markDirty (); });
  connect (addParent, &QPushButton::clicked, this,
           [this] () { addExplicitParent (); });
  connect (removeParent, &QPushButton::clicked, this,
           [this] () { removeSelectedExplicitParents (); });
  connect (explicitParentCombo->lineEdit (), &QLineEdit::returnPressed, this,
           [this] () { addExplicitParent (); });
  connect (trivialSorterCheck, &QCheckBox::toggled, this,
           [this] () { updateModeUi (); markDirty (); });
  connect (homepageEdit, &QLineEdit::textChanged, this,
           [this] () { updateModeUi (); markDirty (); });
  for (QLineEdit* edit: { nameEdit, templateEdit, sorterEdit, styleEdit,
                          initialContentEdit })
    connect (edit, &QLineEdit::textChanged, this,
             [this] () { markDirty (); });
  updateModeUi ();
}

QSize
QTMNamespaceManager::sizeHint () const {
  return QSize (1180, 760);
}

void
QTMNamespaceManager::markDirty () {
  if (loadingUi || dirty) return;
  dirty= true;
  updateModeUi ();
}

void
QTMNamespaceManager::restoreLoadedSelection () {
  QSignalBlocker blocker (namespaceList);
  QList<QListWidgetItem*> matches=
    namespaceList->findItems (loadedName, Qt::MatchExactly);
  if (!matches.isEmpty ()) namespaceList->setCurrentItem (matches.first ());
  else namespaceList->clearSelection ();
}

void
QTMNamespaceManager::switchToNamespace (const QString& name) {
  if (name.isEmpty () || name == loadedName) return;
  if (dirty && !loadedName.isEmpty ()) {
    QMessageBox prompt (QMessageBox::Warning, "Unsaved Namespace Changes",
      QString ("Save changes to namespace \"%1\" before selecting \"%2\"?")
        .arg (loadedName, name), QMessageBox::NoButton, this);
    QPushButton* save= prompt.addButton ("Save", QMessageBox::AcceptRole);
    QPushButton* discard=
      prompt.addButton ("Discard", QMessageBox::DestructiveRole);
    prompt.addButton (QMessageBox::Cancel);
    prompt.setDefaultButton (save);
    prompt.exec ();
    if (prompt.clickedButton () == save) {
      if (!saveNamespace ()) {
        restoreLoadedSelection ();
        return;
      }
    }
    else if (prompt.clickedButton () != discard) {
      restoreLoadedSelection ();
      return;
    }
  }

  QList<QListWidgetItem*> matches=
    namespaceList->findItems (name, Qt::MatchExactly);
  if (matches.isEmpty ()) return;
  {
    QSignalBlocker blocker (namespaceList);
    namespaceList->setCurrentItem (matches.first ());
  }
  loadNamespace (matches.first ());
}

void
QTMNamespaceManager::showValidationError (QWidget* tab, QWidget* field,
                                           const QString& message) {
  editorTabs->setCurrentWidget (tab);
  field->setFocus (Qt::OtherFocusReason);
  QMessageBox::warning (this, "Namespace Manager", message);
}

void
QTMNamespaceManager::refreshAll () {
  refreshNamespaces ();
  refreshRelations ();
}

bool
QTMNamespaceManager::selectNamespace (const QString& name) {
  if (name.trimmed ().isEmpty ()) return false;
  refreshAll ();
  QList<QListWidgetItem*> matches=
    namespaceList->findItems (name, Qt::MatchExactly);
  if (matches.isEmpty ()) return false;
  switchToNamespace (name);
  namespaceList->scrollToItem (matches.first (),
                               QAbstractItemView::PositionAtCenter);
  namespaceList->setFocus ();
  return true;
}

void
QTMNamespaceManager::refreshNamespaces () {
  QString selected= loadedName;
  if (selected.isEmpty () && namespaceList->currentItem () != nullptr)
    selected= namespaceList->currentItem ()->text ();
  bool previousLoading= loadingUi;
  loadingUi= true;
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
  else {
    loadedName.clear ();
    nameEdit->clear ();
    kindCombo->setCurrentText ("concrete");
    templateEdit->clear ();
    trivialSorterCheck->setChecked (false);
    sorterEdit->clear ();
    styleEdit->clear ();
    initialContentEdit->clear ();
    homepageEdit->clear ();
    explicitParentsList->clear ();
    derivedParentsList->clear ();
    dirty= false;
  }
  loadingUi= previousLoading;
  if (!previousLoading && !dirty && namespaceList->currentItem () != nullptr)
    loadNamespace (namespaceList->currentItem ());
  else if (namespaceList->currentItem () == nullptr)
    updateModeUi ();
}

void
QTMNamespaceManager::refreshMembers () {
  membersTree->clear ();
  QString name= nameEdit->text ().trimmed ();
  if (name.isEmpty ()) {
    int index= editorTabs->indexOf (matchedFilesTab);
    if (index >= 0) editorTabs->setTabText (index, "Matched Files (0)");
    return;
  }

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
  int index= editorTabs->indexOf (matchedFilesTab);
  if (index >= 0)
    editorTabs->setTabText (index,
                            QString ("Matched Files (%1)").arg (members.size ()));
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
  std::vector<athena_namespace_relation> relations=
    athena_namespace_relations_list ();
  for (const athena_namespace_relation& rel: relations) {
    QTreeWidgetItem* item= new QTreeWidgetItem ();
    item->setText (0, to_qstring (rel.parent));
    item->setText (1, to_qstring (rel.child));
    item->setText (2, to_qstring (rel.decision));
    item->setText (3, to_qstring (rel.source));
    relationsTree->addTopLevelItem (item);
  }
  int index= editorTabs->indexOf (relationDecisionsTab);
  if (index >= 0)
    editorTabs->setTabText (
      index, QString ("Relation Decisions (%1)").arg (relations.size ()));
}

void
QTMNamespaceManager::loadNamespace (QListWidgetItem* item) {
  if (item == nullptr) return;
  athena_namespace_definition ns;
  if (!athena_namespace_get (from_qstring (item->text ()), ns)) return;

  bool previousLoading= loadingUi;
  loadingUi= true;
  loadedName= item->text ();
  nameEdit->setText (to_qstring (ns.name));
  kindCombo->setCurrentText (to_qstring (ns.kind));
  templateEdit->setText (to_qstring (ns.templ));
  trivialSorterCheck->setChecked (ns.sorter_trivial);
  sorterEdit->setText (to_qstring (ns.sorter_path));
  sorterEdit->setEnabled (!ns.sorter_trivial);
  sorterBrowseButton->setEnabled (!ns.sorter_trivial);
  styleEdit->setText (to_qstring (ns.style_path));
  initialContentEdit->setText (to_qstring (ns.initial_content_path));
  homepageEdit->setText (to_qstring (ns.homepage_path));
  set_qlist_strings (explicitParentsList, ns.parents);
  set_qlist_strings (derivedParentsList, ns.derived_parents);
  dirty= false;
  loadingUi= previousLoading;
  updateModeUi ();
  refreshMembers ();
}

void
QTMNamespaceManager::newNamespace () {
  if (dirty && !loadedName.isEmpty ()) {
    QMessageBox prompt (
      QMessageBox::Warning, "Unsaved Namespace Changes",
      QString ("Save changes to namespace \"%1\" before creating another "
               "namespace?").arg (loadedName),
      QMessageBox::NoButton, this);
    QPushButton* save= prompt.addButton ("Save", QMessageBox::AcceptRole);
    QPushButton* discard=
      prompt.addButton ("Discard", QMessageBox::DestructiveRole);
    prompt.addButton (QMessageBox::Cancel);
    prompt.setDefaultButton (save);
    prompt.exec ();
    if (prompt.clickedButton () == save) {
      if (!saveNamespace ()) return;
    }
    else if (prompt.clickedButton () != discard) return;
  }

  QWizard wizard (this);
  wizard.setWindowTitle ("New Namespace");
  wizard.setWizardStyle (QWizard::ModernStyle);
  wizard.setOption (QWizard::NoBackButtonOnStartPage, false);
  NamespaceKindPage* kind= new NamespaceKindPage (&wizard);
  NamespaceDetailsPage* details= new NamespaceDetailsPage (kind, &wizard);
  NamespaceHomepagePage* homepage= new NamespaceHomepagePage (&wizard);
  wizard.addPage (kind);
  wizard.addPage (details);
  wizard.addPage (homepage);
  wizard.addPage (new NamespaceCreationSummaryPage (details, homepage, &wizard));
  wizard.resize (760, 560);
  if (wizard.exec () != QDialog::Accepted) return;

  athena_namespace_definition ns= details->definition ();
  ns.homepage_path= from_qstring (homepage->homepagePath ());
  athena_namespace_definition existing;
  if (athena_namespace_get (ns.name, existing)) {
    if (QMessageBox::question (
          this, "Update Namespace",
          QString ("Namespace \"%1\" already exists. Update it instead?")
            .arg (to_qstring (ns.name)),
          QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
      return;
  }

  string error;
  if (!athena_namespace_save (ns, error)) {
    QMessageBox::warning (this, "Namespace Manager", to_qstring (error));
    return;
  }

  loadedName= to_qstring (ns.name);
  dirty= false;
  bool previousLoading= loadingUi;
  loadingUi= true;
  refreshNamespaces ();
  refreshRelations ();
  loadingUi= previousLoading;
  QList<QListWidgetItem*> created=
    namespaceList->findItems (loadedName, Qt::MatchExactly);
  if (!created.isEmpty ()) loadNamespace (created.first ());
  updateModeUi ();
  statusLabel->setText ("Namespace created.");
}

bool
QTMNamespaceManager::saveNamespace () {
  bool creating= loadedName.isEmpty ();
  athena_namespace_definition ns;
  ns.name= from_qstring (nameEdit->text ().trimmed ());
  ns.kind= from_qstring (kindCombo->currentText ());
  ns.templ= from_qstring (templateEdit->text ().trimmed ());
  ns.sorter_trivial= trivialSorterCheck->isChecked ();
  ns.sorter_path= from_qstring (sorterEdit->text ().trimmed ());
  ns.style_path= from_qstring (styleEdit->text ().trimmed ());
  ns.initial_content_path=
    from_qstring (initialContentEdit->text ().trimmed ());
  ns.homepage_path= from_qstring (homepageEdit->text ().trimmed ());
  ns.parents= qlist_to_strings (explicitParentsList);
  ns.derived_parents= array<string> ();
  if (ns.kind == "abstract") {
    ns.templ= "";
    ns.sorter_trivial= false;
    ns.sorter_path= "";
    ns.style_path= "";
    ns.initial_content_path= "";
  }
  else if (ns.sorter_trivial) {
    ns.sorter_path= "";
  }
  if (ns.kind != "concrete") {
    ns.style_path= "";
    ns.initial_content_path= "";
  }

  if (ns.name == "") {
    showValidationError (definitionTab, nameEdit,
                         "Namespace name cannot be empty.");
    return false;
  }
  if (std::string (as_charp (ns.name)).find ('!') != std::string::npos) {
    showValidationError (definitionTab, nameEdit,
                         "Namespace name cannot contain '!'.");
    return false;
  }
  if (ns.kind != "abstract" && ns.templ == "") {
    showValidationError (
      definitionTab, templateEdit,
      "Concrete and semi-concrete namespaces need a filename template.");
    return false;
  }
  if (ns.kind != "abstract" && !ns.sorter_trivial && ns.sorter_path == "") {
    showValidationError (
      definitionTab, sorterEdit,
      "Choose a sorter C file or enable the trivial sorting algorithm.");
    return false;
  }
  QString targetName= to_qstring (ns.name);
  if (qlist_contains (explicitParentsList, targetName)) {
    showValidationError (hierarchyTab, explicitParentCombo,
                         "A namespace cannot be its own parent.");
    return false;
  }

  athena_namespace_definition existing;
  bool targetExists= athena_namespace_get (ns.name, existing);
  if (creating && targetExists) {
    if (QMessageBox::question (
          this, "Update Namespace",
          QString ("Namespace \"%1\" already exists. Update it instead?")
            .arg (targetName),
          QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
      return false;
  }
  else if (!creating && loadedName != targetName) {
    if (targetExists) {
      QMessageBox::warning (
        this, "Rename Namespace",
        QString ("Cannot rename to \"%1\" because that namespace already "
                 "exists. Select it from the list to edit it.")
          .arg (targetName));
      return false;
    }
    if (QMessageBox::question (
          this, "Rename Namespace",
          QString ("Rename namespace \"%1\" to \"%2\"?")
            .arg (loadedName, targetName),
          QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
      return false;
  }

  string error;
  if (!athena_namespace_save (ns, error)) {
    editorTabs->setCurrentWidget (definitionTab);
    QMessageBox::warning (this, "Namespace Manager", to_qstring (error));
    return false;
  }
  if (!loadedName.isEmpty () && loadedName != to_qstring (ns.name)) {
    string ignored;
    athena_namespace_remove (from_qstring (loadedName), ignored);
  }
  loadedName= to_qstring (ns.name);
  dirty= false;
  bool previousLoading= loadingUi;
  loadingUi= true;
  refreshNamespaces ();
  refreshRelations ();
  loadingUi= previousLoading;
  refreshMembers ();
  updateModeUi ();
  statusLabel->setText (creating ? "Namespace created." : "Namespace updated.");
  return true;
}

void
QTMNamespaceManager::deleteNamespace () {
  QString name= loadedName;
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
  loadedName.clear ();
  dirty= false;
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
QTMNamespaceManager::showNamespaceContextMenu (const QPoint& pos) {
  QListWidgetItem* item= namespaceList->itemAt (pos);
  if (item == nullptr) return;
  namespaceList->setCurrentItem (item);
  if (loadedName != item->text ()) return;

  QMenu menu (this);
  menu.addAction ("Open direct hierarchy graph", this, [item] () {
    direct_hierarchy_graph_show_namespace (from_qstring (item->text ()));
  });
  menu.addAction ("Show in namespace explorer", this, [item] () {
    namespace_explorer_show_namespace (from_qstring (item->text ()));
  });
  menu.addSeparator ();
  menu.addAction ("Update namespace", this, [this] () {
    saveNamespace ();
  })->setEnabled (!loadedName.isEmpty ());
  menu.addAction ("Delete namespace", this, [this] () {
    deleteNamespace ();
  })->setEnabled (!loadedName.isEmpty ());
  menu.addSeparator ();
  menu.addAction ("Refresh", this, [this] () { refreshAll (); });
  menu.exec (namespaceList->viewport ()->mapToGlobal (pos));
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

  string error;
  bool firstSemi= namespace_at_least_semi_concrete (first);
  bool secondSemi= namespace_at_least_semi_concrete (second);
  if (first.kind != "abstract" && !firstSemi) {
    QMessageBox::warning (
      this, "Generate Sub-products",
      QString ("Namespace \"%1\" has a template but no usable sorter. "
               "Add a sorter or mark it abstract before generating "
               "sub-products.")
        .arg (names[0]));
    return;
  }
  if (second.kind != "abstract" && !secondSemi) {
    QMessageBox::warning (
      this, "Generate Sub-products",
      QString ("Namespace \"%1\" has a template but no usable sorter. "
               "Add a sorter or mark it abstract before generating "
               "sub-products.")
        .arg (names[1]));
    return;
  }

  auto confirmExisting= [this] (const QStringList& productNames) {
    QStringList existing;
    for (const QString& name: productNames) {
      athena_namespace_definition ignored;
      if (athena_namespace_get (from_qstring (name), ignored))
        existing << name;
    }
    if (existing.isEmpty ()) return true;
    return QMessageBox::question (
      this, "Update Existing Namespaces",
      QString ("These namespaces already exist and will be updated:\n%1")
        .arg (existing.join ("\n")),
      QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes;
  };

  auto saveProducts= [this, &first, &second, &error] (
      const QStringList& productNames, const QString& kind,
      const QString& templ, bool sorterTrivial, string sorterPath) {
    for (const QString& name: productNames) {
      athena_namespace_definition ns;
      ns.name= from_qstring (name);
      ns.kind= from_qstring (kind);
      ns.templ= from_qstring (templ);
      ns.sorter_trivial= sorterTrivial;
      ns.sorter_path= sorterTrivial ? string ("") : sorterPath;
      ns.style_path= "";
      ns.initial_content_path= "";
      ns.homepage_path= "";
      ns.parents= array<string> ();
      ns.parents << first.name << second.name;
      ns.derived_parents= array<string> ();
      if (ns.kind == "abstract") {
        ns.templ= "";
        ns.sorter_trivial= false;
        ns.sorter_path= "";
      }
      if (!athena_namespace_save (ns, error)) return false;
    }
    return true;
  };

  QStringList productNames;
  if (firstSemi && secondSemi) {
    bool aggressive=
      get_preference ("vault subproduct consume string aggressively", "on") ==
      "on";
    string suggested;
    if (!athena_namespace_suggest_subproduct_template (
          first.templ, second.templ, aggressive, suggested, error)) {
      suggested= "";
      if (error != "")
        QMessageBox::information (this, "Sub-product Template",
                                  to_qstring (error));
    }

    string firstSource, secondSource;
    if (!athena_namespace_sorter_source (first, firstSource, error)) {
      QMessageBox::warning (this, "Generate Sub-products", to_qstring (error));
      return;
    }
    if (!athena_namespace_sorter_source (second, secondSource, error)) {
      QMessageBox::warning (this, "Generate Sub-products", to_qstring (error));
      return;
    }

    QWizard wizard (this);
    wizard.setWindowTitle ("Generate Sub-products");
    wizard.setWizardStyle (QWizard::ModernStyle);
    wizard.resize (980, 680);
    wizard.addPage (new SorterCompatibilityWizardPage (
      first, second, utf8_to_qstring (firstSource),
      utf8_to_qstring (secondSource), &wizard));
    SubproductNamesWizardPage* namesPage=
      new SubproductNamesWizardPage (names[0], names[1], &wizard);
    SubproductTemplateWizardPage* templatePage=
      new SubproductTemplateWizardPage (first, second, to_qstring (suggested),
                                        &wizard);
    wizard.addPage (namesPage);
    wizard.addPage (templatePage);
    wizard.addPage (new SubproductSummaryWizardPage (namesPage, templatePage,
                                                     &wizard));
    if (wizard.exec () != QDialog::Accepted) return;

    productNames= namesPage->names ();
    QString templ= templatePage->templ ();
    string sorterPath;
    if (!athena_namespace_generate_product_sorter (
          first, second, from_qstring (templ), sorterPath, error)) {
      QMessageBox::warning (this, "Generate Sub-products", to_qstring (error));
      return;
    }
    if (!confirmExisting (productNames)) return;
    if (!saveProducts (productNames, "semi-concrete", templ, false,
                       sorterPath)) {
      QMessageBox::warning (this, "Generate Sub-products", to_qstring (error));
      return;
    }
  }
  else if (firstSemi || secondSemi) {
    const athena_namespace_definition& semi= firstSemi ? first : second;

    QWizard wizard (this);
    wizard.setWindowTitle ("Generate Sub-products");
    wizard.setWizardStyle (QWizard::ModernStyle);
    wizard.resize (760, 560);
    SubproductNamesWizardPage* namesPage=
      new SubproductNamesWizardPage (names[0], names[1], &wizard);
    SingleParentSubproductTemplateWizardPage* templatePage=
      new SingleParentSubproductTemplateWizardPage (
        semi, to_qstring (semi.templ), &wizard);
    wizard.addPage (namesPage);
    wizard.addPage (templatePage);
    wizard.addPage (new SingleParentSubproductSummaryWizardPage (
      namesPage, templatePage, &wizard));
    if (wizard.exec () != QDialog::Accepted) return;

    productNames= namesPage->names ();
    bool sorterTrivial= semi.sorter_trivial;
    string sorterPath;
    if (!sorterTrivial &&
        !athena_namespace_generate_restricted_sorter (
          semi, from_qstring (templatePage->templ ()), sorterPath, error)) {
      QMessageBox::warning (this, "Generate Sub-products", to_qstring (error));
      return;
    }
    if (!confirmExisting (productNames)) return;
    if (!saveProducts (productNames, "semi-concrete", templatePage->templ (),
                       sorterTrivial, sorterPath)) {
      QMessageBox::warning (this, "Generate Sub-products", to_qstring (error));
      return;
    }
  }
  else {
    QWizard wizard (this);
    wizard.setWindowTitle ("Generate Sub-products");
    wizard.setWizardStyle (QWizard::ModernStyle);
    wizard.resize (700, 430);
    SubproductNamesWizardPage* namesPage=
      new SubproductNamesWizardPage (names[0], names[1], &wizard);
    wizard.addPage (namesPage);
    wizard.addPage (new AbstractSubproductSummaryWizardPage (namesPage,
                                                             &wizard));
    if (wizard.exec () != QDialog::Accepted) return;

    productNames= namesPage->names ();
    if (!confirmExisting (productNames)) return;
    if (!saveProducts (productNames, "abstract", "", false, "")) {
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
    modeLabel->setText ("Mode: no namespace selected");
    saveNamespaceAction->setText ("Create namespace");
    saveNamespaceAction->setToolTip (
      "Use New namespace... to create a namespace");
  }
  else {
    modeLabel->setText (
      QString ("Mode: editing namespace \"%1\"%2")
        .arg (loadedName, dirty ? QString (" (modified)") : QString ()));
    saveNamespaceAction->setText ("Update namespace");
    saveNamespaceAction->setToolTip (
      "Update the selected namespace using the current form fields");
  }
  saveNamespaceAction->setEnabled (!creating);
  deleteNamespaceAction->setEnabled (!creating);

  bool abstract= kindCombo->currentText () == "abstract";
  bool concrete= kindCombo->currentText () == "concrete";
  templateEdit->setEnabled (!abstract);
  templateEdit->setPlaceholderText (
    abstract ? "Abstract namespaces aggregate subspaces" : "%w Lecture Notes %R");
  styleEdit->setEnabled (concrete);
  styleBrowseButton->setEnabled (concrete);
  styleEdit->setPlaceholderText (
    concrete ? "Optional .ts stylesheet for new files" :
               "Only concrete namespaces use a style");
  initialContentEdit->setEnabled (concrete);
  initialContentBrowseButton->setEnabled (concrete);
  initialContentEdit->setPlaceholderText (
    concrete ? "Optional .ath template document" :
               "Only concrete namespaces use initial content");
  trivialSorterCheck->setEnabled (!abstract);
  bool sorterEnabled= !abstract && !trivialSorterCheck->isChecked ();
  sorterEdit->setEnabled (sorterEnabled);
  sorterBrowseButton->setEnabled (sorterEnabled);
  sorterEdit->setPlaceholderText (
    abstract ? "Abstract namespaces do not match files directly" :
    trivialSorterCheck->isChecked ()
      ? "Built-in sorter returns 0 for every comparison" : "");
  homepageEdit->setPlaceholderText ("Optional .ath homepage document");
  homepageEditButton->setEnabled (!homepageEdit->text ().trimmed ().isEmpty ());
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
QTMNamespaceManager::chooseInitialContentPath () {
  QString selected= namespace_choose_file (
    this, initialContentEdit, "Choose Initial Content",
    "*.ath|ATHENA documents");
  if (!selected.isEmpty ()) initialContentEdit->setText (selected);
}

void
QTMNamespaceManager::chooseHomepagePath () {
  QString selected= namespace_choose_file (
    this, homepageEdit, "Choose Namespace Homepage", "*.ath|ATHENA documents");
  if (!selected.isEmpty ()) homepageEdit->setText (selected);
  updateModeUi ();
}

void
QTMNamespaceManager::createHomepagePath () {
  QString selected= namespace_choose_homepage_target (this, homepageEdit);
  if (selected.isEmpty ()) return;
  QString error;
  if (!namespace_create_homepage_file (selected, error)) {
    QMessageBox::warning (this, "Namespace Manager", error);
    return;
  }
  homepageEdit->setText (selected);
  updateModeUi ();
}

void
QTMNamespaceManager::editHomepage () {
  QString path= homepageEdit->text ().trimmed ();
  if (path.isEmpty ()) return;
  QString root= namespace_vault_root_path ();
  QString abs= QFileInfo (path).isAbsolute () ? path :
    QDir (root).absoluteFilePath (path);
  if (!QFileInfo::exists (abs)) {
    QMessageBox::warning (this, "Namespace Manager",
                          "Homepage file does not exist.");
    return;
  }
  exec_delayed (scheme_cmd (list_object (symbol_object ("load-buffer"),
                            object (url_system (from_qstring (abs))))));
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
  markDirty ();
}

void
QTMNamespaceManager::removeSelectedExplicitParents () {
  QList<QListWidgetItem*> items= explicitParentsList->selectedItems ();
  if (items.isEmpty ()) return;
  for (QListWidgetItem* item: items)
    delete explicitParentsList->takeItem (explicitParentsList->row (item));
  markDirty ();
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
  if (qt_defer_to_main_thread (namespace_manager_show)) return;
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

  win->showAdsDockWidget (namespace_manager_dock, ads::RightDockWidgetArea);

  namespace_manager_dock->setWindowTitle (title);
  namespace_manager_widget->setFocus ();
}

void
namespace_manager_show_namespace (string name) {
  if (qt_defer_to_main_thread (namespace_manager_show_namespace, name)) return;
  namespace_manager_show ();
  if (namespace_manager_widget == nullptr) return;

  QString qname= to_qstring (name);
  if (!namespace_manager_widget->selectNamespace (qname)) {
    QMessageBox::warning (
      QApplication::activeWindow (), "Namespace Manager",
      QString ("Namespace \"%1\" is not visible in the namespace manager.")
        .arg (qname));
  }
}

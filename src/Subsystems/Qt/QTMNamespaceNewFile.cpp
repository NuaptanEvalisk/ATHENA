/******************************************************************************
* MODULE     : QTMNamespaceNewFile.cpp
* DESCRIPTION: Native Qt namespace file creation helpers
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMNamespaceNewFile.hpp"

#include "file.hpp"
#include "namespaces.hpp"
#include "qt_utilities.hpp"
#include "vault.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWizard>
#include <QWizardPage>

namespace {

QString
namespace_new_vault_root () {
  if (!vault_active ()) return QDir::homePath ();
  return QFileInfo (to_qstring (concretize (vault_get_root ())))
    .absoluteFilePath ();
}

QString
namespace_new_stem_from_path (const QString& path) {
  QFileInfo info (path);
  QString name= info.completeBaseName ();
  if (!name.isEmpty ()) return name;
  return info.fileName ();
}

std::vector<athena_namespace_definition>
concrete_namespaces () {
  std::vector<athena_namespace_definition> out;
  for (const athena_namespace_definition& ns: athena_namespaces_list ())
    if (ns.kind == "concrete") out.push_back (ns);
  return out;
}

QValidator*
namespace_field_validator (string type, QObject* parent) {
  QString pattern;
  if (type == "word") pattern= "\\S+";
  else if (type == "char") pattern= ".";
  else if (type == "int") pattern= "-?[0-9]+";
  else if (type == "positive-int") pattern= "0*[1-9][0-9]*";
  else if (type == "roman") pattern= "[IVXLCDMivxlcdm]+";
  else return nullptr;
  return new QRegularExpressionValidator (QRegularExpression (pattern),
                                          parent);
}

class NamespaceSelectPage : public QWizardPage {
public:
  NamespaceSelectPage (QWidget* parent = nullptr)
    : QWizardPage (parent), combo (new QComboBox (this)) {
    setTitle ("Concrete Namespace");
    setSubTitle ("Choose the concrete namespace that determines the file "
                 "name, style, and optional initial content.");
    QVBoxLayout* layout= new QVBoxLayout (this);
    layout->addWidget (combo);
  }

  void initializePage () override {
    combo->clear ();
    namespaces= concrete_namespaces ();
    for (size_t i=0; i<namespaces.size (); i++)
      combo->addItem (to_qstring (namespaces[i].name), (int) i);
  }

  bool validatePage () override {
    if (combo->currentIndex () < 0) {
      QMessageBox::warning (this, "New within namespace",
                            "No concrete namespace is available.");
      return false;
    }
    return true;
  }

  athena_namespace_definition selectedNamespace () const {
    int i= combo->currentData ().toInt ();
    return namespaces[(size_t) i];
  }

private:
  QComboBox* combo;
  std::vector<athena_namespace_definition> namespaces;
};

class NamespaceFieldsPage : public QWizardPage {
public:
  NamespaceFieldsPage (NamespaceSelectPage* select, QWidget* parent = nullptr)
    : QWizardPage (parent), select (select), form (new QFormLayout) {
    setTitle ("File Name Fields");
    setSubTitle ("Fill the fields of the selected namespace filename "
                 "template.");
    QVBoxLayout* layout= new QVBoxLayout (this);
    QLabel* hint= new QLabel (this);
    hint->setWordWrap (true);
    hint->setText ("The final file name is produced by substituting these "
                   "values into the namespace template.");
    layout->addWidget (hint);
    layout->addLayout (form);
  }

  void initializePage () override {
    while (QLayoutItem* item= form->takeAt (0)) {
      if (item->widget ()) item->widget ()->deleteLater ();
      delete item;
    }
    edits.clear ();

    string error;
    fields= athena_namespace_template_fields (select->selectedNamespace (),
                                              error);
    if (error != "")
      QMessageBox::warning (this, "New within namespace", to_qstring (error));
    for (size_t i=0; i<fields.size (); i++) {
      QLineEdit* edit= new QLineEdit (this);
      edit->setPlaceholderText (to_qstring (fields[i].type));
      if (QValidator* validator=
            namespace_field_validator (fields[i].type, edit))
        edit->setValidator (validator);
      edits.push_back (edit);
      QString label= QString ("%1 (%2)")
        .arg (to_qstring (fields[i].placeholder), to_qstring (fields[i].type));
      form->addRow (label, edit);
    }
  }

  bool validatePage () override {
    for (QLineEdit* edit: edits) {
      if (edit->text ().isEmpty ()) {
        QMessageBox::warning (this, "New within namespace",
                              "All filename fields must be filled.");
        return false;
      }
    }
    string stem;
    string error;
    if (!athena_namespace_build_stem (select->selectedNamespace (), values (),
                                      stem, error)) {
      QMessageBox::warning (this, "New within namespace", to_qstring (error));
      return false;
    }
    return true;
  }

  strings values () const {
    strings out;
    for (QLineEdit* edit: edits) out << from_qstring (edit->text ());
    return out;
  }

private:
  NamespaceSelectPage* select;
  QFormLayout* form;
  std::vector<athena_namespace_template_field> fields;
  std::vector<QLineEdit*> edits;
};

class NamespaceDirectoryPage : public QWizardPage {
public:
  NamespaceDirectoryPage (QWidget* parent = nullptr)
    : QWizardPage (parent), edit (new QLineEdit (this)),
      browse (new QPushButton ("Browse...", this)) {
    setTitle ("Storage Directory");
    setSubTitle ("Choose where the new .ath file will be stored.");
    QHBoxLayout* row= new QHBoxLayout ();
    row->addWidget (edit, 1);
    row->addWidget (browse);
    QVBoxLayout* layout= new QVBoxLayout (this);
    layout->addLayout (row);
    connect (browse, &QPushButton::clicked, this, [this] () {
      QString dir= QFileDialog::getExistingDirectory (
        this, "Choose storage directory",
        edit->text ().isEmpty () ? namespace_new_vault_root () : edit->text ());
      if (!dir.isEmpty ()) edit->setText (dir);
    });
  }

  void initializePage () override {
    if (edit->text ().isEmpty ()) edit->setText (namespace_new_vault_root ());
  }

  bool validatePage () override {
    QDir dir (edit->text ());
    if (!dir.exists ()) {
      QMessageBox::warning (this, "New within namespace",
                            "The storage directory does not exist.");
      return false;
    }
    return true;
  }

  QString directory () const { return edit->text ().trimmed (); }

private:
  QLineEdit* edit;
  QPushButton* browse;
};

class NamespaceConfirmPage : public QWizardPage {
public:
  NamespaceConfirmPage (NamespaceSelectPage* select, NamespaceFieldsPage* fields,
                        NamespaceDirectoryPage* dir,
                        QWidget* parent = nullptr)
    : QWizardPage (parent), select (select), fields (fields), dir (dir),
      label (new QLabel (this)) {
    setTitle ("Confirm File Creation");
    label->setWordWrap (true);
    label->setTextFormat (Qt::PlainText);
    label->setTextInteractionFlags (Qt::TextSelectableByMouse);
    QScrollArea* scroll= new QScrollArea (this);
    scroll->setWidgetResizable (true);
    scroll->setFrameShape (QFrame::NoFrame);
    scroll->setWidget (label);
    QVBoxLayout* layout= new QVBoxLayout (this);
    layout->addWidget (scroll, 1);
  }

  void initializePage () override {
    string error;
    string stem;
    if (!athena_namespace_build_stem (select->selectedNamespace (),
                                      fields->values (), stem, error)) {
      target.clear ();
      label->setText ("Could not build the target filename:\n" +
                      to_qstring (error));
      return;
    }
    target= QDir (dir->directory ())
      .absoluteFilePath (to_qstring (stem) + ".ath");
    athena_namespace_definition ns= select->selectedNamespace ();
    QStringList lines;
    lines << "Namespace: " + to_qstring (ns.name)
          << "Target: " + target
          << "Style: " + (ns.style_path == "" ? QString ("<none>") :
                          to_qstring (ns.style_path))
          << "Initial content: " +
             (ns.initial_content_path == "" ? QString ("<none>") :
              to_qstring (ns.initial_content_path));
    label->setText (lines.join ("\n"));
  }

  bool validatePage () override {
    if (target.isEmpty ()) {
      QMessageBox::warning (this, "New within namespace",
                            "The target filename is invalid.");
      return false;
    }
    if (QFileInfo::exists (target)) {
      QMessageBox::warning (this, "New within namespace",
                            "The target file already exists.");
      return false;
    }
    return true;
  }

  QString targetPath () const { return target; }

private:
  NamespaceSelectPage* select;
  NamespaceFieldsPage* fields;
  NamespaceDirectoryPage* dir;
  QLabel* label;
  QString target;
};

class NamespaceNewFileWizard : public QWizard {
public:
  NamespaceNewFileWizard (QWidget* parent = nullptr)
    : QWizard (parent),
      select (new NamespaceSelectPage (this)),
      fields (new NamespaceFieldsPage (select, this)),
      dir (new NamespaceDirectoryPage (this)),
      confirm (new NamespaceConfirmPage (select, fields, dir, this)) {
    setWindowTitle ("New within namespace");
    setPage (0, select);
    setPage (1, fields);
    setPage (2, dir);
    setPage (3, confirm);
    setMinimumSize (760, 560);
    resize (780, 620);
  }

  bool create (QString& target, QString& message) {
    target= confirm->targetPath ();
    string error;
    athena_namespace_definition ns= select->selectedNamespace ();
    bool ok= athena_namespace_create_file (
      ns, url_system (from_qstring (target)), "", true, error);
    if (!ok) {
      message= to_qstring (error);
      return false;
    }
    return true;
  }

private:
  NamespaceSelectPage* select;
  NamespaceFieldsPage* fields;
  NamespaceDirectoryPage* dir;
  NamespaceConfirmPage* confirm;
};

} // namespace

string
namespace_new_file_wizard () {
  NamespaceNewFileWizard wizard;
  if (wizard.exec () != QDialog::Accepted) return "";
  QString target;
  QString message;
  if (!wizard.create (target, message)) {
    QMessageBox::warning (nullptr, "New within namespace", message);
    return "";
  }
  return from_qstring (target);
}

bool
namespace_create_file_with_optional_initializer (string system_path,
                                                 string& error) {
  QString target= to_qstring (system_path);
  QString stem= namespace_new_stem_from_path (target);
  string match_error;
  std::vector<athena_namespace_definition> matches=
    athena_namespace_concrete_matches_stem (from_qstring (stem), match_error);
  if (match_error != "" && matches.empty ()) error= match_error;

  int chosen= -1;
  if (!matches.empty ()) {
    QDialog dialog;
    dialog.setWindowTitle ("Initialize from namespace");
    QVBoxLayout* layout= new QVBoxLayout (&dialog);
    QLabel* label= new QLabel (
      "This file name matches one or more concrete namespaces. Choose an "
      "initializer, or create a plain ATHENA document.", &dialog);
    label->setWordWrap (true);
    layout->addWidget (label);
    QComboBox* combo= new QComboBox (&dialog);
    combo->addItem ("Do not initialize from namespace", -1);
    for (size_t i=0; i<matches.size (); i++)
      combo->addItem (to_qstring (matches[i].name), (int) i);
    combo->setCurrentIndex (matches.size () == 1 ? 1 : 0);
    layout->addWidget (combo);
    QHBoxLayout* buttons= new QHBoxLayout ();
    buttons->addStretch (1);
    QPushButton* cancel= new QPushButton ("Cancel", &dialog);
    QPushButton* create= new QPushButton ("Create", &dialog);
    buttons->addWidget (cancel);
    buttons->addWidget (create);
    layout->addLayout (buttons);
    QObject::connect (cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect (create, &QPushButton::clicked, &dialog, &QDialog::accept);
    if (dialog.exec () != QDialog::Accepted) {
      error= "cancelled";
      return false;
    }
    chosen= combo->currentData ().toInt ();
  }

  url target_url= url_system (system_path);
  if (chosen >= 0)
    return athena_namespace_create_file (matches[(size_t) chosen], target_url,
                                         "", true, error);
  return athena_namespace_create_plain_file (target_url, error);
}

/******************************************************************************
* MODULE     : QTMWebsitesManager.cpp
* DESCRIPTION: Qt websites manager pane for ATHENA vaults
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMWebsitesManager.hpp"

#include "ATHENA/Data/websites.hpp"
#include "QTMMainTabWindow.hpp"
#include "namespaces.hpp"
#include "qt_utilities.hpp"
#include "vault.hpp"

#include <DockWidget.h>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QTextEdit>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWizard>
#include <QWizardPage>

#include <algorithm>
#include <set>

static QWidget* websites_manager_widget= nullptr;
static ads::CDockWidget* websites_manager_dock= nullptr;

namespace {

class QAnsiTextEdit : public QTextEdit {
public:
  explicit QAnsiTextEdit (QWidget* parent= nullptr)
    : QTextEdit (parent), currentFormat (baseFormat ()) {
    setReadOnly (true);
    setAcceptRichText (false);
    setLineWrapMode (QTextEdit::NoWrap);
    setFont (QFontDatabase::systemFont (QFontDatabase::FixedFont));
  }

  void appendAnsiText (const QString& text) {
    QTextCursor cursor= textCursor ();
    cursor.movePosition (QTextCursor::End);
    for (int i=0; i<text.size (); ) {
      QChar ch= text[i];
      if (ch == QChar (0x1b)) {
        if (handleEscape (text, i, cursor)) continue;
        i++;
        continue;
      }
      if (ch == '\r') {
        clearCurrentLine (cursor);
        i++;
        continue;
      }
      if (ch == '\b') {
        cursor.deletePreviousChar ();
        i++;
        continue;
      }
      if (ch == '\n') {
        cursor.insertBlock ();
        i++;
        continue;
      }

      int start= i;
      while (i<text.size () && text[i] != QChar (0x1b) &&
             text[i] != '\r' && text[i] != '\b' && text[i] != '\n')
        i++;
      cursor.insertText (text.mid (start, i - start), currentFormat);
    }
    setTextCursor (cursor);
    ensureCursorVisible ();
  }

private:
  QTextCharFormat currentFormat;

  QTextCharFormat baseFormat () const {
    QTextCharFormat format;
    format.setFont (QFontDatabase::systemFont (QFontDatabase::FixedFont));
    return format;
  }

  void clearCurrentLine (QTextCursor& cursor) {
    cursor.movePosition (QTextCursor::StartOfBlock);
    cursor.movePosition (QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    cursor.removeSelectedText ();
  }

  bool handleEscape (const QString& text, int& i, QTextCursor& cursor) {
    if (i + 1 >= text.size () || text[i + 1] != '[') return false;

    int j= i + 2;
    while (j<text.size () &&
           ((text[j] >= '0' && text[j] <= '9') || text[j] == ';' ||
            text[j] == '?' || text[j] == ':' || text[j] == ' '))
      j++;
    if (j >= text.size ()) return false;

    QString params= text.mid (i + 2, j - i - 2).trimmed ();
    QChar command= text[j];
    if (command == 'm') applySgr (params);
    else if (command == 'K') clearCurrentLine (cursor);
    i= j + 1;
    return true;
  }

  void applySgr (const QString& params) {
    QStringList parts= params.isEmpty () ? QStringList ("0") :
                       params.split (';', Qt::KeepEmptyParts);
    for (int i=0; i<parts.size (); i++) {
      bool ok= false;
      int code= parts[i].isEmpty () ? 0 : parts[i].toInt (&ok);
      if (!ok) continue;
      if (code == 0) currentFormat= baseFormat ();
      else if (code == 1) currentFormat.setFontWeight (QFont::Bold);
      else if (code == 3) currentFormat.setFontItalic (true);
      else if (code == 4) currentFormat.setFontUnderline (true);
      else if (code == 22) currentFormat.setFontWeight (QFont::Normal);
      else if (code == 23) currentFormat.setFontItalic (false);
      else if (code == 24) currentFormat.setFontUnderline (false);
      else if (code == 39) currentFormat.clearForeground ();
      else if (code == 49) currentFormat.clearBackground ();
      else if ((code >= 30 && code <= 37) || (code >= 90 && code <= 97))
        currentFormat.setForeground (ansiColor (code));
      else if ((code >= 40 && code <= 47) || (code >= 100 && code <= 107))
        currentFormat.setBackground (ansiColor (code - 10));
    }
  }

  QColor ansiColor (int code) const {
    static const QColor normal[]= {
      QColor (0, 0, 0), QColor (170, 0, 0), QColor (0, 170, 0),
      QColor (170, 85, 0), QColor (0, 0, 170), QColor (170, 0, 170),
      QColor (0, 170, 170), QColor (170, 170, 170)
    };
    static const QColor bright[]= {
      QColor (85, 85, 85), QColor (255, 85, 85), QColor (85, 255, 85),
      QColor (255, 255, 85), QColor (85, 85, 255),
      QColor (255, 85, 255), QColor (85, 255, 255),
      QColor (255, 255, 255)
    };
    if (code >= 90 && code <= 97) return bright[code - 90];
    return normal[code - 30];
  }
};

static QString
qss (const std::string& s) {
  return QString::fromUtf8 (s.c_str ());
}

static std::string
qstd (const QString& s) {
  QByteArray bytes= s.toUtf8 ();
  return std::string (bytes.constData (), (size_t) bytes.size ());
}

static QString
vault_root_qstring () {
  if (!vault_active ()) return QString ();
  return to_qstring (concretize (vault_get_root ()));
}

static std::string
vault_root_std () {
  return qstd (vault_root_qstring ());
}

static QString
selector_summary (const athena_website_selector& selector) {
  return qss (athena_website_selector_summary (selector));
}

static bool
valid_public_site_url (const QString& raw) {
  QString text= raw.trimmed ();
  if (text.isEmpty ()) return false;
  QUrl url (text);
  QString scheme= url.scheme ().toLower ();
  return url.isValid () && !url.isRelative () &&
         (scheme == "http" || scheme == "https") && !url.host ().isEmpty ();
}

static QStringList
namespace_names () {
  QStringList out;
  for (const athena_namespace_definition& ns: athena_namespaces_list ())
    out << to_qstring (ns.name);
  out.sort ();
  out.removeDuplicates ();
  return out;
}

static QStringList
document_paths_for_selector (const athena_website_selector& selector) {
  std::vector<std::string> files;
  std::string error;
  QStringList out;
  if (!athena_website_selector_files (vault_root_std (), selector, files,
                                      error))
    return out;
  for (const std::string& file: files) out << qss (file);
  return out;
}

static QString
destination_path (const athena_website_entry& website) {
  QString dest= qss (website.destination);
  if (dest.isEmpty ()) dest= qss (website.name);
  if (QDir::isAbsolutePath (dest)) return QDir::cleanPath (dest);
  return QDir (vault_root_qstring ()).absoluteFilePath (dest);
}

static QString
unique_website_id (const std::vector<athena_website_entry>& websites,
                   const QString& name) {
  QString base= name.toLower ().trimmed ();
  base.replace (QRegularExpression ("[^a-z0-9_-]+"), "-");
  while (base.startsWith ('-')) base.remove (0, 1);
  while (base.endsWith ('-')) base.chop (1);
  if (base.isEmpty ()) base= "website";
  std::set<QString> used;
  for (const athena_website_entry& website: websites)
    used.insert (qss (website.id));
  QString candidate= base;
  int suffix= 2;
  while (used.count (candidate) != 0)
    candidate= base + "-" + QString::number (suffix++);
  return candidate;
}

static bool
website_name_taken (const std::vector<athena_website_entry>& websites,
                    const QString& name, const std::string& except_id) {
  for (const athena_website_entry& website: websites) {
    if (website.id == except_id) continue;
    if (qss (website.name).compare (name, Qt::CaseInsensitive) == 0)
      return true;
  }
  return false;
}

class SelectorPage : public QWizardPage {
public:
  SelectorPage (QWidget* parent= nullptr): QWizardPage (parent) {
    setTitle ("Selector");
    setSubTitle ("Build the document set from paths, namespaces, and boolean operators.");

    summary= new QTextEdit;
    summary->setReadOnly (true);
    summary->setMinimumHeight (120);

    op= new QComboBox;
    op->addItem ("OR", "or");
    op->addItem ("AND", "and");
    op->addItem ("XOR", "xor");
    op->addItem ("NAND", "nand");
    op->addItem ("NOR", "nor");

    QPushButton* addPath= new QPushButton ("Add path");
    QPushButton* addNamespace= new QPushButton ("Add namespace");
    QPushButton* wrapNot= new QPushButton ("Wrap NOT");
    QPushButton* clear= new QPushButton ("Clear");

    QHBoxLayout* controls= new QHBoxLayout;
    controls->addWidget (new QLabel ("Combine:"));
    controls->addWidget (op);
    controls->addWidget (addPath);
    controls->addWidget (addNamespace);
    controls->addWidget (wrapNot);
    controls->addWidget (clear);
    controls->addStretch ();

    QVBoxLayout* layout= new QVBoxLayout;
    layout->addWidget (summary);
    layout->addLayout (controls);
    setLayout (layout);

    connect (addPath, &QPushButton::clicked, this, [this] () {
      QString root= vault_root_qstring ();
      QString selected= QFileDialog::getExistingDirectory (
        this, "Choose vault folder", root);
      if (selected.isEmpty ()) {
        selected= QFileDialog::getOpenFileName (
          this, "Choose vault document", root,
          "ATHENA documents (*.ath *.tm);;All files (*)");
      }
      if (selected.isEmpty ()) return;
      QString rel= QDir (root).relativeFilePath (selected);
      rel= QDir::cleanPath (rel);
      if (rel == "." || rel.startsWith ("../") || QDir::isAbsolutePath (rel)) {
        QMessageBox::warning (this, "Websites manager",
                              "Selected path must be inside the active vault.");
        return;
      }
      athena_website_selector leaf;
      leaf.op= "path";
      leaf.value= qstd (rel);
      combine (leaf);
    });

    connect (addNamespace, &QPushButton::clicked, this, [this] () {
      QStringList names= namespace_names ();
      if (names.isEmpty ()) {
        QMessageBox::warning (this, "Websites manager",
                              "No namespaces are available in this vault.");
        return;
      }
      bool ok= false;
      QString name= QInputDialog::getItem (this, "Choose namespace",
                                           "Namespace:", names, 0, false, &ok);
      if (!ok || name.isEmpty ()) return;
      athena_website_selector leaf;
      leaf.op= "namespace";
      leaf.value= qstd (name);
      combine (leaf);
    });

    connect (wrapNot, &QPushButton::clicked, this, [this] () {
      if (athena_website_selector_empty (selector)) return;
      athena_website_selector wrapped;
      wrapped.op= "not";
      wrapped.children.push_back (selector);
      selector= wrapped;
      refresh ();
    });

    connect (clear, &QPushButton::clicked, this, [this] () {
      selector= athena_website_selector ();
      refresh ();
    });
    refresh ();
  }

  void setSelector (const athena_website_selector& next) {
    selector= next;
    refresh ();
  }

  athena_website_selector currentSelector () const {
    return selector;
  }

  bool isComplete () const override {
    return !athena_website_selector_empty (selector);
  }

private:
  QTextEdit* summary;
  QComboBox* op;
  athena_website_selector selector;

  void combine (const athena_website_selector& leaf) {
    if (athena_website_selector_empty (selector)) {
      selector= leaf;
    }
    else {
      athena_website_selector combined;
      combined.op= qstd (op->currentData ().toString ());
      combined.children.push_back (selector);
      combined.children.push_back (leaf);
      selector= combined;
    }
    refresh ();
  }

  void refresh () {
    summary->setPlainText (selector_summary (selector));
    emit completeChanged ();
  }
};

class RedirectionsPage : public QWizardPage {
public:
  explicit RedirectionsPage (QWidget* parent= nullptr): QWizardPage (parent) {
    setTitle ("Redirections");
    setSubTitle ("Create Cloudflare Pages shortcuts for exported documents.");

    enabled= new QCheckBox ("Generate Cloudflare Pages _redirects", this);
    table= new QTableWidget (0, 3, this);
    table->setHorizontalHeaderLabels ({"Shortcut", "Document", ""});
    table->horizontalHeader ()->setSectionResizeMode (
      0, QHeaderView::ResizeToContents);
    table->horizontalHeader ()->setSectionResizeMode (1, QHeaderView::Stretch);
    table->horizontalHeader ()->setSectionResizeMode (
      2, QHeaderView::ResizeToContents);
    table->verticalHeader ()->setVisible (false);
    table->setSelectionMode (QAbstractItemView::NoSelection);
    table->setMinimumHeight (240);

    QPushButton* add= new QPushButton (
      QIcon::fromTheme ("list-add"), "Add redirection", this);
    QVBoxLayout* layout= new QVBoxLayout (this);
    layout->addWidget (enabled);
    layout->addWidget (table);
    layout->addWidget (add, 0, Qt::AlignLeft);

    connect (enabled, &QCheckBox::toggled, this, [this, add] (bool on) {
      table->setEnabled (on);
      add->setEnabled (on);
      emit completeChanged ();
    });
    connect (add, &QPushButton::clicked, this, [this] () {
      addRow (QString (), QString ());
    });
    table->setEnabled (false);
    add->setEnabled (false);
  }

  void setDocuments (const QStringList& next) {
    documents= next;
    for (int row=0; row<table->rowCount (); row++) {
      QComboBox* combo= qobject_cast<QComboBox*> (table->cellWidget (row, 1));
      if (combo == nullptr) continue;
      QString previous= combo->currentData ().toString ();
      combo->clear ();
      for (const QString& document: documents)
        combo->addItem (document, document);
      if (!previous.isEmpty () && !documents.contains (previous)) {
        combo->insertItem (0, previous + " (not exported)", previous);
        combo->setCurrentIndex (0);
      }
      else if (!previous.isEmpty ()) combo->setCurrentText (previous);
    }
    emit completeChanged ();
  }

  void setRedirections (
      bool generate,
      const std::vector<athena_website_redirection>& redirections) {
    table->setRowCount (0);
    for (const athena_website_redirection& redirection: redirections)
      addRow (qss (redirection.shortcut), qss (redirection.document));
    enabled->setChecked (generate);
  }

  bool generationEnabled () const { return enabled->isChecked (); }

  std::vector<athena_website_redirection> redirections () const {
    std::vector<athena_website_redirection> out;
    for (int row=0; row<table->rowCount (); row++) {
      QLineEdit* shortcut=
        qobject_cast<QLineEdit*> (table->cellWidget (row, 0));
      QComboBox* document=
        qobject_cast<QComboBox*> (table->cellWidget (row, 1));
      if (shortcut == nullptr || document == nullptr) continue;
      athena_website_redirection redirection;
      redirection.shortcut= qstd (shortcut->text ().trimmed ());
      redirection.document= qstd (document->currentData ().toString ());
      out.push_back (redirection);
    }
    return out;
  }

  bool validate (QString& error) const {
    if (!generationEnabled ()) return true;
    if (table->rowCount () > 2000) {
      error= "Cloudflare Pages supports at most 2,000 static shortcuts.";
      return false;
    }
    std::set<QString> shortcuts;
    for (int row=0; row<table->rowCount (); row++) {
      QLineEdit* shortcutEdit=
        qobject_cast<QLineEdit*> (table->cellWidget (row, 0));
      QComboBox* documentCombo=
        qobject_cast<QComboBox*> (table->cellWidget (row, 1));
      QString shortcut= shortcutEdit == nullptr ? QString () :
                                      shortcutEdit->text ().trimmed ();
      QString document= documentCombo == nullptr ? QString () :
                                      documentCombo->currentData ().toString ();
      if (!validShortcut (shortcut)) {
        error= "Shortcut must be a site path beginning with '/', without "
               "whitespace, a query, or a fragment.";
        return false;
      }
      if (!shortcuts.insert (shortcut).second) {
        error= "Duplicate redirection shortcut: " + shortcut;
        return false;
      }
      if (!documents.contains (document)) {
        error= "Redirection target is not in the website export range: " +
               document;
        return false;
      }
    }
    return true;
  }

private:
  QCheckBox* enabled;
  QTableWidget* table;
  QStringList documents;

  static bool validShortcut (const QString& shortcut) {
    if (!shortcut.startsWith ('/') || shortcut.startsWith ("//")) return false;
    for (QChar c: shortcut)
      if (c.isSpace () || c == '#' || c == '?') return false;
    return true;
  }

  void addRow (const QString& shortcutText, const QString& documentPath) {
    int row= table->rowCount ();
    table->insertRow (row);
    QLineEdit* shortcut= new QLineEdit (table);
    shortcut->setPlaceholderText ("/short-name");
    shortcut->setText (shortcutText);
    QComboBox* document= new QComboBox (table);
    for (const QString& item: documents) document->addItem (item, item);
    if (!documentPath.isEmpty () && !documents.contains (documentPath))
      document->insertItem (0, documentPath + " (not exported)",
                            documentPath);
    if (!documentPath.isEmpty ()) {
      int index= document->findData (documentPath);
      if (index >= 0) document->setCurrentIndex (index);
    }
    QToolButton* remove= new QToolButton (table);
    remove->setIcon (QIcon::fromTheme ("edit-delete"));
    remove->setToolTip ("Remove redirection");
    table->setCellWidget (row, 0, shortcut);
    table->setCellWidget (row, 1, document);
    table->setCellWidget (row, 2, remove);
    connect (shortcut, &QLineEdit::textChanged, this,
             [this] () { emit completeChanged (); });
    connect (document, qOverload<int> (&QComboBox::currentIndexChanged),
             this, [this] () { emit completeChanged (); });
    connect (remove, &QToolButton::clicked, this, [this, remove] () {
      for (int i=0; i<table->rowCount (); i++)
        if (table->cellWidget (i, 2) == remove) {
          table->removeRow (i);
          emit completeChanged ();
          return;
        }
    });
    emit completeChanged ();
  }
};

class WebsiteWizard : public QWizard {
public:
  WebsiteWizard (const std::vector<athena_website_entry>& existing,
                 const athena_website_entry* initial,
                 QWidget* parent= nullptr):
    QWizard (parent), websites (existing) {
    setWindowTitle (initial == nullptr ? "Create website" :
                                      "Configure website");

    namePage= new QWizardPage;
    namePage->setTitle ("Name");
    nameEdit= new QLineEdit;
    QFormLayout* nameLayout= new QFormLayout;
    nameLayout->addRow ("Website name:", nameEdit);
    namePage->setLayout (nameLayout);
    addPage (namePage);

    selectorPage= new SelectorPage;
    addPage (selectorPage);

    publishPage= new QWizardPage;
    publishPage->setTitle ("Destination");
    destinationEdit= new QLineEdit;
    QPushButton* browseDestination= new QPushButton ("Browse...");
    QHBoxLayout* destRow= new QHBoxLayout;
    destRow->addWidget (destinationEdit);
    destRow->addWidget (browseDestination);
    regenerateCombo= new QComboBox;
    regenerateCombo->addItem ("Manual", "manual");
    regenerateCombo->addItem ("Vault maintenance", "maintenance");
    postEnabled= new QCheckBox ("Run command after generation");
    postProgram= new QLineEdit;
    QPushButton* browseProgram= new QPushButton ("Browse...");
    QHBoxLayout* postProgramRow= new QHBoxLayout;
    postProgramRow->addWidget (postProgram);
    postProgramRow->addWidget (browseProgram);
    postArguments= new QLineEdit;
    QFormLayout* publishLayout= new QFormLayout;
    publishLayout->addRow ("Destination folder:", destRow);
    publishLayout->addRow ("Regenerate:", regenerateCombo);
    publishLayout->addRow ("Post command:", postEnabled);
    publishLayout->addRow ("Program:", postProgramRow);
    publishLayout->addRow ("Arguments:", postArguments);
    publishPage->setLayout (publishLayout);
    addPage (publishPage);

    sitemapPage= new QWizardPage;
    sitemapPage->setTitle ("Generated files");
    generateSitemap= new QCheckBox ("Generate sitemap.xml");
    generatePdfs= new QCheckBox (
      "Generate downloadable PDF for every document");
    publicUrlEdit= new QLineEdit;
    publicUrlEdit->setPlaceholderText ("https://example.org/athena/");
    descriptionEdit= new QLineEdit;
    descriptionEdit->setPlaceholderText (
      "Short description for search results");
    faviconEdit= new QLineEdit;
    faviconEdit->setPlaceholderText (
      "Default: ATHENA logo");
    QPushButton* browseFavicon= new QPushButton ("Browse...");
    QHBoxLayout* faviconRow= new QHBoxLayout;
    faviconRow->addWidget (faviconEdit);
    faviconRow->addWidget (browseFavicon);
    publicUrlEdit->setEnabled (false);
    QFormLayout* sitemapLayout= new QFormLayout;
    sitemapLayout->addRow (generatePdfs);
    sitemapLayout->addRow (generateSitemap);
    sitemapLayout->addRow ("Website base URL:", publicUrlEdit);
    sitemapLayout->addRow ("Description:", descriptionEdit);
    sitemapLayout->addRow ("Favicon:", faviconRow);
    sitemapPage->setLayout (sitemapLayout);
    addPage (sitemapPage);

    redirectionsPage= new RedirectionsPage;
    addPage (redirectionsPage);

    entryPage= new QWizardPage;
    entryPage->setTitle ("Entrypoint");
    fileEntry= new QRadioButton ("Document");
    namespaceEntry= new QRadioButton ("Namespace homepage");
    fileEntry->setChecked (true);
    entryFile= new QComboBox;
    entryNamespace= new QComboBox;
    QFormLayout* entryLayout= new QFormLayout;
    entryLayout->addRow (fileEntry);
    entryLayout->addRow ("Document:", entryFile);
    entryLayout->addRow (namespaceEntry);
    entryLayout->addRow ("Namespace:", entryNamespace);
    entryPage->setLayout (entryLayout);
    addPage (entryPage);

    confirmPage= new QWizardPage;
    confirmPage->setTitle ("Confirm");
    confirmation= new QTextEdit;
    confirmation->setReadOnly (true);
    QVBoxLayout* confirmLayout= new QVBoxLayout;
    confirmLayout->addWidget (confirmation);
    confirmPage->setLayout (confirmLayout);
    addPage (confirmPage);

    connect (browseDestination, &QPushButton::clicked, this, [this] () {
      QString initial= destinationEdit->text ().trimmed ().isEmpty ()
        ? vault_root_qstring () : destinationEdit->text ().trimmed ();
      QString selected= QFileDialog::getExistingDirectory (
        this, "Choose website destination", initial);
      if (!selected.isEmpty ()) destinationEdit->setText (selected);
    });
    connect (browseProgram, &QPushButton::clicked, this, [this] () {
      QString selected= QFileDialog::getOpenFileName (
        this, "Choose post-generation program", vault_root_qstring ());
      if (!selected.isEmpty ()) postProgram->setText (selected);
    });
    connect (browseFavicon, &QPushButton::clicked, this, [this] () {
      QString initial= faviconEdit->text ().trimmed ().isEmpty ()
        ? vault_root_qstring () : faviconEdit->text ().trimmed ();
      QString selected= QFileDialog::getOpenFileName (
        this, "Choose website favicon", initial,
        "Images (*.png *.jpg *.jpeg *.svg *.ico);;All files (*)");
      if (selected.isEmpty ()) return;
      QDir root (vault_root_qstring ());
      QString relative= root.relativeFilePath (selected);
      faviconEdit->setText (relative.startsWith ("../") ? selected : relative);
    });
    connect (selectorPage, &QWizardPage::completeChanged, this,
             [this] () {
               refreshEntrypoints ();
               refreshRedirectionDocuments ();
               refreshSummary ();
             });
    connect (nameEdit, &QLineEdit::textChanged, this,
             [this] () { refreshSummary (); });
    connect (destinationEdit, &QLineEdit::textChanged, this,
             [this] () { refreshSummary (); });
    connect (generateSitemap, &QCheckBox::toggled, this,
             [this] (bool enabled) {
               publicUrlEdit->setEnabled (enabled);
               refreshSummary ();
             });
    connect (generatePdfs, &QCheckBox::toggled, this,
             [this] () { refreshSummary (); });
    connect (publicUrlEdit, &QLineEdit::textChanged, this,
             [this] () { refreshSummary (); });
    connect (descriptionEdit, &QLineEdit::textChanged, this,
             [this] () { refreshSummary (); });
    connect (faviconEdit, &QLineEdit::textChanged, this,
             [this] () { refreshSummary (); });
    connect (regenerateCombo, qOverload<int> (&QComboBox::currentIndexChanged),
             this,
             [this] () { refreshSummary (); });
    connect (redirectionsPage, &QWizardPage::completeChanged, this,
             [this] () { refreshSummary (); });

    if (initial != nullptr) {
      editingId= initial->id;
      nameEdit->setText (qss (initial->name));
      selectorPage->setSelector (initial->selector);
      destinationEdit->setText (qss (initial->destination));
      generatePdfs->setChecked (initial->generate_pdfs);
      generateSitemap->setChecked (initial->generate_sitemap);
      publicUrlEdit->setText (qss (initial->public_url));
      descriptionEdit->setText (qss (initial->description));
      faviconEdit->setText (qss (initial->favicon));
      int regen= regenerateCombo->findData (qss (initial->regenerate));
      if (regen >= 0) regenerateCombo->setCurrentIndex (regen);
      postEnabled->setChecked (initial->post_command.enabled);
      postProgram->setText (qss (initial->post_command.program));
      postArguments->setText (qss (initial->post_command.arguments));
      initialEntrypointKind= qss (initial->entrypoint_kind);
      initialEntrypointValue= qss (initial->entrypoint_value);
    }
    else {
      destinationEdit->setText ("website");
    }
    refreshEntrypoints ();
    refreshRedirectionDocuments ();
    if (initial != nullptr)
      redirectionsPage->setRedirections (
        initial->generate_redirections, initial->redirections);
    refreshSummary ();
  }

  bool validateCurrentPage () override {
    if (currentPage () == namePage) {
      QString name= nameEdit->text ().trimmed ();
      if (name.isEmpty ()) {
        QMessageBox::warning (this, "Websites manager",
                              "Website name cannot be empty.");
        return false;
      }
      if (website_name_taken (websites, name, editingId)) {
        QMessageBox::warning (this, "Websites manager",
                              "Website name must be unique in this vault.");
        return false;
      }
    }
    if (currentPage () == selectorPage &&
        athena_website_selector_empty (selectorPage->currentSelector ())) {
      QMessageBox::warning (this, "Websites manager",
                            "Website selector cannot be empty.");
      return false;
    }
    if (currentPage () == sitemapPage && generateSitemap->isChecked () &&
        !valid_public_site_url (publicUrlEdit->text ())) {
      QMessageBox::warning (
        this, "Websites manager",
        "Website base URL must be an absolute http(s) URL when sitemap "
        "generation is enabled.");
      return false;
    }
    if (currentPage () == redirectionsPage) {
      QString error;
      if (!redirectionsPage->validate (error)) {
        QMessageBox::warning (this, "Websites manager", error);
        return false;
      }
    }
    return QWizard::validateCurrentPage ();
  }

  athena_website_entry resultEntry () const {
    athena_website_entry out;
    out.id= editingId.empty () ? qstd (unique_website_id (
      websites, nameEdit->text ().trimmed ())) : editingId;
    out.name= qstd (nameEdit->text ().trimmed ());
    out.selector= selectorPage->currentSelector ();
    out.destination= qstd (destinationEdit->text ().trimmed ());
    out.public_url= qstd (publicUrlEdit->text ().trimmed ());
    out.description= qstd (descriptionEdit->text ().trimmed ());
    out.favicon= qstd (faviconEdit->text ().trimmed ());
    out.generate_pdfs= generatePdfs->isChecked ();
    out.generate_sitemap= generateSitemap->isChecked ();
    out.generate_redirections= redirectionsPage->generationEnabled ();
    out.redirections= redirectionsPage->redirections ();
    out.regenerate= qstd (regenerateCombo->currentData ().toString ());
    if (namespaceEntry->isChecked ()) {
      out.entrypoint_kind= "namespace";
      out.entrypoint_value= qstd (entryNamespace->currentText ());
    }
    else {
      out.entrypoint_kind= "file";
      out.entrypoint_value= qstd (entryFile->currentText ());
    }
    out.post_command.enabled= postEnabled->isChecked ();
    out.post_command.program= qstd (postProgram->text ().trimmed ());
    out.post_command.arguments= qstd (postArguments->text ().trimmed ());
    return out;
  }

private:
  std::vector<athena_website_entry> websites;
  std::string editingId;
  QString initialEntrypointKind;
  QString initialEntrypointValue;

  QWizardPage* namePage;
  SelectorPage* selectorPage;
  QWizardPage* publishPage;
  QWizardPage* sitemapPage;
  RedirectionsPage* redirectionsPage;
  QWizardPage* entryPage;
  QWizardPage* confirmPage;
  QLineEdit* nameEdit;
  QLineEdit* destinationEdit;
  QCheckBox* generatePdfs;
  QCheckBox* generateSitemap;
  QLineEdit* publicUrlEdit;
  QLineEdit* descriptionEdit;
  QLineEdit* faviconEdit;
  QComboBox* regenerateCombo;
  QCheckBox* postEnabled;
  QLineEdit* postProgram;
  QLineEdit* postArguments;
  QRadioButton* fileEntry;
  QRadioButton* namespaceEntry;
  QComboBox* entryFile;
  QComboBox* entryNamespace;
  QTextEdit* confirmation;

  void refreshEntrypoints () {
    QString previousFile= entryFile->currentText ();
    QString previousNamespace= entryNamespace->currentText ();
    entryFile->clear ();
    entryFile->addItems (document_paths_for_selector (
      selectorPage->currentSelector ()));
    entryNamespace->clear ();
    entryNamespace->addItems (namespace_names ());
    if (!initialEntrypointValue.isEmpty ()) {
      if (initialEntrypointKind == "namespace") {
        namespaceEntry->setChecked (true);
        entryNamespace->setCurrentText (initialEntrypointValue);
      }
      else {
        fileEntry->setChecked (true);
        entryFile->setCurrentText (initialEntrypointValue);
      }
      initialEntrypointValue.clear ();
    }
    else {
      if (!previousFile.isEmpty ()) entryFile->setCurrentText (previousFile);
      if (!previousNamespace.isEmpty ())
        entryNamespace->setCurrentText (previousNamespace);
    }
  }

  void refreshRedirectionDocuments () {
    redirectionsPage->setDocuments (document_paths_for_selector (
      selectorPage->currentSelector ()));
  }

  void refreshSummary () {
    confirmation->setPlainText (
      QString ("Name: ") + nameEdit->text ().trimmed () + "\n" +
      "Selector: " + selector_summary (selectorPage->currentSelector ()) +
      "\nDestination: " + destinationEdit->text ().trimmed () +
      "\nDocument PDFs: " +
      (generatePdfs->isChecked () ? "enabled" : "disabled") +
      "\nSitemap: " +
      (generateSitemap->isChecked () ? "enabled" : "disabled") +
      "\nWebsite base URL: " +
      (publicUrlEdit->text ().trimmed ().isEmpty () ?
       "(none)" : publicUrlEdit->text ().trimmed ()) +
      "\nDescription: " +
      (descriptionEdit->text ().trimmed ().isEmpty () ?
       "(none)" : descriptionEdit->text ().trimmed ()) +
      "\nFavicon: " +
      (faviconEdit->text ().trimmed ().isEmpty () ?
       "ATHENA logo" : faviconEdit->text ().trimmed ()) +
      "\nRedirections: " +
      (redirectionsPage->generationEnabled () ?
       QString::number ((int) redirectionsPage->redirections ().size ()) +
         " configured" : "disabled") +
      "\nRegenerate: " + regenerateCombo->currentText () + "\n");
  }
};

class WebsitesManagerPane : public QWidget {
public:
  WebsitesManagerPane () {
    list= new QListWidget;
    QPushButton* create= new QPushButton ("Create");
    QPushButton* remove= new QPushButton ("Delete");
    QPushButton* rename= new QPushButton ("Rename");
    QPushButton* inspect= new QPushButton ("Inspect");
    QPushButton* configure= new QPushButton ("Configure");
    QPushButton* generate= new QPushButton ("Generate now");
    log= new QPlainTextEdit;
    log->setReadOnly (true);
    log->setMaximumHeight (120);

    QHBoxLayout* buttons= new QHBoxLayout;
    buttons->addWidget (create);
    buttons->addWidget (remove);
    buttons->addWidget (rename);
    buttons->addWidget (inspect);
    buttons->addWidget (configure);
    buttons->addWidget (generate);
    buttons->addStretch ();

    QVBoxLayout* layout= new QVBoxLayout;
    layout->addWidget (list);
    layout->addLayout (buttons);
    layout->addWidget (log);
    setLayout (layout);

    connect (create, &QPushButton::clicked, this,
             [this] () { createWebsite (); });
    connect (remove, &QPushButton::clicked, this,
             [this] () { deleteWebsite (); });
    connect (rename, &QPushButton::clicked, this,
             [this] () { renameWebsite (); });
    connect (inspect, &QPushButton::clicked, this,
             [this] () { inspectWebsite (); });
    connect (configure, &QPushButton::clicked, this,
             [this] () { configureWebsite (); });
    connect (generate, &QPushButton::clicked, this,
             [this] () { generateWebsite (); });
    connect (list, &QListWidget::itemDoubleClicked, this,
             [this] () { configureWebsite (); });
    refresh ();
  }

  void refresh (const QString& preferredId= QString ()) {
    QString selectedId= preferredId;
    if (selectedId.isEmpty () && list->currentItem () != nullptr)
      selectedId= list->currentItem ()->data (Qt::UserRole).toString ();
    std::string error;
    websites.clear ();
    list->clear ();
    if (!athena_websites_load (vault_root_std (), websites, error)) {
      log->appendPlainText ("Error: " + qss (error));
      return;
    }
    for (const athena_website_entry& website: websites) {
      QListWidgetItem* item= new QListWidgetItem (qss (website.name));
      item->setData (Qt::UserRole, qss (website.id));
      item->setToolTip (selector_summary (website.selector));
      list->addItem (item);
      if (!selectedId.isEmpty () && qss (website.id) == selectedId)
        list->setCurrentItem (item);
    }
  }

private:
  QListWidget* list;
  QPlainTextEdit* log;
  std::vector<athena_website_entry> websites;

  int currentIndex () const {
    QListWidgetItem* item= list->currentItem ();
    if (item == nullptr) return -1;
    QString id= item->data (Qt::UserRole).toString ();
    for (size_t i=0; i<websites.size (); i++)
      if (qss (websites[i].id) == id) return (int) i;
    return -1;
  }

  bool save (const QString& preferredId= QString ()) {
    std::string error;
    if (!athena_websites_save (vault_root_std (), websites, error)) {
      QMessageBox::warning (this, "Websites manager", qss (error));
      return false;
    }
    refresh (preferredId);
    return true;
  }

  void createWebsite () {
    WebsiteWizard wizard (websites, nullptr, this);
    if (wizard.exec () != QDialog::Accepted) return;
    athena_website_entry created= wizard.resultEntry ();
    websites.push_back (created);
    save (qss (created.id));
  }

  void configureWebsite () {
    int index= currentIndex ();
    if (index < 0) return;
    WebsiteWizard wizard (websites, &websites[(size_t) index], this);
    if (wizard.exec () != QDialog::Accepted) return;
    websites[(size_t) index]= wizard.resultEntry ();
    save ();
  }

  void deleteWebsite () {
    int index= currentIndex ();
    if (index < 0) return;
    QMessageBox box (QMessageBox::Question, "Delete website",
                     "Delete this website definition?",
                     QMessageBox::Yes | QMessageBox::No, this);
    QCheckBox* removeArtifacts= new QCheckBox (
      "Also remove generated destination folder");
    box.setCheckBox (removeArtifacts);
    if (box.exec () != QMessageBox::Yes) return;
    QString dest= destination_path (websites[(size_t) index]);
    websites.erase (websites.begin () + index);
    QString nextId;
    if (!websites.empty ()) {
      size_t next= std::min ((size_t) index, websites.size () - 1);
      nextId= qss (websites[next].id);
    }
    if (!save (nextId)) return;
    if (removeArtifacts->isChecked ())
      QDir (dest).removeRecursively ();
  }

  void renameWebsite () {
    int index= currentIndex ();
    if (index < 0) return;
    bool ok= false;
    QString name= QInputDialog::getText (
      this, "Rename website", "Name:", QLineEdit::Normal,
      qss (websites[(size_t) index].name), &ok).trimmed ();
    if (!ok || name.isEmpty ()) return;
    if (website_name_taken (websites, name, websites[(size_t) index].id)) {
      QMessageBox::warning (this, "Websites manager",
                            "Website name must be unique in this vault.");
      return;
    }
    websites[(size_t) index].name= qstd (name);
    save ();
  }

  void inspectWebsite () {
    int index= currentIndex ();
    if (index < 0) return;
    QDesktopServices::openUrl (
      QUrl::fromLocalFile (destination_path (websites[(size_t) index])));
  }

  void generateWebsite () {
    int index= currentIndex ();
    if (index < 0) {
      QMessageBox::information (this, "Generate website",
                                "Select a website to generate.");
      return;
    }
    showGenerationPane (websites[(size_t) index]);
  }

  void showGenerationPane (const athena_website_entry& website) {
    QTMMainTabWindow* win= QTMMainTabWindow::topTabWindow ();
    if (win == nullptr || win->dockManager () == nullptr) return;

    QWidget* pane= new QWidget;
    QVBoxLayout* layout= new QVBoxLayout;
    QLabel* status= new QLabel ("Starting website generation...");
    QProgressBar* progress= new QProgressBar;
    progress->setRange (0, 0);
    QPushButton* rerunPost= new QPushButton (
      "Rerun post-generation script");
    rerunPost->setVisible (false);
    QAnsiTextEdit* output= new QAnsiTextEdit;
    layout->addWidget (status);
    layout->addWidget (progress);
    layout->addWidget (rerunPost, 0, Qt::AlignLeft);
    layout->addWidget (output);
    pane->setLayout (layout);

    ads::CDockWidget* dock= new ads::CDockWidget (
      "Generate website: " + qss (website.name));
    dock->setWidget (pane, ads::CDockWidget::ForceNoScrollArea);
    dock->setFeature (ads::CDockWidget::DockWidgetDeleteOnClose, true);
    win->dockManager ()->addDockWidgetFloating (dock);
    dock->resize (760, 420);
    dock->show ();

    QProcess* process= new QProcess (pane);
    pane->setProperty ("athenaPostFailed", false);
    pane->setProperty ("athenaPostOnly", false);
    pane->setProperty ("athenaWebsiteOutputPending", QString ());
    process->setProgram (QCoreApplication::applicationFilePath ());
    process->setArguments (
      QStringList () << "--generate-website" << vault_root_qstring ()
                     << qss (website.id));
    process->setProcessChannelMode (QProcess::MergedChannels);
    auto consumeOutput= [=] (bool flush) {
      QString text= pane->property ("athenaWebsiteOutputPending").toString () +
                    QString::fromUtf8 (process->readAll ());
      QStringList lines= text.split ('\n');
      QString pending= lines.takeLast ();
      if (flush && !pending.isEmpty ()) {
        lines.append (pending);
        pending.clear ();
      }
      pane->setProperty ("athenaWebsiteOutputPending", pending);
      for (const QString& raw: lines) {
        QString displayLine= raw;
        QString line= raw.trimmed ();
        if (line.isEmpty ()) continue;
        if (line.startsWith ("ATHENA_WEBSITE_PROGRESS ")) {
          QStringList parts= line.split (' ');
          if (parts.size () >= 4) {
            bool ok1= false, ok2= false;
            int current= parts[1].toInt (&ok1);
            int total= parts[2].toInt (&ok2);
            if (ok1 && ok2 && total > 0) {
              progress->setRange (0, total);
              progress->setValue (current);
            }
            status->setText (line.section (' ', 3));
          }
        }
        else if (line.startsWith ("ATHENA_WEBSITE_LOG ")) {
          output->appendAnsiText (line.mid (19) + "\n");
        }
        else if (line.startsWith ("ATHENA_WEBSITE_POST_STATUS ")) {
          pane->setProperty ("athenaPostFailed",
                             line.section (' ', 1, 1) == "failed");
        }
        else output->appendAnsiText (displayLine + "\n");
      }
    };
    connect (process, &QProcess::readyRead, pane,
             [=] () { consumeOutput (false); });
    connect (process,
             qOverload<int,QProcess::ExitStatus> (&QProcess::finished),
             pane,
             [=] (int code, QProcess::ExitStatus exitStatus) {
      consumeOutput (true);
      bool postOnly= pane->property ("athenaPostOnly").toBool ();
      bool succeeded= exitStatus == QProcess::NormalExit && code == 0;
      progress->setRange (0, 1);
      progress->setValue (succeeded ? 1 : 0);
      if (succeeded)
        status->setText (postOnly ? "Post-generation script complete"
                                  : "Generation complete");
      else
        status->setText (postOnly ? "Post-generation script failed"
                                  : "Generation failed");
      rerunPost->setVisible (
        pane->property ("athenaPostFailed").toBool ());
    });
    connect (rerunPost, &QPushButton::clicked, pane, [=] () {
      if (process->state () != QProcess::NotRunning) return;
      pane->setProperty ("athenaPostFailed", false);
      pane->setProperty ("athenaPostOnly", true);
      rerunPost->setVisible (false);
      status->setText ("Running post-generation script...");
      progress->setRange (0, 0);
      output->appendAnsiText ("\nRerunning post-generation script...\n");
      process->setArguments (
        QStringList () << "--run-website-post-command"
                       << vault_root_qstring () << qss (website.id));
      process->start ();
    });
    process->start ();
  }
};

} // namespace

void
websites_manager_show () {
  if (qt_defer_to_main_thread (websites_manager_show)) return;
  if (!vault_active ()) {
    QMessageBox::warning (QApplication::activeWindow (), "Websites manager",
                          "No active vault. Please load a vault first.");
    return;
  }

  QTMMainTabWindow* win= QTMMainTabWindow::topTabWindow ();
  if (win == nullptr || win->dockManager () == nullptr) {
    QMessageBox::warning (QApplication::activeWindow (), "Websites manager",
                          "No active ATHENA window.");
    return;
  }

  bool freshDock= websites_manager_dock == nullptr;
  if (websites_manager_widget == nullptr) {
    websites_manager_widget= new WebsitesManagerPane;
    websites_manager_widget->resize (900, 520);
    QObject::connect (websites_manager_widget, &QObject::destroyed, [] () {
      websites_manager_widget= nullptr;
      websites_manager_dock= nullptr;
    });
  }
  else if (WebsitesManagerPane* pane=
             dynamic_cast<WebsitesManagerPane*> (websites_manager_widget))
    pane->refresh ();

  if (freshDock) {
    websites_manager_dock= new ads::CDockWidget ("Websites manager");
    websites_manager_dock->setObjectName ("athena-websites-manager");
    websites_manager_dock->resize (900, 520);
    websites_manager_dock->setWidget (
      websites_manager_widget, ads::CDockWidget::ForceNoScrollArea);
    websites_manager_dock->setFeature (
      ads::CDockWidget::DockWidgetDeleteOnClose, false);
    QObject::connect (websites_manager_dock, &QObject::destroyed, [] () {
      websites_manager_dock= nullptr;
    });
  }

  win->showAdsDockWidget (websites_manager_dock, ads::RightDockWidgetArea);
  websites_manager_dock->show ();
  websites_manager_dock->raise ();
}

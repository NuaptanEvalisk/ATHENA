/******************************************************************************
* MODULE     : QTMMaterialsManager.cpp
* DESCRIPTION: Vault Materials manager and landing pad
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMMaterialsManager.hpp"

#include "ATHENA/Data/materials_recognition.hpp"
#include "ATHENA/Data/materials_engine.hpp"
#include "ATHENA/Data/vault.hpp"
#include "QTMMainTabWindow.hpp"
#include "QTMZoteroImporter.hpp"
#include "boot.hpp"
#include "convert.hpp"
#include "scheme.hpp"

#include <DockWidget.h>
#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>

namespace fs= std::filesystem;

namespace {

QTMMaterialsManager* materials_widget= nullptr;
ads::CDockWidget* materials_dock= nullptr;

constexpr int material_import_progress_width= 560;

class MaterialImportProgress final: public QProgressDialog {
public:
  MaterialImportProgress (const QString& title, int count, QWidget* parent):
    QProgressDialog (QString (), "Cancel", 0, count, parent) {
    setWindowTitle (title);
    setWindowModality (Qt::WindowModal);
    setMinimumDuration (0);
    setAutoClose (false);
    setAutoReset (false);
    setFixedWidth (material_import_progress_width);
    QLabel* label= findChild<QLabel*> ();
    if (label != nullptr) {
      label->setWordWrap (true);
      label->setMinimumWidth (0);
      label->setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Preferred);
    }
  }

  void setProgressText (const QString& text) {
    setLabelText (text);
    ensurePolished ();
    int required= std::max (sizeHint ().height (), minimumSizeHint ().height ());
    if (required > monotonic_height) {
      monotonic_height= required;
      setMinimumHeight (monotonic_height);
    }
    if (height () < monotonic_height)
      resize (width (), monotonic_height);
  }

private:
  int monotonic_height= 0;
};

QString
qstr (const std::string& value) {
  return QString::fromUtf8 (value.data (), (qsizetype) value.size ());
}

std::string
stdstr (const QString& value) {
  QByteArray bytes= value.toUtf8 ();
  return std::string (bytes.constData (), (size_t) bytes.size ());
}

QString
preference (const char* name, const char* fallback= "") {
  string value= get_preference (string (name), string (fallback));
  return QString::fromUtf8 (as_charp (value), N(value));
}

bool
preference_on (const char* name) {
  return preference (name, "off") == "on";
}

MaterialsStore*
store_or_warn (QWidget* parent) {
  MaterialsStore* store= vault_get_materials_store ();
  if (store == nullptr)
    QMessageBox::warning (parent, "Materials",
                          "No active vault. Load a vault first.");
  return store;
}

QTableWidgetItem*
cell (const QString& value, bool editable= true) {
  QTableWidgetItem* item= new QTableWidgetItem (value);
  if (!editable) item->setFlags (item->flags () & ~Qt::ItemIsEditable);
  return item;
}

QString
creator_summary (const MaterialRecord& material) {
  QStringList names;
  for (const MaterialCreator& creator: material.creators) {
    QString name= qstr (creator.literal);
    if (name.isEmpty ()) {
      name= qstr (creator.family);
      if (!creator.given.empty ()) {
        if (!name.isEmpty ()) name += ", ";
        name += qstr (creator.given);
      }
    }
    if (!name.isEmpty ()) names << name;
  }
  return names.join ("; ");
}

QString
identifier_summary (const std::vector<MaterialIdentifier>& identifiers) {
  QStringList values;
  for (const MaterialIdentifier& identifier: identifiers)
    values << qstr (identifier.scheme + ": " + identifier.value);
  return values.join ("\n");
}

MaterialProviderOptions
provider_options () {
  MaterialProviderOptions options;
  options.crossref= preference_on ("materials provider crossref");
  options.openalex= preference_on ("materials provider openalex");
  options.open_library= preference_on ("materials provider open library");
  options.google_books= preference_on ("materials provider google books");
  options.arxiv= preference_on ("materials provider arxiv");
  options.pubmed= preference_on ("materials provider pubmed");
  options.contact_email= stdstr (
    preference ("materials provider contact email"));
  return options;
}

MaterialRecognitionOptions
recognition_options () {
  MaterialRecognitionOptions options;
  options.metadata_extractor= stdstr (
    preference ("materials local metadata extractor", "exiftool"));
  options.pdf_text_extractor= stdstr (
    preference ("materials local text extractor", "pdftotext"));
  options.providers= provider_options ();
  return options;
}

QStringList
dropped_files (const QMimeData* data) {
  QStringList files;
  if (data == nullptr || !data->hasUrls ()) return files;
  for (const QUrl& url: data->urls ())
    if (url.isLocalFile () && QFileInfo (url.toLocalFile ()).isFile ())
      files << url.toLocalFile ();
  files.removeDuplicates ();
  return files;
}

std::string
container_field (const MaterialSchema& schema, const std::string& item_type) {
  const MaterialSchemaItemType* type= schema.item_type (item_type);
  auto supports= [&] (const std::string& name) {
    return type != nullptr && std::any_of (
      type->fields.begin (), type->fields.end (),
      [&] (const MaterialSchemaField& field) { return field.name == name; });
  };
  if ((item_type == "bookSection" || item_type == "encyclopediaArticle" ||
       item_type == "dictionaryEntry") && supports ("bookTitle"))
    return "bookTitle";
  if (item_type == "conferencePaper" && supports ("proceedingsTitle"))
    return "proceedingsTitle";
  if (supports ("publicationTitle")) return "publicationTitle";
  if (supports ("bookTitle")) return "bookTitle";
  if (supports ("proceedingsTitle")) return "proceedingsTitle";
  return "publicationTitle";
}

} // namespace

QTMMaterialsManager::QTMMaterialsManager (QWidget* parent): QWidget (parent) {
  setAcceptDrops (true);
  QVBoxLayout* outer= new QVBoxLayout (this);

  std::string schema_error;
  if (!schema.load_bundled (schema_error))
    QMessageBox::critical (
      this, "Materials schema",
      "ATHENA could not load its pinned Zotero schema. Material editing will "
      "be unavailable.\n\n" + qstr (schema_error));

  QHBoxLayout* commands= new QHBoxLayout;
  QPushButton* addEmpty= new QPushButton ("New", this);
  QPushButton* addFiles= new QPushButton ("Add files...", this);
  QPushButton* addDirectory= new QPushButton ("Add directory...", this);
  QPushButton* importBib= new QPushButton ("Import BibTeX...", this);
  QPushButton* importZoteroButton= new QPushButton ("Import Zotero...", this);
  QPushButton* refreshButton= new QPushButton ("Refresh", this);
  deleteButton= new QPushButton ("Delete", this);
  reidentifyButton= new QPushButton ("Re-identify", this);
  commands->addWidget (addEmpty);
  commands->addWidget (addFiles);
  commands->addWidget (addDirectory);
  commands->addWidget (importBib);
  commands->addWidget (importZoteroButton);
  commands->addWidget (reidentifyButton);
  commands->addWidget (deleteButton);
  commands->addStretch (1);
  commands->addWidget (refreshButton);
  outer->addLayout (commands);

  landingPad= new QLabel (
    "Drop books, papers, slides, reports, or other source files here", this);
  landingPad->setAlignment (Qt::AlignCenter);
  landingPad->setWordWrap (true);
  landingPad->setMinimumWidth (0);
  landingPad->setMinimumHeight (
    landingPad->fontMetrics ().lineSpacing () * 2 + 28);
  landingPad->setStyleSheet (
    "QLabel { border: 1px dashed palette(mid); padding: 12px; }" );
  outer->addWidget (landingPad);

  QSplitter* splitter= new QSplitter (Qt::Horizontal, this);
  QWidget* browser= new QWidget (splitter);
  QVBoxLayout* browserLayout= new QVBoxLayout (browser);
  browserLayout->setContentsMargins (0, 0, 0, 0);
  searchEdit= new QLineEdit (browser);
  searchEdit->setPlaceholderText ("Search title, creator, identifier, or tag");
  browserLayout->addWidget (searchEdit);
  materialTable= new QTableWidget (0, 4, browser);
  materialTable->setHorizontalHeaderLabels ({"Type", "Creator", "Title", "Date"});
  materialTable->setSelectionBehavior (QAbstractItemView::SelectRows);
  materialTable->setSelectionMode (QAbstractItemView::ExtendedSelection);
  materialTable->setEditTriggers (QAbstractItemView::NoEditTriggers);
  materialTable->verticalHeader ()->hide ();
  materialTable->horizontalHeader ()->setSectionResizeMode (
    0, QHeaderView::ResizeToContents);
  materialTable->horizontalHeader ()->setSectionResizeMode (
    1, QHeaderView::ResizeToContents);
  materialTable->horizontalHeader ()->setSectionResizeMode (
    2, QHeaderView::Stretch);
  materialTable->horizontalHeader ()->setSectionResizeMode (
    3, QHeaderView::ResizeToContents);
  browserLayout->addWidget (materialTable, 1);

  QWidget* editor= new QWidget (splitter);
  QVBoxLayout* editorLayout= new QVBoxLayout (editor);
  editorLayout->setContentsMargins (0, 0, 0, 0);
  stateLabel= new QLabel (editor);
  stateLabel->setWordWrap (true);
  editorLayout->addWidget (stateLabel);
  QTabWidget* tabs= new QTabWidget (editor);

  QWidget* metadataPage= new QWidget (tabs);
  QFormLayout* metadataForm= new QFormLayout (metadataPage);
  typeEdit= new QComboBox (metadataPage);
  typeEdit->setEditable (false);
  populateTypeCombo (typeEdit, "document");
  titleEdit= new QLineEdit (metadataPage);
  dateEdit= new QLineEdit (metadataPage);
  containerEdit= new QLineEdit (metadataPage);
  publisherEdit= new QLineEdit (metadataPage);
  tagsEdit= new QLineEdit (metadataPage);
  metadataForm->addRow ("Type:", typeEdit);
  metadataForm->addRow ("Title:", titleEdit);
  metadataForm->addRow ("Date:", dateEdit);
  metadataForm->addRow ("Container:", containerEdit);
  metadataForm->addRow ("Publisher:", publisherEdit);
  metadataForm->addRow ("Tags:", tagsEdit);
  tabs->addTab (metadataPage, "Metadata");

  creatorTable= new QTableWidget (0, 4, tabs);
  creatorTable->setHorizontalHeaderLabels ({"Role", "Given", "Family", "Literal"});
  creatorTable->horizontalHeader ()->setSectionResizeMode (QHeaderView::Stretch);
  tabs->addTab (creatorTable, "Creators");

  identifierTable= new QTableWidget (0, 2, tabs);
  identifierTable->setHorizontalHeaderLabels ({"Scheme", "Value"});
  identifierTable->horizontalHeader ()->setSectionResizeMode (1, QHeaderView::Stretch);
  tabs->addTab (identifierTable, "Identifiers");

  fieldTable= new QTableWidget (0, 3, tabs);
  fieldTable->setHorizontalHeaderLabels ({"Field", "Value", "Language"});
  fieldTable->horizontalHeader ()->setSectionResizeMode (1, QHeaderView::Stretch);
  tabs->addTab (fieldTable, "Other fields");

  attachmentTable= new QTableWidget (0, 4, tabs);
  attachmentTable->setHorizontalHeaderLabels ({"Role", "File", "Type", "Size"});
  attachmentTable->setEditTriggers (QAbstractItemView::NoEditTriggers);
  attachmentTable->setSelectionBehavior (QAbstractItemView::SelectRows);
  attachmentTable->horizontalHeader ()->setSectionResizeMode (1, QHeaderView::Stretch);
  tabs->addTab (attachmentTable, "Attachments");
  editorLayout->addWidget (tabs, 1);

  QHBoxLayout* editorButtons= new QHBoxLayout;
  QPushButton* addCreator= new QPushButton ("Add creator", editor);
  QPushButton* addIdentifier= new QPushButton ("Add identifier", editor);
  QPushButton* addField= new QPushButton ("Add field", editor);
  saveButton= new QPushButton ("Save", editor);
  editorButtons->addWidget (addCreator);
  editorButtons->addWidget (addIdentifier);
  editorButtons->addWidget (addField);
  editorButtons->addStretch (1);
  editorButtons->addWidget (saveButton);
  editorLayout->addLayout (editorButtons);

  splitter->addWidget (browser);
  splitter->addWidget (editor);
  splitter->setStretchFactor (0, 2);
  splitter->setStretchFactor (1, 3);
  outer->addWidget (splitter, 1);

  connect (searchEdit, &QLineEdit::textChanged,
           this, [this] { rebuildList (); });
  connect (materialTable, &QTableWidget::itemSelectionChanged,
           this, [this] { loadSelection (); });
  connect (addEmpty, &QPushButton::clicked, this,
           [this] { createEmpty (); });
  connect (addFiles, &QPushButton::clicked, this,
           [this] { chooseFiles (); });
  connect (addDirectory, &QPushButton::clicked, this,
           [this] { chooseDirectory (); });
  connect (importBib, &QPushButton::clicked, this,
           [this] { importBibtex (); });
  connect (importZoteroButton, &QPushButton::clicked, this,
           [this] { importZotero (); });
  connect (reidentifyButton, &QPushButton::clicked, this,
           [this] { reidentifySelected (); });
  connect (deleteButton, &QPushButton::clicked, this,
           [this] { removeSelected (); });
  connect (refreshButton, &QPushButton::clicked, this,
           [this] { refresh (); });
  connect (saveButton, &QPushButton::clicked, this,
           [this] { saveEditor (); });
  connect (addCreator, &QPushButton::clicked, this, [this] {
    addCreatorRow ();
  });
  connect (addIdentifier, &QPushButton::clicked, this, [this] {
    identifierTable->insertRow (identifierTable->rowCount ());
  });
  connect (addField, &QPushButton::clicked, this, [this] {
    addFieldRow ();
  });
  connect (attachmentTable, &QTableWidget::cellDoubleClicked,
           this, [this] (int row, int) { openAttachment (row); });
  refresh ();
}

void
QTMMaterialsManager::populateTypeCombo (QComboBox* combo,
                                        const std::string& selected) {
  combo->clear ();
  int selected_index= -1;
  for (const MaterialSchemaItemType& type: schema.item_types ()) {
    combo->addItem (qstr (type.label), qstr (type.name));
    if (type.name == selected) selected_index= combo->count () - 1;
  }
  if (selected_index < 0 && !selected.empty ()) {
    combo->addItem (qstr (selected), qstr (selected));
    selected_index= combo->count () - 1;
  }
  if (selected_index >= 0) combo->setCurrentIndex (selected_index);
}

void
QTMMaterialsManager::addCreatorRow (const MaterialCreator* creator) {
  int row= creatorTable->rowCount ();
  creatorTable->insertRow (row);
  QComboBox* role= new QComboBox (creatorTable);
  const MaterialSchemaItemType* type= schema.item_type (
    stdstr (typeEdit->currentData ().toString ()));
  std::string selected= creator == nullptr ? "author" : creator->role;
  int selected_index= -1;
  if (type != nullptr)
    for (const MaterialSchemaCreatorType& creator_type: type->creator_types) {
      role->addItem (qstr (creator_type.label), qstr (creator_type.name));
      if (creator_type.name == selected) selected_index= role->count () - 1;
    }
  if (selected_index < 0) {
    role->addItem (qstr (schema.creator_type_label (selected)), qstr (selected));
    selected_index= role->count () - 1;
  }
  role->setCurrentIndex (selected_index);
  creatorTable->setCellWidget (row, 0, role);
  QStringList values= creator == nullptr
    ? QStringList ({"", "", ""})
    : QStringList ({qstr (creator->given), qstr (creator->family),
                    qstr (creator->literal)});
  for (int column=1; column<4; ++column)
    creatorTable->setItem (row, column, cell (values[column - 1]));
}

void
QTMMaterialsManager::addFieldRow (const MaterialField* field) {
  int row= fieldTable->rowCount ();
  fieldTable->insertRow (row);
  QComboBox* name= new QComboBox (fieldTable);
  name->setEditable (false);
  const MaterialSchemaItemType* type= schema.item_type (
    stdstr (typeEdit->currentData ().toString ()));
  std::string selected= field == nullptr ? std::string () : field->name;
  int selected_index= -1;
  const std::set<std::string> primary= {
    "title", "date", "publisher", "publicationTitle", "bookTitle",
    "proceedingsTitle"};
  if (type != nullptr)
    for (const MaterialSchemaField& schema_field: type->fields) {
      if (primary.count (schema_field.name)) continue;
      name->addItem (qstr (schema_field.label), qstr (schema_field.name));
      if (schema_field.name == selected) selected_index= name->count () - 1;
    }
  if (!selected.empty () && selected_index < 0) {
    name->addItem (qstr (schema.field_label (selected)), qstr (selected));
    selected_index= name->count () - 1;
  }
  if (selected_index >= 0) name->setCurrentIndex (selected_index);
  fieldTable->setCellWidget (row, 0, name);
  fieldTable->setItem (row, 1, cell (field == nullptr ? QString ()
                                                      : qstr (field->value)));
  fieldTable->setItem (row, 2, cell (field == nullptr ? QString ()
                                                      : qstr (field->language)));
}

std::string
QTMMaterialsManager::creatorRoleAt (int row) const {
  QComboBox* role= qobject_cast<QComboBox*> (creatorTable->cellWidget (row, 0));
  return role == nullptr ? "author"
                         : stdstr (role->currentData ().toString ());
}

std::string
QTMMaterialsManager::fieldNameAt (int row) const {
  QComboBox* name= qobject_cast<QComboBox*> (fieldTable->cellWidget (row, 0));
  return name == nullptr ? std::string ()
                         : stdstr (name->currentData ().toString ());
}

void
QTMMaterialsManager::refresh () {
  records.clear ();
  MaterialsStore* store= vault_get_materials_store ();
  if (store != nullptr) {
    std::string error;
    records= store->list (100000, 0, error);
    if (!error.empty ()) QMessageBox::warning (this, "Materials", qstr (error));
  }
  rebuildList ();
  if (materialTable->rowCount () == 0) clearEditor ();
}

void
QTMMaterialsManager::rebuildList () {
  QString selected= selectedUuid ();
  QString query= searchEdit->text ().trimmed ();
  std::vector<MaterialSearchHit> shown= records;
  if (!query.isEmpty ()) {
    MaterialsStore* store= vault_get_materials_store ();
    if (store != nullptr) {
      std::string error;
      shown= store->search (stdstr (query), 100000, error);
      if (!error.empty ()) stateLabel->setText (qstr (error));
    }
  }
  materialTable->setRowCount (0);
  for (const MaterialSearchHit& hit: shown) {
    int row= materialTable->rowCount ();
    materialTable->insertRow (row);
    QStringList values= {qstr (hit.item_type), qstr (hit.creators),
                         qstr (hit.title), qstr (hit.issued)};
    for (int column=0; column<values.size (); ++column) {
      QTableWidgetItem* item= cell (values[column], false);
      item->setData (Qt::UserRole, qstr (hit.uuid));
      materialTable->setItem (row, column, item);
    }
  }
  selectUuid (selected);
}

QString
QTMMaterialsManager::selectedUuid () const {
  QStringList selected= selectedUuids ();
  return selected.size () == 1 ? selected.front () : QString ();
}

QStringList
QTMMaterialsManager::selectedUuids () const {
  QStringList result;
  const QModelIndexList rows= materialTable->selectionModel ()->selectedRows (0);
  for (const QModelIndex& index: rows) {
    QTableWidgetItem* item= materialTable->item (index.row (), 0);
    if (item != nullptr) result << item->data (Qt::UserRole).toString ();
  }
  return result;
}

void
QTMMaterialsManager::selectUuid (const QString& uuid) {
  if (uuid.isEmpty ()) return;
  for (int row=0; row<materialTable->rowCount (); ++row)
    if (materialTable->item (row, 0)->data (Qt::UserRole).toString () == uuid) {
      materialTable->selectRow (row);
      materialTable->scrollToItem (materialTable->item (row, 0));
      return;
    }
}

void
QTMMaterialsManager::clearEditor () {
  loaded= MaterialRecord {};
  populateTypeCombo (typeEdit, "document");
  titleEdit->clear ();
  dateEdit->clear ();
  containerEdit->clear ();
  publisherEdit->clear ();
  tagsEdit->clear ();
  creatorTable->setRowCount (0);
  identifierTable->setRowCount (0);
  fieldTable->setRowCount (0);
  attachmentTable->setRowCount (0);
  stateLabel->setText ("Select a Material or drop a file to begin.");
  saveButton->setEnabled (false);
  deleteButton->setEnabled (false);
  reidentifyButton->setEnabled (false);
}

void
QTMMaterialsManager::loadSelection () {
  QStringList selected= selectedUuids ();
  if (selected.size () > 1) {
    clearEditor ();
    stateLabel->setText (
      QString ("%1 Materials selected. Editing is available for a single "
               "Material; Delete applies to the complete selection.")
        .arg (selected.size ()));
    deleteButton->setEnabled (true);
    reidentifyButton->setEnabled (false);
    return;
  }
  QString uuid= selected.isEmpty () ? QString () : selected.front ();
  if (uuid.isEmpty ()) { clearEditor (); return; }
  MaterialsStore* store= vault_get_materials_store ();
  if (store == nullptr) { clearEditor (); return; }
  std::string error;
  std::optional<MaterialRecord> material= store->get (stdstr (uuid), error);
  if (!material) {
    QMessageBox::warning (this, "Materials", qstr (error));
    clearEditor ();
    return;
  }
  loaded= *material;
  populateTypeCombo (typeEdit, loaded.item_type);
  titleEdit->setText (qstr (loaded.field ("title")));
  dateEdit->setText (qstr (loaded.field ("date")));
  std::string container_name= container_field (schema, loaded.item_type);
  std::string container= loaded.field (container_name);
  if (container.empty ()) container= loaded.field ("containerTitle");
  containerEdit->setText (qstr (container));
  publisherEdit->setText (qstr (loaded.field ("publisher")));
  QStringList tags;
  for (const std::string& tag: loaded.tags) tags << qstr (tag);
  tagsEdit->setText (tags.join (", "));

  creatorTable->setRowCount (0);
  for (const MaterialCreator& creator: loaded.creators)
    addCreatorRow (&creator);

  identifierTable->setRowCount (0);
  for (const MaterialIdentifier& identifier: loaded.identifiers) {
    int row= identifierTable->rowCount ();
    identifierTable->insertRow (row);
    identifierTable->setItem (row, 0, cell (qstr (identifier.scheme)));
    identifierTable->setItem (row, 1, cell (qstr (identifier.value)));
  }

  const std::set<std::string> primary_fields= {
    "title", "date", "containerTitle", "publicationTitle", "bookTitle",
    "proceedingsTitle", "publisher"};
  fieldTable->setRowCount (0);
  for (const MaterialField& field: loaded.fields) {
    if (primary_fields.count (field.name)) continue;
    addFieldRow (&field);
  }

  attachmentTable->setRowCount (0);
  std::vector<MaterialAttachment> attachments= store->attachments (
    loaded.uuid, error);
  bool has_primary_attachment= false;
  for (const MaterialAttachment& attachment: attachments) {
    has_primary_attachment|= attachment.primary;
    int row= attachmentTable->rowCount ();
    attachmentTable->insertRow (row);
    QStringList values= {
      qstr (attachment.role),
      qstr (attachment.canonical_name) + (attachment.primary ? " [primary]" : ""),
      qstr (attachment.mime_type),
      QString::number ((double) attachment.byte_size / 1024.0, 'f', 1) + " KiB"};
    for (int column=0; column<4; ++column) {
      QTableWidgetItem* item= cell (values[column], false);
      item->setData (Qt::UserRole, qstr (attachment.stored_path));
      attachmentTable->setItem (row, column, item);
    }
  }
  stateLabel->setText (
    QString ("UUID: %1    State: %2    Revision: %3")
      .arg (qstr (loaded.uuid), qstr (loaded.review_state))
      .arg ((qlonglong) loaded.revision));
  saveButton->setEnabled (true);
  deleteButton->setEnabled (true);
  reidentifyButton->setEnabled (has_primary_attachment);
}

bool
QTMMaterialsManager::saveEditor () {
  if (loaded.uuid.empty ()) return false;
  MaterialsStore* store= store_or_warn (this);
  if (store == nullptr) return false;
  MaterialRecord next= loaded;
  next.item_type= stdstr (typeEdit->currentData ().toString ());
  if (next.item_type.empty ()) next.item_type= "document";
  next.fields.clear ();
  auto add= [&] (const std::string& name, const QString& value,
                 const QString& language= QString ()) {
    QString trimmed= value.trimmed ();
    if (!trimmed.isEmpty ())
      next.fields.push_back ({name, stdstr (trimmed), stdstr (language), 0});
  };
  add ("title", titleEdit->text ());
  add ("date", dateEdit->text ());
  add (container_field (schema, next.item_type), containerEdit->text ());
  add ("publisher", publisherEdit->text ());
  for (int row=0; row<fieldTable->rowCount (); ++row) {
    QTableWidgetItem* value= fieldTable->item (row, 1);
    QTableWidgetItem* language= fieldTable->item (row, 2);
    std::string name= fieldNameAt (row);
    if (!name.empty () && value != nullptr)
      add (name, value->text (),
           language == nullptr ? QString () : language->text ());
  }
  next.creators.clear ();
  for (int row=0; row<creatorTable->rowCount (); ++row) {
    auto value= [&] (int column) {
      QTableWidgetItem* item= creatorTable->item (row, column);
      return item == nullptr ? std::string () : stdstr (item->text ().trimmed ());
    };
    MaterialCreator creator {creatorRoleAt (row), value (1), value (2), value (3), "",
                             (int) next.creators.size ()};
    if (creator.role.empty ()) creator.role= "author";
    if (!creator.given.empty () || !creator.family.empty () ||
        !creator.literal.empty ()) next.creators.push_back (creator);
  }
  next.identifiers.clear ();
  for (int row=0; row<identifierTable->rowCount (); ++row) {
    QTableWidgetItem* scheme= identifierTable->item (row, 0);
    QTableWidgetItem* value= identifierTable->item (row, 1);
    if (scheme != nullptr && value != nullptr &&
        !scheme->text ().trimmed ().isEmpty () &&
        !value->text ().trimmed ().isEmpty ())
      next.identifiers.push_back ({stdstr (scheme->text ().trimmed ()),
                                   stdstr (value->text ().trimmed ()), ""});
  }
  next.tags.clear ();
  for (const QString& tag: tagsEdit->text ().split (
         QRegularExpression ("[,;]"), Qt::SkipEmptyParts))
    next.tags.push_back (stdstr (tag.trimmed ()));
  next.review_state= next.field ("title").empty () ? "needs_review" : "ready";
  next.provenance.push_back (
    {"*", "manual", "Materials Manager", "edited", 1.0});

  std::string error;
  if (!store->update (next, loaded.revision, error)) {
    QMessageBox::warning (this, "Save Material", qstr (error));
    return false;
  }
  loaded= next;
  refresh ();
  selectUuid (qstr (next.uuid));
  return true;
}

void
QTMMaterialsManager::createEmpty () {
  MaterialsStore* store= store_or_warn (this);
  if (store == nullptr) return;
  bool ok= false;
  QString title= QInputDialog::getText (
    this, "New Material", "Title:", QLineEdit::Normal, QString (), &ok);
  if (!ok) return;
  MaterialRecord material;
  material.review_state= title.trimmed ().isEmpty () ? "needs_review" : "ready";
  if (!title.trimmed ().isEmpty ())
    material.fields.push_back ({"title", stdstr (title.trimmed ()), "", 0});
  material.provenance.push_back (
    {"*", "manual", "Materials Manager", "created", 1.0});
  std::string error;
  if (!store->create (material, error)) {
    QMessageBox::warning (this, "New Material", qstr (error));
    return;
  }
  refresh ();
  selectUuid (qstr (material.uuid));
}

void
QTMMaterialsManager::removeSelected () {
  QStringList uuids= selectedUuids ();
  if (uuids.isEmpty ()) return;
  QString prompt;
  if (uuids.size () == 1)
    prompt= "Delete this Material and all managed attachment copies? Existing "
            "citations to its UUID will become unresolved.";
  else
    prompt= QString (
      "Delete all %1 selected Materials and their managed attachment copies? "
      "Existing citations to their UUIDs will become unresolved.")
        .arg (uuids.size ());
  if (QMessageBox::question (
        this, uuids.size () == 1 ? "Delete Material" : "Delete Materials",
        prompt) != QMessageBox::Yes)
    return;
  MaterialsStore* store= store_or_warn (this);
  if (store == nullptr) return;
  int removed= 0;
  QStringList failures;
  for (const QString& uuid: uuids) {
    std::string error;
    if (store->remove (stdstr (uuid), true, error)) removed++;
    else failures << QString ("%1: %2").arg (uuid, qstr (error));
  }
  refresh ();
  if (!failures.isEmpty ())
    QMessageBox::warning (
      this, "Delete Materials",
      QString ("Deleted %1 of %2 selected Materials.\n\n%3")
        .arg (removed).arg (uuids.size ()).arg (failures.join ('\n')));
}

void
QTMMaterialsManager::reidentifySelected () {
  QString uuid= selectedUuid ();
  MaterialsStore* store= store_or_warn (this);
  if (uuid.isEmpty () || store == nullptr) return;

  std::string error;
  std::optional<MaterialRecord> current= store->get (stdstr (uuid), error);
  if (!current) {
    QMessageBox::warning (this, "Re-identify Material", qstr (error));
    return;
  }
  std::optional<MaterialAttachment> attachment=
    store->primary_attachment (current->uuid, error);
  if (!attachment) {
    QMessageBox::warning (
      this, "Re-identify Material",
      error.empty () ? "This Material has no primary attachment."
                     : qstr (error));
    return;
  }
  fs::path source= store->vault_root () / fs::u8path (attachment->stored_path);
  QString source_path= qstr (source.u8string ());

  MaterialRecognitionResult recognition;
  MaterialImportProgress progress ("Re-identifying Material", 0, this);
  MaterialRecognitionOptions options= recognition_options ();
  options.progress= [&] (const std::string& stage) {
    progress.setProgressText (
      QDir::toNativeSeparators (source_path) + "\n" + qstr (stage));
    QApplication::processEvents (QEventLoop::AllEvents, 25);
  };
  options.cancelled= [&] { return progress.wasCanceled (); };
  options.progress ("Starting recognition");
  bool recognized= athena_material_recognize_file (
    source, options, recognition, error);
  bool cancelled= progress.wasCanceled () ||
                  error == "Material recognition cancelled";
  progress.close ();
  if (cancelled) return;
  if (!recognized) {
    QMessageBox::warning (this, "Re-identify Material", qstr (error));
    return;
  }
  if (!reviewRecognition (source_path, recognition, "Update Material")) return;

  MaterialRecord replacement= recognition.material;
  replacement.uuid= current->uuid;
  replacement.revision= current->revision;
  replacement.created_at= current->created_at;
  replacement.updated_at= current->updated_at;
  replacement.extra_json= current->extra_json;
  replacement.tags= current->tags;
  replacement.identifiers= recognition.identifiers;
  replacement.provenance.insert (
    replacement.provenance.begin (), current->provenance.begin (),
    current->provenance.end ());
  replacement.provenance.push_back (
    {"*", "re-identification", attachment->canonical_name,
     "accepted", recognition.confidence});
  if (!store->update (replacement, current->revision, error)) {
    QMessageBox::warning (this, "Re-identify Material", qstr (error));
    return;
  }
  refresh ();
  selectUuid (uuid);
}

void
QTMMaterialsManager::chooseFiles () {
  QStringList files= QFileDialog::getOpenFileNames (
    this, "Add Materials", QDir::homePath (), "All files (*)");
  if (!files.isEmpty ()) importFiles (files, files.size () == 1);
}

void
QTMMaterialsManager::chooseDirectory () {
  QDialog dialog (this);
  dialog.setWindowTitle ("Add Materials from Directory");
  dialog.setMinimumWidth (560);
  QVBoxLayout* layout= new QVBoxLayout (&dialog);

  QLabel* explanation= new QLabel (
    "Choose a directory whose files should be processed by the Materials "
    "recognition pipeline.", &dialog);
  explanation->setWordWrap (true);
  layout->addWidget (explanation);

  QHBoxLayout* directory_row= new QHBoxLayout;
  QLineEdit* directory_edit= new QLineEdit (&dialog);
  directory_edit->setReadOnly (true);
  directory_edit->setPlaceholderText ("No directory selected");
  QPushButton* browse= new QPushButton ("Browse...", &dialog);
  directory_row->addWidget (directory_edit, 1);
  directory_row->addWidget (browse);
  layout->addLayout (directory_row);

  QCheckBox* recursive= new QCheckBox ("Include subdirectories", &dialog);
  layout->addWidget (recursive);

  QDialogButtonBox* buttons= new QDialogButtonBox (
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  buttons->button (QDialogButtonBox::Ok)->setText ("Add directory");
  buttons->button (QDialogButtonBox::Ok)->setEnabled (false);
  layout->addWidget (buttons);

  connect (browse, &QPushButton::clicked, &dialog, [&] {
    QString initial= directory_edit->text ().isEmpty ()
      ? QDir::homePath () : directory_edit->text ();
    QString directory= QFileDialog::getExistingDirectory (
      &dialog, "Choose Materials Directory", initial,
      QFileDialog::ShowDirsOnly);
    if (directory.isEmpty ()) return;
    directory_edit->setText (QDir::cleanPath (directory));
    buttons->button (QDialogButtonBox::Ok)->setEnabled (true);
  });
  connect (buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec () != QDialog::Accepted) return;

  QStringList files;
  QDirIterator::IteratorFlags flags= recursive->isChecked ()
    ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags;
  QDirIterator iterator (
    directory_edit->text (), QDir::Files | QDir::Readable, flags);
  while (iterator.hasNext ()) files << iterator.next ();
  std::sort (files.begin (), files.end (), [] (const QString& left,
                                                const QString& right) {
    return QString::compare (left, right, Qt::CaseInsensitive) < 0;
  });
  if (files.isEmpty ()) {
    QMessageBox::information (
      this, "Add Materials", "The selected directory contains no files.");
    return;
  }
  importFiles (files, false);
}

void
QTMMaterialsManager::importBibtex () {
  MaterialsStore* store= store_or_warn (this);
  if (store == nullptr) return;
  QString path= QFileDialog::getOpenFileName (
    this, "Import BibTeX or BibLaTeX", QDir::homePath (),
    "BibTeX files (*.bib);;All files (*)");
  if (path.isEmpty ()) return;
  std::vector<MaterialRecord> imported;
  std::string error;
  if (!athena_materials_import_bibtex (
        fs::u8path (stdstr (path)), imported, error)) {
    QMessageBox::warning (this, "Import BibTeX", qstr (error));
    return;
  }
  int added= 0;
  int duplicates= 0;
  QString last_uuid;
  for (MaterialRecord& record: imported) {
    std::optional<std::string> existing;
    for (const MaterialIdentifier& identifier: record.identifiers) {
      existing= store->material_for_identifier (
        identifier.scheme, identifier.value, error);
      if (!error.empty () || existing) break;
    }
    if (!error.empty ()) {
      QMessageBox::warning (this, "Import BibTeX", qstr (error));
      return;
    }
    if (existing) {
      duplicates++;
      last_uuid= qstr (*existing);
      continue;
    }
    if (!store->create (record, error)) {
      QMessageBox::warning (this, "Import BibTeX", qstr (error));
      return;
    }
    added++;
    last_uuid= qstr (record.uuid);
  }
  refresh ();
  selectUuid (last_uuid);
  QMessageBox::information (
    this, "Import BibTeX",
    QString ("Imported %1 Material(s). %2 strong-identifier duplicate(s) "
             "were left unchanged.").arg (added).arg (duplicates));
}

void
QTMMaterialsManager::importZotero () {
  MaterialsStore* store= store_or_warn (this);
  if (store == nullptr) return;
  QTMZoteroImportResult result;
  if (qtm_import_zotero_library (this, *store, result)) refresh ();
}

bool
QTMMaterialsManager::reviewRecognition (
  const QString& path, MaterialRecognitionResult& recognition,
  const QString& accept_text) {
  QDialog dialog (this);
  dialog.setWindowTitle ("Review recognized Material");
  dialog.resize (760, 700);
  QVBoxLayout* outer= new QVBoxLayout (&dialog);
  QLabel* source= new QLabel ("Source: " + QDir::toNativeSeparators (path), &dialog);
  source->setWordWrap (true);
  outer->addWidget (source);

  QFormLayout* type_form= new QFormLayout;
  QComboBox* type= new QComboBox (&dialog);
  populateTypeCombo (type, recognition.material.item_type);
  type_form->addRow ("Type:", type);
  outer->addLayout (type_form);

  std::map<std::string, QString> field_values;
  std::map<std::string, MaterialField> original_fields;
  for (const MaterialField& field: recognition.material.fields) {
    field_values[field.name]= qstr (field.value);
    original_fields[field.name]= field;
  }
  std::map<std::string, QLineEdit*> field_edits;
  QScrollArea* field_area= new QScrollArea (&dialog);
  field_area->setWidgetResizable (true);
  field_area->setFrameShape (QFrame::NoFrame);
  field_area->setMinimumHeight (300);
  outer->addWidget (field_area, 1);

  QLineEdit* creators= new QLineEdit (creator_summary (recognition.material), &dialog);
  const QString original_creators= creators->text ();
  QLabel* creator_label= new QLabel ("Creators:", &dialog);
  QFormLayout* creator_form= new QFormLayout;
  creator_form->addRow (creator_label, creators);
  outer->addLayout (creator_form);

  auto capture_fields= [&] {
    for (const auto& [name, edit]: field_edits)
      field_values[name]= edit->text ();
  };
  auto rebuild_fields= [&] {
    capture_fields ();
    field_edits.clear ();
    if (QWidget* old= field_area->takeWidget ()) delete old;
    QWidget* page= new QWidget (field_area);
    QFormLayout* form= new QFormLayout (page);
    std::string item_type= stdstr (type->currentData ().toString ());
    const MaterialSchemaItemType* item= schema.item_type (item_type);
    if (item != nullptr) {
      const MaterialSchemaCreatorType* primary_creator= nullptr;
      for (const MaterialSchemaCreatorType& creator: item->creator_types)
        if (creator.primary) { primary_creator= &creator; break; }
      creator_label->setText (
        primary_creator == nullptr
          ? "Creators:"
          : qstr (primary_creator->label + "s:"));
      for (const MaterialSchemaField& field: item->fields) {
        QLineEdit* edit= new QLineEdit (field_values[field.name], page);
        QString label= qstr (field.label) + ":";
        if (item_type == "journalArticle") {
          if (field.name == "publicationTitle") label= "Journal:";
          else if (field.name == "date") label= "Year:";
          else if (field.name == "volume") label= "Volume:";
          else if (field.name == "issue") label= "Issue:";
          else if (field.name == "pages") label= "Pages:";
        }
        form->addRow (label, edit);
        field_edits[field.name]= edit;
      }
    }
    else {
      creator_label->setText ("Creators:");
      for (const auto& [name, value]: field_values) {
        QLineEdit* edit= new QLineEdit (value, page);
        form->addRow (qstr (schema.field_label (name)) + ":", edit);
        field_edits[name]= edit;
      }
    }
    field_area->setWidget (page);
  };
  connect (type, &QComboBox::currentIndexChanged, &dialog,
           [&] (int) { rebuild_fields (); });
  rebuild_fields ();

  QLabel* identifiers= new QLabel (
    identifier_summary (recognition.identifiers), &dialog);
  identifiers->setTextInteractionFlags (Qt::TextSelectableByMouse);
  identifiers->setWordWrap (true);
  outer->addWidget (new QLabel ("Identifiers:", &dialog));
  outer->addWidget (identifiers);
  QTextEdit* diagnostics= new QTextEdit (&dialog);
  diagnostics->setReadOnly (true);
  diagnostics->setMaximumHeight (150);
  QStringList lines;
  lines << QString ("Confidence: %1%").arg (
    recognition.confidence * 100.0, 0, 'f', 0);
  for (const std::string& line: recognition.diagnostics) lines << qstr (line);
  diagnostics->setPlainText (lines.join ("\n"));
  outer->addWidget (diagnostics, 1);
  QDialogButtonBox* buttons= new QDialogButtonBox (
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  buttons->button (QDialogButtonBox::Ok)->setText (accept_text);
  outer->addWidget (buttons);
  connect (buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec () != QDialog::Accepted) return false;

  capture_fields ();
  recognition.material.item_type= stdstr (type->currentData ().toString ());
  if (recognition.material.item_type.empty ())
    recognition.material.item_type= "document";

  std::set<std::string> schema_fields;
  for (const MaterialSchemaItemType& item: schema.item_types ())
    for (const MaterialSchemaField& field: item.fields)
      schema_fields.insert (field.name);
  std::vector<MaterialField> accepted_fields;
  for (const MaterialField& field: recognition.material.fields)
    if (!schema_fields.count (field.name)) accepted_fields.push_back (field);
  const MaterialSchemaItemType* accepted_type=
    schema.item_type (recognition.material.item_type);
  if (accepted_type != nullptr)
    for (const MaterialSchemaField& field: accepted_type->fields) {
      QString value= field_values[field.name].trimmed ();
      if (value.isEmpty ()) continue;
      MaterialField accepted {field.name, stdstr (value), "", 0};
      auto old= original_fields.find (field.name);
      if (old != original_fields.end ()) {
        accepted.language= old->second.language;
        accepted.ordinal= old->second.ordinal;
      }
      accepted_fields.push_back (accepted);
    }
  else {
    accepted_fields.clear ();
    for (const auto& [name, value]: field_values) {
      QString trimmed= value.trimmed ();
      if (trimmed.isEmpty ()) continue;
      MaterialField accepted {name, stdstr (trimmed), "", 0};
      auto old= original_fields.find (name);
      if (old != original_fields.end ()) {
        accepted.language= old->second.language;
        accepted.ordinal= old->second.ordinal;
      }
      accepted_fields.push_back (accepted);
    }
  }
  recognition.material.fields= std::move (accepted_fields);

  if (creators->text ().trimmed () != original_creators.trimmed ()) {
    std::string creator_role= "author";
    if (accepted_type != nullptr) {
      auto primary= std::find_if (
        accepted_type->creator_types.begin (), accepted_type->creator_types.end (),
        [] (const MaterialSchemaCreatorType& creator) {
          return creator.primary;
        });
      if (primary != accepted_type->creator_types.end ())
        creator_role= primary->name;
    }
    recognition.material.creators.clear ();
    for (const QString& name:
         creators->text ().split (';', Qt::SkipEmptyParts))
      recognition.material.creators.push_back (
        {creator_role, "", "", stdstr (name.trimmed ()), "",
         (int) recognition.material.creators.size ()});
  }
  QString accepted_title= field_values["title"].trimmed ();
  recognition.material.review_state=
    accepted_title.isEmpty () || accepted_title == "Untitled"
      ? "needs_review" : "ready";
  recognition.material.provenance.push_back (
    {"*", "manual-review", QFileInfo (path).fileName ().toStdString (),
     "accepted", 1.0});
  return true;
}

void
QTMMaterialsManager::importFiles (const QStringList& files,
                                  bool review_recognition) {
  MaterialsStore* store= store_or_warn (this);
  if (store == nullptr || files.isEmpty ()) return;
  QString lastUuid;
  QStringList failures;
  MaterialImportProgress progress ("Adding Materials", files.size (), this);
  progress.setValue (0);
  for (int index=0; index<files.size (); ++index) {
    progress.show ();
    progress.setValue (index);
    QString file= files[index];
    MaterialRecognitionResult recognition;
    std::string error;
    bool recognized= false;
    bool cancelled= false;
    const QString context=
      QString ("%1 of %2\n%3\n")
        .arg (index + 1).arg (files.size ())
        .arg (QDir::toNativeSeparators (file));
    MaterialRecognitionOptions options= recognition_options ();
    options.progress= [&] (const std::string& stage) {
      progress.setProgressText (context + qstr (stage));
      QApplication::processEvents (QEventLoop::AllEvents, 25);
    };
    options.cancelled= [&] { return progress.wasCanceled (); };
    options.progress ("Starting recognition");
    recognized= athena_material_recognize_file (
      fs::u8path (stdstr (file)), options, recognition, error);
    cancelled= progress.wasCanceled () ||
               error == "Material recognition cancelled";
    if (cancelled) break;
    if (!recognized) {
      if (review_recognition) {
        progress.hide ();
        QMessageBox::warning (this, "Add Material", qstr (error));
        progress.show ();
      }
      else failures << QString ("%1: %2").arg (file, qstr (error));
      progress.setValue (index + 1);
      continue;
    }
    if (review_recognition) {
      progress.hide ();
      if (!reviewRecognition (file, recognition)) {
        progress.setValue (index + 1);
        continue;
      }
      progress.show ();
    }
    else {
      recognition.material.provenance.push_back (
        {"*", "automatic-review", QFileInfo (file).fileName ().toStdString (),
         "accepted", recognition.confidence});
    }

    std::optional<std::string> existing;
    for (const MaterialIdentifier& identifier: recognition.identifiers) {
      existing= store->material_for_identifier (
        identifier.scheme, identifier.value, error);
      if (!error.empty ()) break;
      if (existing) break;
    }
    if (!error.empty ()) {
      if (review_recognition) {
        progress.hide ();
        QMessageBox::warning (this, "Add Material", qstr (error));
        progress.show ();
      }
      else failures << QString ("%1: %2").arg (file, qstr (error));
      progress.setValue (index + 1);
      continue;
    }
    if (existing) {
      bool declined= false;
      if (review_recognition) {
        std::optional<MaterialRecord> old= store->get (*existing, error);
        if (!old && !error.empty ()) {
          progress.hide ();
          QMessageBox::warning (this, "Add Material", qstr (error));
          progress.show ();
          progress.setValue (index + 1);
          continue;
        }
        QString title= old ? qstr (old->field ("title")) : qstr (*existing);
        progress.hide ();
        declined= QMessageBox::question (
          this, "Existing Material",
          QString ("The identifier already belongs to “%1”. Add this file "
                   "as an attachment to that Material?").arg (title)) !=
          QMessageBox::Yes;
        progress.show ();
      }
      if (declined) {
        progress.setValue (index + 1);
        continue;
      }
      MaterialImportResult imported;
      if (!store->import_file (*existing, fs::u8path (stdstr (file)),
                               "document", false, imported, error)) {
        if (review_recognition) {
          progress.hide ();
          QMessageBox::warning (this, "Add Material", qstr (error));
          progress.show ();
        }
        else failures << QString ("%1: %2").arg (file, qstr (error));
        progress.setValue (index + 1);
        continue;
      }
      lastUuid= qstr (*existing);
      progress.setValue (index + 1);
      continue;
    }

    MaterialRecord material= recognition.material;
    material.identifiers= recognition.identifiers;
    MaterialImportResult imported;
    if (!store->import_material_file (
          material, fs::u8path (stdstr (file)), "document", true,
          imported, error)) {
      if (review_recognition) {
        progress.hide ();
        QMessageBox::warning (this, "Add Material", qstr (error));
        progress.show ();
      }
      else failures << QString ("%1: %2").arg (file, qstr (error));
      progress.setValue (index + 1);
      continue;
    }
    lastUuid= qstr (material.uuid);
    progress.setValue (index + 1);
  }
  progress.close ();
  refresh ();
  selectUuid (lastUuid);
  if (!failures.isEmpty ()) {
    QStringList shown= failures.mid (0, 20);
    if (failures.size () > shown.size ())
      shown << QString ("...and %1 more failure(s)")
                 .arg (failures.size () - shown.size ());
    QMessageBox::warning (
      this, "Add Materials",
      QString ("%1 file(s) could not be imported:\n\n%2")
        .arg (failures.size ()).arg (shown.join ('\n')));
  }
}

void
QTMMaterialsManager::openAttachment (int row) {
  QTableWidgetItem* item= attachmentTable->item (row, 0);
  MaterialsStore* store= vault_get_materials_store ();
  if (item == nullptr || store == nullptr) return;
  QString relative= item->data (Qt::UserRole).toString ();
  fs::path path= store->vault_root () / fs::u8path (stdstr (relative));
  QDesktopServices::openUrl (QUrl::fromLocalFile (qstr (path.u8string ())));
}

void
QTMMaterialsManager::dragEnterEvent (QDragEnterEvent* event) {
  if (!dropped_files (event->mimeData ()).isEmpty ())
    event->acceptProposedAction ();
}

void
QTMMaterialsManager::dropEvent (QDropEvent* event) {
  QStringList files= dropped_files (event->mimeData ());
  if (files.isEmpty ()) return;
  event->acceptProposedAction ();
  importFiles (files, files.size () == 1);
}

void
materials_manager_show () {
  if (!vault_active ()) {
    QMessageBox::warning (QApplication::activeWindow (), "Materials",
                          "No active vault. Load a vault first.");
    return;
  }
  QTMMainTabWindow* window= QTMMainTabWindow::topTabWindow ();
  if (window == nullptr || window->dockManager () == nullptr) return;
  if (materials_widget == nullptr) {
    materials_widget= new QTMMaterialsManager;
    QObject::connect (materials_widget, &QObject::destroyed, [] {
      materials_widget= nullptr;
      materials_dock= nullptr;
    });
  }
  materials_widget->refresh ();
  if (materials_dock == nullptr) {
    materials_dock= new ads::CDockWidget ("Materials");
    materials_dock->setObjectName ("athena-materials-manager");
    materials_dock->setWidget (materials_widget,
                               ads::CDockWidget::ForceNoScrollArea);
    materials_dock->setFeature (
      ads::CDockWidget::DockWidgetDeleteOnClose, false);
    QObject::connect (materials_dock, &QObject::destroyed,
                      [] { materials_dock= nullptr; });
  }
  window->showAdsDockWidget (materials_dock, ads::RightDockWidgetArea);
}

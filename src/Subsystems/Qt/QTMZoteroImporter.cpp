/******************************************************************************
* MODULE     : QTMZoteroImporter.cpp
* DESCRIPTION: Zotero Local API bulk import UI for ATHENA Materials
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMZoteroImporter.hpp"

#include "ATHENA/Data/materials.hpp"
#include "ATHENA/Data/materials_zotero.hpp"
#include "tm_configure.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressDialog>
#include <QPushButton>
#include <QSizePolicy>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

#include <filesystem>
#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace fs= std::filesystem;

namespace {

const QUrl zotero_api ("http://127.0.0.1:23119/api/");

struct ZoteroLibraryChoice {
  QString label;
  QString prefix;
};

struct HttpResult {
  QByteArray body;
  QByteArray server_id;
  QByteArray api_version;
  QByteArray zotero_version;
  int status= 0;
};

struct ResolvedZoteroAttachment {
  const ZoteroAttachmentDescriptor* descriptor= nullptr;
  QString local_path;
};

enum class MetadataConflictDecision {
  KeepExisting,
  UseZotero,
  KeepBoth,
  Cancel
};

std::string
stdstr (const QString& value) {
  QByteArray bytes= value.toUtf8 ();
  return std::string (bytes.constData (), (size_t) bytes.size ());
}

QString
qstr (const std::string& value) {
  return QString::fromUtf8 (value.data (), (qsizetype) value.size ());
}

MetadataConflictDecision
choose_metadata_conflict (
  QWidget* parent, const MaterialRecord& existing,
  const MaterialRecord& incoming,
  const MaterialMetadataReconciliation& reconciliation) {
  QDialog dialog (parent);
  dialog.setWindowTitle ("Resolve duplicate Material");
  dialog.resize (900, 420);
  QVBoxLayout* layout= new QVBoxLayout (&dialog);
  QLabel* explanation= new QLabel (
    QString ("The Zotero item <b>%1</b> has the same file as an existing "
             "Material, but some metadata conflicts. Select which conflicting "
             "values to trust, or keep both records.")
      .arg (qstr (incoming.field ("title")).toHtmlEscaped ()), &dialog);
  explanation->setWordWrap (true);
  layout->addWidget (explanation);

  QTableWidget* table= new QTableWidget (
    (int) reconciliation.conflicts.size (), 3, &dialog);
  table->setHorizontalHeaderLabels ({"Field", "Existing Material", "Zotero"});
  table->setEditTriggers (QAbstractItemView::NoEditTriggers);
  table->setSelectionMode (QAbstractItemView::NoSelection);
  table->setWordWrap (true);
  for (int row=0; row<(int) reconciliation.conflicts.size (); ++row) {
    const MaterialMetadataConflict& conflict=
      reconciliation.conflicts[(size_t) row];
    table->setItem (row, 0, new QTableWidgetItem (qstr (conflict.field)));
    table->setItem (
      row, 1, new QTableWidgetItem (qstr (conflict.existing_value)));
    table->setItem (
      row, 2, new QTableWidgetItem (qstr (conflict.incoming_value)));
  }
  table->horizontalHeader ()->setSectionResizeMode (0,
                                                    QHeaderView::ResizeToContents);
  table->horizontalHeader ()->setSectionResizeMode (1, QHeaderView::Stretch);
  table->horizontalHeader ()->setSectionResizeMode (2, QHeaderView::Stretch);
  table->verticalHeader ()->setVisible (false);
  table->resizeRowsToContents ();
  layout->addWidget (table, 1);

  QLabel* existing_title= new QLabel (
    "Existing: " + qstr (existing.field ("title")), &dialog);
  existing_title->setWordWrap (true);
  layout->addWidget (existing_title);
  QDialogButtonBox* buttons= new QDialogButtonBox (&dialog);
  QPushButton* keep_existing= buttons->addButton (
    "Use existing", QDialogButtonBox::AcceptRole);
  QPushButton* use_zotero= buttons->addButton (
    "Use Zotero", QDialogButtonBox::AcceptRole);
  QPushButton* keep_both= buttons->addButton (
    "Keep both", QDialogButtonBox::ActionRole);
  QPushButton* cancel= buttons->addButton (QDialogButtonBox::Cancel);
  keep_existing->setDefault (true);
  MetadataConflictDecision decision= MetadataConflictDecision::Cancel;
  QObject::connect (keep_existing, &QPushButton::clicked, &dialog, [&] {
    decision= MetadataConflictDecision::KeepExisting;
    dialog.accept ();
  });
  QObject::connect (use_zotero, &QPushButton::clicked, &dialog, [&] {
    decision= MetadataConflictDecision::UseZotero;
    dialog.accept ();
  });
  QObject::connect (keep_both, &QPushButton::clicked, &dialog, [&] {
    decision= MetadataConflictDecision::KeepBoth;
    dialog.accept ();
  });
  QObject::connect (cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
  layout->addWidget (buttons);
  if (dialog.exec () != QDialog::Accepted)
    return MetadataConflictDecision::Cancel;
  return decision;
}

QUrl
api_url (const QString& path) {
  QString relative= path;
  while (relative.startsWith ('/')) relative.remove (0, 1);
  return zotero_api.resolved (QUrl (relative));
}

bool
http_get (const QUrl& url, QProgressDialog* progress, HttpResult& result,
          QString& error) {
  result= HttpResult {};
  QNetworkAccessManager manager;
  QNetworkRequest request (url);
  request.setRawHeader ("Zotero-API-Version", "3");
  request.setRawHeader ("Accept", "application/json");
  request.setRawHeader ("User-Agent",
                        QByteArray ("ATHENA-Materials-Zotero/") +
                          QByteArray (ATHENA_VERSION));
  request.setAttribute (QNetworkRequest::RedirectPolicyAttribute,
                        QNetworkRequest::NoLessSafeRedirectPolicy);
  QNetworkReply* reply= manager.get (request);
  QEventLoop loop;
  QTimer timeout;
  timeout.setSingleShot (true);
  timeout.start (30000);
  QObject::connect (reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  QObject::connect (&timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
  QTimer cancel_poll;
  if (progress != nullptr) {
    cancel_poll.setInterval (50);
    QObject::connect (&cancel_poll, &QTimer::timeout, reply,
                      [progress, reply] {
                        if (progress->wasCanceled ()) reply->abort ();
                      });
    cancel_poll.start ();
  }
  loop.exec ();
  cancel_poll.stop ();
  timeout.stop ();
  result.status= reply->attribute (
    QNetworkRequest::HttpStatusCodeAttribute).toInt ();
  result.server_id= reply->rawHeader ("Zotero-Server-ID");
  result.api_version= reply->rawHeader ("Zotero-API-Version");
  result.zotero_version= reply->rawHeader ("X-Zotero-Version");
  result.body= reply->readAll ();
  if (progress != nullptr && progress->wasCanceled ()) {
    error= "Import cancelled";
    reply->deleteLater ();
    return false;
  }
  if (reply->error () != QNetworkReply::NoError) {
    error= reply->errorString ();
    if (result.status == 403)
      error= "Zotero denied Local API access. Enable Settings -> Advanced -> "
             "Allow other applications on this computer to communicate with "
             "Zotero.";
    else if (result.status == 0)
      error= "Could not contact Zotero at 127.0.0.1:23119. Start Zotero and "
             "enable its Local API.";
    reply->deleteLater ();
    return false;
  }
  reply->deleteLater ();
  return true;
}

bool
parse_groups (const QByteArray& body, std::vector<ZoteroLibraryChoice>& choices,
              QString& error) {
  QJsonParseError parse_error;
  QJsonDocument document= QJsonDocument::fromJson (body, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isArray ()) {
    error= "Zotero returned an invalid group-library list: " +
           parse_error.errorString ();
    return false;
  }
  for (const QJsonValue& value: document.array ()) {
    QJsonObject object= value.toObject ();
    QJsonObject data= object.value ("data").toObject ();
    if (data.isEmpty ()) data= object;
    qint64 id= data.value ("id").toInteger (
      object.value ("id").toInteger ());
    QString name= data.value ("name").toString (
      object.value ("name").toString ());
    if (id <= 0) continue;
    if (name.isEmpty ()) name= QString ("Group %1").arg (id);
    choices.push_back ({"Group: " + name,
                        QString ("groups/%1").arg (id)});
  }
  return true;
}

bool
choose_library (QWidget* parent,
                const std::vector<ZoteroLibraryChoice>& choices,
                int& selected, bool& copy_attachments) {
  QDialog dialog (parent);
  dialog.setWindowTitle ("Import Zotero library");
  QVBoxLayout* outer= new QVBoxLayout (&dialog);
  QLabel* explanation= new QLabel (
    "Import every bibliographic item in a Zotero library into this vault's "
    "Materials database. Existing Zotero item keys and strong identifiers are "
    "deduplicated.", &dialog);
  explanation->setWordWrap (true);
  outer->addWidget (explanation);
  QFormLayout* form= new QFormLayout;
  QComboBox* library= new QComboBox (&dialog);
  for (const ZoteroLibraryChoice& choice: choices)
    library->addItem (choice.label);
  QCheckBox* attachments= new QCheckBox (
    "Copy locally available file attachments into the vault", &dialog);
  attachments->setChecked (true);
  form->addRow ("Library:", library);
  form->addRow (QString (), attachments);
  outer->addLayout (form);
  QDialogButtonBox* buttons= new QDialogButtonBox (
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  buttons->button (QDialogButtonBox::Ok)->setText ("Import");
  QObject::connect (buttons, &QDialogButtonBox::accepted,
                    &dialog, &QDialog::accept);
  QObject::connect (buttons, &QDialogButtonBox::rejected,
                    &dialog, &QDialog::reject);
  outer->addWidget (buttons);
  if (dialog.exec () != QDialog::Accepted) return false;
  selected= library->currentIndex ();
  copy_attachments= attachments->isChecked ();
  return selected >= 0;
}

std::optional<std::string>
find_identifier_duplicate (MaterialsStore& store,
                           const MaterialRecord& material,
                           std::string& error) {
  static const std::set<std::string> strong_schemes= {
    "doi", "isbn", "arxiv", "pmid", "pmcid"
  };
  std::optional<std::string> found;
  for (const MaterialIdentifier& identifier: material.identifiers) {
    if (strong_schemes.count (identifier.scheme) == 0) continue;
    std::optional<std::string> current= store.material_for_identifier (
      identifier.scheme, identifier.value, error);
    if (!error.empty ()) return {};
    if (!current) continue;
    if (found && *found != *current) {
      error= "Zotero item identifiers match multiple existing Materials";
      return {};
    }
    found= current;
  }
  return found;
}

bool
record_zotero_source (MaterialsStore& store, const std::string& uuid,
                      const MaterialProvenance& source, std::string& error) {
  std::optional<MaterialRecord> existing= store.get (uuid, error);
  if (!existing) return false;
  bool present= std::any_of (
    existing->provenance.begin (), existing->provenance.end (),
    [&] (const MaterialProvenance& old) {
      return old.field_name == "@record" && old.source_kind == source.source_kind &&
             old.source_reference == source.source_reference;
    });
  if (present) return true;
  existing->provenance.push_back (source);
  return store.update (*existing, existing->revision, error);
}

QString
attachment_path (const QByteArray& body) {
  QString value= QString::fromUtf8 (body).trimmed ();
  QUrl url (value);
  if (url.isLocalFile ()) return url.toLocalFile ();
  if (QFileInfo (value).isAbsolute ()) return value;
  return {};
}

void
configure_progress_dialog (QProgressDialog& progress) {
  progress.setFixedWidth (560);
  QLabel* label= progress.findChild<QLabel*> ();
  if (label == nullptr) return;
  label->setWordWrap (true);
  label->setMinimumWidth (0);
  label->setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Preferred);
}

bool
fetch_all_items (const ZoteroLibraryChoice& library,
                 QProgressDialog& progress, QByteArray& body,
                 QString& error) {
  constexpr int page_size= 100;
  QJsonArray all;
  int start= 0;
  while (true) {
    progress.setLabelText (
      QString ("Reading all items from %1... %2 received")
        .arg (library.label).arg (all.size ()));
    QUrl url= api_url (library.prefix + "/items");
    QUrlQuery query;
    query.addQueryItem ("format", "json");
    query.addQueryItem ("include", "data");
    query.addQueryItem ("limit", QString::number (page_size));
    query.addQueryItem ("start", QString::number (start));
    url.setQuery (query);

    HttpResult page;
    if (!http_get (url, &progress, page, error)) return false;
    QJsonParseError parse_error;
    QJsonDocument document= QJsonDocument::fromJson (page.body, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isArray ()) {
      error= "Zotero returned an invalid item page: " +
             parse_error.errorString ();
      return false;
    }
    QJsonArray values= document.array ();
    for (const QJsonValue& value: values) all.append (value);
    if (values.size () < page_size) break;
    start += values.size ();
  }
  body= QJsonDocument (all).toJson (QJsonDocument::Compact);
  return true;
}

} // namespace

bool
qtm_import_zotero_library (QWidget* parent, MaterialsStore& store,
                           QTMZoteroImportResult& result) {
  result= QTMZoteroImportResult {};
  QString error;
  QProgressDialog progress ("Connecting to Zotero Local API...", "Cancel",
                            0, 0, parent);
  progress.setWindowTitle ("Import Zotero library");
  progress.setWindowModality (Qt::WindowModal);
  progress.setMinimumDuration (0);
  configure_progress_dialog (progress);
  progress.show ();

  HttpResult identity;
  if (!http_get (zotero_api, &progress, identity, error)) {
    result.cancelled= progress.wasCanceled ();
    if (!result.cancelled)
      QMessageBox::warning (parent, "Import Zotero library", error);
    return false;
  }
  QString server_id= QString::fromUtf8 (identity.server_id);
  if (identity.api_version.isEmpty () && identity.zotero_version.isEmpty ()) {
    QMessageBox::warning (
      parent, "Import Zotero library",
      "The service at 127.0.0.1:23119 did not identify itself as the Zotero "
      "Local API.");
    return false;
  }

  progress.setLabelText ("Reading available Zotero libraries...");
  std::vector<ZoteroLibraryChoice> choices= {
    {"My Library", "users/0"}
  };
  HttpResult groups;
  if (http_get (api_url ("users/0/groups"), &progress, groups, error)) {
    QString parse_error;
    if (!parse_groups (groups.body, choices, parse_error)) {
      QMessageBox::warning (parent, "Import Zotero library", parse_error);
      return false;
    }
  }
  else if (progress.wasCanceled ()) {
    result.cancelled= true;
    return false;
  }
  error.clear ();
  progress.hide ();

  int selected= 0;
  bool copy_attachments= true;
  if (!choose_library (parent, choices, selected, copy_attachments)) return false;
  const ZoteroLibraryChoice& library= choices[(size_t) selected];

  progress.setLabelText ("Reading all items from " + library.label + "...");
  progress.setRange (0, 0);
  progress.setValue (0);
  progress.show ();
  QByteArray items;
  if (!fetch_all_items (library, progress, items, error)) {
    result.cancelled= progress.wasCanceled ();
    if (!result.cancelled)
      QMessageBox::warning (parent, "Import Zotero library", error);
    return false;
  }

  std::vector<ZoteroMaterialImport> imports;
  ZoteroParseSummary parsed;
  std::string parse_error;
  if (!athena_materials_parse_zotero_items (
        std::string (items.constData (), (size_t) items.size ()),
        stdstr (server_id), "/" + stdstr (library.prefix), imports, parsed,
        parse_error)) {
    QMessageBox::warning (parent, "Import Zotero library", qstr (parse_error));
    return false;
  }

  int attachment_count= 0;
  if (copy_attachments)
    for (const ZoteroMaterialImport& entry: imports)
      attachment_count += (int) entry.attachments.size ();
  progress.setRange (0, (int) imports.size () + attachment_count);
  progress.setValue (0);

  int step= 0;
  for (const ZoteroMaterialImport& entry: imports) {
    if (progress.wasCanceled ()) {
      result.cancelled= true;
      break;
    }
    QString title= qstr (entry.material.field ("title"));
    if (title.isEmpty ()) title= qstr (entry.item_key);
    progress.setLabelText (
      QString ("Importing Material %1 of %2: %3")
        .arg (step + 1).arg (imports.size ()).arg (title));

    std::string store_error;
    std::optional<std::string> target= store.material_for_source (
      "zotero", entry.source_reference, store_error);
    bool known_source= target.has_value ();
    bool identifier_duplicate= false;
    if (!target && store_error.empty ())
      target= find_identifier_duplicate (store, entry.material, store_error);
    identifier_duplicate= !known_source && target.has_value ();
    if (!store_error.empty ()) {
      result.failed_items++;
      step += 1 + (copy_attachments ? (int) entry.attachments.size () : 0);
      progress.setValue (step);
      continue;
    }

    std::vector<ResolvedZoteroAttachment> resolved_attachments;
    std::optional<std::string> hash_target;
    if (copy_attachments) {
      for (const ZoteroAttachmentDescriptor& attachment: entry.attachments) {
        if (progress.wasCanceled ()) { result.cancelled= true; break; }
        QString attachment_name= qstr (attachment.filename.empty ()
          ? attachment.title : attachment.filename);
        progress.setLabelText ("Reading Zotero attachment: " + attachment_name);
        HttpResult location;
        QString location_error;
        QString endpoint= library.prefix + "/items/" +
                          qstr (attachment.item_key) + "/file/view/url";
        if (!http_get (api_url (endpoint), &progress, location,
                       location_error)) {
          if (progress.wasCanceled ()) result.cancelled= true;
          resolved_attachments.push_back ({&attachment, {}});
          if (result.cancelled) break;
          continue;
        }
        QString local= attachment_path (location.body);
        if (local.isEmpty () || !QFileInfo (local).isFile ()) {
          resolved_attachments.push_back ({&attachment, {}});
          continue;
        }
        resolved_attachments.push_back ({&attachment, local});
        if (target) continue;
        std::string sha256;
        if (!MaterialsStore::file_sha256 (
              fs::u8path (stdstr (local)), sha256, store_error))
          break;
        std::optional<std::string> owner= store.material_for_sha256 (
          sha256, store_error);
        if (!store_error.empty ()) break;
        if (owner && hash_target && *owner != *hash_target) {
          store_error= "Zotero attachments match multiple existing Materials";
          break;
        }
        if (owner) hash_target= owner;
      }
    }
    if (result.cancelled) break;
    if (!store_error.empty ()) {
      result.failed_items++;
      step += 1 + (copy_attachments ? (int) entry.attachments.size () : 0);
      progress.setValue (step);
      continue;
    }

    if (!target && hash_target) {
      std::optional<MaterialRecord> existing= store.get (*hash_target,
                                                         store_error);
      if (!existing) {
        result.failed_items++;
        step += 1 + (int) entry.attachments.size ();
        progress.setValue (step);
        continue;
      }
      MaterialMetadataReconciliation reconciliation=
        athena_materials_reconcile_metadata (*existing, entry.material);
      MetadataConflictDecision decision= MetadataConflictDecision::KeepExisting;
      if (existing->review_state == "unrecognized" ||
          existing->review_state == "error")
        decision= MetadataConflictDecision::UseZotero;
      else if (!reconciliation.compatible ())
        decision= choose_metadata_conflict (
          parent, *existing, entry.material, reconciliation);
      if (decision == MetadataConflictDecision::Cancel) {
        result.cancelled= true;
        break;
      }
      if (decision == MetadataConflictDecision::KeepBoth) {
        result.hash_conflicts_kept_both++;
      }
      else {
        MaterialRecord merged;
        if (existing->review_state == "unrecognized" ||
            existing->review_state == "error")
          merged= athena_materials_replace_metadata (*existing,
                                                      entry.material);
        else
          merged= decision == MetadataConflictDecision::UseZotero
            ? reconciliation.prefer_incoming
            : reconciliation.prefer_existing;
        if (!store.update (merged, existing->revision, store_error)) {
          result.failed_items++;
          step += 1 + (int) entry.attachments.size ();
          progress.setValue (step);
          continue;
        }
        target= merged.uuid;
        result.hash_reconciled++;
      }
    }

    if (known_source) result.already_imported++;
    else if (identifier_duplicate) {
      result.identifier_duplicates++;
      const MaterialProvenance& marker= entry.material.provenance.back ();
      if (!record_zotero_source (store, *target, marker, store_error)) {
        result.failed_items++;
        step += 1 + (copy_attachments ? (int) entry.attachments.size () : 0);
        progress.setValue (step);
        continue;
      }
    }
    else if (!target) {
      MaterialRecord material= entry.material;
      if (!store.create (material, store_error)) {
        result.failed_items++;
        step += 1 + (copy_attachments ? (int) entry.attachments.size () : 0);
        progress.setValue (step);
        continue;
      }
      target= material.uuid;
      result.added++;
    }
    step++;
    progress.setValue (step);

    if (!copy_attachments) continue;
    bool have_primary= store.primary_attachment (*target, store_error).has_value ();
    store_error.clear ();
    for (const ResolvedZoteroAttachment& resolved: resolved_attachments) {
      if (progress.wasCanceled ()) { result.cancelled= true; break; }
      const ZoteroAttachmentDescriptor& attachment= *resolved.descriptor;
      QString attachment_name= qstr (attachment.filename.empty ()
        ? attachment.title : attachment.filename);
      progress.setLabelText ("Copying Zotero attachment: " + attachment_name);
      if (resolved.local_path.isEmpty ()) {
        result.attachments_unavailable++;
        step++;
        progress.setValue (step);
        continue;
      }
      MaterialImportResult imported;
      bool make_primary= !have_primary;
      std::string role= make_primary ? "document" : "supplement";
      if (!store.import_file (*target,
                              fs::u8path (stdstr (resolved.local_path)), role,
                              make_primary, imported, store_error)) {
        result.attachments_unavailable++;
        store_error.clear ();
      }
      else if (imported.duplicate)
        result.attachments_already_present++;
      else {
        result.attachments_copied++;
        have_primary= true;
      }
      step++;
      progress.setValue (step);
    }
    if (result.cancelled) break;
  }
  progress.close ();

  QString summary= QString (
    "Imported %1 new Material(s).\n"
    "%2 Zotero item(s) were already imported.\n"
    "%3 item(s) matched existing Materials by identifier.\n"
    "%4 item(s) reconciled by attachment hash; %5 conflicting item(s) kept "
    "separately.\n"
    "%6 attachment(s) copied; %7 already present; %8 unavailable.\n"
    "%9 item(s) failed.")
      .arg (result.added).arg (result.already_imported)
      .arg (result.identifier_duplicates).arg (result.hash_reconciled)
      .arg (result.hash_conflicts_kept_both).arg (result.attachments_copied)
      .arg (result.attachments_already_present)
      .arg (result.attachments_unavailable).arg (result.failed_items);
  if (result.cancelled) summary.prepend ("Import was cancelled.\n\n");
  QMessageBox::information (parent, "Import Zotero library", summary);
  return true;
}

/******************************************************************************
* MODULE     : materials_zotero.cpp
* DESCRIPTION: Zotero Local API import mapping for ATHENA Materials
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "ATHENA/Data/materials_zotero.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QString>

#include <algorithm>
#include <map>
#include <set>

namespace {

std::string
utf8 (const QString& value) {
  QByteArray bytes= value.toUtf8 ();
  return std::string (bytes.constData (), (size_t) bytes.size ());
}

QString
text_value (const QJsonValue& value) {
  if (value.isString ()) return value.toString ();
  if (value.isDouble ()) return QString::number (value.toDouble (), 'g', 16);
  if (value.isBool ()) return value.toBool () ? "true" : "false";
  return {};
}

QJsonObject
item_data (const QJsonObject& object) {
  QJsonObject data= object.value ("data").toObject ();
  return data.isEmpty () ? object : data;
}

std::string
source_reference (const std::string& server_id,
                  const std::string& library_prefix,
                  const std::string& item_key,
                  const QJsonObject& wrapper) {
  QJsonObject library= wrapper.value ("library").toObject ();
  QString type= library.value ("type").toString ().trimmed ();
  QString id= text_value (library.value ("id")).trimmed ();
  if (type.isEmpty ()) type= "library";
  if (id.isEmpty ()) id= QString::fromUtf8 (library_prefix.c_str ());
  if (id.isEmpty ()) id= QString::fromUtf8 (server_id.c_str ());
  return "zotero:" + utf8 (type) + ":" + utf8 (id) + ":" + item_key;
}

void
add_identifier (MaterialRecord& material, const char* scheme,
                const QString& value) {
  QString clean= value.trimmed ();
  if (clean.isEmpty ()) return;
  material.identifiers.push_back ({scheme, utf8 (clean), {}});
}

void
add_extra_identifiers (MaterialRecord& material, const QString& extra) {
  const QList<QPair<QString, const char*>> names= {
    {"DOI", "doi"}, {"ISBN", "isbn"}, {"ISSN", "issn"},
    {"PMID", "pmid"}, {"PMCID", "pmcid"}, {"arXiv", "arxiv"}
  };
  for (const QString& line: extra.split ('\n')) {
    int colon= line.indexOf (':');
    if (colon <= 0) continue;
    QString name= line.left (colon).trimmed ();
    QString value= line.mid (colon + 1).trimmed ();
    for (const auto& entry: names)
      if (name.compare (entry.first, Qt::CaseInsensitive) == 0)
        add_identifier (material, entry.second, value);
  }
}

bool
same_identifier (const MaterialIdentifier& a, const MaterialIdentifier& b) {
  return a.scheme == b.scheme &&
         MaterialsStore::normalize_identifier (a.scheme, a.value) ==
           MaterialsStore::normalize_identifier (b.scheme, b.value);
}

MaterialRecord
parse_record (const QJsonObject& wrapper, const QJsonObject& data,
              const std::string& source, const std::string& server_id) {
  MaterialRecord material;
  material.item_type= utf8 (data.value ("itemType").toString ("document"));
  material.review_state= data.value ("title").toString ().trimmed ().isEmpty ()
                            ? "unrecognized" : "ready";

  static const std::set<std::string> excluded= {
    "key", "version", "itemType", "creators", "tags", "collections",
    "relations", "dateAdded", "dateModified", "parentItem", "linkMode",
    "contentType", "charset", "filename", "md5", "mtime", "note"
  };
  int ordinal= 0;
  for (auto it= data.begin (); it != data.end (); ++it) {
    std::string name= utf8 (it.key ());
    if (excluded.count (name) != 0) continue;
    QString value= text_value (it.value ()).trimmed ();
    if (value.isEmpty ()) continue;
    material.fields.push_back ({name, utf8 (value), {}, ordinal++});
    material.provenance.push_back (
      {name, "zotero", source, utf8 (value), 1.0});
  }

  int creator_ordinal= 0;
  for (const QJsonValue& value: data.value ("creators").toArray ()) {
    QJsonObject creator= value.toObject ();
    MaterialCreator parsed;
    parsed.role= utf8 (creator.value ("creatorType").toString ("author"));
    parsed.given= utf8 (creator.value ("firstName").toString ());
    parsed.family= utf8 (creator.value ("lastName").toString ());
    parsed.literal= utf8 (creator.value ("name").toString ());
    parsed.ordinal= creator_ordinal++;
    if (!parsed.given.empty () || !parsed.family.empty () ||
        !parsed.literal.empty ())
      material.creators.push_back (std::move (parsed));
  }
  for (const QJsonValue& value: data.value ("tags").toArray ()) {
    QString tag= value.isObject () ? value.toObject ().value ("tag").toString ()
                                   : value.toString ();
    if (!tag.trimmed ().isEmpty ()) material.tags.push_back (utf8 (tag.trimmed ()));
  }

  add_identifier (material, "doi", data.value ("DOI").toString ());
  add_identifier (material, "isbn", data.value ("ISBN").toString ());
  add_identifier (material, "issn", data.value ("ISSN").toString ());
  add_extra_identifiers (material, data.value ("extra").toString ());
  std::vector<MaterialIdentifier> unique;
  for (const MaterialIdentifier& identifier: material.identifiers)
    if (std::none_of (unique.begin (), unique.end (), [&] (const auto& old) {
          return same_identifier (old, identifier);
        }))
      unique.push_back (identifier);
  material.identifiers= std::move (unique);
  material.provenance.push_back ({"@record", "zotero", source,
                                  utf8 (data.value ("key").toString ()), 1.0});

  QJsonObject preserved;
  preserved.insert ("sourceReference", QString::fromUtf8 (source.c_str ()));
  if (!server_id.empty ())
    preserved.insert ("localServerId", QString::fromUtf8 (server_id.c_str ()));
  preserved.insert ("key", data.value ("key"));
  preserved.insert ("version", data.value ("version"));
  preserved.insert ("collections", data.value ("collections"));
  preserved.insert ("relations", data.value ("relations"));
  preserved.insert ("library", wrapper.value ("library"));
  QJsonObject root;
  root.insert ("zotero", preserved);
  material.extra_json= utf8 (QString::fromUtf8 (
    QJsonDocument (root).toJson (QJsonDocument::Compact)));
  return material;
}

ZoteroAttachmentDescriptor
parse_attachment (const QJsonObject& data) {
  return {utf8 (data.value ("key").toString ()),
          utf8 (data.value ("parentItem").toString ()),
          utf8 (data.value ("title").toString ()),
          utf8 (data.value ("filename").toString ()),
          utf8 (data.value ("contentType").toString ()),
          utf8 (data.value ("linkMode").toString ())};
}

} // namespace

bool
athena_materials_parse_zotero_items (
  const std::string& json, const std::string& server_id,
  const std::string& library_prefix,
  std::vector<ZoteroMaterialImport>& imports,
  ZoteroParseSummary& summary, std::string& error) {
  imports.clear ();
  summary= ZoteroParseSummary {};
  error.clear ();
  QJsonParseError parse_error;
  QJsonDocument document= QJsonDocument::fromJson (
    QByteArray (json.data (), (qsizetype) json.size ()), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isArray ()) {
    error= "Invalid Zotero item response: " + utf8 (parse_error.errorString ());
    return false;
  }

  std::map<std::string, size_t> by_key;
  std::vector<std::pair<QJsonObject, QJsonObject>> attachments;
  for (const QJsonValue& value: document.array ()) {
    if (!value.isObject ()) continue;
    QJsonObject wrapper= value.toObject ();
    QJsonObject data= item_data (wrapper);
    QString type= data.value ("itemType").toString ();
    QString key= data.value ("key").toString (wrapper.value ("key").toString ());
    if (key.isEmpty ()) continue;
    data.insert ("key", key);
    if (type == "attachment") {
      attachments.push_back ({wrapper, data});
      continue;
    }
    if (type == "note") { summary.ignored_notes++; continue; }
    if (type == "annotation") { summary.ignored_annotations++; continue; }
    ZoteroMaterialImport entry;
    entry.item_key= utf8 (key);
    entry.source_reference= source_reference (
      server_id, library_prefix, entry.item_key, wrapper);
    entry.material= parse_record (
      wrapper, data, entry.source_reference, server_id);
    by_key[entry.item_key]= imports.size ();
    imports.push_back (std::move (entry));
    summary.bibliographic_items++;
  }

  for (const auto& pair: attachments) {
    const QJsonObject& wrapper= pair.first;
    const QJsonObject& data= pair.second;
    ZoteroAttachmentDescriptor attachment= parse_attachment (data);
    if (!attachment.parent_key.empty ()) {
      auto parent= by_key.find (attachment.parent_key);
      if (parent != by_key.end ()) {
        imports[parent->second].attachments.push_back (std::move (attachment));
        summary.child_attachments++;
      }
      continue;
    }

    ZoteroMaterialImport entry;
    entry.item_key= attachment.item_key;
    entry.source_reference= source_reference (
      server_id, library_prefix, entry.item_key, wrapper);
    QJsonObject synthetic= data;
    synthetic.insert ("itemType", "document");
    if (synthetic.value ("title").toString ().trimmed ().isEmpty ())
      synthetic.insert ("title", synthetic.value ("filename"));
    entry.material= parse_record (
      wrapper, synthetic, entry.source_reference, server_id);
    entry.attachments.push_back (std::move (attachment));
    imports.push_back (std::move (entry));
    summary.standalone_attachments++;
  }
  return true;
}

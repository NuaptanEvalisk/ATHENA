/******************************************************************************
* MODULE     : materials_schema.cpp
* DESCRIPTION: Pinned Zotero schema access for ATHENA Materials
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
******************************************************************************/

#include "ATHENA/Data/materials_schema.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cstdlib>

namespace fs= std::filesystem;

namespace {

std::string
utf8 (const QString& value) {
  QByteArray bytes= value.toUtf8 ();
  return std::string (bytes.constData (), (size_t) bytes.size ());
}

QString
english_label (const QJsonObject& locales, const char* section,
               const QString& key) {
  QJsonObject english= locales.value ("en-US").toObject ();
  return english.value (section).toObject ().value (key).toString (key);
}

} // namespace

bool
MaterialSchema::load (const fs::path& path, std::string& error) {
  error.clear ();
  QFile file (QString::fromUtf8 (path.u8string ().c_str ()));
  if (!file.open (QIODevice::ReadOnly)) {
    error= "could not open Zotero schema: " + path.string ();
    return false;
  }
  QJsonParseError parse_error;
  QJsonDocument document= QJsonDocument::fromJson (file.readAll (), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject ()) {
    error= "invalid Zotero schema: " + utf8 (parse_error.errorString ());
    return false;
  }

  QJsonObject root= document.object ();
  QJsonObject locales= root.value ("locales").toObject ();
  std::vector<MaterialSchemaItemType> parsed_types;
  std::vector<MaterialSchemaField> parsed_fields;
  std::vector<MaterialSchemaCreatorType> parsed_creators;
  for (const QJsonValue& value: root.value ("itemTypes").toArray ()) {
    QJsonObject object= value.toObject ();
    QString type_name= object.value ("itemType").toString ();
    if (type_name.isEmpty () || type_name == "attachment" ||
        type_name == "note" || type_name == "annotation")
      continue;
    MaterialSchemaItemType type;
    type.name= utf8 (type_name);
    type.label= utf8 (english_label (locales, "itemTypes", type_name));
    for (const QJsonValue& field_value: object.value ("fields").toArray ()) {
      QJsonObject field_object= field_value.toObject ();
      QString name= field_object.value ("field").toString ();
      if (name.isEmpty ()) continue;
      MaterialSchemaField field {
        utf8 (name), utf8 (english_label (locales, "fields", name)),
        utf8 (field_object.value ("baseField").toString ())};
      type.fields.push_back (field);
      if (std::none_of (parsed_fields.begin (), parsed_fields.end (),
                        [&] (const MaterialSchemaField& old) {
                          return old.name == field.name;
                        }))
        parsed_fields.push_back (field);
    }
    for (const QJsonValue& creator_value:
         object.value ("creatorTypes").toArray ()) {
      QJsonObject creator_object= creator_value.toObject ();
      QString name= creator_object.value ("creatorType").toString ();
      if (name.isEmpty ()) continue;
      MaterialSchemaCreatorType creator {
        utf8 (name), utf8 (english_label (locales, "creatorTypes", name)),
        creator_object.value ("primary").toBool (false)};
      type.creator_types.push_back (creator);
      if (std::none_of (parsed_creators.begin (), parsed_creators.end (),
                        [&] (const MaterialSchemaCreatorType& old) {
                          return old.name == creator.name;
                        }))
        parsed_creators.push_back (creator);
    }
    parsed_types.push_back (std::move (type));
  }
  if (parsed_types.empty ()) {
    error= "Zotero schema contains no bibliographic item types";
    return false;
  }
  schema_version= root.value ("version").toInt ();
  types= std::move (parsed_types);
  field_labels= std::move (parsed_fields);
  creator_labels= std::move (parsed_creators);
  return true;
}

bool
MaterialSchema::load_bundled (std::string& error) {
  return load (bundled_path (), error);
}

bool MaterialSchema::is_loaded () const { return !types.empty (); }
int MaterialSchema::version () const { return schema_version; }

const std::vector<MaterialSchemaItemType>&
MaterialSchema::item_types () const { return types; }

const MaterialSchemaItemType*
MaterialSchema::item_type (const std::string& name) const {
  auto found= std::find_if (types.begin (), types.end (),
                            [&] (const MaterialSchemaItemType& type) {
                              return type.name == name;
                            });
  return found == types.end () ? nullptr : &*found;
}

std::string
MaterialSchema::item_type_label (const std::string& name) const {
  const MaterialSchemaItemType* type= item_type (name);
  return type == nullptr ? name : type->label;
}

std::string
MaterialSchema::field_label (const std::string& name) const {
  auto found= std::find_if (field_labels.begin (), field_labels.end (),
                            [&] (const MaterialSchemaField& field) {
                              return field.name == name;
                            });
  return found == field_labels.end () ? name : found->label;
}

std::string
MaterialSchema::creator_type_label (const std::string& name) const {
  auto found= std::find_if (creator_labels.begin (), creator_labels.end (),
                            [&] (const MaterialSchemaCreatorType& creator) {
                              return creator.name == name;
                            });
  return found == creator_labels.end () ? name : found->label;
}

fs::path
MaterialSchema::bundled_path () {
  if (const char* athena_path= std::getenv ("ATHENA_PATH"))
    if (*athena_path != '\0')
      return fs::u8path (athena_path) / "misc/materials/zotero-schema.json";
  fs::path executable= fs::u8path (
    QCoreApplication::applicationDirPath ().toUtf8 ().constData ());
  return executable.parent_path () / "misc/materials/zotero-schema.json";
}

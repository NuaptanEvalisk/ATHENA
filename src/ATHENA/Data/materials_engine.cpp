/******************************************************************************
* MODULE     : materials_engine.cpp
* DESCRIPTION: Hayagriva bridge for Materials import and CSL rendering
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
******************************************************************************/

#include "ATHENA/Data/materials_engine.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdlib>
#include <set>

namespace fs= std::filesystem;

namespace {

std::string
utf8 (const QString& value) {
  QByteArray bytes= value.toUtf8 ();
  return std::string (bytes.constData (), (size_t) bytes.size ());
}

QString
qstr (const std::string& value) {
  return QString::fromUtf8 (value.data (), (qsizetype) value.size ());
}

bool
run_engine (const QStringList& arguments, QByteArray& output,
            std::string& error) {
  error.clear ();
  QProcess process;
  process.setProgram (QString::fromUtf8 (
    athena_materials_engine_path ().u8string ().c_str ()));
  process.setArguments (arguments);
  process.setProcessChannelMode (QProcess::SeparateChannels);
  process.start ();
  if (!process.waitForStarted (10000)) {
    error= "could not start athena-materials-engine: " +
           utf8 (process.errorString ());
    return false;
  }
  if (!process.waitForFinished (120000)) {
    process.kill ();
    process.waitForFinished ();
    error= "athena-materials-engine timed out";
    return false;
  }
  output= process.readAllStandardOutput ();
  QByteArray diagnostics= process.readAllStandardError ();
  if (process.exitStatus () != QProcess::NormalExit || process.exitCode () != 0) {
    error= utf8 (QString::fromUtf8 (diagnostics).trimmed ());
    if (error.empty ()) error= "athena-materials-engine failed";
    return false;
  }
  return true;
}

std::string
scalar_text (const QJsonValue& value) {
  if (value.isString ()) return utf8 (value.toString ());
  if (value.isDouble ()) return utf8 (QString::number (value.toDouble (), 'g', 16));
  return {};
}

std::string
zotero_type (const std::string& type, const QJsonObject& object) {
  if (type == "article") return "journalArticle";
  if (type == "chapter" || type == "anthos") return "bookSection";
  if (type == "anthology") return "book";
  if (type == "conference") return "conferencePaper";
  if (type == "thesis") return "thesis";
  if (type == "report") return "report";
  if (type == "web") return "webpage";
  if (type == "video") return "videoRecording";
  if (type == "audio") return "audioRecording";
  if (type == "patent") return "patent";
  if (type == "legislation") return "statute";
  if (type == "book" || type == "manuscript" || type == "reference" ||
      type == "repository" || type == "newspaper" || type == "periodical")
    return type == "reference" ? "encyclopediaArticle" : type;
  if (object.contains ("publisher")) return "book";
  return "document";
}

QJsonObject
first_parent (const QJsonObject& object) {
  QJsonValue value= object.value ("parent");
  if (value.isObject ()) return value.toObject ();
  if (value.isArray () && !value.toArray ().isEmpty ())
    return value.toArray ().first ().toObject ();
  return {};
}

QJsonObject
parent_of_type (const QJsonObject& object, const QString& type) {
  QJsonValue value= object.value ("parent");
  QJsonArray parents= value.isArray () ? value.toArray ()
                                       : QJsonArray ({value});
  for (const QJsonValue& parent: parents) {
    QJsonObject candidate= parent.toObject ();
    if (candidate.value ("type").toString () == type) return candidate;
  }
  return {};
}

void
add_field (MaterialRecord& record, const std::string& name,
           const QJsonValue& value) {
  std::string text= scalar_text (value);
  if (!text.empty ())
    record.fields.push_back ({name, text, "", (int) record.fields.size ()});
}

void
add_creators (MaterialRecord& record, const std::string& role,
              const QJsonValue& value) {
  QJsonArray creators= value.isArray () ? value.toArray ()
                                        : QJsonArray ({value});
  for (const QJsonValue& creator_value: creators) {
    std::string text= scalar_text (creator_value);
    if (text.empty ()) continue;
    MaterialCreator creator;
    creator.role= role;
    creator.ordinal= (int) record.creators.size ();
    size_t comma= text.find (',');
    if (comma == std::string::npos) creator.literal= text;
    else {
      creator.family= text.substr (0, comma);
      creator.given= text.substr (comma + 1);
      while (!creator.given.empty () && creator.given.front () == ' ')
        creator.given.erase (creator.given.begin ());
    }
    record.creators.push_back (std::move (creator));
  }
}

std::string
bib_escape (std::string value) {
  size_t offset= 0;
  while ((offset= value.find ('\\', offset)) != std::string::npos) {
    value.replace (offset, 1, "\\textbackslash{}");
    offset += 16;
  }
  for (char ch: {'{', '}'}) {
    offset= 0;
    while ((offset= value.find (ch, offset)) != std::string::npos) {
      value.insert (offset, 1, '\\');
      offset += 2;
    }
  }
  return value;
}

std::string
bib_type (const std::string& type) {
  if (type == "journalArticle") return "article";
  if (type == "bookSection") return "incollection";
  if (type == "conferencePaper") return "inproceedings";
  if (type == "thesis") return "thesis";
  if (type == "report") return "report";
  if (type == "webpage") return "online";
  if (type == "patent") return "patent";
  return type == "book" ? "book" : "misc";
}

std::string
records_to_biblatex (const std::vector<MaterialRecord>& records) {
  std::string output;
  for (const MaterialRecord& record: records) {
    output += "@" + bib_type (record.item_type) + "{" + record.uuid + ",\n";
    for (const MaterialField& field: record.fields) {
      static const std::set<std::string> accepted= {
        "title", "date", "publisher", "edition", "volume", "issue", "pages",
        "url", "language", "abstractNote", "note", "location",
        "publicationTitle", "bookTitle", "proceedingsTitle", "conferenceName",
        "series", "number", "rights"};
      if (!accepted.count (field.name) || field.value.empty ()) continue;
      std::string name= field.name;
      if (name == "publicationTitle") name= "journaltitle";
      else if (name == "bookTitle" || name == "proceedingsTitle")
        name= "booktitle";
      else if (name == "conferenceName") name= "eventtitle";
      else if (name == "abstractNote") name= "abstract";
      output += "  " + name + "={" + bib_escape (field.value) + "},\n";
    }
    std::vector<std::string> authors, editors, translators;
    for (const MaterialCreator& creator: record.creators) {
      std::string name= creator.literal.empty ()
        ? bib_escape (creator.family +
                      (creator.given.empty () ? "" : ", " + creator.given))
        : "{" + bib_escape (creator.literal) + "}";
      if (creator.role == "editor") editors.push_back (name);
      else if (creator.role == "translator") translators.push_back (name);
      else authors.push_back (name);
    }
    auto add_names= [&] (const char* field, const std::vector<std::string>& names) {
      if (names.empty ()) return;
      output += "  " + std::string (field) + "={";
      for (size_t i=0; i<names.size (); ++i) {
        if (i != 0) output += " and ";
        output += names[i];
      }
      output += "},\n";
    };
    add_names ("author", authors);
    add_names ("editor", editors);
    add_names ("translator", translators);
    for (const MaterialIdentifier& identifier: record.identifiers) {
      std::string scheme= identifier.scheme;
      std::transform (scheme.begin (), scheme.end (), scheme.begin (), ::tolower);
      if (scheme == "doi" || scheme == "isbn" || scheme == "issn" ||
          scheme == "pmid")
        output += "  " + scheme + "={" + bib_escape (identifier.value) + "},\n";
      else if (scheme == "arxiv")
        output += "  eprint={" + bib_escape (identifier.value) + "},\n"
                  "  eprinttype={arxiv},\n";
    }
    output += "}\n\n";
  }
  return output;
}

bool
write_file (const QString& path, const QByteArray& data, std::string& error) {
  QFile file (path);
  if (!file.open (QIODevice::WriteOnly) || file.write (data) != data.size ()) {
    error= "could not write temporary Materials engine input";
    return false;
  }
  return true;
}

} // namespace

fs::path
athena_materials_engine_path () {
  if (const char* athena_path= std::getenv ("ATHENA_PATH")) {
    fs::path bundled= fs::u8path (athena_path) / "bin/athena-materials-engine";
    if (fs::exists (bundled)) return bundled;
  }
  fs::path executable= fs::u8path (
    QCoreApplication::applicationDirPath ().toUtf8 ().constData ());
  fs::path sibling= executable / "athena-materials-engine";
  if (fs::exists (sibling)) return sibling;
  return executable.parent_path () /
         "materials-engine-cargo/release/athena-materials-engine";
}

bool
athena_materials_list_csl_styles (std::vector<MaterialCslStyle>& styles,
                                  std::string& error) {
  styles.clear ();
  QByteArray output;
  if (!run_engine ({"list-styles"}, output, error)) return false;
  QJsonParseError parse_error;
  QJsonDocument document= QJsonDocument::fromJson (output, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isArray ()) {
    error= "invalid CSL style list response: " +
           utf8 (parse_error.errorString ());
    return false;
  }
  for (const QJsonValue& value: document.array ()) {
    QJsonObject object= value.toObject ();
    MaterialCslStyle style {utf8 (object.value ("name").toString ()),
                            utf8 (object.value ("title").toString ())};
    if (style.name.empty () || style.title.empty ()) {
      error= "invalid CSL style entry returned by Materials engine";
      styles.clear ();
      return false;
    }
    styles.push_back (std::move (style));
  }
  if (styles.empty ()) {
    error= "Materials engine returned no independent CSL styles";
    return false;
  }
  return true;
}

bool
athena_materials_import_bibtex (const fs::path& path,
                                std::vector<MaterialRecord>& records,
                                std::string& error) {
  records.clear ();
  QByteArray output;
  if (!run_engine ({"import-bib", QString::fromUtf8 (path.u8string ().c_str ())},
                   output, error)) return false;
  QJsonParseError parse_error;
  QJsonDocument document= QJsonDocument::fromJson (output, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject ()) {
    error= "invalid BibTeX import response: " + utf8 (parse_error.errorString ());
    return false;
  }
  QJsonObject library= document.object ();
  for (auto it= library.begin (); it != library.end (); ++it) {
    QJsonObject object= it.value ().toObject ();
    MaterialRecord record;
    record.item_type= zotero_type (utf8 (object.value ("type").toString ()), object);
    record.extra_json= utf8 (QJsonDocument (object).toJson (QJsonDocument::Compact));
    record.review_state= "ready";
    const std::vector<std::pair<const char*, const char*>> fields= {
      {"title", "title"}, {"date", "date"}, {"publisher", "publisher"},
      {"location", "location"}, {"edition", "edition"}, {"volume", "volume"},
      {"issue", "issue"}, {"page-range", "pages"}, {"url", "url"},
      {"language", "language"}, {"note", "note"}};
    for (const auto& [source, target]: fields) add_field (record, target, object.value (source));
    QJsonObject parent= first_parent (object);
    if (!parent.isEmpty ()) {
      std::string container_field= record.item_type == "bookSection"
        ? "bookTitle"
        : record.item_type == "conferencePaper"
          ? "proceedingsTitle" : "publicationTitle";
      add_field (record, container_field, parent.value ("title"));
      add_field (record, "volume", parent.value ("volume"));
      add_field (record, "issue", parent.value ("issue"));
      QJsonObject publisher= parent.value ("publisher").toObject ();
      if (!publisher.isEmpty ()) {
        add_field (record, "publisher", publisher.value ("name"));
        add_field (record, "location", publisher.value ("location"));
      }
    }
    QJsonObject conference= parent_of_type (object, "conference");
    if (!conference.isEmpty ())
      add_field (record, "conferenceName", conference.value ("title"));
    add_creators (record, "author", object.value ("author"));
    add_creators (record, "editor", object.value ("editor"));
    add_creators (record, "translator", object.value ("translator"));
    QJsonObject serials= object.value ("serial-number").toObject ();
    for (auto serial= serials.begin (); serial != serials.end (); ++serial)
      record.identifiers.push_back ({utf8 (serial.key ()), scalar_text (serial.value ()), ""});
    record.provenance.push_back (
      {"*", "biblatex", utf8 (it.key ()), record.extra_json, 1.0});
    records.push_back (std::move (record));
  }
  return true;
}

bool
athena_materials_render (
  const std::vector<MaterialRecord>& records,
  const std::vector<MaterialCitationCluster>& citations,
  const std::vector<std::string>& bibliography_only,
  const std::string& csl_style, MaterialRenderedDocument& rendered,
  std::string& error) {
  rendered= {};
  QTemporaryDir temporary;
  if (!temporary.isValid ()) { error= "could not create Materials render workspace"; return false; }
  QString bib_path= temporary.filePath ("materials.bib");
  QString request_path= temporary.filePath ("request.json");
  if (!write_file (bib_path, QByteArray::fromStdString (records_to_biblatex (records)), error))
    return false;
  QJsonObject request;
  request["style"]= qstr (csl_style.empty () ? "springer-mathphys"
                                              : csl_style);
  QJsonArray clusters;
  for (const MaterialCitationCluster& cluster: citations) {
    QJsonArray items;
    for (const MaterialCitationItem& item: cluster.items)
      items.append (QJsonObject {{"key", qstr (item.uuid)},
                                 {"locator_type", qstr (item.locator_type)},
                                 {"locator_value", qstr (item.locator_value)},
                                 {"hidden", item.hidden}});
    clusters.append (QJsonObject {{"items", items}});
  }
  request["citations"]= clusters;
  QJsonArray only;
  for (const std::string& uuid: bibliography_only) only.append (qstr (uuid));
  request["bibliography_only"]= only;
  if (!write_file (request_path, QJsonDocument (request).toJson (QJsonDocument::Compact), error))
    return false;
  QByteArray output;
  if (!run_engine ({"render", bib_path, request_path}, output, error)) return false;
  QJsonParseError parse_error;
  QJsonDocument response= QJsonDocument::fromJson (output, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !response.isObject ()) {
    error= "invalid CSL render response: " + utf8 (parse_error.errorString ());
    return false;
  }
  QJsonObject result= response.object ();
  QJsonArray citation_results= result.value ("citations").toArray ();
  QJsonArray bibliography_results= result.value ("bibliography").toArray ();
  for (const QJsonValue& value: citation_results)
    rendered.citation_html.push_back (utf8 (value.toString ()));
  for (const QJsonValue& value: bibliography_results) {
    QJsonObject item= value.toObject ();
    MaterialRenderedBibliographyItem rendered_item {
      utf8 (item.value ("key").toString ()),
      utf8 (item.value ("html").toString ())};
    if (rendered_item.uuid.empty () || rendered_item.html.empty ()) {
      error= "invalid bibliography item returned by Materials engine";
      rendered= {};
      return false;
    }
    rendered.bibliography.push_back (std::move (rendered_item));
  }
  return true;
}

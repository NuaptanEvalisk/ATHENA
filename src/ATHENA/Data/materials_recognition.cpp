/******************************************************************************
* MODULE     : materials_recognition.cpp
* DESCRIPTION: Local extraction and provider-assisted Material recognition
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "ATHENA/Data/materials_recognition.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QXmlStreamReader>

#include <algorithm>
#include <set>

namespace fs= std::filesystem;

namespace {

QString
qs (const std::string& value) {
  return QString::fromUtf8 (value.data (), (qsizetype) value.size ());
}

std::string
ss (const QString& value) {
  QByteArray bytes= value.toUtf8 ();
  return std::string (bytes.constData (), (size_t) bytes.size ());
}

QString
qpath (const fs::path& path) {
  return QString::fromUtf8 (path.u8string ().c_str ());
}

std::string
json_text (const QJsonValue& value) {
  if (value.isString ()) return ss (value.toString ().trimmed ());
  if (value.isDouble ()) return ss (QString::number (value.toDouble (), 'g', 15));
  return {};
}

void
set_field (MaterialRecord& material, const std::string& name,
           const std::string& value, const std::string& source,
           double confidence, bool replace= false) {
  if (value.empty ()) return;
  auto existing= std::find_if (
    material.fields.begin (), material.fields.end (),
    [&] (const MaterialField& field) { return field.name == name; });
  if (existing == material.fields.end ())
    material.fields.push_back ({name, value, "", 0});
  else if (replace)
    existing->value= value;
  else return;
  material.provenance.push_back (
    {name, source, "", value, confidence});
}

void
add_creator (MaterialRecord& material, const std::string& role,
             const std::string& given, const std::string& family,
             const std::string& literal) {
  if (given.empty () && family.empty () && literal.empty ()) return;
  for (const MaterialCreator& creator: material.creators)
    if (creator.role == role && creator.given == given &&
        creator.family == family && creator.literal == literal) return;
  material.creators.push_back (
    {role, given, family, literal, "", (int) material.creators.size ()});
}

void
add_literal_creators (MaterialRecord& material, const QString& raw,
                      const std::string& role= "author") {
  QString value= raw.trimmed ();
  if (value.isEmpty ()) return;
  const QStringList names= value.split (
    QRegularExpression (QStringLiteral ("\\s*(?:;|\\band\\b)\\s*"),
                        QRegularExpression::CaseInsensitiveOption),
    Qt::SkipEmptyParts);
  for (const QString& name: names) add_creator (material, role, "", "", ss (name));
}

bool
run_process (const std::string& executable, const QStringList& arguments,
             int timeout_ms, QByteArray& output, std::string& diagnostic,
             const std::function<bool ()>& cancelled) {
  if (executable.empty ()) return false;
  QProcess process;
  process.setProgram (qs (executable));
  process.setArguments (arguments);
  process.setProcessChannelMode (QProcess::SeparateChannels);
  process.start ();
  if (!process.waitForStarted (3000)) {
    diagnostic= "Could not start " + executable + ": " +
                ss (process.errorString ());
    return false;
  }
  QElapsedTimer elapsed;
  elapsed.start ();
  const int limit= std::max (1000, timeout_ms);
  while (process.state () != QProcess::NotRunning) {
    process.waitForFinished (100);
    QCoreApplication::processEvents (QEventLoop::AllEvents, 25);
    if (cancelled && cancelled ()) {
      process.kill ();
      process.waitForFinished (1000);
      diagnostic= "Material recognition cancelled";
      return false;
    }
    if (process.state () == QProcess::NotRunning) break;
    if (elapsed.elapsed () >= limit) {
      process.kill ();
      process.waitForFinished (1000);
      diagnostic= executable + " timed out";
      return false;
    }
  }
  output= process.readAllStandardOutput ();
  QByteArray stderr_data= process.readAllStandardError ();
  if (process.exitStatus () != QProcess::NormalExit || process.exitCode () != 0) {
    diagnostic= executable + " failed";
    if (!stderr_data.trimmed ().isEmpty ())
      diagnostic += ": " + ss (QString::fromUtf8 (stderr_data).trimmed ());
    return false;
  }
  if (!stderr_data.trimmed ().isEmpty ())
    diagnostic= executable + ": " +
                ss (QString::fromUtf8 (stderr_data).trimmed ());
  return true;
}

QString
first_json_string (const QJsonObject& object,
                   std::initializer_list<const char*> names) {
  for (const char* name: names) {
    QJsonValue value= object.value (QString::fromLatin1 (name));
    if (value.isString () && !value.toString ().trimmed ().isEmpty ())
      return value.toString ().trimmed ();
    if (value.isArray ()) {
      QStringList parts;
      for (const QJsonValue& part: value.toArray ())
        if (part.isString () && !part.toString ().trimmed ().isEmpty ())
          parts << part.toString ().trimmed ();
      if (!parts.isEmpty ()) return parts.join ("; ");
    }
  }
  return {};
}

void
apply_exiftool_metadata (const QJsonObject& object, MaterialRecord& material,
                         std::vector<std::string>& diagnostics) {
  set_field (material, "title",
             ss (first_json_string (object, {"Title", "DocumentName"})),
             "embedded-metadata", 0.72);
  set_field (material, "date",
             ss (first_json_string (
               object, {"Date", "PublicationDate", "PublishedDate"})),
             "embedded-metadata", 0.60);
  set_field (material, "publisher",
             ss (first_json_string (object, {"Publisher"})),
             "embedded-metadata", 0.65);
  set_field (material, "language",
             ss (first_json_string (object, {"Language"})),
             "embedded-metadata", 0.65);
  set_field (material, "abstractNote",
             ss (first_json_string (object, {"Description", "Subject"})),
             "embedded-metadata", 0.55);
  add_literal_creators (
    material, first_json_string (object, {"Author", "Creator"}));
  if (material.field ("title").empty ())
    diagnostics.push_back ("Embedded metadata did not contain a title");
}

bool
valid_isbn (QString value) {
  value.remove (QRegularExpression ("[^0-9Xx]"));
  if (value.size () == 10) {
    int sum= 0;
    for (int i=0; i<10; ++i) {
      int digit= (i == 9 && value[i].toUpper () == 'X')
        ? 10 : value[i].digitValue ();
      if (digit < 0) return false;
      sum += (10 - i) * digit;
    }
    return sum % 11 == 0;
  }
  if (value.size () == 13) {
    int sum= 0;
    for (int i=0; i<13; ++i) {
      int digit= value[i].digitValue ();
      if (digit < 0) return false;
      sum += digit * ((i % 2) ? 3 : 1);
    }
    return sum % 10 == 0;
  }
  return false;
}

void
append_identifier (std::vector<MaterialIdentifier>& identifiers,
                   const std::string& scheme, const std::string& value) {
  std::string normalized= MaterialsStore::normalize_identifier (scheme, value);
  if (normalized.empty ()) return;
  for (const MaterialIdentifier& existing: identifiers)
    if (existing.scheme == scheme && existing.normalized_value == normalized)
      return;
  identifiers.push_back ({scheme, value, normalized});
}

void
extract_identifiers (const QString& text,
                     std::vector<MaterialIdentifier>& identifiers) {
  QRegularExpression doi (
    QStringLiteral ("\\b10\\.[0-9]{4,9}/[-._;()/:A-Z0-9]+"),
    QRegularExpression::CaseInsensitiveOption);
  QRegularExpressionMatchIterator doi_matches= doi.globalMatch (text);
  while (doi_matches.hasNext ()) {
    QString value= doi_matches.next ().captured (0);
    while (!value.isEmpty () && QStringLiteral (".,;:)]}").contains (value.back ()))
      value.chop (1);
    append_identifier (identifiers, "doi", ss (value));
  }

  QRegularExpression isbn (
    QStringLiteral ("(?:ISBN(?:-1[03])?\\s*:?\\s*)?"
                    "((?:97[89][ -]?)?[0-9][0-9Xx -]{8,20}[0-9Xx])"),
    QRegularExpression::CaseInsensitiveOption);
  QRegularExpressionMatchIterator isbn_matches= isbn.globalMatch (text);
  while (isbn_matches.hasNext ()) {
    QString value= isbn_matches.next ().captured (1);
    if (valid_isbn (value)) append_identifier (identifiers, "isbn", ss (value));
  }

  QRegularExpression arxiv (
    QStringLiteral ("(?:arXiv\\s*:\\s*)?"
                    "((?:[a-z-]+(?:\\.[A-Z]{2})?/[0-9]{7}|"
                    "[0-9]{4}\\.[0-9]{4,5})(?:v[0-9]+)?)"),
    QRegularExpression::CaseInsensitiveOption);
  QRegularExpressionMatchIterator arxiv_matches= arxiv.globalMatch (text);
  while (arxiv_matches.hasNext ())
    append_identifier (identifiers, "arxiv",
                       ss (arxiv_matches.next ().captured (1)));

  QRegularExpression pmid (
    QStringLiteral ("\\bPMID\\s*:?\\s*([0-9]{4,12})\\b"),
    QRegularExpression::CaseInsensitiveOption);
  QRegularExpressionMatchIterator pmid_matches= pmid.globalMatch (text);
  while (pmid_matches.hasNext ())
    append_identifier (identifiers, "pmid",
                       ss (pmid_matches.next ().captured (1)));
}

int
first_printed_page_number (const QString& page_text) {
  QRegularExpression standalone_number (
    QStringLiteral ("(?m)^[ \\t]*([0-9]{1,6})[ \\t]*$"));
  QRegularExpressionMatch match= standalone_number.match (page_text);
  return match.hasMatch () ? match.captured (1).toInt () : -1;
}

QString
collapse_spaced_letters (const QString& text) {
  QStringList words= text.simplified ().split (' ', Qt::SkipEmptyParts);
  QStringList result;
  for (int i=0; i<words.size ();) {
    int end= i;
    while (end<words.size () && words[end].size () == 1 &&
           words[end][0].isLetter ())
      ++end;
    if (end - i >= 2) {
      QString word;
      for (int j=i; j<end; ++j) word += words[j];
      result << word;
      i= end;
    }
    else result << words[i++];
  }
  return result.join (' ');
}

QString
name_case (const QString& text) {
  QString result= text.simplified ().toLower ();
  bool capitalize= true;
  for (QChar& character: result) {
    if (character.isLetter ()) {
      if (capitalize) character= character.toUpper ();
      capitalize= false;
    }
    else capitalize= character.isSpace () || character == '-' ||
                     character == '\'';
  }
  return result;
}

bool
probable_author_line (const QString& text) {
  QString value= text.simplified ();
  QStringList words= value.split (' ', Qt::SkipEmptyParts);
  if (words.size () < 2 || words.size () > 12) return false;
  bool has_letter= false;
  for (QChar character: value) {
    if (character.isDigit ()) return false;
    if (!character.isLetter ()) continue;
    has_letter= true;
    if (character.isLower ()) return false;
  }
  return has_letter;
}

bool
apply_journal_structure (const QString& first_page, MaterialRecord& material,
                         std::vector<std::string>& diagnostics) {
  QStringList lines= first_page.split ('\n');
  QRegularExpression masthead (
    QStringLiteral ("^(.+?)\\s+([0-9]+)(?:\\.([0-9]+))?\\.\\s+"
                    "(.+?)\\s+((?:18|19|20)[0-9]{2})\\s*,\\s*"
                    "([0-9]+)\\s*[-\\x{2013}\\x{2014}]\\s*([0-9]+)\\s*$"),
    QRegularExpression::UseUnicodePropertiesOption);
  QRegularExpression abstract_marker (
    QStringLiteral ("^a\\s*b\\s*s\\s*t\\s*r\\s*a\\s*c\\s*t\\s*[.:]"),
    QRegularExpression::CaseInsensitiveOption |
      QRegularExpression::UseUnicodePropertiesOption);
  int masthead_line= -1;
  int abstract_line= -1;
  QRegularExpressionMatch masthead_match;
  for (int i=0; i<lines.size (); ++i) {
    QString line= lines[i].simplified ();
    if (masthead_line < 0) {
      QRegularExpressionMatch match= masthead.match (line);
      if (match.hasMatch ()) {
        masthead_line= i;
        masthead_match= match;
      }
    }
    if (abstract_marker.match (line).hasMatch ()) {
      abstract_line= i;
      break;
    }
  }
  if (masthead_line < 0 || abstract_line < 0) return false;

  int title_start= -1;
  bool crossed_blank_line= false;
  for (int i= masthead_line + 1; i<abstract_line; ++i) {
    QString line= lines[i].trimmed ();
    if (line.isEmpty ()) {
      crossed_blank_line= true;
      continue;
    }
    if (crossed_blank_line) {
      title_start= i;
      break;
    }
  }
  if (title_start < 0) return false;

  QStringList title_and_author;
  for (int i= title_start; i<abstract_line; ++i) {
    QString line= collapse_spaced_letters (lines[i]);
    if (!line.isEmpty ()) title_and_author << line;
  }
  if (title_and_author.size () < 2 ||
      !probable_author_line (title_and_author.back ()))
    return false;
  QString author= name_case (title_and_author.takeLast ());
  QString title= title_and_author.join (' ').simplified ();
  if (title.isEmpty ()) return false;

  material.item_type= "journalArticle";
  set_field (material, "title", ss (title), "document-structure", 0.90, true);
  set_field (material, "publicationTitle", ss (masthead_match.captured (1)),
             "document-structure", 0.94, true);
  set_field (material, "volume", ss (masthead_match.captured (2)),
             "document-structure", 0.88, true);
  if (!masthead_match.captured (3).isEmpty ())
    set_field (material, "issue", ss (masthead_match.captured (3)),
               "document-structure", 0.86, true);
  set_field (material, "date", ss (masthead_match.captured (5)),
             "document-structure", 0.92, true);
  set_field (material, "pages",
             ss (masthead_match.captured (6) + "-" +
                 masthead_match.captured (7)),
             "document-structure", 0.96, true);
  if (material.creators.empty ())
    add_literal_creators (material, author);
  diagnostics.push_back (
    "Recognized a journal article from its masthead and abstract heading");
  return true;
}

void
apply_pdf_structure (const QString& text, int physical_page_count,
                     MaterialRecord& material,
                     std::vector<std::string>& diagnostics) {
  QStringList physical_pages= text.split ('\f');
  while (!physical_pages.isEmpty () &&
         physical_pages.front ().trimmed ().isEmpty ())
    physical_pages.removeFirst ();
  while (!physical_pages.isEmpty () &&
         physical_pages.back ().trimmed ().isEmpty ())
    physical_pages.removeLast ();
  if (physical_pages.isEmpty ()) return;
  if (apply_journal_structure (physical_pages.front (), material,
                               diagnostics))
    return;
  QStringList lines= physical_pages.front ().split ('\n');
  int heading_line= -1;
  QString chapter_title;
  QRegularExpression chapter_heading (
    QStringLiteral ("^chapter\\s+([0-9]+|[ivxlcdm]+)"
                    "(?:\\s*[:.\\-]\\s*(.+))?$"),
    QRegularExpression::CaseInsensitiveOption);
  for (int i=0; i<lines.size (); ++i) {
    QString line= lines[i].trimmed ();
    if (line.isEmpty ()) continue;
    QRegularExpressionMatch match= chapter_heading.match (line);
    if (!match.hasMatch ()) break;
    heading_line= i;
    chapter_title= match.captured (2).trimmed ();
    break;
  }
  if (heading_line < 0) return;
  if (chapter_title.isEmpty ())
    for (int i= heading_line + 1; i<lines.size (); ++i) {
      QString candidate= lines[i].simplified ();
      if (candidate.isEmpty ()) continue;
      chapter_title= candidate;
      break;
    }
  if (chapter_title.isEmpty () || chapter_title.size () > 300) return;

  std::string old_title= material.field ("title");
  if (!old_title.empty () && old_title != ss (chapter_title))
    set_field (material, "bookTitle", old_title, "embedded-metadata", 0.72,
               material.field ("bookTitle").empty ());
  material.item_type= "bookSection";
  set_field (material, "title", ss (chapter_title), "document-structure",
             0.94, true);

  int first_page= first_printed_page_number (physical_pages.front ());
  int last_page= first_printed_page_number (physical_pages.back ());
  if (first_page > 0 && last_page >= first_page &&
      (physical_page_count <= 0 ||
       last_page - first_page + 1 <= physical_page_count + 2)) {
    std::string pages= std::to_string (first_page);
    if (last_page != first_page)
      pages += "-" + std::to_string (last_page);
    set_field (material, "pages", pages, "document-structure", 0.96, true);
  }
  diagnostics.push_back (
    "Recognized a book section from its chapter heading and printed pages");
}

struct HttpResult {
  QByteArray body;
  int status= 0;
  std::string error;
};

HttpResult
http_get (const QUrl& url, const MaterialProviderOptions& options,
          const QByteArray& accept= "application/json") {
  QNetworkAccessManager manager;
  QNetworkRequest request (url);
  request.setRawHeader ("Accept", accept);
  QByteArray agent= "ATHENA-Materials/1.0";
  if (!options.contact_email.empty ())
    agent += " (mailto:" + QByteArray::fromStdString (options.contact_email) + ")";
  request.setRawHeader ("User-Agent", agent);
  QNetworkReply* reply= manager.get (request);
  QEventLoop loop;
  QTimer timer;
  QTimer cancellation_timer;
  bool was_cancelled= false;
  timer.setSingleShot (true);
  QObject::connect (&timer, &QTimer::timeout, &loop, [&] {
    reply->abort ();
    loop.quit ();
  });
  cancellation_timer.setInterval (100);
  QObject::connect (&cancellation_timer, &QTimer::timeout, &loop, [&] {
    if (options.cancelled && options.cancelled ()) {
      was_cancelled= true;
      reply->abort ();
      loop.quit ();
    }
  });
  QObject::connect (reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  timer.start (std::max (1000, options.timeout_ms));
  cancellation_timer.start ();
  loop.exec ();
  cancellation_timer.stop ();
  timer.stop ();

  HttpResult result;
  result.status= reply->attribute (
    QNetworkRequest::HttpStatusCodeAttribute).toInt ();
  result.body= reply->readAll ();
  if (was_cancelled)
    result.error= "Material recognition cancelled";
  else if (reply->error () != QNetworkReply::NoError)
    result.error= ss (reply->errorString ());
  reply->deleteLater ();
  return result;
}

std::string
date_parts (const QJsonValue& value) {
  if (!value.isObject ()) return {};
  QJsonArray outer= value.toObject ().value ("date-parts").toArray ();
  if (outer.isEmpty () || !outer[0].isArray ()) return {};
  QStringList parts;
  for (const QJsonValue& part: outer[0].toArray ())
    if (part.isDouble ()) parts << QString::number (part.toInt ());
  return ss (parts.join ("-"));
}

std::string
crossref_type (const std::string& value) {
  if (value == "journal-article") return "journalArticle";
  if (value == "book-chapter" || value == "book-section") return "bookSection";
  if (value == "proceedings-article") return "conferencePaper";
  if (value == "dissertation") return "thesis";
  if (value == "report") return "report";
  if (value == "posted-content") return "preprint";
  if (value == "book" || value == "monograph" || value == "edited-book")
    return "book";
  return "document";
}

std::string
container_field (const std::string& item_type) {
  if (item_type == "bookSection" || item_type == "encyclopediaArticle" ||
      item_type == "dictionaryEntry")
    return "bookTitle";
  if (item_type == "conferencePaper") return "proceedingsTitle";
  return "publicationTitle";
}

bool
apply_crossref (const std::string& doi, const MaterialProviderOptions& options,
                MaterialRecord& material, std::string& error) {
  QUrl url (QStringLiteral ("https://api.crossref.org/works/") +
            QString::fromLatin1 (QUrl::toPercentEncoding (qs (doi))));
  HttpResult response= http_get (url, options);
  if (!response.error.empty ()) { error= response.error; return false; }
  QJsonDocument document= QJsonDocument::fromJson (response.body);
  if (!document.isObject ()) { error= "Crossref returned invalid JSON"; return false; }
  QJsonObject message= document.object ().value ("message").toObject ();
  if (message.isEmpty ()) { error= "Crossref did not return a work"; return false; }
  material.item_type= crossref_type (json_text (message.value ("type")));
  QJsonArray titles= message.value ("title").toArray ();
  QJsonArray containers= message.value ("container-title").toArray ();
  set_field (material, "title",
             titles.isEmpty () ? std::string () : json_text (titles.at (0)),
             "crossref", 0.98, true);
  set_field (material, container_field (material.item_type),
             containers.isEmpty () ? std::string ()
                                   : json_text (containers.at (0)),
             "crossref", 0.96, true);
  set_field (material, "date", date_parts (message.value ("issued")),
             "crossref", 0.96, true);
  for (const char* name: {"publisher", "volume", "issue", "page", "URL", "abstract"}) {
    std::string target= name;
    if (target == "page") target= "pages";
    if (target == "URL") target= "url";
    if (target == "abstract") target= "abstractNote";
    set_field (material, target, json_text (message.value (name)),
               "crossref", 0.94, true);
  }
  material.creators.clear ();
  for (const QJsonValue& value: message.value ("author").toArray ()) {
    QJsonObject author= value.toObject ();
    add_creator (material, "author", json_text (author.value ("given")),
                 json_text (author.value ("family")),
                 json_text (author.value ("name")));
  }
  return !material.field ("title").empty ();
}

bool
apply_openalex (const std::string& doi, const MaterialProviderOptions& options,
                MaterialRecord& material, std::string& error) {
  QUrl url (QStringLiteral ("https://api.openalex.org/works/https://doi.org/") +
            qs (doi));
  QUrlQuery query;
  if (!options.contact_email.empty ()) query.addQueryItem ("mailto", qs (options.contact_email));
  url.setQuery (query);
  HttpResult response= http_get (url, options);
  if (!response.error.empty ()) { error= response.error; return false; }
  QJsonDocument document= QJsonDocument::fromJson (response.body);
  if (!document.isObject ()) { error= "OpenAlex returned invalid JSON"; return false; }
  QJsonObject work= document.object ();
  set_field (material, "title", json_text (work.value ("title")),
             "openalex", 0.94, material.field ("title").empty ());
  set_field (material, "date", json_text (work.value ("publication_date")),
             "openalex", 0.92, material.field ("date").empty ());
  QJsonObject location= work.value ("primary_location").toObject ();
  QJsonObject source= location.value ("source").toObject ();
  std::string container_name= container_field (material.item_type);
  set_field (material, container_name, json_text (source.value ("display_name")),
             "openalex", 0.90, material.field (container_name).empty ());
  if (material.creators.empty ())
    for (const QJsonValue& value: work.value ("authorships").toArray ()) {
      QJsonObject author= value.toObject ().value ("author").toObject ();
      add_creator (material, "author", "", "",
                   json_text (author.value ("display_name")));
    }
  return !material.field ("title").empty ();
}

bool
apply_open_library (const std::string& isbn,
                    const MaterialProviderOptions& options,
                    MaterialRecord& material, std::string& error) {
  QUrl url (qs (options.open_library_endpoint));
  QUrlQuery query;
  QString key= "ISBN:" + qs (isbn);
  query.addQueryItem ("bibkeys", key);
  query.addQueryItem ("jscmd", "data");
  query.addQueryItem ("format", "json");
  url.setQuery (query);
  HttpResult response= http_get (url, options);
  if (!response.error.empty ()) { error= response.error; return false; }
  QJsonParseError parse_error;
  QJsonDocument document= QJsonDocument::fromJson (response.body, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject ()) {
    error= "Open Library returned invalid JSON";
    return false;
  }
  QJsonObject entry= document.object ().value (key).toObject ();
  if (entry.isEmpty ()) {
    error= "Open Library did not return a volume";
    return false;
  }
  bool book_section= material.item_type == "bookSection";
  if (!book_section) material.item_type= "book";
  set_field (material, book_section ? "bookTitle" : "title",
             json_text (entry.value ("title")), "open-library", 0.97, true);
  set_field (material, "date", json_text (entry.value ("publish_date")),
             "open-library", 0.94, true);
  if (!book_section)
    set_field (material, "numPages", json_text (entry.value ("pagination")),
               "open-library", 0.90, true);
  set_field (material, "url", json_text (entry.value ("url")),
             "open-library", 0.90, material.field ("url").empty ());
  QJsonArray publishers= entry.value ("publishers").toArray ();
  if (!publishers.isEmpty ())
    set_field (material, "publisher",
               json_text (publishers[0].toObject ().value ("name")),
               "open-library", 0.94, true);
  material.creators.clear ();
  for (const QJsonValue& author: entry.value ("authors").toArray ())
    add_creator (material, "author", "", "",
                 json_text (author.toObject ().value ("name")));
  return !material.field ("title").empty ();
}

bool
apply_google_books (const std::string& isbn,
                    const MaterialProviderOptions& options,
                    MaterialRecord& material, std::string& error) {
  QUrl url ("https://www.googleapis.com/books/v1/volumes");
  QUrlQuery query;
  query.addQueryItem ("q", "isbn:" + qs (isbn));
  query.addQueryItem ("maxResults", "1");
  url.setQuery (query);
  HttpResult response= http_get (url, options);
  if (!response.error.empty ()) { error= response.error; return false; }
  QJsonDocument document= QJsonDocument::fromJson (response.body);
  QJsonArray items= document.object ().value ("items").toArray ();
  if (items.isEmpty ()) { error= "Google Books did not return a volume"; return false; }
  QJsonObject info= items[0].toObject ().value ("volumeInfo").toObject ();
  bool book_section= material.item_type == "bookSection";
  if (!book_section) material.item_type= "book";
  set_field (material, book_section ? "bookTitle" : "title",
             json_text (info.value ("title")), "google-books", 0.94, true);
  set_field (material, "date", json_text (info.value ("publishedDate")),
             "google-books", 0.92, true);
  set_field (material, "publisher", json_text (info.value ("publisher")),
             "google-books", 0.92, true);
  set_field (material, "abstractNote", json_text (info.value ("description")),
             "google-books", 0.85, material.field ("abstractNote").empty ());
  set_field (material, "language", json_text (info.value ("language")),
             "google-books", 0.85, true);
  material.creators.clear ();
  for (const QJsonValue& author: info.value ("authors").toArray ())
    add_creator (material, "author", "", "", json_text (author));
  return !material.field ("title").empty ();
}

bool
apply_arxiv (const std::string& id, const MaterialProviderOptions& options,
             MaterialRecord& material, std::string& error) {
  QUrl url ("https://export.arxiv.org/api/query");
  QUrlQuery query;
  query.addQueryItem ("id_list", qs (id));
  url.setQuery (query);
  HttpResult response= http_get (url, options, "application/atom+xml");
  if (!response.error.empty ()) { error= response.error; return false; }
  QXmlStreamReader xml (response.body);
  bool in_entry= false;
  QString title, summary, published;
  QStringList authors;
  while (!xml.atEnd ()) {
    xml.readNext ();
    if (xml.isStartElement ()) {
      QStringView name= xml.name ();
      if (name == u"entry") in_entry= true;
      else if (in_entry && name == u"title") title= xml.readElementText ();
      else if (in_entry && name == u"summary") summary= xml.readElementText ();
      else if (in_entry && name == u"published") published= xml.readElementText ();
      else if (in_entry && name == u"name") authors << xml.readElementText ();
    }
    else if (xml.isEndElement () && xml.name () == u"entry") break;
  }
  if (xml.hasError () || title.trimmed ().isEmpty ()) {
    error= xml.hasError () ? ss (xml.errorString ()) : "arXiv did not return a work";
    return false;
  }
  material.item_type= "preprint";
  set_field (material, "title", ss (title.simplified ()), "arxiv", 0.98, true);
  set_field (material, "abstractNote", ss (summary.simplified ()),
             "arxiv", 0.96, true);
  set_field (material, "date", ss (published.left (10)), "arxiv", 0.96, true);
  material.creators.clear ();
  for (const QString& author: authors)
    add_creator (material, "author", "", "", ss (author.trimmed ()));
  return true;
}

bool
apply_pubmed (const std::string& pmid, const MaterialProviderOptions& options,
              MaterialRecord& material, std::string& error) {
  QUrl url ("https://eutils.ncbi.nlm.nih.gov/entrez/eutils/esummary.fcgi");
  QUrlQuery query;
  query.addQueryItem ("db", "pubmed");
  query.addQueryItem ("id", qs (pmid));
  query.addQueryItem ("retmode", "json");
  if (!options.contact_email.empty ()) query.addQueryItem ("email", qs (options.contact_email));
  url.setQuery (query);
  HttpResult response= http_get (url, options);
  if (!response.error.empty ()) { error= response.error; return false; }
  QJsonObject root= QJsonDocument::fromJson (response.body).object ();
  QJsonObject entry= root.value ("result").toObject ().value (qs (pmid)).toObject ();
  if (entry.isEmpty ()) { error= "PubMed did not return a record"; return false; }
  material.item_type= "journalArticle";
  set_field (material, "title", json_text (entry.value ("title")),
             "pubmed", 0.98, true);
  set_field (material, "date", json_text (entry.value ("pubdate")),
             "pubmed", 0.95, true);
  set_field (material, "publicationTitle", json_text (entry.value ("fulljournalname")),
             "pubmed", 0.95, true);
  set_field (material, "volume", json_text (entry.value ("volume")),
             "pubmed", 0.93, true);
  set_field (material, "issue", json_text (entry.value ("issue")),
             "pubmed", 0.93, true);
  set_field (material, "pages", json_text (entry.value ("pages")),
             "pubmed", 0.93, true);
  material.creators.clear ();
  for (const QJsonValue& value: entry.value ("authors").toArray ())
    add_creator (material, "author", "", "",
                 json_text (value.toObject ().value ("name")));
  return !material.field ("title").empty ();
}

const MaterialIdentifier*
identifier (const std::vector<MaterialIdentifier>& identifiers,
            const std::string& scheme) {
  auto found= std::find_if (
    identifiers.begin (), identifiers.end (),
    [&] (const MaterialIdentifier& value) { return value.scheme == scheme; });
  return found == identifiers.end () ? nullptr : &*found;
}

void
query_providers (const MaterialProviderOptions& options,
                 MaterialRecognitionResult& result,
                 const std::function<void (const std::string&)>& progress) {
  const MaterialIdentifier* doi= identifier (result.identifiers, "doi");
  const MaterialIdentifier* isbn= identifier (result.identifiers, "isbn");
  const MaterialIdentifier* arxiv= identifier (result.identifiers, "arxiv");
  const MaterialIdentifier* pmid= identifier (result.identifiers, "pmid");
  std::string error;
  bool applied= false;
  if (options.cancelled && options.cancelled ()) return;
  if (doi && options.crossref) {
    if (progress)
      progress ("Waiting for Crossref metadata for DOI " +
                doi->normalized_value);
    applied= apply_crossref (doi->normalized_value, options, result.material,
                             error);
    if (!applied) result.diagnostics.push_back ("Crossref: " + error);
  }
  if (options.cancelled && options.cancelled ()) return;
  if (doi && options.openalex) {
    if (progress)
      progress ("Waiting for OpenAlex metadata for DOI " +
                doi->normalized_value);
    error.clear ();
    bool ok= apply_openalex (doi->normalized_value, options, result.material,
                             error);
    applied= applied || ok;
    if (!ok) result.diagnostics.push_back ("OpenAlex: " + error);
  }
  if (options.cancelled && options.cancelled ()) return;
  if (isbn && options.open_library) {
    if (progress)
      progress ("Waiting for Open Library metadata for ISBN " +
                isbn->normalized_value);
    error.clear ();
    bool ok= apply_open_library (isbn->normalized_value, options,
                                 result.material, error);
    applied= applied || ok;
    if (!ok) result.diagnostics.push_back ("Open Library: " + error);
  }
  if (options.cancelled && options.cancelled ()) return;
  if (isbn && options.google_books) {
    if (progress)
      progress ("Waiting for Google Books metadata for ISBN " +
                isbn->normalized_value);
    error.clear ();
    bool ok= apply_google_books (isbn->normalized_value, options,
                                 result.material, error);
    applied= applied || ok;
    if (!ok) result.diagnostics.push_back ("Google Books: " + error);
  }
  if (options.cancelled && options.cancelled ()) return;
  if (arxiv && options.arxiv) {
    if (progress)
      progress ("Waiting for arXiv metadata for " + arxiv->normalized_value);
    error.clear ();
    bool ok= apply_arxiv (arxiv->normalized_value, options, result.material,
                          error);
    applied= applied || ok;
    if (!ok) result.diagnostics.push_back ("arXiv: " + error);
  }
  if (options.cancelled && options.cancelled ()) return;
  if (pmid && options.pubmed) {
    if (progress)
      progress ("Waiting for PubMed metadata for PMID " +
                pmid->normalized_value);
    error.clear ();
    bool ok= apply_pubmed (pmid->normalized_value, options, result.material,
                           error);
    applied= applied || ok;
    if (!ok) result.diagnostics.push_back ("PubMed: " + error);
  }
  result.external_metadata_used= applied;
}

bool
recognition_cancelled (const MaterialRecognitionOptions& options,
                       std::string& error) {
  if (!options.cancelled || !options.cancelled ()) return false;
  error= "Material recognition cancelled";
  return true;
}

void
report_progress (const MaterialRecognitionOptions& options,
                 const std::string& stage) {
  if (options.progress) options.progress (stage);
}

} // namespace

bool
athena_material_recognize_file (const fs::path& source,
                                const MaterialRecognitionOptions& options,
                                MaterialRecognitionResult& result,
                                std::string& error) {
  result= MaterialRecognitionResult {};
  if (!fs::exists (source) || !fs::is_regular_file (source)) {
    error= "Material source is not a regular file: " + source.string ();
    return false;
  }
  if (recognition_cancelled (options, error)) return false;

  QByteArray metadata_output;
  std::string diagnostic;
  int pdf_page_count= 0;
  report_progress (options, "Reading embedded metadata with exiftool");
  if (run_process (options.metadata_extractor,
                   {"-json", "-charset", "filename=utf8",
                    qpath (source)}, options.providers.timeout_ms,
                   metadata_output, diagnostic, options.cancelled)) {
    QJsonDocument metadata= QJsonDocument::fromJson (metadata_output);
    if (metadata.isArray () && !metadata.array ().isEmpty () &&
        metadata.array ()[0].isObject ()) {
      QJsonObject object= metadata.array ()[0].toObject ();
      pdf_page_count= object.value ("PageCount").toInt ();
      apply_exiftool_metadata (object, result.material, result.diagnostics);
      extract_identifiers (
        QString::fromUtf8 (QJsonDocument (object).toJson (
          QJsonDocument::Compact)), result.identifiers);
      result.metadata_source= "exiftool";
    }
    else result.diagnostics.push_back ("exiftool returned invalid JSON");
  }
  else if (diagnostic == "Material recognition cancelled") {
    error= diagnostic;
    return false;
  }
  else if (!diagnostic.empty ()) result.diagnostics.push_back (diagnostic);

  if (recognition_cancelled (options, error)) return false;
  report_progress (options, "Detecting document type");
  QMimeDatabase mime_database;
  QString mime= mime_database.mimeTypeForFile (
    qpath (source), QMimeDatabase::MatchContent).name ();
  QByteArray text_bytes;
  diagnostic.clear ();
  QString suffix= QFileInfo (qpath (source)).suffix ();
  if (mime == "application/pdf" ||
      suffix.compare ("pdf", Qt::CaseInsensitive) == 0) {
    report_progress (
      options, "Extracting text from the first " +
               std::to_string (std::max (1, options.pdf_pages)) +
               " PDF pages with pdftotext");
    run_process (options.pdf_text_extractor,
                 {"-f", "1", "-l", QString::number (std::max (1, options.pdf_pages)),
                  "-enc", "UTF-8", qpath (source), "-"},
                 options.providers.timeout_ms, text_bytes, diagnostic,
                 options.cancelled);
    if (diagnostic.empty () &&
        pdf_page_count > std::max (1, options.pdf_pages)) {
      QByteArray final_page;
      std::string final_diagnostic;
      report_progress (options, "Extracting the final PDF page for page range");
      run_process (options.pdf_text_extractor,
                   {"-f", QString::number (pdf_page_count), "-l",
                    QString::number (pdf_page_count), "-enc", "UTF-8",
                    qpath (source), "-"},
                   options.providers.timeout_ms, final_page,
                   final_diagnostic, options.cancelled);
      if (final_diagnostic == "Material recognition cancelled") {
        error= final_diagnostic;
        return false;
      }
      if (!final_diagnostic.empty ())
        result.diagnostics.push_back (final_diagnostic);
      if (!final_page.trimmed ().isEmpty ()) {
        if (!text_bytes.endsWith ('\f')) text_bytes.append ('\f');
        text_bytes.append (final_page);
      }
    }
  }
  else if (mime.startsWith ("text/") || mime == "application/rtf") {
    report_progress (options, "Reading text content");
    QFile file (qpath (source));
    if (file.open (QIODevice::ReadOnly))
      text_bytes= file.read (std::max (1024, options.maximum_text_bytes));
  }
  if (diagnostic == "Material recognition cancelled") {
    error= diagnostic;
    return false;
  }
  if (!diagnostic.empty ()) result.diagnostics.push_back (diagnostic);
  if (text_bytes.size () > options.maximum_text_bytes)
    text_bytes.truncate (options.maximum_text_bytes);
  result.extracted_text= ss (QString::fromUtf8 (text_bytes));
  if (recognition_cancelled (options, error)) return false;
  if (mime == "application/pdf" ||
      suffix.compare ("pdf", Qt::CaseInsensitive) == 0)
    apply_pdf_structure (QString::fromUtf8 (text_bytes), pdf_page_count,
                         result.material, result.diagnostics);
  report_progress (options,
                   "Recognizing DOI, ISBN, arXiv, and PMID identifiers");
  extract_identifiers (QString::fromUtf8 (text_bytes), result.identifiers);

  result.material.identifiers= result.identifiers;
  MaterialProviderOptions providers= options.providers;
  providers.cancelled= options.cancelled;
  query_providers (providers, result, options.progress);
  if (recognition_cancelled (options, error)) return false;
  result.material.identifiers= result.identifiers;

  report_progress (options, "Preparing recognized metadata for review");
  if (result.external_metadata_used) result.confidence= 0.96;
  else if (!result.material.field ("title").empty () &&
           !result.identifiers.empty ()) result.confidence= 0.78;
  else if (!result.material.field ("title").empty () &&
           result.material.item_type != "document") result.confidence= 0.82;
  else if (!result.material.field ("title").empty ()) result.confidence= 0.62;
  else {
    set_field (result.material, "title", "Untitled", "fallback", 0.0);
    result.confidence= 0.0;
    result.diagnostics.push_back (
      "No trustworthy title was recognized; manual review is required");
  }
  result.material.review_state=
    result.confidence >= 0.85 ? "ready" : "needs_review";
  return true;
}

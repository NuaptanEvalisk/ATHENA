/******************************************************************************
* MODULE     : format_registry.cpp
* DESCRIPTION: Native, data-driven conversion format registry
* COPYRIGHT  : (C) 2026 ATHENA authors
*******************************************************************************
* GNU general public license version 3 or later; see LICENSE.
******************************************************************************/

#include "convert.hpp"
#include "conversion_registry.hpp"
#include "Data/Convert/Xml/athena_document_xml.hpp"
#include "file.hpp"
#include <QXmlStreamReader>
#include <nlohmann/json.hpp>
#include <map>
#include <stdexcept>
#include <vector>

namespace {
using json = nlohmann::json;

struct Format {
  string name;
  array<string> suffixes;
  bool hidden;
  bool image;
  std::string recognizer;
  array<string> signatures;
  bool must_recognize;
  bool ignore_case;
  bool skip_whitespace;
};

struct Registry {
  std::map<std::string, Format> formats;
  std::map<std::string, string> suffixes;
  std::vector<std::string> recognition_order;

  Registry () {
    string source;
    if (load_string (url ("$ATHENA_PATH/misc/convert/formats.json"), source, false))
      throw std::runtime_error ("Cannot load conversion format registry");
    auto data= json::parse (source.data (), source.data () + N(source));
    if (data.at ("version").get<int> () != 1)
      throw std::runtime_error ("Unsupported conversion format registry version");
    for (const auto& entry: data.at ("formats")) {
      auto id= entry.at ("id").get<std::string> ();
      auto name= entry.at ("name").get<std::string> ();
      Format format {string (name.c_str ()), {}, entry.value ("hidden", false),
                     entry.value ("image", false), entry.value ("recognizer", ""), {},
                     entry.value ("must_recognize", false),
                     entry.value ("ignore_case", false), entry.value ("skip_whitespace", false)};
      if (format.recognizer != "" && format.recognizer != "prefix" &&
          format.recognizer != "athena" && format.recognizer != "html")
        throw std::runtime_error ("Unknown format recognizer: " + format.recognizer);
      for (const auto& value: entry.value ("signatures", json::array ()))
        format.signatures << string (value.get<std::string> ().c_str ());
      if (!format.recognizer.empty ()) recognition_order.push_back (id);
      for (const auto& value: entry.value ("suffixes", json::array ())) {
        auto suffix= value.get<std::string> ();
        format.suffixes << string (suffix.c_str ());
        if (!suffixes.emplace (suffix, string (id.c_str ())).second)
          throw std::runtime_error ("Duplicate conversion suffix: " + suffix);
      }
      if (!formats.emplace (id, format).second)
        throw std::runtime_error ("Duplicate conversion format: " + id);
    }
  }
};

const Registry& registry () {
  static const Registry value;
  return value;
}

bool matches (const Format& format, string source) {
  int start= starts (source, "\xef\xbb\xbf") ? 3 : 0;
  if (format.skip_whitespace)
    while (start < N(source) && (source[start] == ' ' || source[start] == '\t' ||
           source[start] == '\r' || source[start] == '\n' || source[start] == '\f')) ++start;
  for (int i=0; i<N(format.signatures); ++i) {
    const string signature= format.signatures[i];
    if (N(source)-start < N(signature)) continue;
    string probe= source (start, start+N(signature));
    if (format.ignore_case) probe= locase_all (probe);
    if (probe == signature) return true;
  }
  if (format.recognizer == "athena") {
    try {
      return athena::document::inspect_xml_version (
        std::string_view (source.data (), N(source))) == athena::document::xml_format_version::v3;
    }
    catch (const athena::document::codec_exception&) { return false; }
  }
  if (format.recognizer == "html") {
    // The XML reader handles declarations/comments before the first element;
    // this is format detection, not validation or HTML document parsing.
    QXmlStreamReader xml (QByteArray (source.data (), N(source)));
    while (!xml.atEnd ()) {
      const auto token= xml.readNext ();
      if (token == QXmlStreamReader::DTD && xml.dtdName ().compare (
            QLatin1StringView ("html"), Qt::CaseInsensitive) == 0) return true;
      if (token != QXmlStreamReader::StartElement) continue;
      string root= "<" * string (xml.name ().toString ().toUtf8 ().constData ());
      if (format.ignore_case) root= locase_all (root);
      for (int i=0; i<N(format.signatures); ++i)
        if (root == format.signatures[i]) return true;
      return false;
    }
  }
  return false;
}
}

scheme_tree conversion_format_name (string id) {
  const auto& formats= registry ().formats;
  auto it= formats.find (std::string (id.data (), N(id)));
  if (it == formats.end ()) return tree ("#f");
  return tree (scm_quote (it->second.name));
}

bool conversion_format_hidden (string id) {
  const auto& formats= registry ().formats;
  auto it= formats.find (std::string (id.data (), N(id)));
  return it == formats.end () || it->second.hidden;
}

bool conversion_format_image (string id) {
  const auto& formats= registry ().formats;
  auto found= formats.find (std::string (id.data (), N(id)));
  return found != formats.end () && found->second.image;
}

scheme_tree conversion_format_suffixes (string id) {
  tree result= tuple ("tuple");
  for (const auto& entry: registry ().formats)
    if (id == string (entry.first.c_str ()) || (id == "image" && entry.second.image))
      for (int i=0; i<N(entry.second.suffixes); ++i)
        result << tree (scm_quote (entry.second.suffixes[i]));
  return result;
}

scheme_tree conversion_image_formats () {
  tree result (TUPLE);
  for (const auto& entry: registry ().formats)
    if (entry.second.image) result << tree (scm_quote (string (entry.first.c_str ())));
  return result;
}

array<string> conversion_format_ids () {
  array<string> result;
  for (const auto& entry: registry ().formats) result << string (entry.first.c_str ());
  return result;
}

string conversion_format_from_suffix (string suffix) {
  suffix= locase_all (suffix);
  const auto& suffixes= registry ().suffixes;
  auto it= suffixes.find (std::string (suffix.data (), N(suffix)));
  return it == suffixes.end () ? string ("generic") : it->second;
}

bool conversion_format_exists (string id) {
  return registry ().formats.count (std::string (id.data (), N(id))) != 0;
}

string conversion_format_default_suffix (string id) {
  if (id == "image") return "png";
  const auto& formats= registry ().formats;
  auto it= formats.find (std::string (id.data (), N(id)));
  if (it != formats.end () && N(it->second.suffixes)) return it->second.suffixes[0];
  if (ends (id, "-file")) return conversion_format_default_suffix (id (0, N(id)-5));
  return "";
}

bool conversion_format_recognizes (string source, string id) {
  const auto& formats= registry ().formats;
  auto it= formats.find (std::string (id.data (), N(id)));
  return it != formats.end () && !it->second.recognizer.empty () && matches (it->second, source);
}

string conversion_format_determine (string source, string suffix) {
  const auto& data= registry ();
  for (const auto& id: data.recognition_order)
    if (matches (data.formats.at (id), source)) return string (id.c_str ());
  string id= conversion_format_from_suffix (suffix);
  if (id == "generic") return "verbatim";
  return data.formats.at (std::string (id.data (), N(id))).must_recognize ? string ("verbatim") : id;
}

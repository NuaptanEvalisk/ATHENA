/******************************************************************************
* MODULE     : artifact_title_filter.cpp
* DESCRIPTION: Per-vault artifact title rejection list
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "ATHENA/Data/artifact_title_filter.hpp"

#include "ATHENA/Data/artifact_radioactive_links.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"
#include "Data/Convert/Xml/document_file_codec.hpp"
#include "node_metadata.hpp"

#include <QCryptographicHash>
#include <QByteArrayView>
#include <QString>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <fstream>
#include <system_error>
#include <mutex>

namespace fs= std::filesystem;

namespace {

std::recursive_mutex filter_mutex;
const char* format_header= "# ATHENA artifact title filter v2";

tree name_content (const tree& value) {
  tree result= athena::node::content_projection (value);
  while ((is_func (result, DOCUMENT, 1) || is_func (result, CONCAT, 1)))
    result= result[0];
  return result;
}

bool text_name (const tree& value, std::string& text) {
  if (is_atomic (value)) {
    text.append (value->label.data (), N(value->label));
    return true;
  }
  if (!is_func (value, CONCAT)) return false;
  for (int i=0; i<N(value); ++i)
    if (!text_name (value[i], text)) return false;
  return true;
}

std::string structured_key (const tree& name) {
  return athena::document::write_xml (
    name_content (name), athena::document::xml_kind::fragment);
}

std::string
utf8 (const QString& value) {
  QByteArray bytes= value.toUtf8 ();
  return std::string (bytes.constData (), (size_t) bytes.size ());
}

std::string
normalize_entry (const std::string& value) {
  QString normalized= QString::fromUtf8 (value.data (), (qsizetype) value.size ())
    .normalized (QString::NormalizationForm_KC).trimmed ().toCaseFolded ();
  normalized.replace (QChar (0x2018), QChar ('\''));
  normalized.replace (QChar (0x2019), QChar ('\''));
  normalized= normalized.simplified ();
  return utf8 (normalized);
}

bool
configured_path (const fs::path& root, const AthenaVaultfileInfo& info,
                 fs::path& path, std::string& error) {
  fs::path relative= fs::path (info.artifact_title_filter_path)
                       .lexically_normal ();
  if (relative.empty () || relative.is_absolute ()) {
    error= "Artifact title filter path must be relative to the vault";
    return false;
  }
  for (const fs::path& part: relative)
    if (part == "." || part == "..") {
      error= "Artifact title filter path must remain inside the vault";
      return false;
    }
  QString extension= QString::fromStdString (relative.extension ().string ());
  if (extension.compare (".lst", Qt::CaseInsensitive) != 0) {
    error= "Artifact title filter must be stored in a .lst file";
    return false;
  }
  path= root / relative;
  return true;
}

bool
write_entries (const fs::path& path, const AthenaArtifactTitleFilter& filter,
               std::string& error) {
  std::error_code ec;
  fs::create_directories (path.parent_path (), ec);
  if (ec) {
    error= "Could not create artifact title filter directory: " + ec.message ();
    return false;
  }
  QJsonArray text, structured;
  for (const auto& entry: filter.entries)
    text.append (QString::fromStdString (entry));
  for (const auto& entry: filter.structured_entries)
    structured.append (QString::fromStdString (entry));
  QJsonObject data {{"version", 2}, {"text", text}, {"structured", structured}};
  QByteArray bytes= QByteArray (format_header) + '\n' +
    QJsonDocument (data).toJson (QJsonDocument::Indented);
  QSaveFile output (QString::fromStdString (path.string ()));
  if (!output.open (QIODevice::WriteOnly) ||
      output.write (bytes) != bytes.size () || !output.commit ()) {
    error= "Could not write artifact title filter: " + output.errorString ().toStdString ();
    return false;
  }
  return true;
}

} // namespace

AthenaArtifactTitleFilter
athena_artifact_title_filter_defaults () {
  return athena_artifact_title_filter_from_entries ({
    "no", "not", "however", "but",
    "am", "is", "are", "was", "were", "be", "being", "been",
    "do", "does", "did", "doing", "done",
    "have", "has", "had", "having",
    "can", "cannot", "can not", "could", "may", "might", "must",
    "shall", "should", "should not", "will", "would",
    "is not", "ain't", "aren't", "can't", "couldn't", "didn't",
    "doesn't", "don't", "hadn't", "hasn't", "haven't", "isn't",
    "mightn't", "mustn't", "needn't", "shan't", "shouldn't",
    "wasn't", "weren't", "won't", "wouldn't"
  });
}

AthenaArtifactTitleFilter
athena_artifact_title_filter_from_entries (
  const std::vector<std::string>& entries) {
  AthenaArtifactTitleFilter result;
  for (const std::string& entry: entries) {
    if (entry.find ('\n') != std::string::npos ||
        entry.find ('\r') != std::string::npos) continue;
    std::string normalized= normalize_entry (entry);
    if (normalized.empty () || result.normalized.count (normalized)) continue;
    result.normalized.insert (normalized);
    result.entries.push_back (utf8 (QString::fromUtf8 (
      entry.data (), (qsizetype) entry.size ()).trimmed ()));
  }
  return result;
}

bool
athena_artifact_title_filter_contains (
  const AthenaArtifactTitleFilter& filter, const std::string& candidate_utf8) {
  return filter.normalized.count (normalize_entry (candidate_utf8)) != 0;
}

bool athena_artifact_title_filter_contains (
  const AthenaArtifactTitleFilter& filter, const tree& candidate) {
  tree name= name_content (candidate);
  std::string text;
  if (text_name (name, text))
    return athena_artifact_title_filter_contains (filter, text);
  return !filter.structured.empty () && filter.structured.count (structured_key (name));
}

void athena_artifact_title_filter_add (
  AthenaArtifactTitleFilter& filter, const tree& candidate) {
  tree name= name_content (candidate);
  std::string text;
  if (text_name (name, text)) {
    auto entries= filter.entries;
    entries.push_back (text);
    auto plain= athena_artifact_title_filter_from_entries (entries);
    filter.entries= std::move (plain.entries);
    filter.normalized= std::move (plain.normalized);
  }
  else {
    auto key= structured_key (name);
    if (filter.structured.insert (key).second)
      filter.structured_entries.push_back (std::move (key));
  }
}

std::string
athena_artifact_title_filter_fingerprint (
  const AthenaArtifactTitleFilter& filter) {
  std::vector<std::string> normalized (filter.normalized.begin (),
                                       filter.normalized.end ());
  std::sort (normalized.begin (), normalized.end ());
  std::vector<std::string> structured (filter.structured.begin (), filter.structured.end ());
  std::sort (structured.begin (), structured.end ());
  QCryptographicHash hash (QCryptographicHash::Sha256);
  for (const std::string& entry: normalized) {
    hash.addData (QByteArrayView (entry.data (), (qsizetype) entry.size ()));
    hash.addData (QByteArrayView ("\n", 1));
  }
  for (const auto& entry: structured) {
    hash.addData (QByteArrayView ("\0tree\0", 6));
    hash.addData (QByteArrayView (entry.data (), entry.size ()));
  }
  return hash.result ().toHex ().toStdString ();
}

bool
athena_artifact_title_filter_read (
  const fs::path& vault_root, AthenaArtifactTitleFilter& filter,
  std::string& error, bool create_defaults) {
  std::lock_guard<std::recursive_mutex> guard (filter_mutex);
  AthenaVaultfileInfo info;
  if (!athena_vaultfile_read (vault_root, info, error)) return false;
  fs::path path;
  if (!configured_path (vault_root, info, path, error)) return false;
  if (!fs::exists (path)) {
    filter= athena_artifact_title_filter_defaults ();
    if (!create_defaults) return true;
    if (!write_entries (path, filter, error)) return false;
    // Persist the default field for vaults created before this setting existed.
    return athena_vaultfile_write (vault_root, info, error);
  }
  std::ifstream input (path, std::ios::binary);
  if (!input) {
    error= "Could not read " + path.string ();
    return false;
  }
  std::vector<std::string> entries;
  std::string line;
  std::getline (input, line);
  if (!line.empty () && line.back () == '\r') line.pop_back ();
  if (line == format_header) {
    std::string bytes ((std::istreambuf_iterator<char> (input)), {});
    QJsonParseError parse_error;
    auto data= QJsonDocument::fromJson (QByteArray::fromStdString (bytes), &parse_error);
    auto obj= data.object ();
    if (parse_error.error != QJsonParseError::NoError || !data.isObject () ||
        obj.value ("version").toInt () != 2 || !obj.value ("text").isArray () ||
        !obj.value ("structured").isArray ()) {
      error= "Invalid artifact title filter v2: " + path.string ();
      return false;
    }
    try {
      for (const auto& value: obj.value ("text").toArray ()) {
        if (!value.isString ()) throw std::runtime_error ("Expected a text name");
        entries.push_back (value.toString ().toStdString ());
      }
      filter= athena_artifact_title_filter_from_entries (entries);
      for (const auto& value: obj.value ("structured").toArray ()) {
        if (!value.isString ()) throw std::runtime_error ("Expected a name fragment");
        athena_artifact_title_filter_add (filter, athena::document::read_xml (
          value.toString ().toStdString (), athena::document::xml_kind::fragment));
      }
    }
    catch (const std::exception& e) { error= e.what (); return false; }
    return true;
  }
  entries.push_back (line);
  while (std::getline (input, line)) {
    if (!line.empty () && line.back () == '\r') line.pop_back ();
    entries.push_back (line);
  }
  if (!input.good () && !input.eof ()) {
    error= "Could not read " + path.string ();
    return false;
  }
  filter= athena_artifact_title_filter_from_entries (entries);
  return true;
}

bool
athena_artifact_title_filter_write (
  const fs::path& vault_root, const AthenaArtifactTitleFilter& filter,
  std::string& error) {
  std::lock_guard<std::recursive_mutex> guard (filter_mutex);
  AthenaVaultfileInfo info;
  if (!athena_vaultfile_read (vault_root, info, error)) return false;
  fs::path path;
  if (!configured_path (vault_root, info, path, error)) return false;
  if (!write_entries (path, filter, error)) return false;
  athena_artifact_radioactive_invalidate ();
  return true;
}

bool athena_artifact_title_filter_write (
  const fs::path& root, const std::vector<std::string>& entries, std::string& error) {
  std::lock_guard<std::recursive_mutex> guard (filter_mutex);
  AthenaArtifactTitleFilter filter;
  if (!athena_artifact_title_filter_read (root, filter, error)) return false;
  auto plain= athena_artifact_title_filter_from_entries (entries);
  filter.entries= std::move (plain.entries);
  filter.normalized= std::move (plain.normalized);
  return athena_artifact_title_filter_write (root, filter, error);
}

bool athena_artifact_title_filter_reject (
  const fs::path& root, const tree& name, std::string& error) {
  std::lock_guard<std::recursive_mutex> guard (filter_mutex);
  AthenaArtifactTitleFilter filter;
  if (!athena_artifact_title_filter_read (root, filter, error)) return false;
  athena_artifact_title_filter_add (filter, name);
  return athena_artifact_title_filter_write (root, filter, error);
}

bool athena_artifact_title_filter_replace_if_current (
  const fs::path& root, const AthenaArtifactTitleFilter& expected,
  const AthenaArtifactTitleFilter& replacement, bool& matched, std::string& error,
  const std::function<bool()>& permitted) {
  std::lock_guard<std::recursive_mutex> guard (filter_mutex);
  matched= false;
  AthenaArtifactTitleFilter current;
  if (!athena_artifact_title_filter_read (root, current, error, false)) return false;
  auto sorted= [] (std::vector<std::string> values) {
    std::sort (values.begin (), values.end ()); return values;
  };
  if (sorted (current.entries) != sorted (expected.entries) ||
      sorted (current.structured_entries) != sorted (expected.structured_entries)) return true;
  if (permitted && !permitted ()) return true;
  if (!athena_artifact_title_filter_write (root, replacement, error)) return false;
  matched= true;
  return true;
}

/******************************************************************************
* MODULE     : artifact_title_filter.cpp
* DESCRIPTION: Per-vault artifact title rejection list
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "ATHENA/Data/artifact_title_filter.hpp"

#include "ATHENA/Data/hodarium_local_notifications.hpp"
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
#include <sstream>

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

std::string structured_key (const tree& name,
                            athena::document::xml_format_version version) {
  return athena::document::encode_native_xml_bytes (
    name_content (name), version, athena::document::xml_kind::fragment);
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
  std::string serialized;
  try { serialized= athena_artifact_title_filter_serialize (filter); }
  catch (const std::exception& e) { error= e.what (); return false; }
  std::error_code ec;
  fs::create_directories (path.parent_path (), ec);
  if (ec) {
    error= "Could not create artifact title filter directory: " + ec.message ();
    return false;
  }
  const QByteArray bytes (serialized.data (), qsizetype (serialized.size ()));
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
  // Lookup must not serialize a native candidate through the legacy filter
  // writer: v3 math policy metadata is part of its meaning, not an error.
  using namespace athena::document;
  for (const auto& encoded: filter.structured) {
    const tree stored= decode_native_xml_bytes (encoded, xml_kind::fragment).document;
    if (athena::node::content_equal (name, name_content (stored))) return true;
  }
  return false;
}

void athena_artifact_title_filter_add (
  AthenaArtifactTitleFilter& filter, const tree& candidate) {
  athena_artifact_title_filter_add (
    filter, candidate, athena::document::xml_format_version::v1);
}

void athena_artifact_title_filter_add (
  AthenaArtifactTitleFilter& filter, const tree& candidate,
  athena::document::xml_format_version version) {
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
    auto key= structured_key (name, version);
    if (filter.structured.insert (key).second)
      filter.structured_entries.push_back (std::move (key));
  }
}

void athena_artifact_title_filter_add_fragment (
  AthenaArtifactTitleFilter& filter, std::string_view encoded) {
  const auto decoded= athena::document::decode_native_xml_bytes (
    encoded, athena::document::xml_kind::fragment);
  athena_artifact_title_filter_add (filter, decoded.document, decoded.version);
}

std::string athena_artifact_title_filter_serialize (const AthenaArtifactTitleFilter& filter) {
  QJsonArray text, structured;
  for (const auto& entry: filter.entries)
    text.append (QString::fromStdString (entry));
  for (const auto& entry: filter.structured_entries) {
    const auto decoded= athena::document::decode_native_xml_bytes (
      entry, athena::document::xml_kind::fragment);
    if (structured_key (decoded.document, decoded.version) != entry)
      throw std::runtime_error ("Noncanonical artifact rejection fragment");
    structured.append (QString::fromStdString (entry));
  }
  const QJsonObject data {{"version", 2}, {"text", text}, {"structured", structured}};
  return (QByteArray (format_header) + '\n' +
    QJsonDocument (data).toJson (QJsonDocument::Indented)).toStdString ();
}

AthenaArtifactTitleFilter athena_artifact_title_filter_parse (std::string_view bytes) {
  std::istringstream input {std::string (bytes)};
  std::string line;
  std::getline (input, line);
  if (!line.empty () && line.back () == '\r') line.pop_back ();
  std::vector<std::string> entries;
  if (line != format_header) {
    entries.push_back (line);
    while (std::getline (input, line)) {
      if (!line.empty () && line.back () == '\r') line.pop_back ();
      entries.push_back (line);
    }
    return athena_artifact_title_filter_from_entries (entries);
  }
  const std::string json ((std::istreambuf_iterator<char> (input)), {});
  QJsonParseError parse_error;
  const auto data= QJsonDocument::fromJson (QByteArray::fromStdString (json), &parse_error);
  const auto obj= data.object ();
  if (parse_error.error != QJsonParseError::NoError || !data.isObject () ||
      obj.value ("version").toInt () != 2 || !obj.value ("text").isArray () ||
      !obj.value ("structured").isArray ())
    throw std::runtime_error ("Invalid artifact title filter v2");
  for (const auto& value: obj.value ("text").toArray ()) {
    if (!value.isString ()) throw std::runtime_error ("Expected a text name");
    entries.push_back (value.toString ().toStdString ());
  }
  auto filter= athena_artifact_title_filter_from_entries (entries);
  for (const auto& value: obj.value ("structured").toArray ()) {
    if (!value.isString ()) throw std::runtime_error ("Expected a name fragment");
    athena_artifact_title_filter_add_fragment (filter, value.toString ().toStdString ());
  }
  return filter;
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
  std::string& error, bool create_defaults, bool* persisted) {
  std::lock_guard<std::recursive_mutex> guard (filter_mutex);
  if (persisted) *persisted= false;
  AthenaVaultfileInfo info;
  if (!athena_vaultfile_read (vault_root, info, error)) return false;
  fs::path path;
  if (!configured_path (vault_root, info, path, error)) return false;
  if (!fs::exists (path)) {
    filter= athena_artifact_title_filter_defaults ();
    if (!create_defaults) return true;
    if (!write_entries (path, filter, error)) return false;
    if (persisted) *persisted= true;
    // Persist the default field for vaults created before this setting existed.
    return athena_vaultfile_write (vault_root, info, error);
  }
  std::ifstream input (path, std::ios::binary);
  if (!input) {
    error= "Could not read " + path.string ();
    return false;
  }
  const std::string bytes ((std::istreambuf_iterator<char> (input)), {});
  if (!input.good () && !input.eof ()) {
    error= "Could not read " + path.string ();
    return false;
  }
  try { filter= athena_artifact_title_filter_parse (bytes); }
  catch (const std::exception& e) { error= e.what (); return false; }
  if (persisted) *persisted= true;
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
  athena::hodarium::notify_local_change (vault_root, false);
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
  AthenaVaultfileInfo info;
  if (!athena_vaultfile_read (root, info, error)) return false;
  try {
    using athena::document::xml_format_version;
    if (info.node_model_version < 0 || info.node_model_version > 2)
      throw std::runtime_error ("Unsupported Vault node model for artifact rejection");
    // Older filters use identity-free v1 fragments; preserve their fingerprints.
    const auto version= info.node_model_version == 2 ?
      xml_format_version::v3 : xml_format_version::v1;
    athena_artifact_title_filter_add (filter, name, version);
  }
  catch (const std::exception& e) { error= e.what (); return false; }
  return athena_artifact_title_filter_write (root, filter, error);
}

bool athena_artifact_title_filter_replace_if_current (
  const fs::path& root, const std::optional<AthenaArtifactTitleFilter>& expected,
  const AthenaArtifactTitleFilter& replacement, bool& matched, std::string& error,
  const std::function<bool()>& permitted) {
  std::lock_guard<std::recursive_mutex> guard (filter_mutex);
  matched= false;
  AthenaArtifactTitleFilter current;
  bool persisted= false;
  if (!athena_artifact_title_filter_read (root, current, error, false, &persisted)) return false;
  if (persisted != expected.has_value ()) return true;
  auto sorted= [] (std::vector<std::string> values) {
    std::sort (values.begin (), values.end ()); return values;
  };
  if (expected && (sorted (current.entries) != sorted (expected->entries) ||
      sorted (current.structured_entries) != sorted (expected->structured_entries))) return true;
  if (permitted && !permitted ()) return true;
  if (!athena_artifact_title_filter_write (root, replacement, error)) return false;
  matched= true;
  return true;
}

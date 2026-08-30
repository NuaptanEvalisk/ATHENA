/******************************************************************************
* MODULE     : artifact_title_filter.cpp
* DESCRIPTION: Per-vault artifact title rejection list
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "ATHENA/Data/artifact_title_filter.hpp"

#include "ATHENA/Data/vaultfile_json.hpp"

#include <QCryptographicHash>
#include <QByteArrayView>
#include <QString>

#include <algorithm>
#include <fstream>
#include <system_error>

namespace fs= std::filesystem;

namespace {

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
write_entries (const fs::path& path, const std::vector<std::string>& entries,
               std::string& error) {
  std::error_code ec;
  fs::create_directories (path.parent_path (), ec);
  if (ec) {
    error= "Could not create artifact title filter directory: " + ec.message ();
    return false;
  }
  fs::path temporary= path;
  temporary += ".tmp";
  {
    std::ofstream output (temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
      error= "Could not write " + temporary.string ();
      return false;
    }
    for (const std::string& entry: entries) output << entry << '\n';
    if (!output) {
      error= "Could not write " + temporary.string ();
      return false;
    }
  }
  fs::rename (temporary, path, ec);
  if (ec) {
    fs::remove (temporary);
    error= "Could not replace " + path.string () + ": " + ec.message ();
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

std::string
athena_artifact_title_filter_fingerprint (
  const AthenaArtifactTitleFilter& filter) {
  std::vector<std::string> normalized (filter.normalized.begin (),
                                       filter.normalized.end ());
  std::sort (normalized.begin (), normalized.end ());
  QCryptographicHash hash (QCryptographicHash::Sha256);
  for (const std::string& entry: normalized) {
    hash.addData (QByteArrayView (entry.data (), (qsizetype) entry.size ()));
    hash.addData (QByteArrayView ("\n", 1));
  }
  return hash.result ().toHex ().toStdString ();
}

bool
athena_artifact_title_filter_read (
  const fs::path& vault_root, AthenaArtifactTitleFilter& filter,
  std::string& error) {
  AthenaVaultfileInfo info;
  if (!athena_vaultfile_read (vault_root, info, error)) return false;
  fs::path path;
  if (!configured_path (vault_root, info, path, error)) return false;
  if (!fs::exists (path)) {
    filter= athena_artifact_title_filter_defaults ();
    if (!write_entries (path, filter.entries, error)) return false;
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
  const fs::path& vault_root, const std::vector<std::string>& entries,
  std::string& error) {
  AthenaVaultfileInfo info;
  if (!athena_vaultfile_read (vault_root, info, error)) return false;
  fs::path path;
  if (!configured_path (vault_root, info, path, error)) return false;
  AthenaArtifactTitleFilter filter=
    athena_artifact_title_filter_from_entries (entries);
  if (!write_entries (path, filter.entries, error)) return false;
  return athena_vaultfile_write (vault_root, info, error);
}

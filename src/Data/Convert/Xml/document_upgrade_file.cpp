/******************************************************************************
* MODULE     : document_upgrade_file.cpp
* DESCRIPTION: Verified original-byte backups and revision-checked XML upgrades
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "document_upgrade_file.hpp"
#include <QCryptographicHash>
#include <cerrno>
#include <system_error>

namespace athena::document {
namespace {
std::string sha256 (std::string_view bytes) {
  const auto digest= QCryptographicHash::hash (
    QByteArrayView (bytes.data (), qsizetype (bytes.size ())), QCryptographicHash::Sha256).toHex ();
  return {digest.constData (), std::size_t (digest.size ())};
}
bool starts (std::string_view value, std::string_view prefix) {
  return value.substr (0, prefix.size ()) == prefix;
}
legacy_format identify_legacy (std::string_view bytes) {
  // Match the established reader signatures, never infer the text encoding
  // from UTF-8 validity. The import layer is responsible for syntax validation.
  if (starts (bytes, "<TeXmacs|")) return legacy_format::texmacs;
  if (starts (bytes, "(document (TeXmacs ") ||
      starts (bytes, "(document (apply \"TeXmacs\" ") ||
      starts (bytes, "(document (expand \"TeXmacs\" "))
    return legacy_format::scheme;
  throw codec_exception (codec_error::invalid_structure, "Not a supported legacy document signature");
}
} // namespace

legacy_file::legacy_file (filesystem::confined_root root, std::filesystem::path relative,
    bool in_vault, filesystem::entry file, filesystem::metadata revision,
    std::string original, legacy_format format):
  root_ (std::move (root)), relative_ (std::move (relative)), file_ (std::move (file)),
  revision_ (revision), original_ (std::move (original)), digest_ (sha256 (original_)), format_ (format) {
  if (in_vault)
    backup_= std::filesystem::path (".backup/format-migration/v1") / digest_ / relative_;
  else backup_= relative_.filename ().string () + ".pre-utf8-" + digest_;
}

legacy_file legacy_file::capture (const std::filesystem::path& source,
    const std::optional<std::filesystem::path>& vault, codec_limits limits) {
  const auto absolute= std::filesystem::canonical (source);
  auto root_path= absolute.parent_path ();
  bool in_vault= false;
  if (vault) {
    const auto candidate= std::filesystem::canonical (*vault);
    const auto relative= absolute.lexically_relative (candidate);
    in_vault= !relative.empty () && !relative.is_absolute () && *relative.begin () != "..";
    if (in_vault) root_path= candidate;
  }
  filesystem::confined_root root (root_path);
  const auto relative= absolute.lexically_relative (root_path);
  auto file= root.open (relative);
  const auto revision= file.stat ();
  auto original= file.read (limits.input_bytes);
  if (!filesystem::same_revision (revision, file.stat ()))
    throw std::system_error (EAGAIN, std::generic_category (), "Document changed during migration capture");
  const auto format= identify_legacy (original);
  return legacy_file (std::move (root), relative, in_vault, std::move (file), revision,
                      std::move (original), format);
}

upgrade_result legacy_file::commit (const tree& utf8_document, codec_limits limits) const {
  const auto current= root_.open (relative_);
  if (!current.same_object (file_) || !filesystem::same_revision (revision_, current.stat ()))
    throw std::system_error (ESTALE, std::generic_category (), "Legacy document changed since it was opened");
  std::vector<int> root_child_map;
  const auto document= strip_legacy_document_version (utf8_document, &root_child_map);
  const auto xml= write_xml (document, xml_kind::document, limits);
  // Never produce a document which the configured reader cannot load.
  if (read_xml (xml, xml_kind::document, limits) != document)
    throw codec_exception (codec_error::invalid_structure, "XML upgrade round-trip verification failed");
  const auto backup= root_.preserve (backup_, original_);
  if (sha256 (backup.read (limits.input_bytes)) != digest_)
    throw std::runtime_error ("Original document backup checksum mismatch");
  auto backup_path= backup.path ();
  auto original_digest= digest_;
  auto xml_digest= sha256 (xml);
  // Finish fallible result preparation before the irreversible rename.
  const auto replaced= root_.replace (relative_, file_, revision_, xml);
  return {std::move (backup_path), std::move (original_digest), std::move (xml_digest), replaced.file,
    replaced.directory_synced ? upgrade_durability::durable : upgrade_durability::replaced_not_durable,
    std::move (root_child_map)};
}
} // namespace athena::document

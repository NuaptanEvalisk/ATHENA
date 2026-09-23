/******************************************************************************
* MODULE     : document_file_codec.cpp
* DESCRIPTION: Unified read-only document format dispatch and semantic import
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************/

#include "document_file_codec.hpp"
#include "file.hpp"
#include <QCryptographicHash>

namespace athena::document {
namespace {

bool starts_with (std::string_view source, std::string_view prefix) {
  return source.size () >= prefix.size () &&
         source.substr (0, prefix.size ()) == prefix;
}

document_source_format classify_document (std::string_view source) {
  // UTF-8 BOM is valid XML input and QXmlStreamReader handles it. Legacy file
  // signatures remain exact so random bytes are never guessed as old content.
  std::string_view probe= source;
  if (starts_with (probe, "\xEF\xBB\xBF")) probe.remove_prefix (3);
  if (starts_with (probe, "<?xml") || starts_with (probe, "<athena-document"))
    return document_source_format::xml_v1;
  if (starts_with (source, "<TeXmacs|"))
    return document_source_format::legacy_markup;
  if (!source.empty () && source.front () == '(')
    return document_source_format::legacy_scheme;
  throw codec_exception (
    codec_error::invalid_structure, "Unrecognized ATHENA document format");
}

} // namespace

std::optional<document_path>
document_read_result::relocate_node (const document_path& path) const {
  if (!legacy ()) return path;
  legacy_document_result result {document, mappings};
  return result.relocate_node (path);
}

std::optional<document_position>
document_read_result::relocate (
  const document_path& path, std::size_t byte, boundary_affinity affinity) const {
  if (!legacy ()) return document_position {path, byte};
  legacy_document_result result {document, mappings};
  return result.relocate (path, byte, affinity);
}

document_read_result
decode_document_bytes (
  std::string_view source, const legacy_cork_table& table,
  legacy_import_limits limits, const legacy_slot_policy& policy) {
  const auto format= classify_document (source);
  if (format == document_source_format::xml_v1)
    return {format, read_xml (source, xml_kind::document, limits.codec), {}};
  auto imported= import_legacy_document_bytes (source, table, limits, policy);
  return {format, std::move (imported.document), std::move (imported.mappings)};
}

document_read_result
decode_document_bytes (
  std::string_view source, legacy_import_limits limits,
  const legacy_slot_policy& policy) {
  return decode_document_bytes (
    source, standard_legacy_cork_table (), limits, policy);
}

const legacy_cork_table&
standard_legacy_cork_table () {
  static const legacy_cork_table table= [] {
    const string path= concretize (url ("$ATHENA_PATH/langs/encoding"));
    if (N(path) == 0)
      throw std::runtime_error ("Cannot resolve canonical legacy encoding directory");
    return legacy_cork_table (
      std::string (path.data (), static_cast<std::size_t> (N(path))));
  } ();
  return table;
}

std::string
semantic_document_fingerprint (const tree& document, codec_limits limits) {
  const std::string canonical= write_xml (document, xml_kind::document, limits);
  const QByteArray digest= QCryptographicHash::hash (
    QByteArray (canonical.data (), (qsizetype) canonical.size ()),
    QCryptographicHash::Sha256).toHex ();
  return "sha256:" + std::string (digest.constData (), (std::size_t) digest.size ());
}

} // namespace athena::document

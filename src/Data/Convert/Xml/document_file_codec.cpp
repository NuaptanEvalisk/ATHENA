/******************************************************************************
* MODULE     : document_file_codec.cpp
* DESCRIPTION: Unified read-only document format dispatch and semantic import
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************/

#include "document_file_codec.hpp"
#include "file.hpp"
#include "node_metadata.hpp"
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
  if (starts_with (source, "<TeXmacs|"))
    return document_source_format::legacy_markup;
  while (!probe.empty () && (probe.front () == ' ' || probe.front () == '\t' ||
         probe.front () == '\n' || probe.front () == '\r')) probe.remove_prefix (1);
  if (!probe.empty () && probe.front () == '<')
    return document_source_format::xml_v1;
  if (!source.empty () && source.front () == '(')
    return document_source_format::legacy_scheme;
  throw codec_exception (
    codec_error::invalid_structure, "Unrecognized ATHENA document format");
}

document_read_result decode_xml_document (
    std::string_view source, codec_limits limits) {
  auto decoded= decode_native_xml_bytes (source, xml_kind::document, limits);
  switch (decoded.version) {
    case xml_format_version::v1:
      return {document_source_format::xml_v1,
              std::move (decoded.document), {}};
    case xml_format_version::v2:
      return {document_source_format::xml_v2,
              std::move (decoded.document), {}};
    case xml_format_version::v3:
      return {document_source_format::xml_v3,
              std::move (decoded.document), {}};
  }
  throw codec_exception (codec_error::unsupported_version, "Unsupported ATHENA XML version");
}

} // namespace

native_xml_read_result decode_native_xml_bytes (
    std::string_view source, xml_kind kind, codec_limits limits) {
  const auto version= inspect_xml_version (source, limits);
  switch (version) {
  case xml_format_version::v1: return {version, read_xml (source, kind, limits)};
  case xml_format_version::v2: return {version, read_xml_v2 (source, kind, limits)};
  case xml_format_version::v3: return {version, read_xml_v3 (source, kind, limits)};
  }
  throw codec_exception (codec_error::unsupported_version, "Unsupported ATHENA XML version");
}

std::string encode_native_xml_bytes (
    const tree& source, xml_format_version version, xml_kind kind, codec_limits limits) {
  switch (version) {
  case xml_format_version::v1: return write_xml (source, kind, limits);
  case xml_format_version::v2: return write_xml_v2 (source, kind, limits);
  case xml_format_version::v3: return write_xml_v3 (source, kind, limits);
  }
  throw codec_exception (codec_error::unsupported_version, "Unsupported ATHENA XML version");
}

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
    return decode_xml_document (source, limits.codec);
  auto imported= import_legacy_document_bytes (source, table, limits, policy);
  return {format, std::move (imported.document), std::move (imported.mappings)};
}

document_read_result
decode_document_bytes (
  std::string_view source, const std::filesystem::path& source_path,
  legacy_import_limits limits, const legacy_slot_policy& policy) {
  const auto format= classify_document (source);
  if (format == document_source_format::xml_v1)
    return decode_xml_document (source, limits.codec);
  legacy_import_context context;
  context.source_path= source_path;
  auto imported= import_legacy_document_bytes (
    source, standard_legacy_cork_table (), limits, policy, context);
  return {format, std::move (imported.document), std::move (imported.mappings)};
}

document_read_result
decode_document_bytes (
  std::string_view source, legacy_import_limits limits,
  const legacy_slot_policy& policy) {
  if (classify_document (source) == document_source_format::xml_v1)
    return decode_xml_document (source, limits.codec);
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
  const std::string canonical= athena::node::contains_metadata (document) ?
    write_xml_v2 (document, xml_kind::document, limits) :
    write_xml (document, xml_kind::document, limits);
  const QByteArray digest= QCryptographicHash::hash (
    QByteArray (canonical.data (), (qsizetype) canonical.size ()),
    QCryptographicHash::Sha256).toHex ();
  return "sha256:" + std::string (digest.constData (), (std::size_t) digest.size ());
}

std::string semantic_document_fingerprint (
    const tree& document, xml_format_version version, codec_limits limits) {
  if (version == xml_format_version::v1 || version == xml_format_version::v2)
    return semantic_document_fingerprint (document, limits);
  const auto canonical= encode_native_xml_bytes (document, version, xml_kind::document, limits);
  const auto digest= QCryptographicHash::hash (
    QByteArrayView (canonical.data (), canonical.size ()), QCryptographicHash::Sha256).toHex ();
  return "sha256:" + digest.toStdString ();
}

} // namespace athena::document

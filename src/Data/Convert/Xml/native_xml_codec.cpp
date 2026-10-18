/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "document_file_codec.hpp"

namespace athena::document {

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

} // namespace athena::document

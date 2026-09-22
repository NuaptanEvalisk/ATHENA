/******************************************************************************
* MODULE     : athena_document_xml.hpp
* DESCRIPTION: Versioned lossless UTF-8 XML document and tree-fragment codec
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include "tree.hpp"
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace athena::document {
enum class xml_kind { document, fragment };
enum class codec_error {
  malformed_xml, unsupported_version, invalid_structure, invalid_utf8,
  forbidden_dtd, resource_limit, invalid_base64, opaque_value
};
struct codec_limits {
  std::size_t input_bytes= 64 * 1024 * 1024;
  std::size_t output_bytes= 128 * 1024 * 1024;
  std::size_t text_bytes= 64 * 1024 * 1024;
  std::size_t nodes= 1000000;
  std::size_t depth= 256;
};
class codec_exception: public std::runtime_error {
public:
  codec_error code;
  std::int64_t line, column, character_offset;
  codec_exception (codec_error, const std::string&, std::int64_t line= -1,
                   std::int64_t column= -1, std::int64_t character_offset= -1);
};

// Input/output text is UTF-8, never Cork. RAW_DATA's sole child is bytes.
// The caller owns the tree and all codec state remains local to this call.
tree read_xml (std::string_view, xml_kind = xml_kind::document, codec_limits = {});
std::string write_xml (const tree&, xml_kind = xml_kind::document, codec_limits = {});

// Explicit envelope migration, not a recursive rewrite. Nested TeXmacs macros
// remain content. The optional old-root-child -> new-root-child map records
// removed metadata as -1 so persisted paths can be relocated without guessing.
tree strip_legacy_document_version (const tree&, std::vector<int>* child_map= nullptr);
} // namespace athena::document

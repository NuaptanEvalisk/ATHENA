/******************************************************************************
* MODULE     : rag_document_revision.hpp
* DESCRIPTION: Storage-format-aware semantic revisions for RAG documents
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/
#pragma once

#include "Data/Convert/Xml/document_file_codec.hpp"
#include <stdexcept>

namespace athena::rag {

inline std::string
document_semantic_revision (const document::document_read_result& decoded) {
  using document::document_source_format;
  using document::xml_format_version;
  switch (decoded.format) {
  case document_source_format::xml_v1:
    return document::semantic_document_fingerprint (
      decoded.document, xml_format_version::v1);
  case document_source_format::xml_v2:
    return document::semantic_document_fingerprint (
      decoded.document, xml_format_version::v2);
  case document_source_format::xml_v3:
    return document::semantic_document_fingerprint (
      decoded.document, xml_format_version::v3);
  case document_source_format::legacy_markup:
  case document_source_format::legacy_scheme:
    return document::semantic_document_fingerprint (decoded.document);
  }
  throw std::invalid_argument ("unsupported RAG document format");
}

} // namespace athena::rag

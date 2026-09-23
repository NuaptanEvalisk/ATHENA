/******************************************************************************
* MODULE     : document_file_codec.hpp
* DESCRIPTION: Unified read-only document format dispatch and semantic import
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/
#pragma once

#include "legacy_document_import.hpp"

namespace athena::document {

enum class document_source_format { xml_v1, legacy_markup, legacy_scheme };

struct document_read_result {
  document_source_format format;
  tree document;
  // Empty for native XML. Legacy source paths/bytes can be relocated exactly
  // through the same mappings returned by the semantic importer.
  std::vector<legacy_node_mapping> mappings;

  bool legacy () const { return format != document_source_format::xml_v1; }
  std::optional<document_path> relocate_node (const document_path&) const;
  std::optional<document_position> relocate (
    const document_path&, std::size_t byte, boundary_affinity) const;
};

// Format dispatch is signature based. This is a read-only operation: legacy
// input is migrated in memory and is never rewritten by this API.
document_read_result decode_document_bytes (
  std::string_view, const legacy_cork_table&, legacy_import_limits = {},
  const legacy_slot_policy& = {});
document_read_result decode_document_bytes (
  std::string_view, legacy_import_limits = {}, const legacy_slot_policy& = {});

// Application-wide immutable canonical Cork import table. It is only used by
// the legacy branch above; native XML never passes through Cork.
const legacy_cork_table& standard_legacy_cork_table ();

} // namespace athena::document

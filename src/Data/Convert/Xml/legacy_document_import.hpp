/******************************************************************************
* MODULE     : legacy_document_import.hpp
* DESCRIPTION: Detached legacy tree migration and exact source position mapping
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include "athena_document_xml.hpp"
#include "legacy_cork.hpp"
#include <functional>
#include <optional>

namespace athena::document {
using document_path= std::vector<int>;
struct document_position {
  document_path node;
  std::size_t offset;
};
struct legacy_position_span {
  std::size_t begin, end;
  document_position first, last;
  bool byte_identity= false;
};
class legacy_document_error: public legacy_text_error {
public:
  document_path node;
  legacy_document_error (document_path path, const legacy_text_error& error):
    legacy_text_error (error), node (std::move (path)) {}
};
struct legacy_node_mapping {
  document_path source;
  std::optional<document_path> destination;
  std::vector<legacy_position_span> spans;
};
enum class boundary_affinity { preceding, following };
struct legacy_import_limits {
  codec_limits codec;
  std::size_t positions= 4000000;
};
struct legacy_document_result {
  tree document;
  // Source paths are in lexicographic/preorder order, permitting indexed lookup.
  std::vector<legacy_node_mapping> mappings;
  std::optional<document_path> relocate_node (const document_path&) const;
  // Interior bytes of old character tokens are not legal cursor positions.
  // No nearest-position guessing when relocating persisted ranges.
  std::optional<document_position> relocate (
    const document_path&, std::size_t byte, boundary_affinity) const;
};

// Slot overrides describe a known macro's scalar or code arguments. The
// default uses standard DRD child types, not macro evaluation or expansion.
using legacy_slot_policy= std::function<legacy_text_role (
  const tree& parent, int child, legacy_text_role inherited)>;
legacy_document_result import_legacy_document (
  const tree&, const legacy_cork_table&, legacy_import_limits = {},
  const legacy_slot_policy& = {});
// Dispatch by an explicit legacy file signature, never by byte plausibility.
legacy_document_result import_legacy_document_bytes (
  std::string_view, const legacy_cork_table&, legacy_import_limits = {},
  const legacy_slot_policy& = {});
} // namespace athena::document

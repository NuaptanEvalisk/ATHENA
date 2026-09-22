/******************************************************************************
* MODULE     : legacy_cork.hpp
* DESCRIPTION: Lossless Cork import with explicit field roles and byte mappings
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once

#include <array>
#include <cstddef>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace athena::document {

// Identifiers preserve angle-bracket spellings: these are identity, not glyphs.
// Code decodes character escapes but never performs typographic substitutions.
enum class legacy_text_role { content, identifier, code, scalar };
enum class legacy_piece_kind { text, named_symbol };

struct legacy_text_piece {
  legacy_piece_kind kind;
  std::string value; // UTF-8 text, or namespaced symbol identity.
  std::size_t begin, end; // Half-open range in original Cork bytes.
  bool byte_identity; // ASCII run; every interior byte remains a valid boundary.
};

class legacy_text_error: public std::runtime_error {
public:
  std::size_t byte;
  legacy_text_error (std::size_t position, const std::string& message);
};

// Immutable after loading, hence safely shared between detached import jobs.
// No use of the lossy Cork/Strict-Cork display or export converters.
class legacy_cork_table {
  std::array<std::string, 256> bytes_;
  std::map<std::string, std::string> symbols_;
  std::map<std::string, std::string> sequences_;
public:
  explicit legacy_cork_table (const std::string& encoding_directory);
  std::vector<legacy_text_piece> decode (
    std::string_view source, legacy_text_role role,
    std::size_t output_limit= 64 * 1024 * 1024,
    std::size_t piece_limit= 4000000) const;
};

} // namespace athena::document

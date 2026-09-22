/******************************************************************************
* MODULE     : unicode_text.hpp
* DESCRIPTION: Strict UTF-8 positions and ICU grapheme boundaries
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once

#include <cstddef>
#include <memory>
#include <string_view>

namespace athena::text {

bool valid_utf8 (std::string_view text) noexcept;
void require_utf8 (std::string_view text);
// Scalar navigation assumes text was validated at its ingestion boundary.
// These hot-path operations check the position/local scalar, not the whole leaf.
bool scalar_boundary (std::string_view text, std::size_t byte) noexcept;
std::size_t next_scalar (std::string_view text, std::size_t byte);
std::size_t previous_scalar (std::string_view text, std::size_t byte);
std::size_t byte_to_utf16 (std::string_view text, std::size_t byte);
std::size_t utf16_to_byte (std::string_view text, std::size_t units);
std::size_t byte_to_codepoint (std::string_view text, std::size_t byte);
std::size_t codepoint_to_byte (std::string_view text, std::size_t points);

// The caller owns the immutable bytes for this cursor's lifetime. Rebind after
// an edit; retain the cursor while navigating the same text revision. Each
// owner uses its own iterator, never a shared mutable ICU iterator.
class grapheme_cursor {
  struct implementation;
  std::unique_ptr<implementation> impl_;
public:
  explicit grapheme_cursor (std::string_view text);
  ~grapheme_cursor ();
  grapheme_cursor (const grapheme_cursor&) = delete;
  grapheme_cursor& operator= (const grapheme_cursor&) = delete;
  void reset (std::string_view text);
  bool boundary (std::size_t byte);
  std::size_t next (std::size_t byte);
  std::size_t previous (std::size_t byte);
};

} // namespace athena::text

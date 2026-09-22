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
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace athena::text {

// Which logical side of a text boundary supplies its visual caret. `both`
// denotes coincident positions in layout, not a third editing position.
enum class caret_affinity { upstream, downstream, both };

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

enum class paragraph_direction { automatic_ltr, automatic_rtl, ltr, rtl };

struct line_break {
  std::size_t byte;
  bool mandatory;
};

struct bidi_run {
  std::size_t begin, end; // Absolute UTF-8 bytes, in logical source order.
  std::uint8_t level;
  bool right_to_left () const { return (level & 1) != 0; }
};

struct shaping_item {
  bidi_run run;
  std::string script; // ISO 15924 tag, ready for HarfBuzz shaping options.
};

// Analyze one Unicode paragraph once, then resolve each chosen line separately
// (UBA rule L1 changes trailing whitespace levels at line boundaries).
// Borrow immutable UTF-8 bytes; the necessary ICU UTF-16 copy and all mutable
// ICU objects are private to this owner. No normalization or reordering of the
// source text takes place. Reconstruct after edits; do not share across threads.
class unicode_paragraph {
  struct implementation;
  std::unique_ptr<implementation> impl_;
public:
  explicit unicode_paragraph (std::string_view text,
    paragraph_direction direction= paragraph_direction::automatic_ltr,
    std::string_view locale= "root", std::size_t max_bytes= 16 * 1024 * 1024);
  ~unicode_paragraph ();
  unicode_paragraph (const unicode_paragraph&) = delete;
  unicode_paragraph& operator= (const unicode_paragraph&) = delete;

  std::uint8_t base_level () const;
  std::string_view source () const;
  const std::vector<line_break>& breaks () const;
  // Exact scalar-boundary conversions, including an explicit rejection of
  // surrogate interiors. Repeated line queries do not rescan text prefixes.
  std::size_t byte_to_utf16 (std::size_t byte) const;
  std::size_t utf16_to_byte (std::size_t units) const;
  // Visual run order, but each run retains its logical byte range. Caller still
  // itemizes scripts/fonts. Bidi run boundaries are NOT editing caret stops.
  // Line endpoints must be grapheme boundaries, without an interior hard break;
  // emergency wrapping may choose a boundary absent from breaks().
  std::vector<bidi_run> line (std::size_t begin, std::size_t end);
  // Intersect visual bidi runs with Pango script ranges. Each item still needs
  // font selection/fallback; rendering never guesses one script for a paragraph.
  std::vector<shaping_item> items (std::size_t begin, std::size_t end);
};

} // namespace athena::text

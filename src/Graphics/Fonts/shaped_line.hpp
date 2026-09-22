/******************************************************************************
* MODULE     : shaped_line.hpp
* DESCRIPTION: Visual UTF-8 line runs and affinity-aware logical caret positions
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once

#include "shaped_text.hpp"
#include "unicode_text.hpp"
#include <functional>

namespace athena::text {

enum class caret_affinity { upstream, downstream, both };

struct line_caret {
  std::size_t byte;
  SI x;
  caret_affinity affinity;
};

struct placed_text {
  shaped_text text;
  SI x;
};

struct line_selection_span { SI left, right; };

// Retains font-domain-owned runs, not a copy of the source. A byte position at
// a bidi boundary may have distinct upstream/downstream visual coordinates.
struct shaped_line {
  std::vector<placed_text> runs; // Visual left-to-right order.
  std::vector<line_caret> carets; // Logical byte order, including affinities.
  std::size_t byte_begin= 0, byte_end= 0;
  SI advance= 0;
  bool missing_glyphs= false;

  SI caret_x (std::size_t byte, caret_affinity affinity) const;
  line_caret hit_test (SI x, bool prefer_right= true) const;
  std::vector<line_selection_span> selection_spans (std::size_t begin,
                                                  std::size_t end) const;
  void draw_fixed (renderer ren, std::string_view source, SI x, SI y) const;
};

// The caller supplies font selection; this layer never silently substitutes a
// font or guesses a script. Analysis is reused across wrapped lines. Aggregate
// glyph/caret budgets apply to the whole line, not independently to each run.
using item_shaper= std::function<shaped_text (
  std::string_view, const shaping_item&, const shaping_options&)>;
// Internal, strictly increasing scalar offsets separating selected fonts.
// ICU retains control of bidi order and published grapheme caret positions.
using item_splitter= std::function<std::vector<std::size_t> (const shaping_item&)>;
shaped_line shape_line (unicode_paragraph& paragraph,
  std::size_t begin, std::size_t end, const item_shaper& shape,
  const shaping_options& options= {}, const item_splitter& split= {});

} // namespace athena::text

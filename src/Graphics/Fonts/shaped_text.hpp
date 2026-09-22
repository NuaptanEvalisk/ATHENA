/******************************************************************************
* MODULE     : shaped_text.hpp
* DESCRIPTION: Explicit UTF-8 font runs with byte clusters and positioned glyphs
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once

#include "renderer.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace athena::text {

enum class run_direction { left_to_right, right_to_left };

struct shaping_options {
  run_direction direction= run_direction::left_to_right;
  // An ISO 15924 script tag, or empty to infer it from this homogeneous run.
  std::string script;
  std::string language= "und";
  bool ligatures= true;
  std::size_t max_glyphs= 1000000;
  bool editing_carets= false;
  // Paragraph itemization can split a grapheme across scripts/directions.
  // In that case expose only genuine global boundaries, never item endpoints.
  // A standalone editable text box must leave this disabled.
  bool grapheme_fragments= false;
  std::size_t max_carets= 1000000;
  // Shaping context is the containing line, which may be smaller than the
  // original source atom. Output positions remain absolute source byte offsets.
  std::size_t context_begin= 0;
  std::size_t context_end= std::string_view::npos;
};

struct positioned_glyph {
  std::uint32_t index;
  std::size_t byte; // Absolute byte offset in the input text, not a caret stop.
  SI x, y;
  SI advance_x, advance_y;
  bool unsafe_to_break;
};

struct text_caret {
  std::size_t byte;
  SI x;
};

// Like other font/box resources, a run is confined to its font domain and must
// be consumed before that domain dies. Renderer recording owns emitted pixels.
struct shaped_text {
  std::vector<positioned_glyph> glyphs;
  // Logical byte order. Present only when editing_carets was requested;
  // constructing it requires the item endpoints to be grapheme boundaries.
  std::vector<text_caret> carets;
  font_glyphs glyph_source;
  std::size_t byte_begin= 0, byte_end= 0;
  run_direction direction= run_direction::left_to_right;
  SI advance_x= 0, advance_y= 0;
  SI ink_x1= 0, ink_y1= 0, ink_x2= 0, ink_y2= 0;
  bool has_ink= false;
  bool missing_glyphs= false;

  // Supply the same immutable input used for shaping (including context).
  // Borrow it during drawing instead of copying each leaf into every run.
  void draw_fixed (renderer ren, std::string_view source, SI x, SI y) const;
  SI caret_x (std::size_t byte) const;
  std::size_t hit_test (SI x, bool prefer_right= true) const;
};

// Shape an already itemized, single-font/script/direction run. Surrounding text
// supplies joining context; no paragraph bidi or font fallback is guessed here.
// Font size is in points; all output coordinates use renderer SI units.
shaped_text shape_freetype_utf8 (
  string family, int size, int hdpi, int vdpi, std::string_view text,
  std::size_t begin, std::size_t end, const shaping_options& options= {});

} // namespace athena::text

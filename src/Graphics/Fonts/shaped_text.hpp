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
#include "font_source.hpp"
#include "math_alphabet.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace athena::text {

enum class run_direction { left_to_right, right_to_left };
enum class math_kern_corner { top_right, top_left, bottom_right, bottom_left };

constexpr std::uint32_t
open_type_tag (char a, char b, char c, char d) {
  return (static_cast<std::uint32_t> (static_cast<unsigned char> (a)) << 24) |
         (static_cast<std::uint32_t> (static_cast<unsigned char> (b)) << 16) |
         (static_cast<std::uint32_t> (static_cast<unsigned char> (c)) << 8) |
          static_cast<std::uint32_t> (static_cast<unsigned char> (d));
}

struct open_type_feature {
  std::uint32_t tag= 0;
  std::uint32_t value= 1;
  bool operator== (const open_type_feature& other) const {
    return tag == other.tag && value == other.value;
  }
};

struct native_text_source {
  physical_font_source physical;
  std::vector<open_type_feature> features;
};

struct shaping_options {
  run_direction direction= run_direction::left_to_right;
  // An ISO 15924 script tag, or empty to infer it from this homogeneous run.
  std::string script;
  std::string language= "und";
  bool ligatures= true;
  bool invisible= false;
  std::vector<open_type_feature> features;
  math_alphabet math_variant= math_alphabet::normal;
  unsigned int math_script_level= 0; // OpenType ssty: 0 normal, 1 script, 2 scriptscript.
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

struct bitmap_text_glyph {
  picture pixels;
  SI left= 0, bottom= 0, width= 0, height= 0;
  bool intrinsic_color= false;
  void draw (renderer ren, SI x, SI y) const;
};

// OpenType MATH values for a single shaped glyph, in the run's SI coordinates.
// Absent means the selected physical face has no MATH table (not a zero value).
struct shaped_math_metrics {
  SI italic_correction;
  SI top_accent_attachment;
  physical_font_source source;
  std::uint32_t glyph_index= 0;
};

struct math_font_metrics {
  int script_percent_scale_down= 100;
  int script_script_percent_scale_down= 100;
  SI delimited_sub_formula_min_height= 0;
  SI display_operator_min_height= 0;
  SI math_leading= 0;
  SI axis_height= 0;
  SI subscript_shift_down= 0;
  SI subscript_top_max= 0;
  SI subscript_baseline_drop_min= 0;
  SI superscript_shift_up= 0;
  SI superscript_shift_up_cramped= 0;
  SI superscript_bottom_min= 0;
  SI superscript_baseline_drop_max= 0;
  SI sub_superscript_gap_min= 0;
  SI superscript_bottom_max_with_subscript= 0;
  SI space_after_script= 0;
  SI upper_limit_gap_min= 0;
  SI upper_limit_baseline_rise_min= 0;
  SI lower_limit_gap_min= 0;
  SI lower_limit_baseline_drop_min= 0;
  SI fraction_numerator_shift_up= 0;
  SI fraction_numerator_display_shift_up= 0;
  SI fraction_denominator_shift_down= 0;
  SI fraction_denominator_display_shift_down= 0;
  SI fraction_numerator_gap_min= 0;
  SI fraction_numerator_display_gap_min= 0;
  SI fraction_rule_thickness= 0;
  SI fraction_denominator_gap_min= 0;
  SI fraction_denominator_display_gap_min= 0;
  SI radical_vertical_gap= 0;
  SI radical_display_vertical_gap= 0;
  SI radical_rule_thickness= 0;
  SI radical_extra_ascender= 0;
  SI radical_kern_before_degree= 0;
  SI radical_kern_after_degree= 0;
  int radical_degree_bottom_raise_percent= 0;
};

// Like other font/box resources, a run is confined to its font domain and must
// be consumed before that domain dies. Renderer recording owns emitted pixels.
struct shaped_text {
  std::vector<positioned_glyph> glyphs;
  // Logical byte order. Present only when editing_carets was requested;
  // constructing it requires the item endpoints to be grapheme boundaries.
  std::vector<text_caret> carets;
  font_glyphs glyph_source;
  // Fixed-strike fonts have pixels instead of outline glyph resources.
  // When present this vector has the same order and size as glyphs.
  std::vector<bitmap_text_glyph> bitmaps;
  std::size_t byte_begin= 0, byte_end= 0;
  run_direction direction= run_direction::left_to_right;
  SI advance_x= 0, advance_y= 0;
  SI ink_x1= 0, ink_y1= 0, ink_x2= 0, ink_y2= 0;
  bool has_ink= false;
  bool missing_glyphs= false;
  std::optional<shaped_math_metrics> math;

  // Supply the same immutable input used for shaping (including context).
  // Borrow it during drawing instead of copying each leaf into every run.
  void draw_fixed (renderer ren, std::string_view source, SI x, SI y) const;
  SI caret_x (std::size_t byte) const;
  std::size_t hit_test (SI x, bool prefer_right= true) const;
};

struct math_stretch_result {
  shaped_text run;
  SI extent= 0;
  bool assembled= false;
};

// Shape an already itemized, single-font/script/direction run. Surrounding text
// supplies joining context; no paragraph bidi or font fallback is guessed here.
// Font size is in points; all output coordinates use renderer SI units.
shaped_text shape_freetype_utf8 (
  string family, int size, int hdpi, int vdpi, std::string_view text,
  std::size_t begin, std::size_t end, const shaping_options& options= {});
shaped_text shape_freetype_utf8 (
  const font_file_source& source, int size, int hdpi, int vdpi, std::string_view text,
  std::size_t begin, std::size_t end, const shaping_options& options= {});
bool open_type_has_substitution_feature (
  const physical_font_source& source, std::uint32_t tag);

std::optional<math_font_metrics> open_type_math_metrics (
  const physical_font_source& source);
SI open_type_math_kern (const shaped_math_metrics& glyph, math_kern_corner corner,
                        SI correction_height);
std::optional<math_stretch_result> shape_open_type_math_stretch (
  const physical_font_source& source, std::string_view scalar, SI target_extent,
  bool vertical= true);

} // namespace athena::text

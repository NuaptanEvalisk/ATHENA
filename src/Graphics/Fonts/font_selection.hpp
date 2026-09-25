/******************************************************************************
* MODULE     : font_selection.hpp
* DESCRIPTION: Owner-private font fallback for Unicode paragraphs and lines
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once

#include "shaped_line.hpp"
#include "font_domain.hpp"
#include <memory>

namespace athena::text {

struct font_request {
  physical_font_source primary;
  font_fallback_policy fallback;
  std::string language= "und";
  paragraph_direction direction= paragraph_direction::automatic_ltr;
  math_alphabet math_variant= math_alphabet::normal;
  std::vector<open_type_feature> features;
};

struct selected_font_run {
  std::size_t begin, end;
  font_file_source font;
  int point_size;
  std::string language;
  int horizontal_dpi, vertical_dpi;
  math_alphabet math_variant= math_alphabet::normal;
  std::vector<open_type_feature> features;
};

font_request font_request_from_source (const physical_font_source& source,
                                       std::string language= "und");
// Preserve weight, width, variations and device scale; select a real face with
// the requested slant through ATHENA's shared font database.
font_request font_request_with_italic (font_request request, bool italic);

// Sorted, nonoverlapping scalar ranges. Gaps use the paragraph's base request.
// Paragraph direction must match that base request. Device resolution can
// differ for profile font-size adjustment without creating another paragraph.
struct font_style_span {
  std::size_t begin, end;
  font_request request;
};

// Owner-local selector/cache over the shared immutable ATHENA font database.
// It never discovers fonts or owns platform font-catalog objects.
class font_catalog {
  struct impl;
  std::unique_ptr<impl> state_;
public:
  font_catalog ();
  ~font_catalog ();
  font_catalog (const font_catalog&)= delete;
  font_catalog& operator= (const font_catalog&)= delete;
  std::vector<selected_font_run> select (const std::string& source,
                                        const font_request& request,
                                        std::uint8_t base_level= 0,
                                        const std::vector<font_style_span>& styles= {},
                                        const std::vector<script_run>* scripts= nullptr);
};

font_catalog& current_font_catalog ();

// One immutable source allocation per paragraph. Analysis borrows it; wrapped
// lines reuse font selection, ICU state and absolute byte positions.
class font_paragraph {
  std::string source_;
  unicode_paragraph analysis_;
  font_request request_;
  std::vector<font_style_span> styles_;
  std::vector<selected_font_run> fonts_;
  font_domain* owner_;
public:
  font_paragraph (std::string source, font_request request,
                  font_catalog& catalog= current_font_catalog ());
  font_paragraph (std::string source, font_request request,
                  const std::vector<font_style_span>& styles,
                  font_catalog& catalog= current_font_catalog ());
  font_paragraph (const font_paragraph&)= delete;
  font_paragraph& operator= (const font_paragraph&)= delete;
  unicode_paragraph& analysis ();
  const font_request& request () const { return request_; }
  const std::vector<font_style_span>& styles () const { return styles_; }
  const std::vector<selected_font_run>& fonts () const { return fonts_; }
  // Additional scalar run boundaries (for example paint changes) share the
  // same bidi analysis, font selection and surrounding shaping context.
  shaped_line line (std::size_t begin, std::size_t end,
                    const shaping_options& options= {}, double horizontal_scale= 1.0,
                    const item_splitter& split= {});
};

} // namespace athena::text

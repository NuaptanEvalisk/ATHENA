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
  std::string description_utf8; // Pango family/style description, not a filename.
  std::string language= "und";
  int point_size= 12, horizontal_dpi= 96, vertical_dpi= 96;
  paragraph_direction direction= paragraph_direction::automatic_ltr;
};

struct selected_font_run {
  std::size_t begin, end;
  font_file_source font;
  int point_size;
  std::string language;
};

font_request font_request_from_source (const physical_font_source& source,
                                       std::string language= "und");

// Sorted, nonoverlapping scalar ranges. Gaps use the paragraph's base request.
// Device resolution and paragraph direction must match that base request;
// changing language/family/size does not create another Unicode paragraph.
struct font_style_span {
  std::size_t begin, end;
  font_request request;
};

// A private Fontconfig configuration and PangoFT2 map. No Qt/GTK font objects
// or process-global current configuration; use only on its owning font domain.
class font_catalog {
  struct impl;
  std::unique_ptr<impl> state_;
public:
  explicit font_catalog (bool system_fonts= true,
                         const std::vector<std::string>& files= {},
                         const std::vector<std::string>& directories= {});
  ~font_catalog ();
  font_catalog (const font_catalog&)= delete;
  font_catalog& operator= (const font_catalog&)= delete;
  std::vector<selected_font_run> select (const std::string& source,
                                       const font_request& request,
                                       std::uint8_t base_level= 0,
                                       const std::vector<font_style_span>& styles= {});
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

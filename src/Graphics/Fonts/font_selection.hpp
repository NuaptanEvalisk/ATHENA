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
                                       std::uint8_t base_level= 0);
};

font_catalog& current_font_catalog ();

// One immutable source allocation per paragraph. Analysis borrows it; wrapped
// lines reuse font selection, ICU state and absolute byte positions.
class font_paragraph {
  std::string source_;
  unicode_paragraph analysis_;
  font_request request_;
  std::vector<selected_font_run> fonts_;
  font_domain* owner_;
public:
  font_paragraph (std::string source, font_request request,
                  font_catalog& catalog= current_font_catalog ());
  font_paragraph (const font_paragraph&)= delete;
  font_paragraph& operator= (const font_paragraph&)= delete;
  unicode_paragraph& analysis ();
  const std::vector<selected_font_run>& fonts () const { return fonts_; }
  shaped_line line (std::size_t begin, std::size_t end,
                    const shaping_options& options= {});
};

} // namespace athena::text

/******************************************************************************
* MODULE     : persistent_shaping_cache.hpp
* DESCRIPTION: Disposable, cross-session Unicode shaping results in LMDB
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/
#pragma once

#include "Rendering/Fonts/shaped_text.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace athena::text {

// Only value data crosses the persistence boundary: no font_domain resources,
// Qt pictures, glyph-source pointers, ICU objects or process-owned boxes.
struct cached_shaping_run {
  std::vector<positioned_glyph> glyphs;
  std::vector<text_caret> carets;
  std::size_t byte_begin= 0, byte_end= 0;
  run_direction direction= run_direction::left_to_right;
  SI advance_x= 0, advance_y= 0;
  SI ink_x1= 0, ink_y1= 0, ink_x2= 0, ink_y2= 0;
  bool has_ink= false, missing_glyphs= false;
  bool has_math= false;
  SI italic_correction= 0, top_accent_attachment= 0;
  std::uint32_t math_glyph= 0;
};

struct shaping_cache_key {
  std::string digest;
  bool math= false;
};

// Key construction includes the physical face/instance, file revision, actual
// source/context and every layout-affecting shaping option. No key is returned
// for large runs, which remain on the usual shaping path.
std::optional<shaping_cache_key> make_shaping_cache_key (
  const font_file_source&, int point_size, int hdpi, int vdpi,
  std::string_view text, std::size_t begin, std::size_t end,
  const shaping_options&);

// No readiness wait. LMDB is opened in a background worker. A read transaction
// lasts only through decoding and never escapes into a layout box.
std::optional<cached_shaping_run> persistent_shaping_lookup (
  const shaping_cache_key&, const shaping_options&);
void persistent_shaping_submit (const shaping_cache_key&, const shaped_text&,
                                unsigned corpus_frequency= 1);

// The CLI builder uses this barrier at the end of a shaping process. It does
// not run on the interactive editor's layout thread.
void persistent_shaping_flush ();

} // namespace athena::text

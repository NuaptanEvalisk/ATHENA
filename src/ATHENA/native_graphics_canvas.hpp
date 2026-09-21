/******************************************************************************
* MODULE     : native_graphics_canvas.hpp
* DESCRIPTION: Native graphics canvas commands shared across editor surfaces
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* See the file LICENSE in the root directory.
******************************************************************************/

#ifndef ATHENA_NATIVE_GRAPHICS_CANVAS_HPP
#define ATHENA_NATIVE_GRAPHICS_CANVAS_HPP

#include <cstdint>

enum class native_graphics_canvas_action: std::uint8_t {
  set_width= 0,
  set_height,
  set_geo_valign,
  set_extents,
  set_unit,
  set_origin,
  toggle_auto_crop,
  set_crop_padding,
  zoom,
  set_zoom,
  move_origin,
  change_extents,
  change_geo_valign
};

#endif

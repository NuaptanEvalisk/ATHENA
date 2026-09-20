/******************************************************************************
* MODULE     : native_ink.hpp
* DESCRIPTION: Detached native drawing input shared by Qt and BufferActor
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#ifndef ATHENA_NATIVE_INK_HPP
#define ATHENA_NATIVE_INK_HPP

#include "basic.hpp"

#include <cstddef>
#include <cstdint>

enum class native_drawing_tool: std::uint8_t {
  pen= 0,
  highlighter,
  object_eraser,
  segment_eraser,
  lasso
};

struct native_drawing_selection_box {
  SI x1= 0;
  SI y1= 0;
  SI x2= 0;
  SI y2= 0;
};

struct native_ink_sample {
  SI x= 0;
  SI y= 0;
  double time= 0.0;
  double pressure= 1.0;
  double rotation= 0.0;
  double x_tilt= 0.0;
  double y_tilt= 0.0;
  double tangential_pressure= 0.0;
};

struct native_ink_interaction_snapshot {
  SI x1= 0;
  SI y1= 0;
  SI x2= 0;
  SI y2= 0;
  std::uint32_t rgba= 0xff000000U;
  double line_width_pixels= 1.0;
  double eraser_radius_pixels= 8.0;
  bool pen_enabled= false;
  bool pressure_enabled= true;
  native_drawing_tool tool= native_drawing_tool::pen;
};

struct native_ink_preview_style {
  std::uint32_t rgba= 0xff000000U;
  double line_width_pixels= 1.0;
  double eraser_radius_pixels= 8.0;
  bool pressure_enabled= true;
  native_drawing_tool tool= native_drawing_tool::pen;
};

#endif // defined ATHENA_NATIVE_INK_HPP

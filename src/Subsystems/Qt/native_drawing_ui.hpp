/******************************************************************************
* MODULE     : native_drawing_ui.hpp
* DESCRIPTION: Shared native drawing command descriptors for Qt surfaces
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#ifndef NATIVE_DRAWING_UI_HPP
#define NATIVE_DRAWING_UI_HPP

#include "ATHENA/native_ink.hpp"

struct native_drawing_tool_descriptor {
  native_drawing_tool tool;
  const char* text;
  const char* icon;
};

struct native_drawing_shape_descriptor {
  native_drawing_shape shape;
  const char* text;
  const char* icon;
};

enum class native_drawing_canvas_command: std::uint8_t {
  insert_horizontal_space= 0,
  insert_vertical_space,
  trim
};

struct native_drawing_canvas_command_descriptor {
  native_drawing_canvas_command command;
  const char* text;
  const char* icon;
};

inline constexpr native_drawing_tool_descriptor native_drawing_tools[]= {
  {native_drawing_tool::pen, "Pen", "tm_native_pen"},
  {native_drawing_tool::highlighter, "Highlighter", "tm_native_highlighter"},
  {native_drawing_tool::object_eraser, "Object eraser", "tm_native_object_eraser"},
  {native_drawing_tool::segment_eraser, "Segment eraser", "tm_native_segment_eraser"},
  {native_drawing_tool::lasso, "Lasso", "tm_native_lasso"},
  {native_drawing_tool::shape, "Shape", "tm_native_shape"},
  {native_drawing_tool::text, "Text", "tm_native_text"},
  {native_drawing_tool::math, "Mathematics", "tm_native_math"}
};

inline constexpr native_drawing_shape_descriptor native_drawing_shapes[]= {
  {native_drawing_shape::line, "Line", "tm_native_shape_line"},
  {native_drawing_shape::square, "Square", "tm_native_shape_rectangle"},
  {native_drawing_shape::rectangle, "Rectangle", "tm_native_shape_rectangle"},
  {native_drawing_shape::circle, "Circle", "tm_native_shape_circle"},
  {native_drawing_shape::ellipse, "Ellipse", "tm_native_shape_ellipse"},
  {native_drawing_shape::triangle, "Triangle", "tm_native_shape_triangle"},
  {native_drawing_shape::right_triangle, "Right triangle", "tm_native_shape_right_triangle"},
  {native_drawing_shape::pentagon, "Pentagon", "tm_native_shape_pentagon"},
  {native_drawing_shape::hexagon, "Hexagon", "tm_native_shape_hexagon"},
  {native_drawing_shape::arrow, "Arrow", "tm_native_shape_arrow"},
  {native_drawing_shape::double_arrow, "Double arrow", "tm_native_shape_double_arrow"},
  {native_drawing_shape::orthogonal_polyline, "Orthogonal polyline", "tm_native_shape_polyline"}
};

inline constexpr native_drawing_canvas_command_descriptor
native_drawing_canvas_commands[]= {
  {native_drawing_canvas_command::insert_horizontal_space,
   "Insert horizontal space", "tm_native_hspace"},
  {native_drawing_canvas_command::insert_vertical_space,
   "Insert vertical space", "tm_native_vspace"},
  {native_drawing_canvas_command::trim, "Trim", "tm_native_trim"}
};

#endif // defined NATIVE_DRAWING_UI_HPP

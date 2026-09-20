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

inline constexpr native_drawing_tool_descriptor native_drawing_tools[]= {
  {native_drawing_tool::pen, "Pen", "draw-freehand"},
  {native_drawing_tool::highlighter, "Highlighter", "draw-highlight"},
  {native_drawing_tool::object_eraser, "Object eraser", "edit-delete"},
  {native_drawing_tool::segment_eraser, "Segment eraser", "draw-eraser"},
  {native_drawing_tool::lasso, "Lasso", "edit-select"},
  {native_drawing_tool::shape, "Shape", "draw-rectangle"}
};

inline constexpr native_drawing_shape_descriptor native_drawing_shapes[]= {
  {native_drawing_shape::line, "Line", "draw-line"},
  {native_drawing_shape::square, "Square", "draw-rectangle"},
  {native_drawing_shape::rectangle, "Rectangle", "draw-rectangle"},
  {native_drawing_shape::circle, "Circle", "draw-ellipse"},
  {native_drawing_shape::ellipse, "Ellipse", "draw-ellipse"},
  {native_drawing_shape::triangle, "Triangle", "draw-polygon"},
  {native_drawing_shape::right_triangle, "Right triangle", "draw-polygon"},
  {native_drawing_shape::pentagon, "Pentagon", "draw-polygon"},
  {native_drawing_shape::hexagon, "Hexagon", "draw-polygon"},
  {native_drawing_shape::arrow, "Arrow", "draw-arrow"},
  {native_drawing_shape::double_arrow, "Double arrow", "draw-arrow"},
  {native_drawing_shape::orthogonal_polyline, "Orthogonal polyline", "draw-polyline"}
};

#endif // defined NATIVE_DRAWING_UI_HPP

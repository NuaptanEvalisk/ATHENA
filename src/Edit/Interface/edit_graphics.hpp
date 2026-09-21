
/******************************************************************************
* MODULE     : edit_graphics.hpp
* DESCRIPTION: the interface for TeXmacs
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef EDIT_GRAPHICS_H
#define EDIT_GRAPHICS_H
#include "editor.hpp"
#include "tm_timer.hpp"
#include <vector>

class edit_graphics_rep: virtual public editor_rep {
private:
  box go_box;           // The graphical object typesetted as a box
  double p_x, p_y;      // Last unadjusted (x, y) position
  double gr_x, gr_y;    // Last (x, y) position of the mouse
  gr_selections gs;     // Last graphical_select (x, y)
  grid gr0;             // Last grid
  std::vector<path> native_ink_paths_;
  std::vector<path> native_drawing_selection_paths_;
  native_drawing_tool native_drawing_tool_= native_drawing_tool::pen;
  native_drawing_shape native_drawing_shape_= native_drawing_shape::line;
  bool native_drawing_color_override_= false;
  std::uint32_t native_drawing_rgba_= 0xff000000U;
  bool native_drawing_width_override_= false;
  double native_drawing_width_pixels_= 1.0;
  bool native_drawing_pressure_enabled_= true;
  bool native_drawing_snap_enabled_= true;
  bool native_ink_interaction_dirty_= true;

protected:
  point cur_pos;
  tree graphical_object;

public:
  edit_graphics_rep ();
  ~edit_graphics_rep ();

  path   graphics_path ();
  bool   inside_graphics (bool b);
  bool   inside_active_graphics (bool b);
  bool   over_graphics (SI x, SI y);
  tree   get_graphics ();
  double get_x ();
  double get_y ();
  double get_pixel ();
  frame  find_frame (bool last= false);
  grid   find_grid ();
  void   find_limits (point& lim1, point& lim2);
  bool   find_graphical_region (SI& x1, SI& y1, SI& x2, SI& y2);
  point  adjust (point p);
  tree   find_point (point p);
  tree   graphical_select (double x, double y);
  tree   graphical_select (double x1, double y1, double x2, double y2);
  tree   get_graphical_object ();
  void   set_graphical_object (tree t);
  void   invalidate_graphical_object ();
  void   draw_graphical_object (renderer ren);
  bool   mouse_graphics (string s, SI x, SI y, int m, time_t t, array<double> d);
  void   refresh_native_ink_interaction ();
  void   mark_native_ink_interaction_dirty ();
  native_drawing_tool get_native_drawing_tool () const;
  void   set_native_drawing_tool (native_drawing_tool tool);
  void   set_native_drawing_property (
    native_drawing_property property, std::uint64_t value);
  void   commit_native_drawing_gesture (
    native_drawing_tool tool, const native_ink_sample* samples,
    std::size_t count);
  void   commit_native_drawing_shape (
    native_drawing_shape shape,
    const native_ink_sample* samples, std::size_t count);
  void   commit_native_drawing_text (
    bool math, const native_ink_sample* samples, std::size_t count);
  void   commit_native_drawing_transform (
    native_drawing_transform transform,
    const native_ink_sample* samples, std::size_t count);
  void   commit_native_drawing_insert_space (
    bool horizontal, const native_ink_sample* samples,
    std::size_t count);
  void   commit_native_drawing_trim ();
  void   commit_native_ink_stroke (const native_ink_sample* samples,
                                   std::size_t count);
  void   collect_native_ink_graphics (tree t, path p, bool in_diagram,
                                      std::vector<path>& result);
  tree   native_ink_property (path graphics, string name, tree fallback);
  tree   native_drawing_object_property (tree object, string name, tree fallback);
  tree   native_drawing_set_object_property (tree object, string name, tree value);
  bool   native_drawing_set_graphics_property (path graphics, string name,
                                               tree value);
  bool   native_drawing_set_graphics_properties (
    path graphics, const std::vector<std::pair<string, tree>>& properties);
  path   native_drawing_active_graphics ();
  bool   native_drawing_grid_enabled (path graphics);
  point  native_drawing_snap_point (path graphics, frame f, point p);
  bool   native_drawing_graphics_box (path graphics, box& result);
  bool   native_drawing_set_canvas_geometry (
    path graphics, SI width, SI height, point actual_shift);
  void   refresh_native_drawing_properties_snapshot ();
  void   publish_native_drawing_focus_refresh ();
  bool   native_ink_region (path graphics,
                            native_ink_interaction_snapshot& region,
                            frame* coordinate_frame= nullptr);
  bool   native_ink_target (SI x, SI y, path& graphics,
                            frame& coordinate_frame);
  bool   native_drawing_object_bounds (path object,
                                       native_drawing_selection_box& bounds);
  bool   native_drawing_selection_bounds (native_drawing_selection_box& bounds);
  void   refresh_native_drawing_selection_snapshot ();
  void   erase_native_drawing_objects (const native_ink_sample* samples,
                                       std::size_t count);
  void   erase_native_drawing_segments (const native_ink_sample* samples,
                                        std::size_t count);
  void   select_native_drawing_lasso (const native_ink_sample* samples,
                                      std::size_t count);
  bool   native_ink_cursor_mode ();
  void   back_in_text_at (tree t, path p, bool forward);
};

#endif // defined EDIT_GRAPHICS_H

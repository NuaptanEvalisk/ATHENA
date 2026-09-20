
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
  void   commit_native_drawing_gesture (
    native_drawing_tool tool, const native_ink_sample* samples,
    std::size_t count);
  void   commit_native_ink_stroke (const native_ink_sample* samples,
                                   std::size_t count);
  void   collect_native_ink_graphics (tree t, path p, bool in_diagram,
                                      std::vector<path>& result);
  tree   native_ink_property (path graphics, string name, tree fallback);
  bool   native_ink_region (path graphics,
                            native_ink_interaction_snapshot& region,
                            frame* coordinate_frame= nullptr);
  bool   native_ink_target (SI x, SI y, path& graphics,
                            frame& coordinate_frame);
  bool   native_drawing_object_bounds (path object,
                                       native_drawing_selection_box& bounds);
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

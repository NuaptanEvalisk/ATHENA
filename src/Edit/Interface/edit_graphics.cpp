
/******************************************************************************
* MODULE     : edit_graphics.cpp
* DESCRIPTION: graphics between the editor and the window manager
* COPYRIGHT  : (C) 2003  Joris van der Hoeven and Henri Lesourd
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "Interface/edit_graphics.hpp"
#include "server.hpp"
#include "scheme.hpp"
#include "curve.hpp"
#include "Boxes/graphics.hpp"
#include "Bridge/impl_typesetter.hpp"
#include "Concat/graphics_transform.hpp"
#include "colors.hpp"
#include "drd_std.hpp"
#include "new_document.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <utility>

/******************************************************************************
* Constructors and destructors
******************************************************************************/

edit_graphics_rep::edit_graphics_rep () {
  p_x= p_y= 0.0;
  gr_x= gr_y= 0.0;
  graphical_object= tree();
}

edit_graphics_rep::~edit_graphics_rep () {}

namespace {

std::atomic<std::uint64_t> native_ink_serial {1};

bool
native_pen_mode (tree mode) {
  return is_func (mode, TUPLE, 2) &&
         mode[0] == "hand-edit" && mode[1] == "penscript";
}

std::uint32_t
native_ink_rgba (color value) {
  int r= 0, g= 0, b= 0, a= 255;
  get_rgb_color (value, r, g, b, a);
  return (static_cast<std::uint32_t> (a & 0xff) << 24) |
         (static_cast<std::uint32_t> (r & 0xff) << 16) |
         (static_cast<std::uint32_t> (g & 0xff) << 8) |
         static_cast<std::uint32_t> (b & 0xff);
}

tree
native_color_tree_from_rgba (std::uint32_t rgba) {
  int a= (rgba >> 24) & 0xff;
  int r= (rgba >> 16) & 0xff;
  int g= (rgba >> 8) & 0xff;
  int b= rgba & 0xff;
  return tree (get_hex_color (rgb_color (r, g, b, a)));
}

string
native_ink_id () {
  std::uint64_t serial=
    native_ink_serial.fetch_add (1, std::memory_order_relaxed);
  return "athena-ink-" * as_string ((long int) texmacs_time ()) * "-" *
         as_string ((long int) serial);
}

double
clamp_pressure (double pressure) {
  if (!std::isfinite (pressure)) return 1.0;
  return std::max (0.0, std::min (1.0, pressure));
}

double
native_line_width_pixels (tree width) {
  if (!is_atomic (width)) return 1.0;
  string raw= width->label;
  if (raw == "default" || N(raw) == 0) return 1.0;
  const char* text= as_charp (raw);
  char* end= nullptr;
  double value= std::strtod (text, &end);
  if (!std::isfinite (value) || value <= 0.0 || end == text) return 1.0;
  string unit (end);
  if (unit == "pt") value *= 96.0 / 72.0;
  else if (unit == "cm") value *= 96.0 / 2.54;
  else if (unit == "mm") value *= 96.0 / 25.4;
  else if (unit == "in") value *= 96.0;
  return std::max (1.0, value);
}

bool
path_has_prefix (path value, path prefix) {
  while (!is_nil (prefix)) {
    if (is_nil (value) || value->item != prefix->item) return false;
    value= value->next;
    prefix= prefix->next;
  }
  return true;
}

struct native_xy {
  double x= 0.0;
  double y= 0.0;
};

double
native_cross (native_xy a, native_xy b, native_xy c) {
  return (b.x - a.x) * (c.y - a.y) -
         (b.y - a.y) * (c.x - a.x);
}

double
native_point_segment_distance2 (native_xy p, native_xy a, native_xy b) {
  double dx= b.x - a.x;
  double dy= b.y - a.y;
  double n= dx * dx + dy * dy;
  if (n <= 1.0e-18) {
    dx= p.x - a.x;
    dy= p.y - a.y;
    return dx * dx + dy * dy;
  }
  double t= ((p.x - a.x) * dx + (p.y - a.y) * dy) / n;
  t= std::max (0.0, std::min (1.0, t));
  double qx= a.x + t * dx;
  double qy= a.y + t * dy;
  dx= p.x - qx;
  dy= p.y - qy;
  return dx * dx + dy * dy;
}

bool
native_segments_intersect (native_xy a, native_xy b,
                           native_xy c, native_xy d) {
  double ab_c= native_cross (a, b, c);
  double ab_d= native_cross (a, b, d);
  double cd_a= native_cross (c, d, a);
  double cd_b= native_cross (c, d, b);
  return ((ab_c <= 0.0 && ab_d >= 0.0) ||
          (ab_c >= 0.0 && ab_d <= 0.0)) &&
         ((cd_a <= 0.0 && cd_b >= 0.0) ||
          (cd_a >= 0.0 && cd_b <= 0.0));
}

double
native_segment_distance2 (native_xy a, native_xy b,
                          native_xy c, native_xy d) {
  if (native_segments_intersect (a, b, c, d)) return 0.0;
  return std::min (
    std::min (native_point_segment_distance2 (a, c, d),
              native_point_segment_distance2 (b, c, d)),
    std::min (native_point_segment_distance2 (c, a, b),
              native_point_segment_distance2 (d, a, b)));
}

std::vector<native_xy>
native_gesture_points (const native_ink_sample* samples, std::size_t count) {
  std::vector<native_xy> result;
  result.reserve (count);
  for (std::size_t i= 0; i < count; ++i)
    result.push_back ({static_cast<double> (samples[i].x),
                       static_cast<double> (samples[i].y)});
  return result;
}

bool
native_polyline_hits (const std::vector<native_xy>& first,
                      const std::vector<native_xy>& second,
                      double radius) {
  if (first.empty () || second.empty ()) return false;
  double r2= radius * radius;
  if (first.size () == 1 && second.size () == 1) {
    double dx= first[0].x - second[0].x;
    double dy= first[0].y - second[0].y;
    return dx * dx + dy * dy <= r2;
  }
  if (first.size () == 1) {
    for (std::size_t j= 1; j < second.size (); ++j)
      if (native_point_segment_distance2 (
            first[0], second[j-1], second[j]) <= r2)
        return true;
    return false;
  }
  if (second.size () == 1) {
    for (std::size_t i= 1; i < first.size (); ++i)
      if (native_point_segment_distance2 (
            second[0], first[i-1], first[i]) <= r2)
        return true;
    return false;
  }
  for (std::size_t i= 1; i < first.size (); ++i)
    for (std::size_t j= 1; j < second.size (); ++j)
      if (native_segment_distance2 (
            first[i-1], first[i], second[j-1], second[j]) <= r2)
        return true;
  return false;
}

bool
native_point_in_polygon (native_xy p, const std::vector<native_xy>& polygon) {
  if (polygon.size () < 3) return false;
  bool inside= false;
  for (std::size_t i=0, j=polygon.size () - 1; i<polygon.size (); j=i++) {
    const native_xy& a= polygon[i];
    const native_xy& b= polygon[j];
    bool crosses= ((a.y > p.y) != (b.y > p.y)) &&
      (p.x < (b.x - a.x) * (p.y - a.y) /
               ((b.y - a.y) == 0.0 ? 1.0e-18 : (b.y - a.y)) + a.x);
    if (crosses) inside= !inside;
  }
  return inside;
}

tree
native_graphics_radical (tree object, std::vector<frame>* transforms= nullptr) {
  tree radical= object;
  while (true) {
    if (is_func (radical, WITH) && N(radical) >= 1) {
      radical= radical[N(radical) - 1];
      continue;
    }
    if (is_func (radical, GR_TRANSFORM, 2) &&
        is_transformation (radical[1])) {
      if (transforms != nullptr)
        transforms->push_back (get_transformation (radical[1]));
      radical= radical[0];
      continue;
    }
    break;
  }
  return radical;
}

tree
native_penscript_radical (tree object) {
  return native_graphics_radical (object, nullptr);
}

tree
native_replace_radical (tree object, tree radical) {
  if (is_func (object, WITH) && N(object) >= 1) {
    tree result= copy (object);
    int last= N(result) - 1;
    result[last]= native_replace_radical (result[last], radical);
    return result;
  }
  if (is_func (object, GR_TRANSFORM, 2)) {
    tree result= copy (object);
    result[0]= native_replace_radical (result[0], radical);
    return result;
  }
  return radical;
}

point
native_apply_post_transforms (point p, std::vector<frame>& transforms) {
  for (auto it= transforms.rbegin (); it != transforms.rend (); ++it)
    p= (*it) (p);
  return p;
}

bool
native_penscript_screen_points (tree object, frame f,
                                std::vector<native_xy>& points,
                                tree* radical_out= nullptr) {
  std::vector<frame> transforms;
  tree stroke= native_graphics_radical (object, &transforms);
  if (!is_func (stroke, PENSCRIPT) || N(stroke) < 4 ||
      !is_func (stroke[3], TUPLE))
    return false;
  points.clear ();
  points.reserve (N(stroke[3]));
  for (int i=0; i<N(stroke[3]); ++i) {
    tree sample= stroke[3][i];
    if (!is_func (sample, TUPLE) || N(sample) < 2 ||
        !is_atomic (sample[0]) || !is_atomic (sample[1]))
      continue;
    point p= f (point (as_double (sample[0]->label),
                       as_double (sample[1]->label)));
    p= native_apply_post_transforms (p, transforms);
    if (N(p) >= 2) points.push_back ({p[0], p[1]});
  }
  if (radical_out != nullptr) *radical_out= stroke;
  return !points.empty ();
}

void
native_collect_graphics_points_impl (
  tree t, frame f, std::vector<frame>& transforms,
  std::vector<native_xy>& points) {
  if (is_func (t, WITH) && N(t) >= 1) {
    native_collect_graphics_points_impl (
      t[N(t)-1], f, transforms, points);
    return;
  }
  if (is_func (t, GR_TRANSFORM, 2) && is_transformation (t[1])) {
    transforms.push_back (get_transformation (t[1]));
    native_collect_graphics_points_impl (t[0], f, transforms, points);
    transforms.pop_back ();
    return;
  }
  if (is_func (t, _POINT) && N(t) >= 2 &&
      is_atomic (t[0]) && is_atomic (t[1])) {
    point p= f (point (as_double (t[0]->label), as_double (t[1]->label)));
    p= native_apply_post_transforms (p, transforms);
    if (N(p) >= 2) points.push_back ({p[0], p[1]});
    return;
  }
  if (is_atomic (t)) return;
  for (int i=0; i<N(t); ++i)
    native_collect_graphics_points_impl (t[i], f, transforms, points);
}

void
native_collect_graphics_points (tree t, frame f,
                                std::vector<native_xy>& points) {
  std::vector<frame> transforms;
  native_collect_graphics_points_impl (t, f, transforms, points);
  tree radical= native_graphics_radical (t, nullptr);
  bool closed= is_func (radical, CLINE) || is_func (radical, CSPLINE) ||
               is_func (radical, CBEZIER) || is_func (radical, CSMOOTH) ||
               is_func (radical, CARC);
  if (closed && points.size () > 2)
    points.push_back (points.front ());
}

bool
native_polyline_hits_box (const std::vector<native_xy>& points,
                          native_drawing_selection_box box,
                          double radius) {
  double x1= std::min ((double) box.x1, (double) box.x2) - radius;
  double x2= std::max ((double) box.x1, (double) box.x2) + radius;
  double y1= std::min ((double) box.y1, (double) box.y2) - radius;
  double y2= std::max ((double) box.y1, (double) box.y2) + radius;
  auto inside= [=] (native_xy p) {
    return p.x >= x1 && p.x <= x2 && p.y >= y1 && p.y <= y2;
  };
  for (const auto& p: points) if (inside (p)) return true;
  native_xy corners[4]= {{x1,y1},{x2,y1},{x2,y2},{x1,y2}};
  for (std::size_t i=1; i<points.size (); ++i)
    for (int e=0; e<4; ++e)
      if (native_segments_intersect (
            points[i-1], points[i], corners[e], corners[(e+1)%4]))
        return true;
  return false;
}

tree
native_point_tree (double x, double y) {
  return tree (_POINT, as_string (x), as_string (y));
}

point
native_constrained_end (point first, point last) {
  if (N(first) < 2 || N(last) < 2) return last;
  double dx= last[0] - first[0];
  double dy= last[1] - first[1];
  double side= std::max (std::fabs (dx), std::fabs (dy));
  if (!(side > 0.0)) return copy (first);
  return point (first[0] + (dx < 0.0 ? -side : side),
                first[1] + (dy < 0.0 ? -side : side));
}

tree
native_shape_polygon (tree_label label, const std::vector<point>& points) {
  tree result (label);
  for (const point& p: points)
    if (N(p) >= 2) result << native_point_tree (p[0], p[1]);
  return result;
}

std::vector<point>
native_regular_polygon_points (point first, point last, int sides) {
  std::vector<point> result;
  if (sides < 3 || N(first) < 2 || N(last) < 2) return result;
  point end= native_constrained_end (first, last);
  double cx= 0.5 * (first[0] + end[0]);
  double cy= 0.5 * (first[1] + end[1]);
  double radius= 0.5 * std::fabs (end[0] - first[0]);
  if (!(radius > 0.0)) return result;
  constexpr double pi= 3.1415926535897932384626433832795;
  result.reserve ((std::size_t) sides);
  for (int i=0; i<sides; ++i) {
    double angle= 0.5 * pi + 2.0 * pi * (double) i / (double) sides;
    result.push_back (point (cx + radius * std::cos (angle),
                             cy + radius * std::sin (angle)));
  }
  return result;
}

std::vector<point>
native_ellipse_points (point first, point last, bool circle) {
  std::vector<point> result;
  if (N(first) < 2 || N(last) < 2) return result;
  point end= circle ? native_constrained_end (first, last) : last;
  double cx= 0.5 * (first[0] + end[0]);
  double cy= 0.5 * (first[1] + end[1]);
  double rx= 0.5 * std::fabs (end[0] - first[0]);
  double ry= 0.5 * std::fabs (end[1] - first[1]);
  if (!(rx > 0.0) || !(ry > 0.0)) return result;
  constexpr double pi= 3.1415926535897932384626433832795;
  constexpr int samples= 16;
  result.reserve (samples);
  for (int i=0; i<samples; ++i) {
    double angle= 2.0 * pi * (double) i / (double) samples;
    result.push_back (point (cx + rx * std::cos (angle),
                             cy + ry * std::sin (angle)));
  }
  return result;
}

} // namespace

/******************************************************************************
* Extra subroutines for graphical selections
******************************************************************************/

static tree snap_mode;
static SI snap_distance;

void
set_snap_mode (tree t) {
  //cout << "Snap mode= " << t << "\n";
  snap_mode= t;
}

void
set_snap_distance (SI d) {
  //cout << "Snap distance= " << d << "\n";
  snap_distance= d;
}

bool
check_snap_mode (string type) {
  if (!is_tuple (snap_mode)) return true;
  for (int i=0; i<N(snap_mode); i++)
    if (snap_mode[i] == "all") return true;
    else if (snap_mode[i] == type) return true;
  return false;
}

bool
can_snap (gr_selection sel) {
  string type= sel->type;
  if (type == "free")
    return true;
  if (type == "box")
    return false;
  if (type == "point")
    return check_snap_mode ("control point");
  if (type == "curve-handle")
    return check_snap_mode ("control point");
  if (type == "curve-point")
    return check_snap_mode ("curve point");
  if (type == "curve-point&curve-point")
    return check_snap_mode ("curve-curve intersection");
  if (type == "grid-point")
    return check_snap_mode ("grid point");
  if (type == "grid-curve-point")
    return check_snap_mode ("grid curve point");
  if (type == "curve-point&grid-curve-point")
    return check_snap_mode ("curve-grid intersection");
  if (type == "grid-curve-point&curve-point")
    return check_snap_mode ("curve-grid intersection");
  if (type == "text" || type == "group")
    return check_snap_mode ("text");
  if (type == "text-handle")
    return check_snap_mode ("control point");
  if (type == "text-border")
    return check_snap_mode ("text border");
  if (type == "text-border-point")
    return check_snap_mode ("text border point");
  if (type == "text-border&grid-curve-point")
    return check_snap_mode ("text border") &&
           check_snap_mode ("curve-curve intersection");
  if (type == "grid-curve-point&text-border")
    return check_snap_mode ("text border") &&
           check_snap_mode ("curve-curve intersection");
  cout << "Uncaptured snap type " << type << "\n";
  return true;
}

gr_selection
snap_to_guide (point p, gr_selections sels, double eps) {
  if (N(sels) == 0) {
    gr_selection snap;
    snap->type= "free";
    snap->p= p;
    snap->dist= 0;
    return snap;
  }

  sort (sels);
  //for (int i=0; i<N(sels); i++)
  //  cout << "snap " << sels[i]->type
  //       << ", " << sels[i]->dist
  //       << ", " << can_snap (sels[i]) << LF;
  //cout << LF;

  gr_selection best;
  best->type= "none";
  for (int i=0; i<N(sels); i++)
    if (can_snap (sels[i])) {
      if (sels[i]->type == "grid-point")
        best= sels[i];
      else if (is_nil (sels[i]->c) &&
               best->type != "grid-point" &&
               !ends (best->type, "-handle"))
        return sels[i];
    }

  for (int i=0; i<N(sels); i++)
    for (int j=i+1; j<N(sels); j++) {
      if (!is_nil (sels[i]->c) &&
          !is_nil (sels[j]->c) &&
          (sels[i]->type != "grid-curve-point" ||
           sels[j]->type != "grid-curve-point") &&
          (!ends (sels[i]->type, "-point") ||
           !ends (sels[j]->type, "-border")) &&
          (!ends (sels[i]->type, "-border") ||
           !ends (sels[j]->type, "-point")) &&
          !ends (sels[i]->type, "handle") &&
          !ends (sels[j]->type, "handle"))
        {
          array<point> ins= intersection (sels[i]->c, sels[j]->c, p, eps);
          for (int k=0; k<N(ins); k++)
            if (best->type == "none" || norm (ins[k] - p) < best->dist) {
              gr_selection sel;
              sel->type= sels[i]->type * "&" * sels[j]->type;
              sel->p   = ins[k];
              sel->dist= (SI) norm (ins[k] - p);
              sel->cp  = append (sels[i]->cp, sels[j]->cp);
              sel->pts = append (sels[i]->pts, sels[j]->pts);
              if (can_snap (sel)) best= sel;
            }
        }
    }

  if (best->type != "none") return best;
  if (can_snap (sels[0])) return sels[0];
  else {
    gr_selection snap;
    snap->type= "free";
    snap->p= p;
    snap->dist= 0;
    return snap;
  }
}

/******************************************************************************
* Main edit_graphics routines
******************************************************************************/

path
edit_graphics_rep::graphics_path () {
  path gp= search_upwards (GRAPHICS);
  if (is_nil (gp)) return tp;
  return gp * 0;
}

bool
edit_graphics_rep::inside_graphics (bool b) {
  try {
    if (as_bool (call ("defined?",
                       symbol_object ("in-commutative-diagram?"))) &&
        as_bool (call ("in-commutative-diagram?")))
      return false;
  }
  catch (...) {}
  path p   = path_up (tp);
  bool flag= false;
  tree st  = et;
  while (!is_nil (p)) {
    if (is_compound (st, "commutative-diagram")) return false;
    if (is_func (st, GRAPHICS)) flag= true;
    if (b && is_graphical_text (st)) flag= false;
    if (is_atomic (st) || p->item < 0 || p->item >= N(st)) break;
    st= st[p->item];
    p = p->next;
  }
  return flag || (L(st) == GRAPHICS);
}

bool
edit_graphics_rep::inside_active_graphics (bool b) {
  return inside_graphics (b) && get_env_string (PREAMBLE) == "false";
}

bool
edit_graphics_rep::over_graphics (SI x, SI y) {
  frame f= find_frame ();
  if (!is_nil (f)) {
    point lim1, lim2;
    find_limits (lim1, lim2);
    point p = adjust (f [point (x, y)]);
    // cout << type << " at " << p << " [" << lim1 << ", " << lim2 << "]\n";
    if (N(lim1) == 2)
      if ((p[0]<lim1[0]) || (p[0]>lim2[0]) || (p[1]<lim1[1]) || (p[1]>lim2[1]))
        return as_bool (call ("graphics-busy?"));
    return true;
  }
  return false;
}

tree
edit_graphics_rep::get_graphics () {
  path p   = path_up (tp);
  tree st  = et;
  tree res = tree ();
  while (!is_nil (p)) {
    if (is_func (st, GRAPHICS)) res= st;
    st= st[p->item];
    p = p->next;
  }
  return res;
}

double
edit_graphics_rep::get_x () {
  return gr_x;
}

double
edit_graphics_rep::get_y () {
  return gr_y;
}

double
edit_graphics_rep::get_pixel () {
  edit_env env= get_typesetter ()->env;
  return env->fr->inverse_scalar (env->pixel);
}

frame
edit_graphics_rep::find_frame (bool last) {
  path gp= graphics_path ();
  bool bp_found;
  path bp= eb->find_box_path (gp, bp_found);
  if (bp_found) return eb->find_frame (path_up (bp), last);
  else return frame ();
}

grid
edit_graphics_rep::find_grid () {
  path gp= graphics_path ();
  bool bp_found;
  path bp= eb->find_box_path (gp, bp_found);
  if (bp_found) return eb->find_grid (path_up (bp));
  else return grid ();
}

void
edit_graphics_rep::find_limits (point& lim1, point& lim2) {
  path gp= graphics_path ();
  lim1= point (); lim2= point ();
  bool bp_found;
  path bp= eb->find_box_path (gp, bp_found);
  if (bp_found) eb->find_limits (path_up (bp), lim1, lim2);
  if (N(lim1) >= 2 && fabs (lim1[0]) <= 0.001) lim1[0]= 0.0;
  if (N(lim1) >= 2 && fabs (lim1[1]) <= 0.001) lim1[1]= 0.0;
  if (N(lim2) >= 2 && fabs (lim2[0]) <= 0.001) lim2[0]= 0.0;
  if (N(lim2) >= 2 && fabs (lim2[1]) <= 0.001) lim2[1]= 0.0;
}

bool
edit_graphics_rep::find_graphical_region (SI& x1, SI& y1, SI& x2, SI& y2) {
  point lim1, lim2;
  find_limits (lim1, lim2);
  if (lim1 == point ()) return false;
  frame f= find_frame ();
  if (is_nil (f)) return false;
  point p1= f (point (lim1[0], lim1[1]));
  point p2= f (point (lim2[0], lim2[1]));
  x1= (SI) p1[0]; y1= (SI) p1[1];
  x2= (SI) p2[0]; y2= (SI) p2[1];
  return true;
}

point
edit_graphics_rep::adjust (point p) {
  frame f= find_frame ();
  grid g= find_grid ();
  if (!is_nil (g) && !is_nil (gr0) &&
      (g != gr0 || p[0] != p_x || p[1] != p_y)) {
    graphical_select (p[0], p[1]);
    g= gr0;
    p[0]= p_x;
    p[1]= p_y;
  }
  if (is_nil (g)) return p;
  point res;
  gr_selections sels= copy (gs);
  frame f2= find_frame (true);
  if (is_nil (f2)) return p;
  point fp= f2 (p);
  if ((tree) g != "empty_grid") {
    point q= g->find_point_around (p, snap_distance, f);
    point fq= f2 (q);
    if (norm (fq - fp) < snap_distance) {
      gr_selection sel;
      sel->type= "grid-point";
      sel->p   = fq;
      sel->dist= (SI) norm (fq - fp);
      sels << sel;
    }
    array<grid_curve> gc=
      g->get_curves_around (p, snap_distance, f);
    for (int i=0; i<N(gc); i++) {
      point fc= closest (f2 (gc[i]->c), fp);
      if (norm (fc - fp) < snap_distance) {
        gr_selection sel;
        sel->type= "grid-curve-point";
        sel->p   = fc;
        sel->dist= (SI) norm (fc - fp);
        sel->c   = f2 (gc[i]->c);
        sels << sel;
      }
    }
  }
  double eps= get_pixel_size () / 10.0;
  gr_selection snap= snap_to_guide (fp, sels, eps);
  //cout << "Snap " << fp << " to " << snap << ", " << snap->p << "\n";
  point snapped= f2[snap->p];
  if (N(snapped) == 2) return snapped;
  return p;
  // FIXME: why can snapped be an invalid point?
}

tree
edit_graphics_rep::find_point (point p) {
  return tree (_POINT, as_string (p[0]), as_string (p[1]));
}

tree
edit_graphics_rep::graphical_select (double x, double y) { 
  frame f= find_frame ();
  if (is_nil (f)) return tuple ();
  gr_selections sels;
  point p0 = point (x, y);
  point p = f (p0);
  sels= eb->graphical_select ((SI)p[0], (SI)p[1], snap_distance);
  //for (int i=0; i<N(sels); i++)
  //  cout << i << ":\t" << sels[i] << "\n";
  gs= sels;
  gr0= empty_grid ();
  grid g= find_grid ();
  frame f2= find_frame (true);
  if (!is_nil (g) && !is_nil (f2)) {
    gr0= g;
    p_x= x;
    p_y= y;
  }
  return as_tree (sels);
}

tree
edit_graphics_rep::graphical_select (
  double x1, double y1, double x2, double y2)
{ 
  frame f= find_frame ();
  if (is_nil (f)) return tuple ();
  gr_selections sels;
  point p1 = f (point (x1, y1)), p2= f (point (x2, y2));
  sels= eb->graphical_select ((SI)p1[0], (SI)p1[1], (SI)p2[0], (SI)p2[1]);
  return as_tree (sels);
}

tree
edit_graphics_rep::get_graphical_object () {
  return graphical_object;
}

void
edit_graphics_rep::set_graphical_object (tree t) {
  go_box= box ();
  graphical_object= t;
  if (N (graphical_object) == 0) return;
  edit_env env= get_typesetter ()->env;
  //tree old_fr= env->local_begin (GR_FRAME, (tree) find_frame ());  
  frame f_env= env->fr;
  env->fr= find_frame ();
  if (!is_nil (env->fr)) {
    int i,n=0;
    go_box= typeset_as_concat (env, t, path (0));
    for (i=0; i<N(go_box); i++)
      if (go_box[i]!="") n++;
    if (n) {
      array<box> bx(n);
      n=0;
      for (i=0; i<N(go_box); i++) if (go_box[i]!="") {
        array<box> bx2(1);
        array<SI> spc2(1);
        bx2[0]= go_box[i];
        spc2[0]=0;
        bx[n]= concat_box (path (0), bx2, spc2);
        n++;
      }
      go_box= composite_box (path (0), bx);
    }
  }
  env->fr= f_env;
  //env->local_end (GR_FRAME, old_fr);
}

void
edit_graphics_rep::invalidate_graphical_object () {
  if (native_ink_cursor_mode ()) return;
  SI gx1, gy1, gx2, gy2;
  if (!is_nil (eb) && !is_nil (go_box) &&
      find_graphical_region (gx1, gy1, gx2, gy2)) {
    int i;
    rectangles rs;
    rectangle gr (gx1, gy1, gx2, gy2);
    for (i=0; i<go_box->subnr(); i++) {
      box b= go_box->subbox (i);
      rs= rectangles (rectangle (b->x3, b->y3, b->x4, b->y4), rs);
    }
    rs= rs & rectangles (gr);
    invalidate (rs);
  }
}

void
edit_graphics_rep::draw_graphical_object (renderer ren) {
  if (is_nil (go_box)) set_graphical_object (graphical_object);
  if (is_nil (go_box)) return;
  SI ox1, oy1, ox2, oy2;
  ren->get_clipping (ox1, oy1, ox2, oy2);
  SI gx1, gy1, gx2, gy2;
  if (find_graphical_region (gx1, gy1, gx2, gy2))
    ren->extra_clipping (gx1, gy1, gx2, gy2);
  int i;
  for (i=0; i<go_box->subnr(); i++) {
    box b= go_box->subbox (i);
    if ((tree)b=="point" || (tree)b=="curve")
      b->display (ren);
    else {
      rectangles rs;
      b->redraw (ren, path (), rs);
    }
  }
  ren->set_clipping (ox1, oy1, ox2, oy2);
}

void
edit_graphics_rep::collect_native_ink_graphics (
  tree t, path p, bool in_diagram, std::vector<path>& result) {
  bool diagram= in_diagram || is_compound (t, "commutative-diagram");
  if (!diagram && is_func (t, GRAPHICS)) result.push_back (copy (p));
  if (is_atomic (t)) return;
  for (int i=0; i<N(t); ++i)
    collect_native_ink_graphics (t[i], p * i, diagram, result);
}

tree
edit_graphics_rep::native_ink_property (
  path graphics, string name, tree fallback) {
  path p= path_up (graphics);
  while (!is_nil (p)) {
    tree t= subtree (et, p);
    if (is_func (t, WITH) && N(t) >= 3) {
      for (int i=0; i+1<N(t)-1; i+=2)
        if (is_atomic (t[i]) && t[i]->label == name)
          return copy (t[i+1]);
    }
    if (p == rp) break;
    p= path_up (p);
  }
  return fallback;
}

tree
edit_graphics_rep::native_drawing_object_property (
  tree object, string name, tree fallback) {
  if (is_func (object, GR_TRANSFORM, 2))
    return native_drawing_object_property (object[0], name, fallback);
  if (is_func (object, WITH) && N(object) >= 3) {
    tree nested= native_drawing_object_property (
      object[N(object)-1], name, tree (UNINIT));
    if (nested != tree (UNINIT)) return nested;
    for (int i=0; i+1<N(object)-1; i+=2)
      if (is_atomic (object[i]) && object[i]->label == name)
        return copy (object[i+1]);
  }
  return fallback;
}

namespace {

tree
native_update_object_property_impl (
  tree object, string name, tree value, bool& updated) {
  if (is_func (object, GR_TRANSFORM, 2)) {
    tree result= copy (object);
    result[0]= native_update_object_property_impl (
      result[0], name, value, updated);
    return result;
  }
  if (is_func (object, WITH) && N(object) >= 3) {
    tree result= copy (object);
    int last= N(result) - 1;
    result[last]= native_update_object_property_impl (
      result[last], name, value, updated);
    if (updated) return result;
    for (int i=0; i+1<last; i+=2)
      if (is_atomic (result[i]) && result[i]->label == name) {
        result[i+1]= copy (value);
        updated= true;
        return result;
      }
    return result;
  }
  return copy (object);
}

tree
native_insert_object_property_impl (tree object, string name, tree value) {
  if (is_func (object, GR_TRANSFORM, 2)) {
    tree result= copy (object);
    result[0]= native_insert_object_property_impl (result[0], name, value);
    return result;
  }
  if (is_func (object, WITH) && N(object) >= 3) {
    tree result= copy (object);
    int last= N(result) - 1;
    result[last]= native_insert_object_property_impl (
      result[last], name, value);
    return result;
  }
  tree wrapped (WITH);
  wrapped << name << copy (value) << copy (object);
  return wrapped;
}

tree
native_set_with_property (tree wrapper, string name, tree value) {
  if (!is_func (wrapper, WITH) || N(wrapper) < 1) return wrapper;
  tree result (WITH);
  bool replaced= false;
  int last= N(wrapper) - 1;
  for (int i=0; i+1<last; i+=2) {
    if (is_atomic (wrapper[i]) && wrapper[i]->label == name) {
      result << copy (wrapper[i]) << copy (value);
      replaced= true;
    }
    else result << copy (wrapper[i]) << copy (wrapper[i+1]);
  }
  if (!replaced) result << name << copy (value);
  result << copy (wrapper[last]);
  return result;
}

} // namespace

tree
edit_graphics_rep::native_drawing_set_object_property (
  tree object, string name, tree value) {
  bool updated= false;
  tree result= native_update_object_property_impl (
    object, name, value, updated);
  if (updated) return result;
  return native_insert_object_property_impl (object, name, value);
}

bool
edit_graphics_rep::native_drawing_set_graphics_property (
  path graphics, string name, tree value) {
  if (is_nil (graphics) || !has_subtree (et, graphics)) return false;
  path p= path_up (graphics);
  while (!is_nil (p)) {
    tree t= subtree (et, p);
    if (is_func (t, WITH) && N(t) >= 1) {
      assign (p, native_set_with_property (t, name, value));
      return true;
    }
    if (p == rp) break;
    p= path_up (p);
  }
  tree wrapped (WITH);
  wrapped << name << copy (value) << copy (subtree (et, graphics));
  assign (graphics, wrapped);
  return true;
}

bool
edit_graphics_rep::native_drawing_set_graphics_properties (
  path graphics, const std::vector<std::pair<string, tree>>& properties) {
  if (is_nil (graphics) || !has_subtree (et, graphics) || properties.empty ())
    return false;
  path p= path_up (graphics);
  while (!is_nil (p)) {
    tree t= subtree (et, p);
    if (is_func (t, WITH) && N(t) >= 1) {
      tree result= copy (t);
      for (const auto& property: properties)
        result= native_set_with_property (
          result, property.first, property.second);
      assign (p, result);
      return true;
    }
    if (p == rp) break;
    p= path_up (p);
  }
  tree wrapped (WITH);
  for (const auto& property: properties)
    wrapped << property.first << copy (property.second);
  wrapped << copy (subtree (et, graphics));
  assign (graphics, wrapped);
  return true;
}

path
edit_graphics_rep::native_drawing_active_graphics () {
  if (!native_drawing_selection_paths_.empty ()) {
    path gp= path_up (native_drawing_selection_paths_.front ());
    if (!is_nil (gp) && has_subtree (et, gp) &&
        is_func (subtree (et, gp), GRAPHICS))
      return gp;
  }
  if (!native_ink_paths_.empty ()) return copy (native_ink_paths_.front ());
  return path ();
}

bool
edit_graphics_rep::native_drawing_grid_enabled (path graphics) {
  if (is_nil (graphics)) return false;
  tree value= native_ink_property (graphics, GR_GRID, tree (""));
  if (is_atomic (value)) return value->label != "";
  if (!is_func (value, TUPLE) || N(value) == 0) return false;
  return !(is_atomic (value[0]) && value[0]->label == "empty");
}

point
edit_graphics_rep::native_drawing_snap_point (path graphics, frame f, point p) {
  if (!native_drawing_snap_enabled_ || is_nil (eb) || is_nil (f) ||
      is_nil (graphics) || !native_drawing_grid_enabled (graphics))
    return p;
  bool box_found= false;
  path bp= eb->find_box_path (graphics * 0, box_found);
  if (!box_found) return p;
  grid g= eb->find_grid (path_up (bp));
  if (is_nil (g) || (tree) g == "empty_grid") return p;
  SI tolerance= 10 * get_pixel_size ();
  point snapped= g->find_point_around (p, tolerance, f);
  point fp= f (p);
  point fs= f (snapped);
  if (N(fp) >= 2 && N(fs) >= 2 && norm (fs - fp) <= tolerance)
    return snapped;
  return p;
}

bool
edit_graphics_rep::native_drawing_graphics_box (path graphics, box& result) {
  result= box ();
  if (is_nil (eb) || is_nil (graphics) || !has_subtree (et, graphics))
    return false;
  bool found= false;
  path bp= eb->find_box_path (graphics * 0, found);
  if (!found) return false;
  path p= path_up (bp);
  while (!is_nil (p)) {
    box candidate= eb[p];
    if (!is_nil (candidate) && (tree) candidate == "graphics") {
      result= candidate;
      return true;
    }
    p= path_up (p);
  }
  return false;
}

bool
edit_graphics_rep::native_drawing_set_canvas_geometry (
  path graphics, SI width, SI height, point actual_shift) {
  if (is_nil (graphics) || N(actual_shift) < 2) return false;
  SI minimum= 16 * get_pixel_size ();
  width= max (minimum, width);
  height= max (minimum, height);

  tree frame_value= native_ink_property (
    graphics, GR_FRAME, tree (UNINIT));
  if (!is_tuple (frame_value, "scale", 2) ||
      !is_func (frame_value[2], TUPLE, 2))
    return false;

  tree geometry_value= native_ink_property (
    graphics, GR_GEOMETRY,
    tree (TUPLE, "geometry", "1par", "0.6par", "center"));
  string align= "center";
  if (is_tuple (geometry_value, "geometry", 3) &&
      is_atomic (geometry_value[3]))
    align= geometry_value[3]->label;

  edit_env env= get_typesetter ()->env;
  SI yinc= align == "top" ? -height :
           align == "bottom" ? 0 :
           align == "axis" ? -(height / 2) + env->as_length ("1yfrac") :
           -(height / 2);
  SI origin_x= (SI) std::llround (actual_shift[0]);
  SI origin_y= (SI) std::llround (actual_shift[1] - (double) yinc);

  tree origin (TUPLE);
  origin << (as_string (origin_x) * "tmpt")
         << (as_string (origin_y) * "tmpt");
  tree new_frame (TUPLE);
  new_frame << "scale" << copy (frame_value[1]) << origin;
  tree new_geometry (TUPLE);
  new_geometry << "geometry"
               << (as_string (width) * "tmpt")
               << (as_string (height) * "tmpt")
               << align;

  std::vector<std::pair<string, tree>> properties;
  properties.push_back ({GR_GEOMETRY, new_geometry});
  properties.push_back ({GR_FRAME, new_frame});
  properties.push_back ({GR_AUTO_CROP, tree ("false")});
  return native_drawing_set_graphics_properties (graphics, properties);
}

void
edit_graphics_rep::publish_native_drawing_focus_refresh () {
  if (ui_endpoint != nullptr)
    ui_endpoint->publish (actor_command_kind::ui_native_drawing_focus_refresh);
}

bool
edit_graphics_rep::native_ink_region (
  path graphics, native_ink_interaction_snapshot& region,
  frame* coordinate_frame) {
  if (is_nil (eb) || is_nil (graphics)) return false;
  tree node= subtree (et, graphics);
  if (!is_func (node, GRAPHICS)) return false;
  tree mode= native_ink_property (graphics, GR_MODE, tree ("line"));
  if (!native_pen_mode (mode)) return false;

  bool box_found= false;
  path bp= eb->find_box_path (graphics * 0, box_found);
  if (!box_found) return false;
  path box_path= path_up (bp);
  frame f= eb->find_frame (box_path);
  if (is_nil (f)) return false;
  point lim1, lim2;
  eb->find_limits (box_path, lim1, lim2);
  if (N(lim1) < 2 || N(lim2) < 2) return false;
  point p1= f (lim1);
  point p2= f (lim2);
  if (N(p1) < 2 || N(p2) < 2) return false;

  region.x1= min ((SI) p1[0], (SI) p2[0]);
  region.y1= min ((SI) p1[1], (SI) p2[1]);
  region.x2= max ((SI) p1[0], (SI) p2[0]);
  region.y2= max ((SI) p1[1], (SI) p2[1]);
  tree color_value=
    native_ink_property (graphics, GR_COLOR, tree ("default"));
  string color_name= is_atomic (color_value) ? color_value->label : "default";
  if (color_name == "default" || N(color_name) == 0) color_name= "black";
  region.rgba= native_drawing_color_override_ ? native_drawing_rgba_ :
               native_ink_rgba (named_color (color_name));
  region.line_width_pixels= native_drawing_width_override_ ?
    native_drawing_width_pixels_ : native_line_width_pixels (
      native_ink_property (graphics, GR_LINE_WIDTH, tree ("1ln")));
  region.tool= native_drawing_tool_;
  switch (native_drawing_tool_) {
  case native_drawing_tool::highlighter: {
    int r= 255, g= 235, b= 59, a= 96;
    color base= named_color (color_name);
    get_rgb_color (base, r, g, b, a);
    a= 96;
    region.rgba= native_ink_rgba (rgb_color (r, g, b, a));
    region.line_width_pixels= std::max (6.0, region.line_width_pixels * 5.0);
    region.pressure_enabled= false;
    break;
  }
  case native_drawing_tool::object_eraser:
    region.rgba= 0x80ffffffU;
    region.line_width_pixels= 16.0;
    region.eraser_radius_pixels= 8.0;
    region.pressure_enabled= false;
    break;
  case native_drawing_tool::segment_eraser:
    region.rgba= 0x80ffffffU;
    region.line_width_pixels= 12.0;
    region.eraser_radius_pixels= 6.0;
    region.pressure_enabled= false;
    break;
  case native_drawing_tool::lasso:
    region.rgba= 0xff4a90e2U;
    region.line_width_pixels= 1.5;
    region.pressure_enabled= false;
    break;
  case native_drawing_tool::shape:
    region.pressure_enabled= false;
    break;
  case native_drawing_tool::pen:
    break;
  }
  region.shape= native_drawing_shape_;
  region.pen_enabled= true;
  if (native_drawing_tool_ == native_drawing_tool::pen)
    region.pressure_enabled= native_drawing_pressure_enabled_;
  if (coordinate_frame != nullptr) *coordinate_frame= f;
  return true;
}

bool
edit_graphics_rep::native_ink_target (
  SI x, SI y, path& graphics, frame& coordinate_frame) {
  std::vector<path> candidates;
  collect_native_ink_graphics (subtree (et, rp), rp, false, candidates);
  bool found= false;
  long double best_area= 0.0;
  for (const path& candidate: candidates) {
    native_ink_interaction_snapshot region;
    frame f;
    if (!native_ink_region (candidate, region, &f)) continue;
    if (x < region.x1 || x > region.x2 || y < region.y1 || y > region.y2)
      continue;
    long double width= static_cast<long double> (region.x2) - region.x1;
    long double height= static_cast<long double> (region.y2) - region.y1;
    long double area= std::max ((long double) 0.0, width) *
                      std::max ((long double) 0.0, height);
    if (!found || area < best_area) {
      found= true;
      best_area= area;
      graphics= copy (candidate);
      coordinate_frame= f;
    }
  }
  return found;
}

bool
edit_graphics_rep::native_ink_cursor_mode () {
  for (const path& gp: native_ink_paths_)
    if (path_has_prefix (tp, gp)) return true;
  return false;
}

void
edit_graphics_rep::mark_native_ink_interaction_dirty () {
  native_ink_interaction_dirty_= true;
}

native_drawing_tool
edit_graphics_rep::get_native_drawing_tool () const {
  return native_drawing_tool_;
}

void
edit_graphics_rep::set_native_drawing_tool (native_drawing_tool tool) {
  if (static_cast<unsigned int> (tool) >
      static_cast<unsigned int> (native_drawing_tool::shape))
    tool= native_drawing_tool::pen;
  native_drawing_tool_= tool;
  refresh_native_ink_interaction ();
  refresh_native_drawing_properties_snapshot ();
  publish_native_drawing_focus_refresh ();
  invalidate_all ();
}

void
edit_graphics_rep::set_native_drawing_property (
  native_drawing_property property, std::uint64_t value) {
  if (property == native_drawing_property::color ||
      property == native_drawing_property::line_width) {
    if (!native_drawing_selection_paths_.empty ()) {
      tree property_value;
      string property_name;
      if (property == native_drawing_property::color) {
        property_name= "color";
        property_value= native_color_tree_from_rgba (
          static_cast<std::uint32_t> (value));
      }
      else {
        property_name= "line-width";
        double width= native_drawing_bits_double (value);
        if (!std::isfinite (width)) return;
        width= std::max (1.0, std::min (64.0, width));
        property_value= tree (as_string (width) * "ln");
      }
      start_editing ();
      for (const path& p: native_drawing_selection_paths_)
        if (has_subtree (et, p))
          assign (p, native_drawing_set_object_property (
            subtree (et, p), property_name, property_value));
      end_editing ();
    }
    else if (property == native_drawing_property::color) {
      native_drawing_color_override_= true;
      native_drawing_rgba_= static_cast<std::uint32_t> (value);
    }
    else {
      double width= native_drawing_bits_double (value);
      if (!std::isfinite (width)) return;
      native_drawing_width_override_= true;
      width= std::max (1.0, std::min (64.0, width));
      if (native_drawing_tool_ == native_drawing_tool::highlighter)
        width= std::max (1.0, width / 5.0);
      native_drawing_width_pixels_= width;
    }
  }
  else if (property == native_drawing_property::pressure)
    native_drawing_pressure_enabled_= value != 0;
  else if (property == native_drawing_property::snap)
    native_drawing_snap_enabled_= value != 0;
  else if (property == native_drawing_property::grid) {
    path gp= native_drawing_active_graphics ();
    if (is_nil (gp)) return;
    tree grid_value;
    if (value != 0)
      grid_value= tree (TUPLE, "cartesian", tree (_POINT, "0", "0"), "1");
    else grid_value= tree (TUPLE, "empty");
    start_editing ();
    bool changed= native_drawing_set_graphics_property (
      gp, GR_GRID, grid_value);
    if (changed) {
      std::vector<path> refreshed;
      collect_native_ink_graphics (subtree (et, rp), rp, false, refreshed);
      for (const path& candidate: refreshed) {
        tree mode= native_ink_property (candidate, GR_MODE, tree ("line"));
        if (native_pen_mode (mode)) {
          native_drawing_set_graphics_property (
            candidate, GR_EDIT_GRID, grid_value);
          break;
        }
      }
    }
    end_editing ();
    native_drawing_selection_paths_.clear ();
    mark_native_ink_interaction_dirty ();
  }
  else if (property == native_drawing_property::shape) {
    native_drawing_shape shape= static_cast<native_drawing_shape> (value);
    if (static_cast<unsigned int> (shape) >
        static_cast<unsigned int> (native_drawing_shape::orthogonal_polyline))
      shape= native_drawing_shape::line;
    native_drawing_shape_= shape;
  }
  refresh_native_ink_interaction ();
  refresh_native_drawing_selection_snapshot ();
  refresh_native_drawing_properties_snapshot ();
  publish_native_drawing_focus_refresh ();
  invalidate_all ();
}

void
edit_graphics_rep::refresh_native_drawing_properties_snapshot () {
  if (ui_endpoint == nullptr) return;
  native_drawing_properties_snapshot snapshot;
  snapshot.tool= native_drawing_tool_;
  snapshot.shape= native_drawing_shape_;
  snapshot.pressure_enabled= native_drawing_tool_ == native_drawing_tool::pen ?
    native_drawing_pressure_enabled_ : false;
  snapshot.snap_enabled= native_drawing_snap_enabled_;
  snapshot.selection_active= !native_drawing_selection_paths_.empty ();

  path gp= native_drawing_active_graphics ();
  if (!is_nil (gp)) snapshot.grid_enabled= native_drawing_grid_enabled (gp);

  if (snapshot.selection_active) {
    const path& p= native_drawing_selection_paths_.front ();
    if (has_subtree (et, p)) {
      tree object= subtree (et, p);
      tree color_value= native_drawing_object_property (
        object, "color", tree ("black"));
      string color_name= is_atomic (color_value) ? color_value->label : "black";
      if (color_name == "default" || N(color_name) == 0) color_name= "black";
      snapshot.rgba= native_ink_rgba (named_color (color_name));
      snapshot.line_width_pixels= native_line_width_pixels (
        native_drawing_object_property (
          object, "line-width", tree ("1ln")));
    }
  }
  else {
    string color_name= "black";
    double width= 1.0;
    if (!is_nil (gp)) {
      tree color_value= native_ink_property (gp, GR_COLOR, tree ("default"));
      if (is_atomic (color_value) && color_value->label != "default" &&
          N(color_value->label) != 0)
        color_name= color_value->label;
      width= native_line_width_pixels (
        native_ink_property (gp, GR_LINE_WIDTH, tree ("1ln")));
    }
    snapshot.rgba= native_drawing_color_override_ ? native_drawing_rgba_ :
      native_ink_rgba (named_color (color_name));
    snapshot.line_width_pixels= native_drawing_width_override_ ?
      native_drawing_width_pixels_ : width;
    if (native_drawing_tool_ == native_drawing_tool::highlighter) {
      int r= (snapshot.rgba >> 16) & 0xff;
      int g= (snapshot.rgba >> 8) & 0xff;
      int b= snapshot.rgba & 0xff;
      snapshot.rgba= native_ink_rgba (rgb_color (r, g, b, 96));
      snapshot.line_width_pixels=
        std::max (6.0, snapshot.line_width_pixels * 5.0);
    }
  }
  ui_endpoint->update_native_drawing_properties (snapshot);
}

void
edit_graphics_rep::refresh_native_ink_interaction () {
  if (ui_endpoint == nullptr) return;
  if (native_ink_interaction_dirty_) {
    std::vector<path> graphics;
    collect_native_ink_graphics (subtree (et, rp), rp, false, graphics);
    std::vector<path> pen_graphics;
    pen_graphics.reserve (graphics.size ());
    for (const path& gp: graphics) {
      tree mode= native_ink_property (gp, GR_MODE, tree ("line"));
      if (native_pen_mode (mode)) pen_graphics.push_back (copy (gp));
    }
    native_ink_paths_= std::move (pen_graphics);
    native_ink_interaction_dirty_= false;
    if (native_ink_paths_.empty ()) {
      ui_endpoint->update_native_ink_regions ({});
      native_drawing_selection_paths_.clear ();
      ui_endpoint->update_native_drawing_selection ({});
      refresh_native_drawing_properties_snapshot ();
      return;
    }
  }
  if (is_nil (eb)) return;

  std::vector<native_ink_interaction_snapshot> regions;
  regions.reserve (native_ink_paths_.size ());
  for (const path& gp: native_ink_paths_) {
    if (!has_subtree (et, gp) || !is_func (subtree (et, gp), GRAPHICS)) {
      native_ink_interaction_dirty_= true;
      return;
    }
    native_ink_interaction_snapshot region;
    bool ready= native_ink_region (gp, region);
    if (!ready) return;
    regions.push_back (region);
  }
  ui_endpoint->update_native_ink_regions (std::move (regions));
  refresh_native_drawing_selection_snapshot ();
  refresh_native_drawing_properties_snapshot ();
}

bool
edit_graphics_rep::native_drawing_object_bounds (
  path object, native_drawing_selection_box& bounds) {
  if (is_nil (eb) || is_nil (object) || !has_subtree (et, object)) return false;
  tree node= subtree (et, object);
  path graphics= path_up (object);
  frame object_frame;
  native_ink_interaction_snapshot region;
  if (!is_nil (graphics) && native_ink_region (graphics, region, &object_frame)) {
    std::vector<native_xy> stroke_points;
    if (native_penscript_screen_points (node, object_frame, stroke_points) &&
        !stroke_points.empty ()) {
      double x1= stroke_points[0].x, x2= stroke_points[0].x;
      double y1= stroke_points[0].y, y2= stroke_points[0].y;
      for (const auto& p: stroke_points) {
        x1= std::min (x1, p.x); x2= std::max (x2, p.x);
        y1= std::min (y1, p.y); y2= std::max (y2, p.y);
      }
      double pad= std::max (2.0, region.line_width_pixels) *
                  get_typesetter ()->env->pixel;
      bounds.x1= (SI) std::floor (x1 - pad);
      bounds.y1= (SI) std::floor (y1 - pad);
      bounds.x2= (SI) std::ceil (x2 + pad);
      bounds.y2= (SI) std::ceil (y2 + pad);
      return true;
    }
    tree radical= native_graphics_radical (node, nullptr);
    bool use_typeset_bounds=
      is_func (radical, TEXT_AT) || is_func (radical, MATH_AT) ||
      is_func (radical, DOCUMENT_AT);
    std::vector<native_xy> object_points;
    if (!use_typeset_bounds)
      native_collect_graphics_points (node, object_frame, object_points);
    if (!object_points.empty ()) {
      double x1= object_points[0].x, x2= object_points[0].x;
      double y1= object_points[0].y, y2= object_points[0].y;
      for (const auto& p: object_points) {
        x1= std::min (x1, p.x); x2= std::max (x2, p.x);
        y1= std::min (y1, p.y); y2= std::max (y2, p.y);
      }
      double pad= std::max (2.0, region.line_width_pixels) *
                  get_typesetter ()->env->pixel;
      bounds.x1= (SI) std::floor (x1 - pad);
      bounds.y1= (SI) std::floor (y1 - pad);
      bounds.x2= (SI) std::ceil (x2 + pad);
      bounds.y2= (SI) std::ceil (y2 + pad);
      return true;
    }
  }
  path anchor= copy (object);
  while (true) {
    if (is_func (node, WITH) && N(node) > 0) {
      int last= N(node) - 1;
      anchor= anchor * last;
      node= node[last];
      continue;
    }
    if (is_func (node, GR_TRANSFORM, 2)) {
      anchor= anchor * 0;
      node= node[0];
      continue;
    }
    break;
  }
  if (!is_atomic (node) && N(node) > 0) anchor= anchor * 0;
  bool found= false;
  path bp= eb->find_box_path (anchor, found);
  if (!found) return false;
  path box_path= path_up (bp);
  frame f= eb->find_frame (box_path);
  if (is_nil (f)) return false;
  point lim1, lim2;
  eb->find_limits (box_path, lim1, lim2);
  if (N(lim1) < 2 || N(lim2) < 2) return false;
  point p1= f (lim1);
  point p2= f (lim2);
  if (N(p1) < 2 || N(p2) < 2) return false;
  bounds.x1= min ((SI) p1[0], (SI) p2[0]);
  bounds.y1= min ((SI) p1[1], (SI) p2[1]);
  bounds.x2= max ((SI) p1[0], (SI) p2[0]);
  bounds.y2= max ((SI) p1[1], (SI) p2[1]);
  return true;
}

bool
edit_graphics_rep::native_drawing_selection_bounds (
  native_drawing_selection_box& bounds) {
  bool found= false;
  for (const path& p: native_drawing_selection_paths_) {
    if (!has_subtree (et, p)) continue;
    native_drawing_selection_box box;
    if (!native_drawing_object_bounds (p, box)) return false;
    if (!found) {
      bounds= box;
      found= true;
    }
    else {
      bounds.x1= min (bounds.x1, box.x1);
      bounds.y1= min (bounds.y1, box.y1);
      bounds.x2= max (bounds.x2, box.x2);
      bounds.y2= max (bounds.y2, box.y2);
    }
  }
  return found;
}

void
edit_graphics_rep::refresh_native_drawing_selection_snapshot () {
  if (ui_endpoint == nullptr) return;
  if (native_drawing_selection_paths_.empty ()) {
    ui_endpoint->update_native_drawing_selection ({});
    refresh_native_drawing_properties_snapshot ();
    return;
  }
  std::vector<native_drawing_selection_box> boxes;
  std::vector<path> valid;
  for (const path& p: native_drawing_selection_paths_) {
    native_drawing_selection_box box;
    if (!has_subtree (et, p)) continue;
    valid.push_back (copy (p));
    if (!native_drawing_object_bounds (p, box)) return;
    boxes.push_back (box);
  }
  native_drawing_selection_paths_= std::move (valid);
  ui_endpoint->update_native_drawing_selection (std::move (boxes));
  refresh_native_drawing_properties_snapshot ();
}

void
edit_graphics_rep::commit_native_drawing_transform (
  native_drawing_transform transform,
  const native_ink_sample* samples, std::size_t count) {
  if (samples == nullptr || count < 2 ||
      native_drawing_selection_paths_.empty ())
    return;

  path gp= path_up (native_drawing_selection_paths_.front ());
  if (is_nil (gp) || !has_subtree (et, gp) ||
      !is_func (subtree (et, gp), GRAPHICS))
    return;
  for (const path& p: native_drawing_selection_paths_)
    if (path_up (p) != gp || !has_subtree (et, p)) return;

  native_drawing_selection_box bounds;
  if (!native_drawing_selection_bounds (bounds)) return;

  tree transform_spec (TUPLE);
  switch (transform) {
  case native_drawing_transform::move: {
    double dx= (double) samples[count-1].x - (double) samples[0].x;
    double dy= (double) samples[count-1].y - (double) samples[0].y;
    if (std::hypot (dx, dy) <= 1.0e-12) return;
    transform_spec << "translation" << as_string (dx) << as_string (dy);
    break;
  }
  case native_drawing_transform::scale: {
    native_xy corners[4]= {
      {(double) bounds.x1, (double) bounds.y1},
      {(double) bounds.x2, (double) bounds.y1},
      {(double) bounds.x2, (double) bounds.y2},
      {(double) bounds.x1, (double) bounds.y2}
    };
    int nearest= 0;
    double best= std::numeric_limits<double>::infinity ();
    for (int i=0; i<4; ++i) {
      double dx= corners[i].x - samples[0].x;
      double dy= corners[i].y - samples[0].y;
      double d2= dx * dx + dy * dy;
      if (d2 < best) { best= d2; nearest= i; }
    }
    native_xy anchor_screen_xy= corners[(nearest + 2) % 4];
    double old_distance= std::hypot (
      (double) samples[0].x - anchor_screen_xy.x,
      (double) samples[0].y - anchor_screen_xy.y);
    double new_distance= std::hypot (
      (double) samples[count-1].x - anchor_screen_xy.x,
      (double) samples[count-1].y - anchor_screen_xy.y);
    if (!(old_distance > 1.0e-12) || !std::isfinite (new_distance)) return;
    double factor= new_distance / old_distance;
    factor= std::max (0.05, std::min (20.0, factor));
    if (std::fabs (factor - 1.0) <= 1.0e-9) return;
    transform_spec << "scaling"
                   << tree (_POINT, as_string (anchor_screen_xy.x),
                            as_string (anchor_screen_xy.y))
                   << as_string (factor) << as_string (factor);
    break;
  }
  case native_drawing_transform::rotate: {
    double cx= 0.5 * ((double) bounds.x1 + (double) bounds.x2);
    double cy= 0.5 * ((double) bounds.y1 + (double) bounds.y2);
    double sx= (double) samples[0].x - cx;
    double sy= (double) samples[0].y - cy;
    double ex= (double) samples[count-1].x - cx;
    double ey= (double) samples[count-1].y - cy;
    if (std::hypot (sx, sy) <= 1.0e-12 ||
        std::hypot (ex, ey) <= 1.0e-12)
      return;
    double angle= std::atan2 (ey, ex) - std::atan2 (sy, sx);
    while (angle > M_PI) angle -= 2.0 * M_PI;
    while (angle < -M_PI) angle += 2.0 * M_PI;
    if (std::fabs (angle) <= 1.0e-9) return;
    double degrees= angle * 57.2957795130823208768;
    transform_spec << "rotation"
                   << tree (_POINT, as_string (cx), as_string (cy))
                   << as_string (degrees);
    break;
  }
  }

  start_editing ();
  for (const path& p: native_drawing_selection_paths_) {
    tree wrapped (GR_TRANSFORM);
    wrapped << copy (subtree (et, p)) << copy (transform_spec);
    assign (p, wrapped);
  }
  end_editing ();
  mark_native_ink_interaction_dirty ();
  invalidate_all ();
}

void
edit_graphics_rep::commit_native_drawing_insert_space (
  bool horizontal, const native_ink_sample* samples, std::size_t count) {
  if (samples == nullptr || count < 2) return;
  path gp;
  frame f;
  if (!native_ink_target (samples[0].x, samples[0].y, gp, f) || is_nil (f))
    return;
  tree graphics= subtree (et, gp);
  if (!is_func (graphics, GRAPHICS)) return;

  tree frame_value= native_ink_property (gp, GR_FRAME, tree (UNINIT));
  if (!is_tuple (frame_value, "scale", 2) ||
      !is_func (frame_value[2], TUPLE, 2))
    return;

  native_ink_interaction_snapshot region;
  if (!native_ink_region (gp, region)) return;
  box gb;
  if (!native_drawing_graphics_box (gp, gb) || is_nil (gb)) return;
  frame local_frame= gb->get_frame ();
  if (is_nil (local_frame)) return;
  SI width= region.x2 - region.x1;
  SI height= region.y2 - region.y1;
  if (width <= 0 || height <= 0) return;

  SI divider= horizontal ? samples[0].x : samples[0].y;
  SI delta= horizontal ?
    samples[count-1].x - samples[0].x :
    samples[count-1].y - samples[0].y;
  SI threshold= 2 * get_pixel_size ();
  if (horizontal) {
    if (delta <= threshold) return;
  }
  else {
    if (delta >= -threshold) return;
  }

  struct move_operation { path p; };
  std::vector<move_operation> moves;
  for (int i=0; i<N(graphics); ++i) {
    if (is_atomic (graphics[i])) continue;
    native_drawing_selection_box bounds;
    if (!native_drawing_object_bounds (gp * i, bounds)) continue;
    SI center= horizontal ?
      (bounds.x1 + bounds.x2) / 2 : (bounds.y1 + bounds.y2) / 2;
    bool move= horizontal ? center > divider : center < divider;
    if (move) moves.push_back ({gp * i});
  }

  point actual_shift= local_frame (point (0.0, 0.0));
  if (N(actual_shift) < 2) return;
  SI new_width= horizontal ? width + delta : width;
  SI new_height= horizontal ? height : height - delta;

  tree transform_spec (TUPLE);
  transform_spec << "translation"
                 << as_string (horizontal ? delta : 0)
                 << as_string (horizontal ? 0 : delta);

  start_editing ();
  for (const move_operation& operation: moves) {
    if (!has_subtree (et, operation.p)) continue;
    tree wrapped (GR_TRANSFORM);
    wrapped << copy (subtree (et, operation.p)) << copy (transform_spec);
    assign (operation.p, wrapped);
  }
  native_drawing_set_canvas_geometry (
    gp, new_width, new_height, actual_shift);
  end_editing ();

  mark_native_ink_interaction_dirty ();
  invalidate_all ();
  publish_native_drawing_focus_refresh ();
}

void
edit_graphics_rep::commit_native_drawing_trim () {
  path gp= native_drawing_active_graphics ();
  if (is_nil (gp) || !has_subtree (et, gp) ||
      !is_func (subtree (et, gp), GRAPHICS))
    return;
  tree frame_value= native_ink_property (gp, GR_FRAME, tree (UNINIT));
  if (!is_tuple (frame_value, "scale", 2) ||
      !is_func (frame_value[2], TUPLE, 2))
    return;

  box gb;
  if (!native_drawing_graphics_box (gp, gb) || is_nil (gb) || N(gb) <= 1)
    return;
  frame local_frame= gb->get_frame ();
  if (is_nil (local_frame)) return;

  SI x1= MAX_SI, y1= MAX_SI, x2= -MAX_SI, y2= -MAX_SI;
  bool found= false;
  for (int i=1; i<N(gb); ++i) {
    box b= gb[i];
    if (is_nil (b)) continue;
    x1= min (x1, min (gb->sx1 (i), gb->sx3 (i)));
    y1= min (y1, min (gb->sy1 (i), gb->sy3 (i)));
    x2= max (x2, max (gb->sx2 (i), gb->sx4 (i)));
    y2= max (y2, max (gb->sy2 (i), gb->sy4 (i)));
    found= true;
  }
  if (!found || x2 <= x1 || y2 <= y1) return;

  edit_env env= get_typesetter ()->env;
  SI padding= max (env->get_length (GR_CROP_PADDING), 8 * get_pixel_size ());
  SI left= x1 - padding;
  SI bottom= y1 - padding;
  SI right= x2 + padding;
  SI top= y2 + padding;
  SI width= right - left;
  SI height= top - bottom;

  point actual_shift= local_frame (point (0.0, 0.0));
  if (N(actual_shift) < 2) return;
  point new_shift (
    actual_shift[0] - (double) left,
    actual_shift[1] - 0.5 * ((double) bottom + (double) top));

  start_editing ();
  native_drawing_set_canvas_geometry (gp, width, height, new_shift);
  end_editing ();

  mark_native_ink_interaction_dirty ();
  invalidate_all ();
  publish_native_drawing_focus_refresh ();
}

void
edit_graphics_rep::erase_native_drawing_objects (
  const native_ink_sample* samples, std::size_t count) {
  if (samples == nullptr || count == 0) return;
  path gp;
  frame f;
  if (!native_ink_target (samples[0].x, samples[0].y, gp, f)) return;
  tree graphics= subtree (et, gp);
  std::vector<native_xy> eraser= native_gesture_points (samples, count);
  double radius= 8.0 * get_typesetter ()->env->pixel;
  std::vector<int> remove_indices;
  for (int i=0; i<N(graphics); ++i) {
    tree object= graphics[i];
    if (is_empty (object)) continue;
    std::vector<native_xy> stroke_points;
    bool hit= native_penscript_screen_points (object, f, stroke_points) ?
      native_polyline_hits (stroke_points, eraser, radius) : false;
    if (!hit) {
      std::vector<native_xy> object_points;
      native_collect_graphics_points (object, f, object_points);
      if (!object_points.empty ())
        hit= native_polyline_hits (object_points, eraser, radius);
    }
    if (!hit) {
      native_drawing_selection_box box;
      if (native_drawing_object_bounds (gp * i, box))
        hit= native_polyline_hits_box (eraser, box, radius);
    }
    if (hit) remove_indices.push_back (i);
  }
  if (remove_indices.empty ()) return;
  start_editing ();
  for (auto it= remove_indices.rbegin (); it != remove_indices.rend (); ++it)
    remove (gp * (*it), 1);
  end_editing ();
  native_drawing_selection_paths_.clear ();
  refresh_native_drawing_selection_snapshot ();
  publish_native_drawing_focus_refresh ();
  refresh_native_ink_interaction ();
}

void
edit_graphics_rep::erase_native_drawing_segments (
  const native_ink_sample* samples, std::size_t count) {
  if (samples == nullptr || count == 0) return;
  path gp;
  frame f;
  if (!native_ink_target (samples[0].x, samples[0].y, gp, f)) return;
  tree graphics= subtree (et, gp);
  std::vector<native_xy> eraser= native_gesture_points (samples, count);
  double radius= 6.0 * get_typesetter ()->env->pixel;
  struct split_operation {
    int index;
    std::vector<tree> replacements;
  };
  std::vector<split_operation> operations;
  for (int i=0; i<N(graphics); ++i) {
    tree object= graphics[i];
    tree stroke;
    std::vector<native_xy> points;
    if (!native_penscript_screen_points (object, f, points, &stroke) ||
        points.empty () || N(stroke) < 4 || !is_func (stroke[3], TUPLE))
      continue;
    int n= std::min ((int) points.size (), N(stroke[3]));
    std::vector<bool> erased ((std::size_t) n, false);
    if (n == 1) {
      erased[0]= native_polyline_hits ({points[0]}, eraser, radius);
    }
    else {
      double r2= radius * radius;
      for (int j=1; j<n; ++j) {
        bool hit= false;
        if (eraser.size () == 1)
          hit= native_point_segment_distance2 (
                 eraser[0], points[j-1], points[j]) <= r2;
        else
          for (std::size_t k=1; k<eraser.size () && !hit; ++k)
            hit= native_segment_distance2 (
                   points[j-1], points[j], eraser[k-1], eraser[k]) <= r2;
        if (hit) erased[(std::size_t) j-1]= erased[(std::size_t) j]= true;
      }
    }
    bool any= std::any_of (erased.begin (), erased.end (), [] (bool v) {
      return v;
    });
    if (!any) continue;

    std::vector<tree> replacements;
    int start= -1;
    for (int j=0; j<=n; ++j) {
      bool keep= j < n && !erased[(std::size_t) j];
      if (keep && start < 0) start= j;
      if ((!keep || j == n) && start >= 0) {
        int end= j;
        tree ink (TUPLE);
        for (int k=start; k<end; ++k) ink << copy (stroke[3][k]);
        tree first_sample= stroke[3][start];
        tree last_sample= stroke[3][end-1];
        tree meta= copy (stroke[2]);
        if (is_compound (meta, "ink-meta") && N(meta) >= 1)
          meta[0]= native_ink_id ();
        tree part (PENSCRIPT);
        part << tree (_POINT, copy (first_sample[0]), copy (first_sample[1]))
             << tree (_POINT, copy (last_sample[0]), copy (last_sample[1]))
             << meta << ink;
        replacements.push_back (native_replace_radical (object, part));
        start= -1;
      }
    }
    operations.push_back ({i, std::move (replacements)});
  }
  if (operations.empty ()) return;
  start_editing ();
  for (auto it= operations.rbegin (); it != operations.rend (); ++it) {
    int i= it->index;
    if (it->replacements.empty ()) {
      remove (gp * i, 1);
      continue;
    }
    assign (gp * i, it->replacements[0]);
    for (std::size_t j=1; j<it->replacements.size (); ++j)
      insert (gp * (i + (int) j), tree (TUPLE, it->replacements[j]));
  }
  end_editing ();
  native_drawing_selection_paths_.clear ();
  refresh_native_drawing_selection_snapshot ();
  publish_native_drawing_focus_refresh ();
  refresh_native_ink_interaction ();
}

void
edit_graphics_rep::select_native_drawing_lasso (
  const native_ink_sample* samples, std::size_t count) {
  native_drawing_selection_paths_.clear ();
  if (samples == nullptr || count < 3) {
    refresh_native_drawing_selection_snapshot ();
    publish_native_drawing_focus_refresh ();
    invalidate_all ();
    return;
  }
  path gp;
  frame f;
  if (!native_ink_target (samples[0].x, samples[0].y, gp, f)) {
    refresh_native_drawing_selection_snapshot ();
    publish_native_drawing_focus_refresh ();
    invalidate_all ();
    return;
  }
  std::vector<native_xy> polygon= native_gesture_points (samples, count);
  tree graphics= subtree (et, gp);
  for (int i=0; i<N(graphics); ++i) {
    if (is_empty (graphics[i])) continue;
    native_drawing_selection_box box;
    if (!native_drawing_object_bounds (gp * i, box)) continue;
    native_xy center {
      0.5 * ((double) box.x1 + (double) box.x2),
      0.5 * ((double) box.y1 + (double) box.y2)
    };
    if (native_point_in_polygon (center, polygon))
      native_drawing_selection_paths_.push_back (gp * i);
  }
  refresh_native_drawing_selection_snapshot ();
  publish_native_drawing_focus_refresh ();
  invalidate_all ();
}

void
edit_graphics_rep::commit_native_drawing_shape (
  native_drawing_shape requested_shape,
  const native_ink_sample* samples, std::size_t count) {
  if (samples == nullptr || count == 0) return;
  path gp;
  frame f;
  if (!native_ink_target (samples[0].x, samples[0].y, gp, f)) return;
  tree graphics= subtree (et, gp);
  if (!is_func (graphics, GRAPHICS) || is_nil (f)) return;

  point first= f[point ((double) samples[0].x, (double) samples[0].y)];
  point last= f[point ((double) samples[count-1].x,
                      (double) samples[count-1].y)];
  if (N(first) < 2 || N(last) < 2) return;
  first= native_drawing_snap_point (gp, f, first);
  last= native_drawing_snap_point (gp, f, last);
  point sf= f (first);
  point sl= f (last);
  if (N(sf) < 2 || N(sl) < 2 ||
      norm (sl - sf) < 2.0 * get_pixel_size ())
    return;

  tree shape;
  switch (requested_shape) {
  case native_drawing_shape::line:
  case native_drawing_shape::arrow:
  case native_drawing_shape::double_arrow: {
    shape= tree (LINE);
    shape << native_point_tree (first[0], first[1])
          << native_point_tree (last[0], last[1]);
    break;
  }
  case native_drawing_shape::square:
  case native_drawing_shape::rectangle: {
    point end= requested_shape == native_drawing_shape::square ?
      native_constrained_end (first, last) : last;
    std::vector<point> points {
      first,
      point (end[0], first[1]),
      end,
      point (first[0], end[1])
    };
    shape= native_shape_polygon (CLINE, points);
    break;
  }
  case native_drawing_shape::circle: {
    point end= native_constrained_end (first, last);
    double cx= 0.5 * (first[0] + end[0]);
    double cy= 0.5 * (first[1] + end[1]);
    double rx= 0.5 * (end[0] - first[0]);
    double ry= 0.5 * (end[1] - first[1]);
    point p (cx + rx, cy);
    point q1 (cx - rx, cy);
    point q2 (cx, cy + ry);
    shape= tree (CARC);
    shape << native_point_tree (p[0], p[1])
          << native_point_tree (q1[0], q1[1])
          << native_point_tree (q2[0], q2[1]);
    break;
  }
  case native_drawing_shape::ellipse:
    shape= native_shape_polygon (
      CSPLINE, native_ellipse_points (first, last, false));
    break;
  case native_drawing_shape::triangle: {
    double x1= std::min (first[0], last[0]);
    double x2= std::max (first[0], last[0]);
    double y1= std::min (first[1], last[1]);
    double y2= std::max (first[1], last[1]);
    std::vector<point> points {
      point (0.5 * (x1 + x2), y2), point (x2, y1), point (x1, y1)
    };
    shape= native_shape_polygon (CLINE, points);
    break;
  }
  case native_drawing_shape::right_triangle: {
    std::vector<point> points {
      first, point (last[0], first[1]), point (first[0], last[1])
    };
    shape= native_shape_polygon (CLINE, points);
    break;
  }
  case native_drawing_shape::pentagon:
    shape= native_shape_polygon (
      CLINE, native_regular_polygon_points (first, last, 5));
    break;
  case native_drawing_shape::hexagon:
    shape= native_shape_polygon (
      CLINE, native_regular_polygon_points (first, last, 6));
    break;
  case native_drawing_shape::orthogonal_polyline: {
    double dx= last[0] - first[0];
    double dy= last[1] - first[1];
    point elbow= std::fabs (dx) >= std::fabs (dy) ?
      point (last[0], first[1]) : point (first[0], last[1]);
    shape= tree (LINE);
    shape << native_point_tree (first[0], first[1])
          << native_point_tree (elbow[0], elbow[1])
          << native_point_tree (last[0], last[1]);
    break;
  }
  }
  if (is_nil (shape) || (is_compound (shape) && N(shape) == 0)) return;

  tree color_value= native_ink_property (gp, GR_COLOR, tree ("default"));
  tree width_value= native_ink_property (gp, GR_LINE_WIDTH, tree ("default"));
  if (native_drawing_color_override_)
    color_value= native_color_tree_from_rgba (native_drawing_rgba_);
  if (native_drawing_width_override_)
    width_value= tree (as_string (native_drawing_width_pixels_) * "ln");

  tree wrapped (WITH);
  if (color_value != "default") wrapped << "color" << color_value;
  if (width_value != "default") wrapped << "line-width" << width_value;
  if (requested_shape == native_drawing_shape::arrow)
    wrapped << ARROW_END << "<gtr>";
  else if (requested_shape == native_drawing_shape::double_arrow)
    wrapped << ARROW_BEGIN << "<less>" << ARROW_END << "<gtr>";
  if (N(wrapped) == 0) wrapped= shape;
  else wrapped << shape;

  start_editing ();
  insert (gp * N(graphics), tree (TUPLE, wrapped));
  end_editing ();
  native_drawing_selection_paths_.clear ();
  refresh_native_ink_interaction ();
  refresh_native_drawing_selection_snapshot ();
  refresh_native_drawing_properties_snapshot ();
  publish_native_drawing_focus_refresh ();
}

void
edit_graphics_rep::commit_native_drawing_gesture (
  native_drawing_tool tool, const native_ink_sample* samples,
  std::size_t count) {
  if (samples == nullptr || count == 0) return;
  if (tool == native_drawing_tool::object_eraser) {
    erase_native_drawing_objects (samples, count);
    return;
  }
  if (tool == native_drawing_tool::segment_eraser) {
    erase_native_drawing_segments (samples, count);
    return;
  }
  if (tool == native_drawing_tool::lasso) {
    select_native_drawing_lasso (samples, count);
    return;
  }
  if (tool == native_drawing_tool::shape) {
    commit_native_drawing_shape (native_drawing_shape_, samples, count);
    return;
  }
  path gp;
  frame f;
  bool target_found= native_ink_target (samples[0].x, samples[0].y, gp, f);
  if (!target_found) return;
  tree graphics= subtree (et, gp);
  if (!is_func (graphics, GRAPHICS)) return;

  tree ink (TUPLE);
  point first;
  point last;
  for (std::size_t i= 0; i < count; ++i) {
    point p= f [point (static_cast<double> (samples[i].x),
                      static_cast<double> (samples[i].y))];
    if (N(p) < 2) continue;
    if (N(ink) == 0) first= p;
    last= p;
    tree sample (TUPLE);
    double pressure= (tool == native_drawing_tool::pen &&
                      native_drawing_pressure_enabled_) ?
      clamp_pressure (samples[i].pressure) : 1.0;
    sample << as_string (p[0]) << as_string (p[1])
           << as_string (samples[i].time)
           << as_string (pressure);
    ink << sample;
  }
  if (N(ink) == 0) return;

  double pixel= get_typesetter ()->env->pixel;
  double virtual_pixel= f->inverse_scalar (pixel);
  tree metadata= tool == native_drawing_tool::highlighter ?
    compound ("ink-meta", native_ink_id (), as_string (virtual_pixel),
              "highlighter") :
    compound ("ink-meta", native_ink_id (), as_string (virtual_pixel));
  tree stroke (PENSCRIPT);
  stroke << find_point (first) << find_point (last) << metadata << ink;

  tree color_value= native_ink_property (gp, GR_COLOR, tree ("default"));
  tree width_value= native_ink_property (gp, GR_LINE_WIDTH, tree ("default"));
  tree enhance_value= native_ink_property (gp, GR_PEN_ENHANCE, tree ("default"));
  if (native_drawing_color_override_)
    color_value= native_color_tree_from_rgba (native_drawing_rgba_);
  if (native_drawing_width_override_)
    width_value= tree (as_string (native_drawing_width_pixels_) * "ln");
  if (tool == native_drawing_tool::highlighter) {
    string color_name= is_atomic (color_value) ? color_value->label : "black";
    if (color_name == "default" || N(color_name) == 0) color_name= "black";
    int r= 0, g= 0, b= 0, a= 255;
    get_rgb_color (named_color (color_name), r, g, b, a);
    color_value= tree (get_hex_color (rgb_color (r, g, b, 96)));
    double width= std::max (6.0, native_line_width_pixels (width_value) * 5.0);
    width_value= tree (as_string (width) * "ln");
  }
  tree wrapped (WITH);
  if (color_value != "default") wrapped << "color" << color_value;
  if (width_value != "default") wrapped << "line-width" << width_value;
  if (enhance_value != "default") wrapped << "pen-enhance" << enhance_value;
  if (N(wrapped) == 0) wrapped= stroke;
  else wrapped << stroke;

  start_editing ();
  insert (gp * N(graphics), tree (TUPLE, wrapped));
  end_editing ();
  refresh_native_ink_interaction ();
}

void
edit_graphics_rep::commit_native_ink_stroke (
  const native_ink_sample* samples, std::size_t count) {
  commit_native_drawing_gesture (native_drawing_tool::pen, samples, count);
}

void
edit_graphics_rep::back_in_text_at (tree t, path p, bool forward) {
  (void) forward;
  int i= last_item (p);
  if ((i == 0) && is_empty (t[0])) {
    p= path_up (p);
    if (is_func (subtree (et, path_up (p)), WITH)) p= path_up (p);
    tree st= subtree (et, path_up (p));
    if (is_func (st, GRAPHICS)) {
      if (N(st) == 1) assign (p, "");
      else {
        remove (p, 1);
        go_to_border (path_up (p) * 0, true);
      }
    }
  }
}

bool
edit_graphics_rep::mouse_graphics (string type, SI x, SI y, int m, time_t t,
                                   array<double> data) {
  //cout << type << ", " << x << ", " << y << ", " << m << ", " << t << "\n";
  //cout << "et= " << et << "\n";
  //cout << "tp= " << tp << "\n";
  //cout << "gp= " << graphics_path () << "\n";
  (void) t;
  // apply_changes (); // FIXME: remove after review of synchronization
  frame f= find_frame ();
  if (!is_nil (f)) {
    if (type == "wheel") {
      point  p0= f [point (0.0, 0.0)];
      point  p1= f [point (data[0], data[1])];
      point  dp= p1 - p0;
      //string sx= as_string (dp[0]);
      //string sy= as_string (dp[1]);
      //call ("graphics-wheel", sx, sy);
      point lim1, lim2;
      find_limits (lim1, lim2);
      double dx= dp[0] / max (lim2[0] - lim1[0], 0.000001);
      double dy= dp[1] / max (lim2[1] - lim1[1], 0.000001);
      call ("graphics-wheel", as_string (dx), as_string (dy));
      return true;
    }

    if (!over_graphics (x, y))
      return false;
    if (type == "move" || type == "dragging-left")
      if (check_event (MOTION_EVENT))
        return true;

    point p = f [point (x, y)];
    graphical_select (p[0], p[1]); // init the caching for adjust().
    p= adjust (p);
    gr_x= p[0];
    gr_y= p[1];
    string sx= as_string (p[0]);
    string sy= as_string (p[1]);
    invalidate_graphical_object ();
    double pressure= (N(data) == 0? 1.0: data[0]);
    call ("set-keyboard-modifiers", object (m));
    if (type == "move")
      call ("graphics-move", sx, sy);
    else if (type == "release-left" || type == "double-left")
      call ("graphics-release-left", sx, sy, (double) t, 1);
    else if (type == "release-middle")
      call ("graphics-release-middle", sx, sy);
    else if (type == "release-right" || type == "double-right")
      call ("graphics-release-right", sx, sy);
    else if (type == "start-drag-left")
      call ("graphics-start-drag-left", sx, sy, (double) t, pressure);
    else if (type == "dragging-left")
      call ("graphics-dragging-left", sx, sy, (double) t, pressure);
    else if (type == "end-drag-left")
      call ("graphics-end-drag-left", sx, sy, (double) t, pressure);
    else if (type == "start-drag-right")
      call ("graphics-start-drag-right", sx, sy);
    else if (type == "dragging-right")
      call ("graphics-dragging-right", sx, sy);
    else if (type == "end-drag-right")
      call ("graphics-end-drag-right", sx, sy);
    else if (type == "drop-object")
      call ("graphics-drop-object", sx, sy);
    invalidate_graphical_object ();
    notify_change (THE_CURSOR);
    return true;
  }
  //cout << "No frame " << tp << ", " << subtree (et, path_up (tp)) << "\n";
  return false;
}

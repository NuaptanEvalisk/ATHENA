
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

path
native_graphics_radical_path (path object_path, tree object) {
  path result= copy (object_path);
  tree radical= object;
  while (true) {
    if (is_func (radical, WITH) && N(radical) >= 1) {
      int last= N(radical) - 1;
      result= result * last;
      radical= radical[last];
      continue;
    }
    if (is_func (radical, GR_TRANSFORM, 2) &&
        is_transformation (radical[1])) {
      result= result * 0;
      radical= radical[0];
      continue;
    }
    break;
  }
  return result;
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
    point p= f [point (x, y)];
    // cout << type << " at " << p << " [" << lim1 << ", " << lim2 << "]\n";
    if (N(lim1) == 2)
      if ((p[0]<lim1[0]) || (p[0]>lim2[0]) || (p[1]<lim1[1]) || (p[1]>lim2[1]))
        return false;
    return true;
  }
  return false;
}

frame
edit_graphics_rep::find_frame (bool last) {
  path gp= graphics_path ();
  bool bp_found;
  path bp= eb->find_box_path (gp, bp_found);
  if (bp_found) return eb->find_frame (path_up (bp), last);
  else return frame ();
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

tree
edit_graphics_rep::find_point (point p) {
  return tree (_POINT, as_string (p[0]), as_string (p[1]));
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

string
native_graphics_object_property_name (string name) {
  return starts (name, "gr-") ? name (3, N(name)) : name;
}

bool
native_graphics_common_property (string name) {
  return name == "gid" || name == "proviso" || name == "magnify" ||
         name == "color" || name == "opacity";
}

bool
native_graphics_curve (tree radical) {
  return is_func (radical, LINE) || is_func (radical, CLINE) ||
         is_func (radical, ARC) || is_func (radical, CARC) ||
         is_func (radical, SPLINE) || is_func (radical, CSPLINE) ||
         is_func (radical, BEZIER) || is_func (radical, CBEZIER) ||
         is_func (radical, SMOOTH) || is_func (radical, CSMOOTH);
}

bool
native_graphics_property_supported (tree object, string property) {
  string name= native_graphics_object_property_name (property);
  tree radical= native_graphics_radical (object, nullptr);
  if (is_func (radical, GR_GROUP)) return true;
  if (native_graphics_common_property (name)) return true;
  if (is_func (radical, _POINT))
    return name == "fill-color" || name == "point-style" ||
           name == "point-size" || name == "point-border";
  if (native_graphics_curve (radical))
    return name == "fill-color" || name == "line-width" ||
           name == "line-join" || name == "line-caps" ||
           name == "line-effects" || name == "line-portion" ||
           name == "dash-style" || name == "dash-style-unit" ||
           name == "arrow-begin" || name == "arrow-end" ||
           name == "arrow-length" || name == "arrow-height";
  if (is_func (radical, TEXT_AT) || is_func (radical, MATH_AT))
    return name == "text-at-halign" || name == "text-at-valign" ||
           name == "text-at-repulse" || name == "text-at-snapping";
  if (is_func (radical, DOCUMENT_AT))
    return name == "text-at-halign" || name == "doc-at-valign" ||
           name == "text-at-repulse" || name == "text-at-snapping" ||
           name == "fill-color" || name == "doc-at-width" ||
           name == "doc-at-hmode" || name == "doc-at-ppsep" ||
           name == "doc-at-border" || name == "doc-at-padding";
  if (is_func (radical, PENSCRIPT))
    return name == "pen-enhance" || name == "line-width" ||
           name == "line-join" || name == "line-caps" ||
           name == "line-effects" || name == "line-portion" ||
           name == "dash-style" || name == "dash-style-unit" ||
           name == "arrow-begin" || name == "arrow-end" ||
           name == "arrow-length" || name == "arrow-height";
  return false;
}

const std::vector<string>&
native_graphics_all_properties () {
  static const std::vector<string> names {
    "gid", "proviso", "magnify", "color", "opacity",
    "point-style", "point-size", "point-border", "line-width",
    "line-join", "line-caps", "line-effects", "line-portion",
    "dash-style", "dash-style-unit", "arrow-begin", "arrow-end",
    "arrow-length", "arrow-height", "fill-color", "fill-style",
    "text-at-halign", "text-at-valign", "text-at-repulse",
    "text-at-snapping", "doc-at-valign", "doc-at-width",
    "doc-at-hmode", "doc-at-ppsep", "doc-at-border", "doc-at-padding",
    "pen-enhance"
  };
  return names;
}

tree
native_graphics_property_default (string property) {
  string name= native_graphics_object_property_name (property);
  if (name == "gid") return tree ("default");
  if (name == "proviso") return tree ("true");
  if (name == "magnify") return tree ("1");
  if (name == "color") return tree ("black");
  if (name == "opacity") return tree ("100%");
  if (name == "point-style") return tree ("disk");
  if (name == "point-size") return tree ("2.5ln");
  if (name == "point-border") return tree ("1ln");
  if (name == "line-width") return tree ("1ln");
  if (name == "line-join" || name == "line-caps" || name == "line-effects")
    return tree ("normal");
  if (name == "line-portion") return tree ("1");
  if (name == "dash-style") return tree ("none");
  if (name == "dash-style-unit") return tree ("5ln");
  if (name == "arrow-begin" || name == "arrow-end") return tree ("none");
  if (name == "arrow-length" || name == "arrow-height") return tree ("5ln");
  if (name == "fill-color") return tree ("none");
  if (name == "fill-style") return tree ("plain");
  if (name == "text-at-halign") return tree ("left");
  if (name == "text-at-valign") return tree ("base");
  if (name == "text-at-repulse") return tree ("off");
  if (name == "text-at-snapping") return tree ("1spc");
  if (name == "doc-at-valign") return tree ("top");
  if (name == "doc-at-width") return tree ("1par");
  if (name == "doc-at-hmode") return tree ("min");
  if (name == "doc-at-ppsep") return tree ("0fn");
  if (name == "doc-at-border") return tree ("0ln");
  if (name == "doc-at-padding") return tree ("0spc");
  if (name == "pen-enhance") return tree ("gaussian");
  return tree ("default");
}

string
native_graphics_canvas_property_name (string object_property) {
  string name= native_graphics_object_property_name (object_property);
  return "gr-" * name;
}

tree
native_remove_object_property_impl (tree object, string name, bool& removed) {
  if (is_func (object, GR_TRANSFORM, 2)) {
    tree result= copy (object);
    result[0]= native_remove_object_property_impl (
      result[0], name, removed);
    return result;
  }
  if (!is_func (object, WITH) || N(object) < 1) return copy (object);
  tree result (WITH);
  int last= N(object)-1;
  for (int i=0; i+1<last; i+=2) {
    if (!removed && is_atomic (object[i]) && object[i]->label == name) {
      removed= true;
      continue;
    }
    result << copy (object[i]) << copy (object[i+1]);
  }
  if (!removed) {
    tree nested= native_remove_object_property_impl (
      object[last], name, removed);
    result << nested;
  }
  else result << copy (object[last]);
  if (N(result) == 1) return copy (result[0]);
  return result;
}

tree
native_remove_with_property_direct (tree wrapper, string name, bool& removed) {
  if (!is_func (wrapper, WITH) || N(wrapper) < 1) return copy (wrapper);
  tree result (WITH);
  int last= N(wrapper)-1;
  for (int i=0; i+1<last; i+=2) {
    if (is_atomic (wrapper[i]) && wrapper[i]->label == name) {
      removed= true;
      continue;
    }
    result << copy (wrapper[i]) << copy (wrapper[i+1]);
  }
  result << copy (wrapper[last]);
  if (N(result) == 1) return copy (result[0]);
  return result;
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

namespace {

string
native_graphics_tmpt (SI value) {
  return as_string (value) * "tmpt";
}

double
native_graphics_numeric (tree value, double fallback= 1.0) {
  if (!is_atomic (value) || !is_double (value->label)) return fallback;
  double result= as_double (value->label);
  return std::isfinite (result) ? result : fallback;
}

} // namespace

path
edit_graphics_rep::native_graphics_canvas_path () {
  path graphics= search_upwards (GRAPHICS);
  if (!is_nil (graphics) && has_subtree (et, graphics) &&
      is_func (subtree (et, graphics), GRAPHICS))
    return graphics;
  graphics= native_drawing_active_graphics ();
  if (!is_nil (graphics) && has_subtree (et, graphics) &&
      is_func (subtree (et, graphics), GRAPHICS))
    return graphics;

  std::vector<path> candidates;
  collect_native_ink_graphics (subtree (et, rp), rp, false, candidates);
  path group_graphics;
  int group_count= 0;
  for (const path& candidate: candidates) {
    if (!has_subtree (et, candidate) ||
        !is_func (subtree (et, candidate), GRAPHICS))
      continue;
    tree mode= native_ink_property (candidate, GR_MODE, tree (UNINIT));
    if (is_func (mode, TUPLE, 2) && is_atomic (mode[0]) &&
        mode[0]->label == "group-edit") {
      group_graphics= copy (candidate);
      ++group_count;
    }
  }
  if (group_count == 1) return group_graphics;
  if (candidates.size () == 1) return copy (candidates.front ());
  return path ();
}

bool
edit_graphics_rep::native_graphics_canvas_focused () {
  if (inside_active_graphics (true)) return true;
  path graphics= native_drawing_active_graphics ();
  if (is_nil (graphics)) return false;
  path wrapper= path_up (graphics);
  return !is_nil (wrapper) && path_has_prefix (tp, wrapper);
}

tree
edit_graphics_rep::native_graphics_canvas_geometry () {
  path graphics= native_graphics_canvas_path ();
  tree geometry= is_nil (graphics) ? tree (UNINIT) :
    native_ink_property (graphics, GR_GEOMETRY, tree (UNINIT));
  if (is_tuple (geometry, "geometry", 2))
    return tree (TUPLE, "geometry", copy (geometry[1]), copy (geometry[2]),
                 "center");
  if (is_tuple (geometry, "geometry", 3)) return copy (geometry);
  return tree (TUPLE, "geometry", "1par", "0.6par", "center");
}

tree
edit_graphics_rep::native_graphics_canvas_frame () {
  path graphics= native_graphics_canvas_path ();
  tree frame_value= is_nil (graphics) ? tree (UNINIT) :
    native_ink_property (graphics, GR_FRAME, tree (UNINIT));
  if (is_tuple (frame_value, "scale", 2) &&
      is_func (frame_value[2], TUPLE, 2))
    return copy (frame_value);
  return tree (TUPLE, "scale", "1cm",
               tree (TUPLE, "0.5gw", "0.5gh"));
}

double
edit_graphics_rep::native_graphics_canvas_zoom () {
  path graphics= native_graphics_canvas_path ();
  if (is_nil (graphics)) return 1.0;
  return native_graphics_numeric (
    native_ink_property (graphics, "magnify", tree ("1")), 1.0);
}

bool
edit_graphics_rep::native_graphics_canvas_auto_crop () {
  path graphics= native_graphics_canvas_path ();
  if (is_nil (graphics)) return false;
  tree value= native_ink_property (graphics, GR_AUTO_CROP, tree ("false"));
  return is_atomic (value) && value->label == "true";
}

string
edit_graphics_rep::native_graphics_canvas_crop_padding () {
  path graphics= native_graphics_canvas_path ();
  if (is_nil (graphics)) return "0spc";
  tree value= native_ink_property (
    graphics, GR_CROP_PADDING, tree ("0spc"));
  return is_atomic (value) ? value->label : "0spc";
}

void
edit_graphics_rep::apply_native_graphics_canvas_action (
  native_graphics_canvas_action action,
  string first, string second, double value) {
  path graphics= native_graphics_canvas_path ();
  if (is_nil (graphics) || !has_subtree (et, graphics)) return;

  edit_env env= get_typesetter ()->env;
  if (is_nil (env)) return;

  auto finish= [&] {
    mark_native_ink_interaction_dirty ();
    refresh_native_ink_interaction ();
    refresh_native_drawing_properties_snapshot ();
    publish_native_drawing_focus_refresh ();
    invalidate_all ();
  };

  auto set_properties=
    [&] (const std::vector<std::pair<string, tree>>& properties) {
      if (properties.empty ()) return;
      start_editing ();
      bool changed= native_drawing_set_graphics_properties (graphics, properties);
      end_editing ();
      if (changed) finish ();
    };

  tree geometry= native_graphics_canvas_geometry ();
  tree frame_value= native_graphics_canvas_frame ();

  if (action == native_graphics_canvas_action::set_width ||
      action == native_graphics_canvas_action::set_height ||
      action == native_graphics_canvas_action::set_geo_valign ||
      action == native_graphics_canvas_action::set_extents) {
    string width= is_atomic (geometry[1]) ? geometry[1]->label : "1par";
    string height= is_atomic (geometry[2]) ? geometry[2]->label : "0.6par";
    string align= is_atomic (geometry[3]) ? geometry[3]->label : "center";
    if (action == native_graphics_canvas_action::set_width) width= first;
    else if (action == native_graphics_canvas_action::set_height) height= first;
    else if (action == native_graphics_canvas_action::set_geo_valign) align= first;
    else {
      width= first;
      height= second;
    }
    set_properties ({{GR_GEOMETRY,
      tree (TUPLE, "geometry", width, height, align)}});
    return;
  }

  if (action == native_graphics_canvas_action::set_unit) {
    tree origin= copy (frame_value[2]);
    set_properties ({{GR_FRAME, tree (TUPLE, "scale", first, origin)}});
    return;
  }

  if (action == native_graphics_canvas_action::set_origin) {
    tree origin (TUPLE);
    origin << first << second;
    set_properties ({{GR_FRAME,
      tree (TUPLE, "scale", copy (frame_value[1]), origin)}});
    return;
  }

  if (action == native_graphics_canvas_action::toggle_auto_crop) {
    set_properties ({{GR_AUTO_CROP,
      tree (native_graphics_canvas_auto_crop () ? "false" : "true")}});
    return;
  }

  if (action == native_graphics_canvas_action::set_crop_padding) {
    set_properties ({{GR_CROP_PADDING, tree (first)}});
    return;
  }

  if (action == native_graphics_canvas_action::zoom ||
      action == native_graphics_canvas_action::set_zoom) {
    double factor= value;
    if (action == native_graphics_canvas_action::set_zoom) {
      double current= native_graphics_canvas_zoom ();
      if (!std::isfinite (current) || current <= 0.0) current= 1.0;
      factor= value / current;
    }
    if (!std::isfinite (factor) || factor <= 0.0) return;

    SI unit= env->as_length (frame_value[1]);
    SI new_unit= (SI) std::llround ((double) unit * factor);
    if (new_unit <= 100 || new_unit >= 10000000) return;

    SI gw= env->as_length ("1gw");
    SI gh= env->as_length ("1gh");
    SI ox= env->as_length (frame_value[2][0]);
    SI oy= env->as_length (frame_value[2][1]);
    SI nox= (SI) std::llround (0.5 * (double) gw +
      factor * ((double) ox - 0.5 * (double) gw));
    SI noy= (SI) std::llround (0.5 * (double) gh +
      factor * ((double) oy - 0.5 * (double) gh));
    tree origin (TUPLE);
    origin << native_graphics_tmpt (nox) << native_graphics_tmpt (noy);
    tree new_frame (TUPLE);
    new_frame << "scale" << native_graphics_tmpt (new_unit) << origin;
    double magnify= native_graphics_canvas_zoom () * factor;
    set_properties ({{GR_FRAME, new_frame},
                     {"magnify", tree (as_string (magnify))}});
    return;
  }

  if (action == native_graphics_canvas_action::move_origin) {
    if (native_graphics_canvas_auto_crop ()) return;
    SI ox= env->as_length (frame_value[2][0]);
    SI oy= env->as_length (frame_value[2][1]);
    SI dx= env->as_length (first);
    SI dy= env->as_length (second);
    tree origin (TUPLE);
    origin << native_graphics_tmpt (ox + dx)
           << native_graphics_tmpt (oy + dy);
    set_properties ({{GR_FRAME,
      tree (TUPLE, "scale", copy (frame_value[1]), origin)}});
    return;
  }

  if (action == native_graphics_canvas_action::change_extents) {
    path p= path_up (tp);
    while (!is_nil (p)) {
      if (has_subtree (et, p)) {
        tree current= subtree (et, p);
        if (is_compound (current, "draw-over") && N(current) >= 3) {
          SI dw= env->as_length (first);
          SI dh= env->as_length (second);
          SI delta= dw != 0 ? dw : dh;
          SI padding= env->as_length (current[2]);
          SI next= max ((SI) 0, padding + delta);
          start_editing ();
          assign (p * 2, tree (native_graphics_tmpt (next)));
          end_editing ();
          finish ();
          return;
        }
      }
      if (p == rp) break;
      p= path_up (p);
    }

    if (native_graphics_canvas_auto_crop ()) return;
    SI width= env->as_length (geometry[1]);
    SI height= env->as_length (geometry[2]);
    SI dw= env->as_length (first);
    SI dh= env->as_length (second);
    SI next_width= max ((SI) 1, width + dw);
    SI next_height= max ((SI) 1, height + dh);
    string align= is_atomic (geometry[3]) ? geometry[3]->label : "center";
    set_properties ({{GR_GEOMETRY,
      tree (TUPLE, "geometry",
            native_graphics_tmpt (next_width),
            native_graphics_tmpt (next_height), align)}});
    return;
  }

  if (action == native_graphics_canvas_action::change_geo_valign) {
    string align= is_atomic (geometry[3]) ? geometry[3]->label : "center";
    bool down= value != 0.0;
    string next;
    if (down) {
      if (align == "top") next= "center";
      else if (align == "center") next= "bottom";
      else if (align == "bottom") next= "top";
      else next= "default";
    }
    else {
      if (align == "top") next= "bottom";
      else if (align == "center") next= "top";
      else if (align == "bottom") next= "center";
      else next= "default";
    }
    set_properties ({{GR_GEOMETRY,
      tree (TUPLE, "geometry", copy (geometry[1]), copy (geometry[2]), next)}});
  }
}

bool
edit_graphics_rep::native_graphics_canvas_keypress (string key) {
  if (!native_graphics_canvas_focused ()) return false;
  if ((key == "delete" || key == "backspace") && selection_active_any ())
    return false;
  path p= tp;
  while (!is_nil (p)) {
    if (has_subtree (et, p)) {
      tree current= subtree (et, p);
      if (is_func (current, TEXT_AT) ||
          is_func (current, MATH_AT) ||
          is_func (current, DOCUMENT_AT))
        return false;
      if (is_func (current, GRAPHICS)) break;
    }
    p= path_up (p);
  }
  path group_graphics;
  string group_submode;
  if (native_graphics_group_mode (group_graphics, group_submode) &&
      (group_submode == "move" || group_submode == "zoom" ||
       group_submode == "rotate" || group_submode == "group-ungroup" ||
       group_submode == "edit-props")) {
    if (key == "return") {
      native_graphics_apply_props_at_mouse ();
      return true;
    }
    if (key == "S-return") {
      native_graphics_get_props_at_mouse ();
      return true;
    }
    if (key == "escape") {
      native_graphics_group_clear_selection ();
      return true;
    }
    if (key == "delete" || key == "backspace") {
      if (!native_graphics_selection_active ()) return true;
      (void) native_graphics_cut_selection ();
      return true;
    }
    if (key == "tab" || key == "S-tab") {
      tree objects= subtree (et, group_graphics);
      if (N(objects) == 0) return true;
      int current= -1;
      if (!native_drawing_selection_paths_.empty () &&
          path_up (native_drawing_selection_paths_.front ()) == group_graphics)
        current= last_item (native_drawing_selection_paths_.front ());
      int direction= key == "S-tab" ? -1 : 1;
      for (int step=1; step<=N(objects); ++step) {
        int index= current < 0 ?
          (direction > 0 ? step-1 : N(objects)-step) :
          (current + direction * step + N(objects) * 2) % N(objects);
        if (!is_atomic (objects[index]) && !is_empty (objects[index])) {
          native_graphics_group_select_one (group_graphics * index, false);
          break;
        }
      }
      return true;
    }
  }
  if (key == "+")
    apply_native_graphics_canvas_action (
      native_graphics_canvas_action::zoom, "", "", 1.189207115);
  else if (key == "-")
    apply_native_graphics_canvas_action (
      native_graphics_canvas_action::zoom, "", "", 0.840896415);
  else if (N(key) == 3 && starts (key, "A-") &&
           key[2] >= '1' && key[2] <= '9')
    apply_native_graphics_canvas_action (
      native_graphics_canvas_action::set_zoom, "", "",
      1.0 / (double) (key[2]-'0'));
  else if (N(key) == 1 && key[0] >= '1' && key[0] <= '9')
    apply_native_graphics_canvas_action (
      native_graphics_canvas_action::set_zoom, "", "",
      (double) (key[0]-'0'));
  else if (key == "c")
    apply_native_graphics_canvas_action (
      native_graphics_canvas_action::set_origin, "0.5gw", "0.5gh");
  else if (key == "t")
    apply_native_graphics_canvas_action (
      native_graphics_canvas_action::set_origin, "0gw", "1gh");
  else if (key == "l")
    apply_native_graphics_canvas_action (
      native_graphics_canvas_action::set_origin, "0gw", "0.5gh");
  else if (key == "b")
    apply_native_graphics_canvas_action (
      native_graphics_canvas_action::set_origin, "0gw", "0gh");
  else if (key == "left")
    apply_native_graphics_canvas_action (
      native_graphics_canvas_action::move_origin, "+0.01gw", "0gh");
  else if (key == "right")
    apply_native_graphics_canvas_action (
      native_graphics_canvas_action::move_origin, "-0.01gw", "0gh");
  else if (key == "down")
    apply_native_graphics_canvas_action (
      native_graphics_canvas_action::move_origin, "0gw", "+0.01gh");
  else if (key == "up")
    apply_native_graphics_canvas_action (
      native_graphics_canvas_action::move_origin, "0gw", "-0.01gh");
  else if (key == "S-left")
    apply_native_graphics_canvas_action (
      native_graphics_canvas_action::move_origin, "+0.1gw", "0gh");
  else if (key == "S-right")
    apply_native_graphics_canvas_action (
      native_graphics_canvas_action::move_origin, "-0.1gw", "0gh");
  else if (key == "S-down")
    apply_native_graphics_canvas_action (
      native_graphics_canvas_action::move_origin, "0gw", "+0.1gh");
  else if (key == "S-up")
    apply_native_graphics_canvas_action (
      native_graphics_canvas_action::move_origin, "0gw", "-0.1gh");
  else if (key == "A-left")
    apply_native_graphics_canvas_action (
      native_graphics_canvas_action::change_extents, "-0.1cm", "0cm");
  else if (key == "A-right")
    apply_native_graphics_canvas_action (
      native_graphics_canvas_action::change_extents, "+0.1cm", "0cm");
  else if (key == "A-down")
    apply_native_graphics_canvas_action (
      native_graphics_canvas_action::change_extents, "0cm", "+0.1cm");
  else if (key == "A-up")
    apply_native_graphics_canvas_action (
      native_graphics_canvas_action::change_extents, "0cm", "-0.1cm");
  else if (key == "A-S-left")
    apply_native_graphics_canvas_action (
      native_graphics_canvas_action::change_extents, "-1cm", "0cm");
  else if (key == "A-S-right")
    apply_native_graphics_canvas_action (
      native_graphics_canvas_action::change_extents, "+1cm", "0cm");
  else if (key == "A-S-down")
    apply_native_graphics_canvas_action (
      native_graphics_canvas_action::change_extents, "0cm", "+1cm");
  else if (key == "A-S-up")
    apply_native_graphics_canvas_action (
      native_graphics_canvas_action::change_extents, "0cm", "-1cm");
  else return false;
  return true;
}

void
edit_graphics_rep::native_graphics_canvas_pinch_start () {
  if (!native_graphics_canvas_focused ()) return;
  if (native_graphics_pinch_active_) return;
  native_graphics_pinch_zoom_= native_graphics_canvas_zoom ();
  native_graphics_pinch_active_= true;
  start_editing ();
}

void
edit_graphics_rep::native_graphics_canvas_pinch_end () {
  if (!native_graphics_pinch_active_) return;
  native_graphics_pinch_active_= false;
  end_editing ();
}

void
edit_graphics_rep::native_graphics_canvas_pinch_scale (double scale) {
  if (!native_graphics_pinch_active_) {
    if (!native_graphics_canvas_focused ()) return;
    native_graphics_canvas_pinch_start ();
  }
  if (!std::isfinite (scale) || scale <= 0.0) return;
  double lg= std::log (scale) / std::log (2.0);
  double rounded= std::round (24.0 * lg) / 24.0;
  double snapped= std::exp (std::log (2.0) * rounded);
  apply_native_graphics_canvas_action (
    native_graphics_canvas_action::set_zoom, "", "",
    native_graphics_pinch_zoom_ * snapped);
}

void
edit_graphics_rep::native_graphics_canvas_wheel (double dx, double dy) {
  if (!native_graphics_canvas_focused ()) return;
  apply_native_graphics_canvas_action (
    native_graphics_canvas_action::move_origin,
    as_string (dx) * "gw", as_string (dy) * "gh");
}

bool
edit_graphics_rep::native_graphics_group_mode (
  path& graphics, string& submode) {
  graphics= native_graphics_canvas_path ();
  submode= "";
  if (is_nil (graphics) || !has_subtree (et, graphics) ||
      !is_func (subtree (et, graphics), GRAPHICS))
    return false;
  tree mode= native_ink_property (graphics, GR_MODE, tree (UNINIT));
  if (!is_func (mode, TUPLE, 2) || !is_atomic (mode[0]) ||
      !is_atomic (mode[1]) || mode[0]->label != "group-edit")
    return false;
  submode= mode[1]->label;
  return true;
}

path
edit_graphics_rep::native_graphics_group_hit (path graphics, SI x, SI y) {
  if (is_nil (graphics) || !has_subtree (et, graphics)) return path ();
  tree objects= subtree (et, graphics);
  SI pad= 4 * get_pixel_size ();
  for (int i=N(objects)-1; i>=0; --i) {
    if (is_atomic (objects[i]) || is_empty (objects[i])) continue;
    native_drawing_selection_box bounds;
    if (!native_drawing_object_bounds (graphics * i, bounds)) continue;
    if (x >= bounds.x1-pad && x <= bounds.x2+pad &&
        y >= bounds.y1-pad && y <= bounds.y2+pad)
      return graphics * i;
  }
  return path ();
}

void
edit_graphics_rep::native_graphics_group_clear_selection () {
  native_drawing_selection_paths_.clear ();
  native_group_selection_active_= false;
  refresh_native_drawing_selection_snapshot ();
  refresh_native_drawing_properties_snapshot ();
  publish_native_drawing_focus_refresh ();
  invalidate_all ();
}

void
edit_graphics_rep::native_graphics_group_select_one (path object, bool toggle) {
  if (is_nil (object) || !has_subtree (et, object)) {
    if (!toggle) native_graphics_group_clear_selection ();
    return;
  }
  auto found= std::find (
    native_drawing_selection_paths_.begin (),
    native_drawing_selection_paths_.end (), object);
  if (!toggle) {
    native_drawing_selection_paths_.clear ();
    native_drawing_selection_paths_.push_back (copy (object));
  }
  else if (found != native_drawing_selection_paths_.end ())
    native_drawing_selection_paths_.erase (found);
  else native_drawing_selection_paths_.push_back (copy (object));
  native_group_selection_active_= !native_drawing_selection_paths_.empty ();
  refresh_native_drawing_selection_snapshot ();
  refresh_native_drawing_properties_snapshot ();
  publish_native_drawing_focus_refresh ();
  invalidate_all ();
}

void
edit_graphics_rep::native_graphics_group_select_area (
  path graphics, SI x1, SI y1, SI x2, SI y2) {
  native_drawing_selection_paths_.clear ();
  if (is_nil (graphics) || !has_subtree (et, graphics)) {
    native_graphics_group_clear_selection ();
    return;
  }
  if (x1 > x2) std::swap (x1, x2);
  if (y1 > y2) std::swap (y1, y2);
  tree objects= subtree (et, graphics);
  for (int i=0; i<N(objects); ++i) {
    if (is_atomic (objects[i]) || is_empty (objects[i])) continue;
    native_drawing_selection_box bounds;
    if (!native_drawing_object_bounds (graphics * i, bounds)) continue;
    bool intersects= bounds.x2 >= x1 && bounds.x1 <= x2 &&
                     bounds.y2 >= y1 && bounds.y1 <= y2;
    if (intersects)
      native_drawing_selection_paths_.push_back (graphics * i);
  }
  native_group_selection_active_= !native_drawing_selection_paths_.empty ();
  refresh_native_drawing_selection_snapshot ();
  refresh_native_drawing_properties_snapshot ();
  publish_native_drawing_focus_refresh ();
  invalidate_all ();
}

bool
edit_graphics_rep::native_graphics_group_or_ungroup (path graphics) {
  if (is_nil (graphics) || !has_subtree (et, graphics) ||
      native_drawing_selection_paths_.empty ())
    return false;
  for (const path& p: native_drawing_selection_paths_)
    if (path_up (p) != graphics || !has_subtree (et, p)) return false;

  if (native_drawing_selection_paths_.size () == 1) {
    path selected= native_drawing_selection_paths_.front ();
    tree object= subtree (et, selected);
    if (is_func (object, GR_GROUP)) {
      int index= last_item (selected);
      std::vector<tree> children;
      children.reserve ((std::size_t) N(object));
      for (int i=0; i<N(object); ++i) children.push_back (copy (object[i]));
      start_editing ();
      remove (selected, 1);
      for (std::size_t i=0; i<children.size (); ++i)
        insert (graphics * (index + (int) i), tree (TUPLE, children[i]));
      end_editing ();
      native_drawing_selection_paths_.clear ();
      for (std::size_t i=0; i<children.size (); ++i)
        native_drawing_selection_paths_.push_back (
          graphics * (index + (int) i));
      native_group_selection_active_= !children.empty ();
      mark_native_ink_interaction_dirty ();
      refresh_native_drawing_selection_snapshot ();
      refresh_native_drawing_properties_snapshot ();
      publish_native_drawing_focus_refresh ();
      invalidate_all ();
      return true;
    }
  }

  if (native_drawing_selection_paths_.size () < 2) return false;
  std::vector<int> indices;
  indices.reserve (native_drawing_selection_paths_.size ());
  for (const path& p: native_drawing_selection_paths_)
    indices.push_back (last_item (p));
  std::sort (indices.begin (), indices.end ());
  indices.erase (std::unique (indices.begin (), indices.end ()), indices.end ());
  tree objects= subtree (et, graphics);
  tree group (GR_GROUP);
  for (int index: indices)
    if (index >= 0 && index < N(objects)) group << copy (objects[index]);
  if (N(group) < 2) return false;
  int insertion= indices.front ();
  start_editing ();
  for (auto it= indices.rbegin (); it != indices.rend (); ++it)
    remove (graphics * *it, 1);
  insert (graphics * insertion, tree (TUPLE, group));
  end_editing ();
  native_drawing_selection_paths_.clear ();
  native_drawing_selection_paths_.push_back (graphics * insertion);
  native_group_selection_active_= true;
  mark_native_ink_interaction_dirty ();
  refresh_native_drawing_selection_snapshot ();
  refresh_native_drawing_properties_snapshot ();
  publish_native_drawing_focus_refresh ();
  invalidate_all ();
  return true;
}

bool
edit_graphics_rep::native_graphics_selection_active () {
  refresh_native_drawing_selection_snapshot ();
  return !native_drawing_selection_paths_.empty ();
}

bool
edit_graphics_rep::native_graphics_edit_props_active () {
  path graphics;
  string submode;
  return native_graphics_group_mode (graphics, submode) &&
         submode == "edit-props" &&
         !native_drawing_selection_paths_.empty ();
}

bool
edit_graphics_rep::native_graphics_selection_supports_property (string name) {
  if (!native_graphics_edit_props_active ()) return false;
  for (const path& p: native_drawing_selection_paths_)
    if (has_subtree (et, p) &&
        native_graphics_property_supported (subtree (et, p), name))
      return true;
  return false;
}

tree
edit_graphics_rep::native_graphics_get_property (string name) {
  if (native_graphics_edit_props_active () &&
      native_graphics_selection_supports_property (name)) {
    string property= native_graphics_object_property_name (name);
    tree common (UNINIT);
    bool found= false;
    for (const path& p: native_drawing_selection_paths_) {
      if (!has_subtree (et, p)) continue;
      tree object= subtree (et, p);
      if (!native_graphics_property_supported (object, property)) continue;
      tree value= native_drawing_object_property (
        object, property, tree ("default"));
      if (!found) {
        common= value;
        found= true;
      }
      else if (value != common) return tree ("mixed");
    }
    return found ? common : tree ("default");
  }

  path graphics= native_graphics_canvas_path ();
  if (is_nil (graphics)) return get_env_value (name);
  tree fallback= get_env_value (name);
  return native_ink_property (graphics, name, fallback);
}

void
edit_graphics_rep::native_graphics_set_property (string name, tree value) {
  if (native_graphics_edit_props_active () &&
      native_graphics_selection_supports_property (name)) {
    string property= native_graphics_object_property_name (name);
    bool remove_value= is_atomic (value) && value->label == "default";
    start_editing ();
    for (const path& p: native_drawing_selection_paths_) {
      if (!has_subtree (et, p)) continue;
      tree object= subtree (et, p);
      if (!native_graphics_property_supported (object, property)) continue;
      if (remove_value) {
        bool removed= false;
        tree updated= native_remove_object_property_impl (
          object, property, removed);
        if (removed) assign (p, updated);
      }
      else assign (p, native_drawing_set_object_property (
        object, property, value));
    }
    end_editing ();
    mark_native_ink_interaction_dirty ();
    refresh_native_drawing_selection_snapshot ();
    refresh_native_drawing_properties_snapshot ();
    publish_native_drawing_focus_refresh ();
    invalidate_all ();
    return;
  }

  path graphics= native_graphics_canvas_path ();
  if (is_nil (graphics)) return;
  string object_property= native_graphics_object_property_name (name);
  tree attr_default= native_graphics_property_default (object_property);
  if ((is_atomic (value) && value->label == "default") || value == attr_default) {
    native_graphics_remove_property (name);
    return;
  }
  start_editing ();
  bool changed= native_drawing_set_graphics_property (graphics, name, value);
  end_editing ();
  if (changed) {
    mark_native_ink_interaction_dirty ();
    refresh_native_ink_interaction ();
    refresh_native_drawing_properties_snapshot ();
    publish_native_drawing_focus_refresh ();
    invalidate_all ();
  }
}

void
edit_graphics_rep::native_graphics_remove_property (string name) {
  if (native_graphics_edit_props_active () &&
      native_graphics_selection_supports_property (name)) {
    native_graphics_set_property (name, tree ("default"));
    return;
  }
  path graphics= native_graphics_canvas_path ();
  if (is_nil (graphics)) return;
  path p= path_up (graphics);
  while (!is_nil (p)) {
    if (has_subtree (et, p)) {
      tree wrapper= subtree (et, p);
      if (is_func (wrapper, WITH)) {
        bool removed= false;
        tree updated= native_remove_with_property_direct (
          wrapper, name, removed);
        if (removed) {
          start_editing ();
          assign (p, updated);
          end_editing ();
          mark_native_ink_interaction_dirty ();
          refresh_native_ink_interaction ();
          refresh_native_drawing_properties_snapshot ();
          publish_native_drawing_focus_refresh ();
          invalidate_all ();
          return;
        }
      }
    }
    if (p == rp) break;
    p= path_up (p);
  }
}

namespace {

path
native_graphics_style_target (
  edit_graphics_rep* editor, path graphics, path hover) {
  if (!is_nil (hover) && path_up (hover) == graphics)
    return hover;
  array<SI> mouse= editor->get_mouse_position ();
  if (N(mouse) >= 2)
    return editor->native_graphics_group_hit (graphics, mouse[0], mouse[1]);
  return path ();
}

} // namespace

void
edit_graphics_rep::native_graphics_get_props_at_mouse () {
  path graphics= native_graphics_canvas_path ();
  if (is_nil (graphics)) return;
  path target= native_graphics_style_target (
    this, graphics, native_graphics_group_hover_path_);
  if (is_nil (target) || !has_subtree (et, target)) return;
  tree object= subtree (et, target);
  start_editing ();
  bool changed= false;
  for (const string& property: native_graphics_all_properties ()) {
    if (property == "gid" ||
        !native_graphics_property_supported (object, property))
      continue;
    string canvas= native_graphics_canvas_property_name (property);
    tree value= native_drawing_object_property (
      object, property, tree (UNINIT));
    if (value == tree (UNINIT))
      value= native_ink_property (
        graphics, canvas, get_env_value (canvas));
    changed= native_drawing_set_graphics_property (
      graphics, canvas, value) || changed;
  }
  end_editing ();
  if (changed) {
    mark_native_ink_interaction_dirty ();
    refresh_native_ink_interaction ();
    refresh_native_drawing_properties_snapshot ();
    publish_native_drawing_focus_refresh ();
    invalidate_all ();
  }
}

void
edit_graphics_rep::native_graphics_apply_props_at_mouse () {
  path graphics= native_graphics_canvas_path ();
  if (is_nil (graphics)) return;
  path target= native_graphics_style_target (
    this, graphics, native_graphics_group_hover_path_);
  if (is_nil (target) || !has_subtree (et, target)) return;
  tree object= subtree (et, target);
  tree updated= copy (object);
  bool any= false;
  for (const string& property: native_graphics_all_properties ()) {
    if (property == "gid" ||
        !native_graphics_property_supported (object, property))
      continue;
    string canvas= native_graphics_canvas_property_name (property);
    tree value= native_ink_property (
      graphics, canvas, get_env_value (canvas));
    if (is_atomic (value) && value->label == "default") {
      bool removed= false;
      updated= native_remove_object_property_impl (
        updated, property, removed);
      any= any || removed;
    }
    else {
      updated= native_drawing_set_object_property (
        updated, property, value);
      any= true;
    }
  }
  if (!any || updated == object) return;
  start_editing ();
  assign (target, updated);
  end_editing ();
  mark_native_ink_interaction_dirty ();
  refresh_native_ink_interaction ();
  refresh_native_drawing_selection_snapshot ();
  refresh_native_drawing_properties_snapshot ();
  publish_native_drawing_focus_refresh ();
  invalidate_all ();
}

bool
edit_graphics_rep::native_graphics_owns_history () {
  path graphics;
  string submode;
  if (native_graphics_group_mode (graphics, submode))
    return true;
  graphics= native_graphics_canvas_path ();
  if (is_nil (graphics) || !has_subtree (et, graphics)) return false;
  tree mode= native_ink_property (graphics, GR_MODE, tree (UNINIT));
  return native_pen_mode (mode);
}

void
edit_graphics_rep::native_graphics_history_reset () {
  native_drawing_selection_paths_.clear ();
  native_group_selection_active_= false;
  native_group_area_selecting_= false;
  native_group_transform_active_= false;
  mark_native_ink_interaction_dirty ();
  refresh_native_ink_interaction ();
  refresh_native_drawing_selection_snapshot ();
  refresh_native_drawing_properties_snapshot ();
  publish_native_drawing_focus_refresh ();
  invalidate_all ();
}

tree
edit_graphics_rep::native_graphics_copy_selection () {
  if (native_drawing_selection_paths_.empty ()) return tree ("");
  path graphics= path_up (native_drawing_selection_paths_.front ());
  if (is_nil (graphics) || !has_subtree (et, graphics)) return tree ("");
  std::vector<int> indices;
  for (const path& p: native_drawing_selection_paths_) {
    if (path_up (p) != graphics || !has_subtree (et, p)) continue;
    indices.push_back (last_item (p));
  }
  std::sort (indices.begin (), indices.end ());
  indices.erase (std::unique (indices.begin (), indices.end ()), indices.end ());
  tree objects= subtree (et, graphics);
  tree result (GRAPHICS);
  for (int index: indices)
    if (index >= 0 && index < N(objects)) result << copy (objects[index]);
  return N(result) == 0 ? tree ("") : result;
}

tree
edit_graphics_rep::native_graphics_cut_selection () {
  tree result= native_graphics_copy_selection ();
  if (!is_func (result, GRAPHICS) || native_drawing_selection_paths_.empty ())
    return result;
  path graphics= path_up (native_drawing_selection_paths_.front ());
  std::vector<int> indices;
  for (const path& p: native_drawing_selection_paths_)
    if (path_up (p) == graphics && has_subtree (et, p))
      indices.push_back (last_item (p));
  std::sort (indices.begin (), indices.end ());
  indices.erase (std::unique (indices.begin (), indices.end ()), indices.end ());
  start_editing ();
  for (auto it= indices.rbegin (); it != indices.rend (); ++it)
    remove (graphics * *it, 1);
  end_editing ();
  native_drawing_selection_paths_.clear ();
  native_group_selection_active_= false;
  mark_native_ink_interaction_dirty ();
  refresh_native_drawing_selection_snapshot ();
  refresh_native_drawing_properties_snapshot ();
  publish_native_drawing_focus_refresh ();
  invalidate_all ();
  return result;
}

bool
edit_graphics_rep::native_graphics_paste_selection (tree selection) {
  if (!is_func (selection, GRAPHICS) || N(selection) == 0) return false;
  path graphics= native_graphics_canvas_path ();
  if (is_nil (graphics) || !has_subtree (et, graphics) ||
      !is_func (subtree (et, graphics), GRAPHICS))
    return false;
  int first= N(subtree (et, graphics));
  start_editing ();
  int inserted= 0;
  for (int i=0; i<N(selection); ++i) {
    if (is_empty (selection[i])) continue;
    insert (graphics * (first + inserted), tree (TUPLE, copy (selection[i])));
    ++inserted;
  }
  end_editing ();
  if (inserted == 0) return false;
  native_drawing_selection_paths_.clear ();
  for (int i=0; i<inserted; ++i)
    native_drawing_selection_paths_.push_back (graphics * (first + i));
  path group_graphics;
  string group_submode;
  native_group_selection_active_=
    native_graphics_group_mode (group_graphics, group_submode);
  mark_native_ink_interaction_dirty ();
  refresh_native_drawing_selection_snapshot ();
  refresh_native_drawing_properties_snapshot ();
  publish_native_drawing_focus_refresh ();
  invalidate_all ();
  return true;
}

bool
edit_graphics_rep::native_graphics_group_event (
  string type, SI x, SI y, int modifiers) {
  path graphics;
  string submode;
  if (!native_graphics_group_mode (graphics, submode)) return false;
  if (submode != "move" && submode != "zoom" &&
      submode != "rotate" && submode != "group-ungroup" &&
      submode != "edit-props")
    return false;

  constexpr int native_shift_mask= 256;
  bool shift= (modifiers & native_shift_mask) != 0;
  path hit= native_graphics_group_hit (graphics, x, y);
  native_graphics_group_hover_path_= copy (hit);
  auto selected= [&] (path p) {
    return std::find (native_drawing_selection_paths_.begin (),
                      native_drawing_selection_paths_.end (), p) !=
           native_drawing_selection_paths_.end ();
  };

  if (type == "move" || type == "dragging-left" || type == "dragging-right")
    return true;

  if (type == "release-right" || type == "double-right") {
    if (is_nil (hit)) native_graphics_group_clear_selection ();
    else native_graphics_group_select_one (hit, true);
    return true;
  }
  if (type == "start-drag-right") {
    native_group_area_selecting_= true;
    native_group_area_start_x_= x;
    native_group_area_start_y_= y;
    return true;
  }
  if (type == "end-drag-right") {
    if (native_group_area_selecting_)
      native_graphics_group_select_area (
        graphics, native_group_area_start_x_, native_group_area_start_y_, x, y);
    native_group_area_selecting_= false;
    return true;
  }

  if (type == "release-middle") {
    if (shift) {
      if (native_drawing_selection_paths_.empty () && !is_nil (hit))
        native_graphics_group_select_one (hit, false);
      (void) native_graphics_cut_selection ();
    }
    else native_graphics_group_clear_selection ();
    return true;
  }

  if (type == "release-left" || type == "double-left") {
    if (submode == "group-ungroup") {
      if (!is_nil (hit) && !selected (hit))
        native_graphics_group_select_one (hit, false);
      (void) native_graphics_group_or_ungroup (graphics);
    }
    else if (is_nil (hit)) native_graphics_group_clear_selection ();
    else if (shift) native_graphics_group_select_one (hit, true);
    else if (!selected (hit)) native_graphics_group_select_one (hit, false);
    return true;
  }

  if (type == "start-drag-left") {
    if (submode == "group-ungroup" || submode == "edit-props") return true;
    if (is_nil (hit)) {
      native_graphics_group_clear_selection ();
      return true;
    }
    if (!selected (hit)) native_graphics_group_select_one (hit, shift);
    if (native_drawing_selection_paths_.empty ()) return true;
    native_group_transform_active_= true;
    native_group_transform_start_x_= x;
    native_group_transform_start_y_= y;
    native_group_transform_kind_= submode == "zoom" ?
      native_drawing_transform::scale : submode == "rotate" ?
      native_drawing_transform::rotate : native_drawing_transform::move;
    return true;
  }
  if (type == "end-drag-left") {
    if (native_group_transform_active_) {
      native_ink_sample samples[2];
      samples[0].x= native_group_transform_start_x_;
      samples[0].y= native_group_transform_start_y_;
      samples[1].x= x;
      samples[1].y= y;
      native_group_transform_active_= false;
      commit_native_drawing_transform (native_group_transform_kind_, samples, 2);
      refresh_native_drawing_selection_snapshot ();
      refresh_native_drawing_properties_snapshot ();
      publish_native_drawing_focus_refresh ();
    }
    return true;
  }

  return type == "press-left" || type == "press-right" ||
         type == "press-middle";
}

void
edit_graphics_rep::publish_native_drawing_focus_refresh () {
  if (ui_endpoint != nullptr)
    ui_endpoint->publish (actor_command_kind::ui_native_drawing_focus_refresh);
}

bool
edit_graphics_rep::native_graphics_coordinate_region (
  path graphics, native_ink_interaction_snapshot& region,
  frame* coordinate_frame) {
  if (is_nil (eb) || is_nil (graphics)) return false;
  tree node= subtree (et, graphics);
  if (!is_func (node, GRAPHICS)) return false;

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
  if (coordinate_frame != nullptr) *coordinate_frame= f;
  return true;
}

bool
edit_graphics_rep::native_ink_region (
  path graphics, native_ink_interaction_snapshot& region,
  frame* coordinate_frame) {
  if (is_nil (graphics) || !has_subtree (et, graphics)) return false;
  tree mode= native_ink_property (graphics, GR_MODE, tree ("line"));
  if (!native_pen_mode (mode)) return false;
  if (!native_graphics_coordinate_region (graphics, region, coordinate_frame))
    return false;
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
  region.recognition_enabled=
    native_drawing_tool_ == native_drawing_tool::pen &&
    native_drawing_recognition_enabled_;
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
  case native_drawing_tool::text:
  case native_drawing_tool::math:
    region.pressure_enabled= false;
    region.line_width_pixels= 1.0;
    break;
  case native_drawing_tool::pen:
    break;
  }
  region.shape= native_drawing_shape_;
  region.pen_enabled= true;
  if (native_drawing_tool_ == native_drawing_tool::pen)
    region.pressure_enabled= native_drawing_pressure_enabled_;
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
      static_cast<unsigned int> (native_drawing_tool::math))
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
  else if (property == native_drawing_property::recognition)
    native_drawing_recognition_enabled_= value != 0;
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
  snapshot.recognition_enabled= native_drawing_recognition_enabled_;
  snapshot.snap_enabled= native_drawing_snap_enabled_;
  snapshot.selection_active= !native_drawing_selection_paths_.empty ();

  path group_graphics;
  string group_submode;
  if (native_graphics_group_mode (group_graphics, group_submode)) {
    snapshot.group_edit_active= true;
    if (group_submode == "move") {
      snapshot.selection_transform_enabled= snapshot.selection_active;
      snapshot.selection_transform= native_drawing_transform::move;
    }
    else if (group_submode == "zoom") {
      snapshot.selection_transform_enabled= snapshot.selection_active;
      snapshot.selection_transform= native_drawing_transform::scale;
    }
    else if (group_submode == "rotate") {
      snapshot.selection_transform_enabled= snapshot.selection_active;
      snapshot.selection_transform= native_drawing_transform::rotate;
    }
  }
  else if (native_drawing_tool_ == native_drawing_tool::lasso)
    snapshot.selection_transform_enabled= snapshot.selection_active;

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
  if (native_group_selection_active_) {
    path group_graphics;
    string group_submode;
    if (!native_graphics_group_mode (group_graphics, group_submode)) {
      native_drawing_selection_paths_.clear ();
      native_group_selection_active_= false;
    }
  }
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
      refresh_native_drawing_selection_snapshot ();
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
  if (!is_nil (graphics) &&
      native_graphics_coordinate_region (graphics, region, &object_frame)) {
    tree width_value= native_drawing_object_property (
      node, "line-width", tree (UNINIT));
    if (width_value == tree (UNINIT))
      width_value= native_ink_property (graphics, GR_LINE_WIDTH, tree ("1ln"));
    double line_width_pixels= native_line_width_pixels (width_value);
    std::vector<native_xy> stroke_points;
    if (native_penscript_screen_points (node, object_frame, stroke_points) &&
        !stroke_points.empty ()) {
      double x1= stroke_points[0].x, x2= stroke_points[0].x;
      double y1= stroke_points[0].y, y2= stroke_points[0].y;
      for (const auto& p: stroke_points) {
        x1= std::min (x1, p.x); x2= std::max (x2, p.x);
        y1= std::min (y1, p.y); y2= std::max (y2, p.y);
      }
      double pad= std::max (2.0, line_width_pixels) *
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
      double pad= std::max (2.0, line_width_pixels) *
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
  native_group_selection_active_= false;
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
edit_graphics_rep::commit_native_drawing_recognition (
  const native_shape_recognition_result& result) {
  if (result.confidence < 0.72 || result.point_count == 0 ||
      result.point_count > 4 ||
      result.kind == native_shape_recognition_kind::none)
    return;
  path gp;
  frame f;
  if (!native_ink_target (result.target_x, result.target_y, gp, f) || is_nil (f))
    return;
  tree graphics= subtree (et, gp);
  if (!is_func (graphics, GRAPHICS)) return;

  std::vector<point> points;
  points.reserve (result.point_count);
  for (std::uint8_t i=0; i<result.point_count; ++i) {
    point p= f[point ((double) result.points[i].x,
                     (double) result.points[i].y)];
    if (N(p) < 2) return;
    points.push_back (p);
  }

  tree shape;
  switch (result.kind) {
  case native_shape_recognition_kind::line:
    if (points.size () != 2) return;
    shape= tree (LINE);
    shape << native_point_tree (points[0][0], points[0][1])
          << native_point_tree (points[1][0], points[1][1]);
    break;
  case native_shape_recognition_kind::circle:
    if (points.size () != 3) return;
    shape= tree (CARC);
    for (const point& p: points)
      shape << native_point_tree (p[0], p[1]);
    break;
  case native_shape_recognition_kind::rectangle:
    if (points.size () != 4) return;
    shape= native_shape_polygon (CLINE, points);
    break;
  case native_shape_recognition_kind::none:
    return;
  }

  tree color_value= native_ink_property (gp, GR_COLOR, tree ("default"));
  tree width_value= native_ink_property (gp, GR_LINE_WIDTH, tree ("default"));
  if (native_drawing_color_override_)
    color_value= native_color_tree_from_rgba (native_drawing_rgba_);
  if (native_drawing_width_override_)
    width_value= tree (as_string (native_drawing_width_pixels_) * "ln");
  tree wrapped (WITH);
  if (color_value != "default") wrapped << "color" << color_value;
  if (width_value != "default") wrapped << "line-width" << width_value;
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
edit_graphics_rep::commit_native_drawing_text (
  bool math, const native_ink_sample* samples, std::size_t count) {
  if (samples == nullptr || count == 0) return;
  path gp;
  frame f;
  if (!native_ink_target (samples[0].x, samples[0].y, gp, f) || is_nil (f))
    return;
  tree graphics= subtree (et, gp);
  if (!is_func (graphics, GRAPHICS)) return;

  tree_label wanted= math ? MATH_AT : TEXT_AT;
  for (int i=N(graphics)-1; i>=0; --i) {
    if (is_atomic (graphics[i]) || is_empty (graphics[i])) continue;
    tree radical= native_graphics_radical (graphics[i], nullptr);
    if (!is_func (radical, wanted)) continue;
    native_drawing_selection_box bounds;
    if (!native_drawing_object_bounds (gp * i, bounds)) continue;
    if (samples[0].x < bounds.x1 || samples[0].x > bounds.x2 ||
        samples[0].y < bounds.y1 || samples[0].y > bounds.y2)
      continue;
    path radical_path= native_graphics_radical_path (gp * i, graphics[i]);
    path content= radical_path * 0;
    if (!has_subtree (et, content)) return;
    native_drawing_selection_paths_.clear ();
    go_to (start (et, content));
    refresh_native_drawing_selection_snapshot ();
    refresh_native_drawing_properties_snapshot ();
    publish_native_drawing_focus_refresh ();
    invalidate_all ();
    return;
  }

  point p= f[point ((double) samples[0].x, (double) samples[0].y)];
  if (N(p) < 2) return;
  p= native_drawing_snap_point (gp, f, p);
  tree object (wanted);
  object << "" << native_point_tree (p[0], p[1]);
  int index= N(graphics);
  start_editing ();
  insert (gp * index, tree (TUPLE, object));
  end_editing ();

  path content= gp * index * 0;
  native_drawing_selection_paths_.clear ();
  if (has_subtree (et, content)) go_to (start (et, content));
  mark_native_ink_interaction_dirty ();
  invalidate_all ();
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
  if (tool == native_drawing_tool::text || tool == native_drawing_tool::math) {
    commit_native_drawing_text (tool == native_drawing_tool::math, samples, count);
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
  (void) m;
  (void) t;
  // apply_changes (); // FIXME: remove after review of synchronization
  if (native_graphics_group_event (type, x, y, m)) return true;
  if (type == "move" || type == "release-left" || type == "double-left" ||
      type == "release-middle" || type == "release-right" ||
      type == "double-right" || type == "start-drag-left" ||
      type == "dragging-left" || type == "end-drag-left" ||
      type == "start-drag-right" || type == "dragging-right" ||
      type == "end-drag-right") {
    path native_graphics;
    frame native_frame;
    if (native_ink_target (x, y, native_graphics, native_frame))
      return true;
  }
  if (type == "wheel") {
    frame f= find_frame ();
    if (is_nil (f) || N(data) < 2) return false;
    point p0= f [point (0.0, 0.0)];
    point p1= f [point (data[0], data[1])];
    point dp= p1-p0;
    point lim1, lim2;
    find_limits (lim1, lim2);
    if (N(lim1) < 2 || N(lim2) < 2) return false;
    double dx= dp[0] / max (lim2[0] - lim1[0], 0.000001);
    double dy= dp[1] / max (lim2[1] - lim1[1], 0.000001);
    native_graphics_canvas_wheel (dx, dy);
    return true;
  }
  return false;
}

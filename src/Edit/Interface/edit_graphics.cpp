
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
#include "colors.hpp"
#include "drd_std.hpp"
#include "new_document.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>

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
  region.rgba= native_ink_rgba (named_color (color_name));
  region.line_width_pixels= native_line_width_pixels (
    native_ink_property (graphics, GR_LINE_WIDTH, tree ("1ln")));
  region.pen_enabled= true;
  region.pressure_enabled= true;
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
}

void
edit_graphics_rep::commit_native_ink_stroke (
  const native_ink_sample* samples, std::size_t count) {
  if (samples == nullptr || count == 0) return;
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
    sample << as_string (p[0]) << as_string (p[1])
           << as_string (samples[i].time)
           << as_string (clamp_pressure (samples[i].pressure));
    ink << sample;
  }
  if (N(ink) == 0) return;

  double pixel= get_typesetter ()->env->pixel;
  double virtual_pixel= f->inverse_scalar (pixel);
  tree metadata= compound (
    "ink-meta", native_ink_id (), as_string (virtual_pixel));
  tree stroke (PENSCRIPT);
  stroke << find_point (first) << find_point (last) << metadata << ink;

  tree color_value= native_ink_property (gp, GR_COLOR, tree ("default"));
  tree width_value= native_ink_property (gp, GR_LINE_WIDTH, tree ("default"));
  tree enhance_value= native_ink_property (gp, GR_PEN_ENHANCE, tree ("default"));
  tree wrapped (WITH);
  if (color_value != "default") wrapped << "color" << color_value;
  if (width_value != "default") wrapped << "line-width" << width_value;
  if (enhance_value != "default") wrapped << "pen-enhance" << enhance_value;
  if (N(wrapped) == 0) wrapped= stroke;
  else wrapped << stroke;

  start_editing ();
  insert (gp * N(graphics), wrapped);
  end_editing ();
  refresh_native_ink_interaction ();
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

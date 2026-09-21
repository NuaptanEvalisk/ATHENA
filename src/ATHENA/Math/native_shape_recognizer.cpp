/******************************************************************************
* MODULE     : native_shape_recognizer.cpp
* DESCRIPTION: Detached geometric stroke recognition for native drawing
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include "native_shape_recognizer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace {

struct p2 {
  double x= 0.0;
  double y= 0.0;
};

double d2 (p2 a, p2 b) {
  double dx= a.x - b.x, dy= a.y - b.y;
  return dx*dx + dy*dy;
}

double length (p2 a, p2 b) {
  return std::sqrt (d2 (a, b));
}

double point_segment_distance (p2 p, p2 a, p2 b) {
  double dx= b.x-a.x, dy= b.y-a.y;
  double den= dx*dx + dy*dy;
  if (!(den > 0.0)) return length (p, a);
  double t= ((p.x-a.x)*dx + (p.y-a.y)*dy) / den;
  t= std::clamp (t, 0.0, 1.0);
  return std::hypot (p.x-(a.x+t*dx), p.y-(a.y+t*dy));
}

std::vector<p2> normalized_points (
  const native_ink_sample* samples, std::size_t count) {
  std::vector<p2> pts;
  pts.reserve (count);
  for (std::size_t i=0; i<count; ++i) {
    p2 p {(double) samples[i].x, (double) samples[i].y};
    if (!std::isfinite (p.x) || !std::isfinite (p.y)) continue;
    if (!pts.empty () && d2 (pts.back (), p) < 0.25) continue;
    pts.push_back (p);
  }
  return pts;
}

struct inertia_stats {
  double mass= 0.0;
  double cx= 0.0;
  double cy= 0.0;
  double ixx= 0.0;
  double ixy= 0.0;
  double iyy= 0.0;
  double determinant= 0.0;
};

inertia_stats inertia (const std::vector<p2>& pts) {
  inertia_stats s;
  double sx= 0.0, sy= 0.0, sxx= 0.0, sxy= 0.0, syy= 0.0;
  for (std::size_t i=1; i<pts.size (); ++i) {
    p2 a= pts[i-1], b= pts[i];
    double l= length (a, b);
    if (!(l > 0.0)) continue;
    s.mass += l;
    sx += l * (a.x+b.x) * 0.5;
    sy += l * (a.y+b.y) * 0.5;
    sxx += l * (a.x*a.x + a.x*b.x + b.x*b.x) / 3.0;
    syy += l * (a.y*a.y + a.y*b.y + b.y*b.y) / 3.0;
    sxy += l * (2.0*a.x*a.y + a.x*b.y + b.x*a.y + 2.0*b.x*b.y) / 6.0;
  }
  if (!(s.mass > 0.0)) return s;
  s.cx= sx / s.mass;
  s.cy= sy / s.mass;
  s.ixx= std::max (0.0, sxx/s.mass - s.cx*s.cx);
  s.ixy= sxy/s.mass - s.cx*s.cy;
  s.iyy= std::max (0.0, syy/s.mass - s.cy*s.cy);
  double tr= s.ixx + s.iyy;
  if (tr > 0.0) {
    double det= std::max (0.0, s.ixx*s.iyy - s.ixy*s.ixy);
    s.determinant= std::clamp (4.0*det/(tr*tr), 0.0, 1.0);
  }
  return s;
}

native_ink_sample sample_at (p2 p) {
  native_ink_sample s;
  s.x= (SI) std::llround (p.x);
  s.y= (SI) std::llround (p.y);
  return s;
}

native_shape_recognition_result base_result (
  const native_ink_sample* samples, std::size_t count) {
  native_shape_recognition_result r;
  if (samples != nullptr && count != 0) {
    r.target_x= samples[0].x;
    r.target_y= samples[0].y;
  }
  return r;
}

bool recognize_line (
  const std::vector<p2>& pts, const inertia_stats& st,
  native_shape_recognition_result& r) {
  if (pts.size () < 2 || !(st.mass > 0.0)) return false;
  double chord= length (pts.front (), pts.back ());
  if (!(chord > 0.0)) return false;
  double straightness= chord / st.mass;
  if (st.determinant > 0.012 || straightness < 0.94) return false;
  r.kind= native_shape_recognition_kind::line;
  r.confidence= std::clamp (
    0.5 * (1.0 - st.determinant/0.012) +
    0.5 * ((straightness-0.94)/0.06), 0.0, 1.0);
  r.point_count= 2;
  r.points[0]= sample_at (pts.front ());
  r.points[1]= sample_at (pts.back ());
  return r.confidence >= 0.72;
}

bool recognize_circle (
  const std::vector<p2>& pts, const inertia_stats& st,
  double diagonal, native_shape_recognition_result& r) {
  if (pts.size () < 8 || !(diagonal > 0.0) || !(st.mass > 0.0)) return false;
  double closure= length (pts.front (), pts.back ()) / diagonal;
  if (closure > 0.18 || st.determinant < 0.93) return false;

  p2 center {st.cx, st.cy};
  double radius= 0.0;
  for (p2 p: pts) radius += length (p, center);
  radius /= (double) pts.size ();
  if (!(radius > 0.0)) return false;
  double radial2= 0.0;
  for (p2 p: pts) {
    double e= length (p, center) - radius;
    radial2 += e*e;
  }
  double radial_rms= std::sqrt (radial2 / (double) pts.size ()) / radius;
  double circumference_error=
    std::fabs (st.mass/(2.0*M_PI*radius) - 1.0);
  if (radial_rms > 0.11 || circumference_error > 0.28) return false;

  double c_close= 1.0 - closure/0.18;
  double c_round= (st.determinant-0.93)/0.07;
  double c_radial= 1.0 - radial_rms/0.11;
  double c_length= 1.0 - circumference_error/0.28;
  r.confidence= std::clamp (
    0.20*c_close + 0.25*c_round + 0.35*c_radial + 0.20*c_length,
    0.0, 1.0);
  if (r.confidence < 0.72) return false;
  r.kind= native_shape_recognition_kind::circle;
  r.point_count= 3;
  r.points[0]= sample_at ({center.x+radius, center.y});
  r.points[1]= sample_at ({center.x-radius, center.y});
  r.points[2]= sample_at ({center.x, center.y+radius});
  return true;
}

std::size_t farthest_from (const std::vector<p2>& pts, std::size_t index) {
  std::size_t best= index;
  double best_d2= -1.0;
  for (std::size_t i=0; i<pts.size (); ++i) {
    double dd= d2 (pts[index], pts[i]);
    if (dd > best_d2) { best_d2= dd; best= i; }
  }
  return best;
}

std::vector<std::size_t> forward_arc (
  std::size_t begin, std::size_t end, std::size_t n) {
  std::vector<std::size_t> out;
  if (n == 0) return out;
  std::size_t i= begin;
  for (std::size_t count=0; count<=n; ++count) {
    out.push_back (i);
    if (i == end) break;
    i= (i+1) % n;
  }
  return out;
}

std::size_t farthest_from_chord (
  const std::vector<p2>& pts, const std::vector<std::size_t>& arc) {
  if (arc.size () < 3) return arc.empty () ? 0 : arc.front ();
  p2 a= pts[arc.front ()], b= pts[arc.back ()];
  std::size_t best= arc[1];
  double best_dist= -1.0;
  for (std::size_t k=1; k+1<arc.size (); ++k) {
    double dist= point_segment_distance (pts[arc[k]], a, b);
    if (dist > best_dist) { best_dist= dist; best= arc[k]; }
  }
  return best;
}

double normalized_abs_dot (p2 a, p2 b, p2 c) {
  double ux= a.x-b.x, uy= a.y-b.y;
  double vx= c.x-b.x, vy= c.y-b.y;
  double den= std::hypot (ux, uy) * std::hypot (vx, vy);
  if (!(den > 0.0)) return 1.0;
  return std::fabs ((ux*vx + uy*vy) / den);
}

double normalized_abs_cross (p2 a, p2 b, p2 c, p2 d) {
  double ux= b.x-a.x, uy= b.y-a.y;
  double vx= d.x-c.x, vy= d.y-c.y;
  double den= std::hypot (ux, uy) * std::hypot (vx, vy);
  if (!(den > 0.0)) return 1.0;
  return std::fabs ((ux*vy - uy*vx) / den);
}

bool recognize_rectangle (
  const std::vector<p2>& pts, double diagonal,
  native_shape_recognition_result& r) {
  if (pts.size () < 8 || !(diagonal > 0.0)) return false;
  double closure= length (pts.front (), pts.back ()) / diagonal;
  if (closure > 0.16) return false;

  std::size_t a= farthest_from (pts, 0);
  std::size_t b= farthest_from (pts, a);
  if (a == b) return false;
  std::vector<std::size_t> arc1= forward_arc (a, b, pts.size ());
  std::vector<std::size_t> arc2= forward_arc (b, a, pts.size ());
  if (arc1.size () < 3 || arc2.size () < 3) return false;
  std::size_t c= farthest_from_chord (pts, arc1);
  std::size_t d= farthest_from_chord (pts, arc2);
  std::array<p2,4> q= {pts[a], pts[c], pts[b], pts[d]};

  std::array<double,4> sides;
  for (int i=0; i<4; ++i) {
    sides[(std::size_t) i]= length (q[(std::size_t) i], q[(std::size_t) ((i+1)%4)]);
    if (sides[(std::size_t) i] < 0.15*diagonal) return false;
  }
  double angle_error= 0.0;
  for (int i=0; i<4; ++i) {
    double e= normalized_abs_dot (
      q[(std::size_t) ((i+3)%4)], q[(std::size_t) i],
      q[(std::size_t) ((i+1)%4)]);
    angle_error= std::max (angle_error, e);
  }
  if (angle_error > 0.18) return false;
  double parallel_error= std::max (
    normalized_abs_cross (q[0], q[1], q[2], q[3]),
    normalized_abs_cross (q[1], q[2], q[3], q[0]));
  if (parallel_error > 0.14) return false;
  double opposite_error= std::max (
    std::fabs (sides[0]-sides[2]) / std::max (sides[0], sides[2]),
    std::fabs (sides[1]-sides[3]) / std::max (sides[1], sides[3]));
  if (opposite_error > 0.22) return false;

  double sum_dist= 0.0, max_dist= 0.0;
  for (p2 p: pts) {
    double best= std::numeric_limits<double>::infinity ();
    for (int i=0; i<4; ++i)
      best= std::min (best, point_segment_distance (
        p, q[(std::size_t) i], q[(std::size_t) ((i+1)%4)]));
    sum_dist += best;
    max_dist= std::max (max_dist, best);
  }
  double mean_error= sum_dist / ((double) pts.size () * diagonal);
  double worst_error= max_dist / diagonal;
  if (mean_error > 0.035 || worst_error > 0.10) return false;

  r.confidence= std::clamp (
    0.15*(1.0-closure/0.16) +
    0.25*(1.0-angle_error/0.18) +
    0.15*(1.0-parallel_error/0.14) +
    0.15*(1.0-opposite_error/0.22) +
    0.20*(1.0-mean_error/0.035) +
    0.10*(1.0-worst_error/0.10), 0.0, 1.0);
  if (r.confidence < 0.72) return false;
  r.kind= native_shape_recognition_kind::rectangle;
  r.point_count= 4;
  for (int i=0; i<4; ++i) r.points[i]= sample_at (q[(std::size_t) i]);
  return true;
}

} // namespace

native_shape_recognition_result
recognize_native_shape (
  const native_ink_sample* samples, std::size_t count) noexcept {
  native_shape_recognition_result result= base_result (samples, count);
  if (samples == nullptr || count < 2) return result;
  std::vector<p2> pts= normalized_points (samples, count);
  if (pts.size () < 2) return result;

  double x1= pts[0].x, x2= pts[0].x, y1= pts[0].y, y2= pts[0].y;
  for (p2 p: pts) {
    x1= std::min (x1, p.x); x2= std::max (x2, p.x);
    y1= std::min (y1, p.y); y2= std::max (y2, p.y);
  }
  double diagonal= std::hypot (x2-x1, y2-y1);
  if (!(diagonal > 1.0)) return result;
  inertia_stats st= inertia (pts);

  native_shape_recognition_result candidate= result;
  if (recognize_line (pts, st, candidate)) return candidate;
  candidate= result;
  if (recognize_circle (pts, st, diagonal, candidate)) return candidate;
  candidate= result;
  if (recognize_rectangle (pts, diagonal, candidate)) return candidate;
  return result;
}

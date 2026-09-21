/******************************************************************************
* MODULE     : commutative_diagram_native.cpp
* DESCRIPTION: Native geometry and ephemeral interaction state for diagrams
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
*/

#include "commutative_diagram_native.hpp"
#include "tm_timer.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <unordered_map>

namespace {

constexpr double vertex_content_radius= 0.24;
constexpr double vertex_move_radius= 0.46;
constexpr double arrow_hit_radius= 0.38;
constexpr double handle_hit_radius= 0.24;

std::mutex session_mutex;
std::unordered_map<tree_rep*, std::weak_ptr<commutative_diagram_session>> sessions;
std::atomic<std::uint64_t> id_counter {1};

tree_rep* body_key (tree body) {
  return inside (body);
}

bool is_vertex (tree t) {
  return is_compound (t, "cd-vertex", 4);
}

bool is_arrow (tree t) {
  return is_compound (t, "cd-arrow", 5);
}

} // namespace

string
cd_string (tree t, string fallback) {
  return is_atomic (t)? t->label: fallback;
}

double
cd_number (tree t, double fallback) {
  if (!is_atomic (t) || !is_double (t->label)) return fallback;
  return as_double (t->label);
}

string
cd_option (tree arrow, string key, string fallback) {
  if (!is_arrow (arrow) || !is_func (arrow[4], TUPLE)) return fallback;
  tree options= arrow[4];
  for (int i=0; i+1<N(options); i+=2)
    if (cd_string (options[i]) == key)
      return cd_string (options[i+1], fallback);
  return fallback;
}

double
cd_option_number (tree arrow, string key, double fallback) {
  string value= cd_option (arrow, key, "");
  return is_double (value)? as_double (value): fallback;
}

tree
cd_default_arrow_options () {
  tree options (TUPLE);
  options << "edge-type" << "arrow"
          << "tail" << "none"
          << "body" << "solid"
          << "head" << "arrowhead"
          << "label-alignment" << "left"
          << "label-position" << "50"
          << "offset" << "0"
          << "curve" << "0"
          << "loop-radius" << "3"
          << "loop-angle" << "0"
          << "shorten-source" << "0"
          << "shorten-target" << "0"
          << "level" << "1"
          << "color" << "black"
          << "label-color" << "black";
  return options;
}

string
cd_new_id (string prefix) {
  std::uint64_t serial= id_counter.fetch_add (1, std::memory_order_relaxed);
  return prefix * "-" * as_string (texmacs_time ()) * "-" * as_string (serial);
}

tree
cd_options_with (tree arrow, string key, string value) {
  tree result (TUPLE);
  bool found= false;
  tree old= is_arrow (arrow)? arrow[4]: tree (TUPLE);
  if (is_func (old, TUPLE)) {
    for (int i=0; i<N(old);) {
      if (i+1<N(old)) {
        string existing= cd_string (old[i]);
        result << copy (old[i]);
        if (existing == key) {
          result << tree (value);
          found= true;
        }
        else result << copy (old[i+1]);
        i+=2;
      }
      else {
        result << copy (old[i]);
        ++i;
      }
    }
  }
  if (!found) result << tree (key) << tree (value);
  return result;
}

string cd_vertex_id (tree vertex) { return is_vertex (vertex)? cd_string (vertex[0]): ""; }
double cd_vertex_x (tree vertex) { return is_vertex (vertex)? cd_number (vertex[1]): 0.0; }
double cd_vertex_y (tree vertex) { return is_vertex (vertex)? cd_number (vertex[2]): 0.0; }
string cd_arrow_id (tree arrow) { return is_arrow (arrow)? cd_string (arrow[0]): ""; }
string cd_arrow_source (tree arrow) { return is_arrow (arrow)? cd_string (arrow[1]): ""; }
string cd_arrow_target (tree arrow) { return is_arrow (arrow)? cd_string (arrow[2]): ""; }

tree
cd_find_vertex (tree body, string id, int* child_index) {
  if (child_index != nullptr) *child_index= -1;
  if (!(is_compound (body, "cd-body") || is_func (body, DOCUMENT))) return tree ();
  for (int i=0; i<N(body); ++i)
    if (is_vertex (body[i]) && cd_vertex_id (body[i]) == id) {
      if (child_index != nullptr) *child_index= i;
      return body[i];
    }
  return tree ();
}

tree
cd_find_arrow (tree body, string id, int* child_index) {
  if (child_index != nullptr) *child_index= -1;
  if (!(is_compound (body, "cd-body") || is_func (body, DOCUMENT))) return tree ();
  for (int i=0; i<N(body); ++i)
    if (is_arrow (body[i]) && cd_arrow_id (body[i]) == id) {
      if (child_index != nullptr) *child_index= i;
      return body[i];
    }
  return tree ();
}

double
cd_distance (cd_point a, cd_point b) {
  return std::hypot (b.x-a.x, b.y-a.y);
}

double
cd_point_segment_distance (cd_point p, cd_point a, cd_point b) {
  double dx= b.x-a.x, dy= b.y-a.y;
  double length2= dx*dx + dy*dy;
  if (length2 < 1.0e-12) return cd_distance (p, a);
  double u= std::clamp (((p.x-a.x)*dx + (p.y-a.y)*dy) / length2, 0.0, 1.0);
  return cd_distance (p, cd_point (a.x + u*dx, a.y + u*dy));
}

cd_point
cd_bezier_point (const cd_geometry& g, double t) {
  double u= 1.0-t;
  return g.p[0]*(u*u*u) + g.p[1]*(3.0*u*u*t) +
         g.p[2]*(3.0*u*t*t) + g.p[3]*(t*t*t);
}

cd_point
cd_bezier_tangent (const cd_geometry& g, double t) {
  double u= 1.0-t;
  cd_point v= (g.p[1]-g.p[0])*(3.0*u*u) +
              (g.p[2]-g.p[1])*(6.0*u*t) +
              (g.p[3]-g.p[2])*(3.0*t*t);
  double length= std::hypot (v.x, v.y);
  if (length < 1.0e-9) return cd_point (1.0, 0.0);
  return v*(1.0/length);
}

cd_geometry
cd_shift_geometry (const cd_geometry& g, double amount) {
  cd_point tangent= cd_bezier_tangent (g, 0.5);
  cd_point normal (-tangent.y, tangent.x);
  cd_geometry shifted;
  for (int i=0; i<4; ++i) shifted.p[i]= g.p[i] + normal*amount;
  return shifted;
}

cd_geometry
cd_loop_geometry (cd_point centre, double angle_degrees, double radius_setting) {
  constexpr double radians= 0.0174532925199432957692;
  double angle= radians*angle_degrees;
  double radius= 0.58 + 0.12*std::fabs (radius_setting);
  double sign= radius_setting < 0.0? -1.0: 1.0;
  cd_point u (std::cos (angle), std::sin (angle));
  cd_point n (sign*(-u.y), sign*u.x);
  cd_geometry g;
  g.p[0]= centre + n*(-0.20);
  g.p[3]= centre + n*(0.20);
  cd_point outer= centre + u*radius;
  g.p[1]= outer + n*(-0.72*radius);
  g.p[2]= outer + n*(0.72*radius);
  return g;
}

bool
cd_arrow_geometry (tree body, tree arrow, cd_geometry& geometry) {
  if (!is_arrow (arrow)) return false;
  tree source= cd_find_vertex (body, cd_arrow_source (arrow));
  tree target= cd_find_vertex (body, cd_arrow_target (arrow));
  if (!is_vertex (source) || !is_vertex (target)) return false;
  cd_point p1 (cd_vertex_x (source), cd_vertex_y (source));
  cd_point p2 (cd_vertex_x (target), cd_vertex_y (target));
  if (cd_arrow_source (arrow) == cd_arrow_target (arrow)) {
    geometry= cd_loop_geometry (
      p1, cd_option_number (arrow, "loop-angle", 0.0),
      cd_option_number (arrow, "loop-radius", 3.0));
    return true;
  }

  cd_point delta= p2-p1;
  double length= std::max (0.001, std::hypot (delta.x, delta.y));
  cd_point u (delta.x/length, delta.y/length);
  cd_point n (-u.y, u.x);
  double shift= 0.08*cd_option_number (arrow, "offset", 0.0);
  double curve= 0.18*cd_option_number (arrow, "curve", 0.0);
  double source_short= 0.18 + length*0.01*
    cd_option_number (arrow, "shorten-source", 0.0);
  double target_short= 0.18 + length*0.01*
    cd_option_number (arrow, "shorten-target", 0.0);
  cd_point q1= p1 + u*source_short + n*shift;
  cd_point q2= p2 - u*target_short + n*shift;
  cd_point route= q2-q1;
  cd_point bend= n*curve;
  geometry.p[0]= q1;
  geometry.p[1]= q1 + route*(1.0/3.0) + bend;
  geometry.p[2]= q1 + route*(2.0/3.0) + bend;
  geometry.p[3]= q2;
  return true;
}

tree
cd_nearest_vertex (tree body, cd_point point, double radius, int* child_index) {
  if (child_index != nullptr) *child_index= -1;
  tree best;
  double best_distance= radius;
  if (!(is_compound (body, "cd-body") || is_func (body, DOCUMENT))) return best;
  for (int i=0; i<N(body); ++i) {
    tree vertex= body[i];
    if (!is_vertex (vertex)) continue;
    double distance= cd_distance (
      point, cd_point (cd_vertex_x (vertex), cd_vertex_y (vertex)));
    if (distance < best_distance) {
      best= vertex;
      best_distance= distance;
      if (child_index != nullptr) *child_index= i;
    }
  }
  return best;
}

tree
cd_nearest_arrow (tree body, cd_point point, double radius, int* child_index) {
  if (child_index != nullptr) *child_index= -1;
  tree best;
  double best_distance= radius;
  if (!(is_compound (body, "cd-body") || is_func (body, DOCUMENT))) return best;
  for (int i=0; i<N(body); ++i) {
    tree arrow= body[i];
    if (!is_arrow (arrow)) continue;
    cd_geometry geometry;
    if (!cd_arrow_geometry (body, arrow, geometry)) continue;
    cd_point previous= cd_bezier_point (geometry, 0.0);
    for (int sample=1; sample<=24; ++sample) {
      cd_point next= cd_bezier_point (geometry, ((double) sample)/24.0);
      double distance= cd_point_segment_distance (point, previous, next);
      if (distance < best_distance) {
        best= arrow;
        best_distance= distance;
        if (child_index != nullptr) *child_index= i;
      }
      previous= next;
    }
  }
  return best;
}

cd_hit
cd_hit_test (tree body, const commutative_diagram_session_ptr& session,
             cd_point point) {
  cd_hit hit;
  if (session && session->selected_kind == "arrow" &&
      session->selected_id != "") {
    int arrow_index= -1;
    tree arrow= cd_find_arrow (body, session->selected_id, &arrow_index);
    cd_geometry geometry;
    if (is_arrow (arrow) && cd_arrow_geometry (body, arrow, geometry)) {
      if (cd_distance (point, geometry.p[0]) < handle_hit_radius)
        return {"handle", cd_arrow_id (arrow), "source", arrow_index};
      if (cd_distance (point, geometry.p[3]) < handle_hit_radius)
        return {"handle", cd_arrow_id (arrow), "target", arrow_index};
    }
  }

  int index= -1;
  tree vertex= cd_nearest_vertex (body, point, vertex_content_radius, &index);
  if (is_vertex (vertex))
    return {"vertex-content", cd_vertex_id (vertex), "", index};
  tree arrow= cd_nearest_arrow (body, point, arrow_hit_radius, &index);
  if (is_arrow (arrow))
    return {"arrow", cd_arrow_id (arrow), "", index};
  vertex= cd_nearest_vertex (body, point, vertex_move_radius, &index);
  if (is_vertex (vertex))
    return {"vertex-move", cd_vertex_id (vertex), "", index};
  return {"empty", "", "", -1};
}

commutative_diagram_session_ptr
cd_begin_session (tree body, path body_path) {
  tree_rep* key= body_key (body);
  std::lock_guard<std::mutex> guard (session_mutex);
  auto found= sessions.find (key);
  if (found != sessions.end ()) {
    if (auto existing= found->second.lock ()) {
      existing->body_path= body_path;
      existing->halos_visible= true;
      return existing;
    }
    sessions.erase (found);
  }
  auto session= std::make_shared<commutative_diagram_session> ();
  session->body= body;
  session->body_path= body_path;
  session->halos_visible= true;
  sessions[key]= session;
  return session;
}

commutative_diagram_session_ptr
cd_lookup_session (tree body) {
  tree_rep* key= body_key (body);
  std::lock_guard<std::mutex> guard (session_mutex);
  auto found= sessions.find (key);
  if (found == sessions.end ()) return {};
  auto session= found->second.lock ();
  if (!session) sessions.erase (found);
  return session;
}

void
cd_reset_interaction (const commutative_diagram_session_ptr& session) {
  if (!session) return;
  session->interaction= "idle";
  session->has_press_point= false;
  session->interaction_id= "";
  session->interaction_part= "";
  session->has_drag_point= false;
  session->drag_target= "";
  session->has_drag_loop_angle= false;
  session->drag_loop_angle= 0.0;
}

void
cd_clear_selection (const commutative_diagram_session_ptr& session) {
  if (!session) return;
  session->selected_kind= "";
  session->selected_id= "";
  session->hover_kind= "";
  session->hover_id= "";
  cd_reset_interaction (session);
}

void
cd_select (const commutative_diagram_session_ptr& session,
           string kind, string id) {
  if (!session) return;
  session->selected_kind= kind;
  session->selected_id= id;
}

void
cd_set_hover (const commutative_diagram_session_ptr& session,
              string kind, string id) {
  if (!session) return;
  session->hover_kind= kind;
  session->hover_id= id;
}

bool
cd_session_drag_geometry (tree body,
                          const commutative_diagram_session_ptr& session,
                          cd_geometry& geometry) {
  if (!session || !session->has_drag_point) return false;
  if (session->interaction == "connecting") {
    tree source= cd_find_vertex (body, session->interaction_id);
    if (!is_vertex (source)) return false;
    cd_point source_point (cd_vertex_x (source), cd_vertex_y (source));
    if (session->drag_target == session->interaction_id)
      geometry= cd_loop_geometry (
        source_point,
        session->has_drag_loop_angle? session->drag_loop_angle: 0.0, 3.0);
    else {
      cd_point delta= session->drag_point-source_point;
      geometry.p[0]= source_point;
      geometry.p[1]= source_point + delta*(1.0/3.0);
      geometry.p[2]= source_point + delta*(2.0/3.0);
      geometry.p[3]= session->drag_point;
    }
    return true;
  }
  if (session->interaction == "reconnecting") {
    tree arrow= cd_find_arrow (body, session->interaction_id);
    cd_geometry current;
    if (!is_arrow (arrow) || !cd_arrow_geometry (body, arrow, current)) return false;
    string fixed_id= session->interaction_part == "source"?
      cd_arrow_target (arrow): cd_arrow_source (arrow);
    tree fixed_vertex= cd_find_vertex (body, fixed_id);
    cd_point fixed= session->interaction_part == "source"?
      current.p[3]: current.p[0];
    if (is_vertex (fixed_vertex) && session->drag_target == fixed_id) {
      cd_point centre (cd_vertex_x (fixed_vertex), cd_vertex_y (fixed_vertex));
      geometry= cd_loop_geometry (
        centre,
        session->has_drag_loop_angle? session->drag_loop_angle:
          cd_option_number (arrow, "loop-angle", 0.0),
        cd_option_number (arrow, "loop-radius", 3.0));
    }
    else {
      cd_point moving= session->drag_point;
      cd_point start= session->interaction_part == "source"? moving: fixed;
      cd_point end= session->interaction_part == "source"? fixed: moving;
      cd_point delta= end-start;
      geometry.p[0]= start;
      geometry.p[1]= start + delta*(1.0/3.0);
      geometry.p[2]= start + delta*(2.0/3.0);
      geometry.p[3]= end;
    }
    return true;
  }
  return false;
}

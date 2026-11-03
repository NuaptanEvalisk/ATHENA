/******************************************************************************
* MODULE     : commutative_diagram_native.hpp
* DESCRIPTION: Native geometry and ephemeral interaction state for diagrams
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* See the file LICENSE in the root directory.
******************************************************************************/

#ifndef ATHENA_COMMUTATIVE_DIAGRAM_NATIVE_HPP
#define ATHENA_COMMUTATIVE_DIAGRAM_NATIVE_HPP

#include "tree.hpp"
#include "path.hpp"

#include <memory>

struct cd_point {
  double x= 0.0;
  double y= 0.0;
  cd_point ()= default;
  cd_point (double x2, double y2): x (x2), y (y2) {}
};

inline cd_point operator + (cd_point a, cd_point b) {
  return cd_point (a.x + b.x, a.y + b.y);
}

inline cd_point operator - (cd_point a, cd_point b) {
  return cd_point (a.x - b.x, a.y - b.y);
}

inline cd_point operator * (cd_point a, double k) {
  return cd_point (a.x * k, a.y * k);
}

struct cd_geometry {
  cd_point p[4];
};

struct cd_hit {
  string kind;
  string id;
  string part;
  int child_index= -1;
};

struct commutative_diagram_session {
  tree body;
  path body_path;
  string selected_kind;
  string selected_id;
  string hover_kind;
  string hover_id;
  bool halos_visible= false;
  string context_kind;
  time_t context_time= 0;
  string interaction= "idle";
  bool has_press_point= false;
  cd_point press_point;
  string interaction_id;
  string interaction_part;
  bool has_drag_point= false;
  cd_point drag_point;
  string drag_target;
  bool has_drag_loop_angle= false;
  double drag_loop_angle= 0.0;
  bool transaction_open= false;
};

using commutative_diagram_session_ptr=
  std::shared_ptr<commutative_diagram_session>;

string cd_string (tree t, string fallback= "");
double cd_number (tree t, double fallback= 0.0);
string cd_option (tree arrow, string key, string fallback= "");
double cd_option_number (tree arrow, string key, double fallback= 0.0);
tree cd_default_arrow_options ();
tree cd_options_with (tree arrow, string key, string value);
string cd_new_id (string prefix);

string cd_vertex_id (tree vertex);
double cd_vertex_x (tree vertex);
double cd_vertex_y (tree vertex);
string cd_arrow_id (tree arrow);
string cd_arrow_source (tree arrow);
string cd_arrow_target (tree arrow);
tree cd_find_vertex (tree body, string id, int* child_index= nullptr);
tree cd_find_arrow (tree body, string id, int* child_index= nullptr);

double cd_distance (cd_point a, cd_point b);
double cd_point_segment_distance (cd_point p, cd_point a, cd_point b);
cd_point cd_bezier_point (const cd_geometry& geometry, double t);
cd_point cd_bezier_tangent (const cd_geometry& geometry, double t);
cd_geometry cd_shift_geometry (const cd_geometry& geometry, double amount);
cd_geometry cd_loop_geometry (
  cd_point centre, double angle_degrees, double radius_setting);
bool cd_arrow_geometry (tree body, tree arrow, cd_geometry& geometry);
tree cd_nearest_vertex (
  tree body, cd_point point, double radius, int* child_index= nullptr);
tree cd_nearest_arrow (
  tree body, cd_point point, double radius, int* child_index= nullptr);
cd_hit cd_hit_test (
  tree body, const commutative_diagram_session_ptr& session,
  cd_point point);

commutative_diagram_session_ptr cd_begin_session (
  tree body, path body_path);
commutative_diagram_session_ptr cd_lookup_session (tree body);
void cd_reset_interaction (const commutative_diagram_session_ptr& session);
void cd_clear_selection (const commutative_diagram_session_ptr& session);
void cd_select (
  const commutative_diagram_session_ptr& session,
  string kind, string id);
void cd_set_hover (
  const commutative_diagram_session_ptr& session,
  string kind, string id);

bool cd_session_drag_geometry (
  tree body, const commutative_diagram_session_ptr& session,
  cd_geometry& geometry);

#endif

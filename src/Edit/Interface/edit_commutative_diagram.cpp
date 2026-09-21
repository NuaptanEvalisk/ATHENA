/******************************************************************************
* MODULE     : edit_commutative_diagram.cpp
* DESCRIPTION: Native editing for ATHENA commutative diagrams
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
*/

#include "edit_interface.hpp"
#include "ATHENA/Math/commutative_diagram_native.hpp"
#include "tm_timer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace {

path
cd_ancestor_path (tree root, path p, const char* label) {
  while (!is_nil (p)) {
    if (has_subtree (root, p)) {
      tree current= subtree (root, p);
      if (is_compound (current, label)) return p;
    }
    p= path_up (p);
  }
  return path ();
}

path
cd_body_path_from_cursor (tree root, path cursor) {
  return cd_ancestor_path (root, cursor, "cd-body");
}

double
cd_snap (double value) {
  return std::round (2.0 * value) / 2.0;
}

string
cd_swap_side (string value, string top, string bottom) {
  if (value == top) return bottom;
  if (value == bottom) return top;
  return value;
}

string
cd_negate (string value) {
  double number= is_double (value)? as_double (value): 0.0;
  return as_string (-number);
}

void
cd_set_option_copy (tree& arrow, string key, string value) {
  arrow[4]= cd_options_with (arrow, key, value);
}

string
cd_arrow_state_text (tree body,
                     const commutative_diagram_session_ptr& session) {
  if (!session || session->selected_kind != "arrow") return "selected=0\n";
  tree arrow= cd_find_arrow (body, session->selected_id);
  if (!is_compound (arrow, "cd-arrow", 5)) return "selected=0\n";
  struct option_default { const char* key; const char* fallback; };
  static const option_default options[]= {
    {"edge-type", "arrow"}, {"tail", "none"}, {"body", "solid"},
    {"head", "arrowhead"}, {"level", "1"}, {"curve", "0"},
    {"offset", "0"}, {"shorten-source", "0"}, {"shorten-target", "0"},
    {"loop-radius", "3"}, {"loop-angle", "0"},
    {"label-alignment", "left"}, {"label-position", "50"},
    {"color", "black"}, {"label-color", "black"}
  };
  string result= "selected=1\n";
  for (const auto& option: options)
    result << option.key << "="
           << cd_option (arrow, option.key, option.fallback) << "\n";
  return result;
}

struct cd_navigation_record {
  string kind;
  string id;
  cd_point position;
  int child_index= -1;
};

std::vector<cd_navigation_record>
cd_navigation_records (tree body) {
  std::vector<cd_navigation_record> records;
  if (!is_compound (body, "cd-body")) return records;
  for (int i=0; i<N(body); ++i) {
    tree child= body[i];
    if (is_compound (child, "cd-vertex", 4))
      records.push_back ({"vertex", cd_vertex_id (child),
        cd_point (cd_vertex_x (child), cd_vertex_y (child)), i});
    else if (is_compound (child, "cd-arrow", 5)) {
      cd_geometry geometry;
      if (cd_arrow_geometry (body, child, geometry))
        records.push_back ({"arrow", cd_arrow_id (child),
          cd_bezier_point (geometry, 0.5), i});
    }
  }
  return records;
}

bool
cd_direction (native_cd_action action, cd_point& direction) {
  switch (action) {
  case native_cd_action::navigate_left: direction= cd_point (-1.0, 0.0); return true;
  case native_cd_action::navigate_right: direction= cd_point (1.0, 0.0); return true;
  case native_cd_action::navigate_up: direction= cd_point (0.0, 1.0); return true;
  case native_cd_action::navigate_down: direction= cd_point (0.0, -1.0); return true;
  default: return false;
  }
}

double
cd_navigation_score (cd_point origin, cd_point candidate, cd_point direction) {
  cd_point delta= candidate-origin;
  double primary= delta.x*direction.x + delta.y*direction.y;
  if (primary <= 1.0e-6) return std::numeric_limits<double>::infinity ();
  double perpendicular= std::fabs (delta.x*direction.y-delta.y*direction.x);
  double distance= std::hypot (delta.x, delta.y);
  return 4.0*(perpendicular/primary) + 0.05*distance;
}

} // namespace

void
edit_interface_rep::commutative_diagram_action (
  native_cd_action action, string first, string second) {
  if (action == native_cd_action::insert_diagram) {
    tree body (make_tree_label ("cd-body"), 1);
    body[0]= "";
    tree diagram (COMMUTATIVE_DIAGRAM);
    diagram << "8.1" << "3.1" << body;
    start_editing ();
    insert_tree (diagram, path (2, 0));
    end_editing ();
    return;
  }

  path body_path= cd_body_path_from_cursor (et, tp);
  if (is_nil (body_path) || !has_subtree (et, body_path)) return;
  tree body= subtree (et, body_path);
  if (!is_compound (body, "cd-body")) return;
  commutative_diagram_session_= cd_begin_session (body, body_path);
  auto session= commutative_diagram_session_;

  auto refresh= [&] {
    typeset_invalidate (path_up (body_path));
    invalidate_all ();
    (void) publish_ui_text (
      actor_command_kind::ui_commutative_diagram_arrow_state,
      cd_arrow_state_text (body, session));
  };

  auto selected_arrow= [&] (int& index) {
    index= -1;
    if (session->selected_kind != "arrow") return tree ();
    return cd_find_arrow (body, session->selected_id, &index);
  };

  if (action == native_cd_action::set_selected_option) {
    int index;
    tree arrow= selected_arrow (index);
    if (!is_compound (arrow, "cd-arrow", 5) || index < 0) return;
    tree updated= copy (arrow);
    cd_set_option_copy (updated, first, second);
    start_editing ();
    assign (body_path * index, updated);
    end_editing ();
    refresh ();
    return;
  }

  if (action == native_cd_action::reverse_selected_arrow ||
      action == native_cd_action::flip_selected_arrow ||
      action == native_cd_action::flip_selected_label) {
    int index;
    tree arrow= selected_arrow (index);
    if (!is_compound (arrow, "cd-arrow", 5) || index < 0) return;
    tree updated= copy (arrow);
    if (action == native_cd_action::reverse_selected_arrow) {
      tree source= copy (updated[1]);
      updated[1]= copy (updated[2]);
      updated[2]= source;
      cd_set_option_copy (updated, "label-position",
        as_string (100.0-std::clamp (
          cd_option_number (updated, "label-position", 50.0), 0.0, 100.0)));
    }
    if (action != native_cd_action::flip_selected_label) {
      cd_set_option_copy (updated, "offset",
        cd_negate (cd_option (updated, "offset", "0")));
      cd_set_option_copy (updated, "curve",
        cd_negate (cd_option (updated, "curve", "0")));
      cd_set_option_copy (updated, "loop-radius",
        cd_negate (cd_option (updated, "loop-radius", "3")));
      cd_set_option_copy (updated, "tail",
        cd_swap_side (cd_option (updated, "tail", "none"),
                      "top-hook", "bottom-hook"));
      cd_set_option_copy (updated, "head",
        cd_swap_side (cd_option (updated, "head", "arrowhead"),
                      "top-harpoon", "bottom-harpoon"));
    }
    cd_set_option_copy (updated, "label-alignment",
      cd_swap_side (cd_option (updated, "label-alignment", "left"),
                    "left", "right"));
    start_editing ();
    assign (body_path * index, updated);
    end_editing ();
    refresh ();
    return;
  }

  if (action == native_cd_action::delete_selected) {
    if (session->selected_id == "") return;
    string kind= session->selected_kind;
    string id= session->selected_id;
    start_editing ();
    for (int i=N(body)-1; i>=0; --i) {
      tree child= body[i];
      bool erase= false;
      if (kind == "arrow" && is_compound (child, "cd-arrow", 5))
        erase= cd_arrow_id (child) == id;
      else if (kind == "vertex") {
        if (is_compound (child, "cd-vertex", 4))
          erase= cd_vertex_id (child) == id;
        else if (is_compound (child, "cd-arrow", 5))
          erase= cd_arrow_source (child) == id || cd_arrow_target (child) == id;
      }
      if (erase) remove (body_path * i, 1);
    }
    end_editing ();
    cd_clear_selection (session);
    if (N(body) > 0) go_to_end (body_path * 0);
    refresh ();
    return;
  }

  if (action == native_cd_action::clear_selection) {
    cd_clear_selection (session);
    refresh ();
    return;
  }

  if (action == native_cd_action::edit_selected_label) {
    int index= -1;
    tree selected= session->selected_kind == "vertex"?
      cd_find_vertex (body, session->selected_id, &index):
      cd_find_arrow (body, session->selected_id, &index);
    if (index >= 0 && (is_compound (selected, "cd-vertex", 4) ||
                       is_compound (selected, "cd-arrow", 5)))
      go_to_end (body_path * index * 3);
    return;
  }

  cd_point direction;
  if (cd_direction (action, direction)) {
    auto records= cd_navigation_records (body);
    auto current= std::find_if (records.begin (), records.end (),
      [&] (const cd_navigation_record& record) {
        return record.kind == session->selected_kind &&
               record.id == session->selected_id;
      });
    if (current == records.end ()) return;
    const cd_navigation_record* best= nullptr;
    double best_score= std::numeric_limits<double>::infinity ();
    for (const auto& candidate: records) {
      if (candidate.id == current->id && candidate.kind == current->kind) continue;
      double score= cd_navigation_score (current->position, candidate.position, direction);
      if (score < best_score) { best= &candidate; best_score= score; }
    }
    if (best != nullptr) {
      cd_select (session, best->kind, best->id);
      cd_set_hover (session, "", "");
      if (N(body) > 0) go_to_end (body_path * 0);
      refresh ();
    }
    return;
  }

  if (action == native_cd_action::enlarge_horizontal ||
      action == native_cd_action::enlarge_vertical) {
    path diagram_path= path_up (body_path);
    if (!has_subtree (et, diagram_path)) return;
    tree diagram= subtree (et, diagram_path);
    if (!is_func (diagram, COMMUTATIVE_DIAGRAM, 3)) return;
    double width= std::max (1.0, cd_number (diagram[0], 8.1));
    double height= std::max (1.0, cd_number (diagram[1], 3.1));
    if (action == native_cd_action::enlarge_horizontal) width += 1.0;
    else height += 1.0;
    start_editing ();
    assign (diagram_path * 0, tree (as_string (width)));
    assign (diagram_path * 1, tree (as_string (height)));
    end_editing ();
    refresh ();
    return;
  }

  if (action == native_cd_action::trim) {
    path diagram_path= path_up (body_path);
    if (!has_subtree (et, diagram_path)) return;
    tree diagram= subtree (et, diagram_path);
    if (!is_func (diagram, COMMUTATIVE_DIAGRAM, 3)) return;
    std::vector<cd_point> points;
    for (int i=0; i<N(body); ++i) {
      tree child= body[i];
      if (is_compound (child, "cd-vertex", 4))
        points.emplace_back (cd_vertex_x (child), cd_vertex_y (child));
      else if (is_compound (child, "cd-arrow", 5)) {
        cd_geometry geometry;
        if (!cd_arrow_geometry (body, child, geometry)) continue;
        for (int sample=0; sample<=24; ++sample)
          points.push_back (cd_bezier_point (geometry, ((double) sample)/24.0));
      }
    }
    start_editing ();
    if (points.empty ()) {
      assign (diagram_path * 0, tree ("2"));
      assign (diagram_path * 1, tree ("2"));
    }
    else {
      double xmin= points[0].x, xmax= points[0].x;
      double ymin= points[0].y, ymax= points[0].y;
      for (const auto& point: points) {
        xmin= std::min (xmin, point.x); xmax= std::max (xmax, point.x);
        ymin= std::min (ymin, point.y); ymax= std::max (ymax, point.y);
      }
      double cx= 0.5*(xmin+xmax), cy= 0.5*(ymin+ymax);
      for (int i=0; i<N(body); ++i) {
        tree child= body[i];
        if (!is_compound (child, "cd-vertex", 4)) continue;
        assign (body_path * i * 1, tree (as_string (cd_vertex_x (child)-cx)));
        assign (body_path * i * 2, tree (as_string (cd_vertex_y (child)-cy)));
      }
      assign (diagram_path * 0, tree (as_string (xmax-xmin+1.2)));
      assign (diagram_path * 1, tree (as_string (ymax-ymin+1.2)));
    }
    end_editing ();
    refresh ();
    return;
  }
}

bool
edit_interface_rep::commutative_diagram_keypress (string key) {
  path body_path= cd_body_path_from_cursor (et, tp);
  if (is_nil (body_path) || !has_subtree (et, body_path)) return false;
  tree body= subtree (et, body_path);
  auto session= cd_lookup_session (body);
  if (!session || session->selected_id == "") return false;

  // When the cursor is inside a vertex or arrow label, normal math/text editing
  // owns the keyboard. Diagram navigation only applies at the body sentinel.
  path object_path= tp;
  while (!is_nil (object_path) && object_path != body_path) {
    if (has_subtree (et, object_path)) {
      tree current= subtree (et, object_path);
      if (is_compound (current, "cd-vertex") || is_compound (current, "cd-arrow"))
        return false;
    }
    object_path= path_up (object_path);
  }

  native_cd_action action;
  if (key == "left") action= native_cd_action::navigate_left;
  else if (key == "right") action= native_cd_action::navigate_right;
  else if (key == "up") action= native_cd_action::navigate_up;
  else if (key == "down") action= native_cd_action::navigate_down;
  else if (key == "return") action= native_cd_action::edit_selected_label;
  else if (key == "delete" || key == "backspace") action= native_cd_action::delete_selected;
  else if (key == "escape") action= native_cd_action::clear_selection;
  else return false;
  commutative_diagram_action (action);
  return true;
}

bool
edit_interface_rep::commutative_diagram_pointer_event (
  string message, double x, double y, path diagram_path) {
  if (is_nil (diagram_path) || !has_subtree (et, diagram_path) ||
      !is_compound (subtree (et, diagram_path), "commutative-diagram"))
    return false;
  path body_path= diagram_path * 2;
  if (!has_subtree (et, body_path)) return false;
  tree body= subtree (et, body_path);
  if (!is_compound (body, "cd-body")) return false;
  commutative_diagram_session_= cd_begin_session (body, body_path);
  auto session= commutative_diagram_session_;
  cd_point point (x, y);

  auto refresh= [&] {
    typeset_invalidate (path_up (body_path));
    invalidate_all ();
  };
  auto publish_arrow_state= [&] {
    (void) publish_ui_text (
      actor_command_kind::ui_commutative_diagram_arrow_state,
      cd_arrow_state_text (body, session));
  };
  auto navigation_focus= [&] {
    if (N(body) > 0) go_to_end (body_path * 0);
  };
  auto label_focus= [&] (const cd_hit& hit) {
    if (hit.child_index >= 0) go_to_end (body_path * hit.child_index * 3);
  };

  if (message == "click") {
    if (session->transaction_open) {
      end_editing ();
      session->transaction_open= false;
      cd_reset_interaction (session);
    }
    cd_hit hit= cd_hit_test (body, session, point);
    string old_kind= session->selected_kind;
    string old_id= session->selected_id;
    session->has_press_point= true;
    session->press_point= point;
    if (hit.kind == "handle") {
      cd_select (session, "arrow", hit.id);
      navigation_focus ();
      session->interaction= "pending-reconnect";
      session->interaction_id= hit.id;
      session->interaction_part= hit.part;
    }
    else if (hit.kind == "vertex-content") {
      cd_select (session, "vertex", hit.id);
      if (!(old_kind == "vertex" && old_id == hit.id)) navigation_focus ();
      session->interaction= "pending-connect";
      session->interaction_id= hit.id;
      if (old_kind == "vertex" && old_id == hit.id) session->interaction_part= "edit";
    }
    else if (hit.kind == "vertex-move") {
      cd_select (session, "vertex", hit.id);
      navigation_focus ();
      session->interaction= "moving";
      session->interaction_id= hit.id;
      start_editing ();
      session->transaction_open= true;
    }
    else if (hit.kind == "arrow") {
      cd_select (session, "arrow", hit.id);
      if (!(old_kind == "arrow" && old_id == hit.id)) navigation_focus ();
      session->interaction= "pending-arrow";
      session->interaction_id= hit.id;
      if (old_kind == "arrow" && old_id == hit.id) session->interaction_part= "edit";
    }
    else {
      tree vertex (make_tree_label ("cd-vertex"), 4);
      vertex[0]= cd_new_id ("vertex");
      vertex[1]= as_string (cd_snap (x));
      vertex[2]= as_string (cd_snap (y));
      vertex[3]= compound ("math", tree ("X"));
      int index= N(body);
      start_editing ();
      insert (body_path * index, tree (TUPLE, vertex));
      end_editing ();
      cd_select (session, "vertex", cd_vertex_id (vertex));
      session->interaction= "created";
      go_to_end (body_path * index * 3);
    }
    refresh ();
    publish_arrow_state ();
    return true;
  }

  if (message == "drag") {
    if ((session->interaction == "pending-connect" ||
         session->interaction == "pending-reconnect") &&
        session->has_press_point &&
        cd_distance (session->press_point, point) > 0.14)
      session->interaction= session->interaction == "pending-connect"?
        "connecting": "reconnecting";

    if (session->interaction == "moving") {
      int index= -1;
      tree vertex= cd_find_vertex (body, session->interaction_id, &index);
      if (is_compound (vertex, "cd-vertex", 4) && index >= 0) {
        cd_point snapped (cd_snap (x), cd_snap (y));
        tree occupied= cd_nearest_vertex (body, snapped, 0.20);
        if (!is_compound (occupied, "cd-vertex", 4) ||
            cd_vertex_id (occupied) == cd_vertex_id (vertex)) {
          assign (body_path * index * 1, tree (as_string (snapped.x)));
          assign (body_path * index * 2, tree (as_string (snapped.y)));
        }
      }
    }
    else if (session->interaction == "connecting" ||
             session->interaction == "reconnecting") {
      session->has_drag_point= true;
      session->drag_point= point;
      tree target= cd_nearest_vertex (body, point, 0.46);
      session->drag_target= is_compound (target, "cd-vertex", 4)?
        cd_vertex_id (target): "";
      tree centre_vertex;
      if (session->interaction == "connecting")
        centre_vertex= cd_find_vertex (body, session->interaction_id);
      else {
        tree arrow= cd_find_arrow (body, session->interaction_id);
        if (is_compound (arrow, "cd-arrow", 5)) {
          string fixed_id= session->interaction_part == "source"?
            cd_arrow_target (arrow): cd_arrow_source (arrow);
          centre_vertex= cd_find_vertex (body, fixed_id);
        }
      }
      if (is_compound (centre_vertex, "cd-vertex", 4)) {
        double dx= x-cd_vertex_x (centre_vertex);
        double dy= y-cd_vertex_y (centre_vertex);
        if (dx*dx+dy*dy > 0.01) {
          session->has_drag_loop_angle= true;
          session->drag_loop_angle= std::atan2 (dy, dx)*57.29577951308232;
        }
      }
    }
    refresh ();
    return true;
  }

  if (message == "select") {
    string interaction= session->interaction;
    string object_id= session->interaction_id;
    string part= session->interaction_part;
    string target= session->drag_target;
    if (interaction == "moving" && session->transaction_open) {
      end_editing ();
      session->transaction_open= false;
    }
    else if (interaction == "connecting" && target != "") {
      tree arrow (make_tree_label ("cd-arrow"), 5);
      arrow[0]= cd_new_id ("arrow");
      arrow[1]= object_id;
      arrow[2]= target;
      arrow[3]= compound ("math", tree (""));
      arrow[4]= cd_default_arrow_options ();
      if (target == object_id && session->has_drag_loop_angle)
        arrow[4]= cd_options_with (
          arrow, "loop-angle", as_string (session->drag_loop_angle));
      int index= N(body);
      start_editing ();
      insert (body_path * index, tree (TUPLE, arrow));
      end_editing ();
      cd_select (session, "arrow", cd_arrow_id (arrow));
      go_to_end (body_path * index * 3);
    }
    else if (interaction == "reconnecting" && target != "") {
      int index= -1;
      tree arrow= cd_find_arrow (body, object_id, &index);
      if (is_compound (arrow, "cd-arrow", 5) && index >= 0) {
        tree updated= copy (arrow);
        bool changed= false;
        if (part == "source" && target != cd_arrow_source (arrow)) {
          updated[1]= target;
          changed= true;
        }
        else if (part == "target" && target != cd_arrow_target (arrow)) {
          updated[2]= target;
          changed= true;
        }
        if (changed) {
          if (cd_arrow_source (updated) == cd_arrow_target (updated) &&
              session->has_drag_loop_angle)
            cd_set_option_copy (
              updated, "loop-angle", as_string (session->drag_loop_angle));
          start_editing ();
          assign (body_path * index, updated);
          end_editing ();
        }
      }
    }
    else if (interaction == "pending-connect" && part == "edit") {
      int index= -1;
      tree vertex= cd_find_vertex (body, object_id, &index);
      if (is_compound (vertex, "cd-vertex", 4) && index >= 0)
        go_to_end (body_path * index * 3);
    }
    else if (interaction == "pending-arrow" && part == "edit") {
      int index= -1;
      tree arrow= cd_find_arrow (body, object_id, &index);
      if (is_compound (arrow, "cd-arrow", 5) && index >= 0)
        go_to_end (body_path * index * 3);
    }
    cd_reset_interaction (session);
    refresh ();
    publish_arrow_state ();
    return true;
  }

  if (message == "move" || message == "enter") {
    session->halos_visible= true;
    cd_hit hit= cd_hit_test (body, session, point);
    if (hit.kind == "arrow" || hit.kind == "handle")
      cd_set_hover (session, "arrow", hit.id);
    else if (hit.kind == "vertex-content" || hit.kind == "vertex-move")
      cd_set_hover (session, "vertex", hit.id);
    else cd_set_hover (session, "", "");
    refresh ();
    return true;
  }

  if (message == "leave") {
    session->halos_visible= false;
    cd_set_hover (session, "", "");
    refresh ();
    return true;
  }

  if (message == "double-click") {
    cd_hit hit= cd_hit_test (body, session, point);
    if (hit.kind == "vertex-content" || hit.kind == "vertex-move") {
      cd_select (session, "vertex", hit.id);
      label_focus (hit);
    }
    else if (hit.kind == "arrow" || hit.kind == "handle") {
      cd_select (session, "arrow", hit.id);
      label_focus (hit);
    }
    else return false;
    refresh ();
    publish_arrow_state ();
    return true;
  }

  if (message == "adjust") {
    cd_hit hit= cd_hit_test (body, session, point);
    navigation_focus ();
    session->context_time= texmacs_time ();
    if (hit.kind == "arrow" || hit.kind == "handle") {
      cd_select (session, "arrow", hit.id);
      session->context_kind= "arrow";
    }
    else session->context_kind= "diagram";
    refresh ();
    publish_arrow_state ();
    (void) publish_ui (
      actor_command_kind::ui_commutative_diagram_popup,
      session->context_kind == "arrow"? 1: 0);
    return true;
  }

  return false;
}

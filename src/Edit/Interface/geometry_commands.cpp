/******************************************************************************
* MODULE     : geometry_commands.cpp
* DESCRIPTION: Actor-owned geometry commands, selection wrapping and gestures
* COPYRIGHT  : (C) 2010 Joris van der Hoeven, 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "format_geometry.hpp"
#include "editor.hpp"
#include "new_view.hpp"
#include "native_interfaces.hpp"
#include "scheme.hpp"

#include <cmath>

bool geometry_space_context (tree t) { return is_func (t, SPACE); }
bool geometry_var_space_context (tree t) {
  return geometry_space_context (t) || is_compound (t, "separating-space", 1) ||
    is_compound (t, "application-space", 1);
}
bool geometry_hspace_context (tree t) { return is_func (t, HSPACE); }
bool geometry_vspace_context (tree t) {
  return is_func (t, VSPACE) || is_compound (t, "vspace*");
}
bool geometry_vadjust_context (tree t) {
  return is_compound (t, "reduce-by") || is_compound (t, "reduce-bottom-by") ||
    is_compound (t, "reduce-top-by");
}
bool geometry_move_context (tree t) {
  return is_func (t, MOVE) || is_func (t, SHIFT);
}
bool geometry_resize_context (tree t) {
  return is_func (t, RESIZE) || is_compound (t, "extend") || is_func (t, CLIPPED);
}

namespace {

enum class adjustment {speed, variant, horizontal, vertical, extremal, incremental};

tree focus_tree () {
  editor ed= get_current_editor ();
  tree root= ed->the_root ();
  return subtree (root, ed->focus_get ());
}

bool parent_tree (tree t, tree& parent) {
  if (admits_edit_observer (t)) return false;
  path ip= obtain_ip (t);
  if (!ip_attached (ip) || is_nil (ip)) return false;
  tree root= get_current_editor ()->the_root ();
  parent= subtree (root, reverse (ip->next));
  return true;
}

// Keep extension dispatch at each enclosing node (math, tables and graphics).
void outward (tree t, const char* command, object argument) {
  tree parent;
  if (parent_tree (t, parent)) call (command, object (parent), argument);
}

struct focus_pointer {
  observer pointer;
  explicit focus_pointer (tree t): pointer (tree_pointer_new (t)) {}
  ~focus_pointer () { tree_pointer_delete (pointer); }
  void restore () {
    tree t= obtain_tree (pointer);
    path ip= obtain_ip (t);
    if (ip_attached (ip)) get_current_editor ()->manual_focus_set (reverse (ip));
  }
};

void make_ternary (tree t) {
  if (N (t) == 1) tree_insert (t, 1, tree (TUPLE, "0ex", "1ex"));
  else if (N (t) == 2) tree_insert (t, 1, tree (TUPLE, "1ex"));
}

void rubber_increase (tree t, int amount) {
  if (N (t) != 1 && (N (t) != 3 ||
      !geometry_lengths_consistent (t[0], t[1]) ||
      !geometry_lengths_consistent (t[1], t[2]))) return;
  for (int i= 0; i < N (t); ++i) geometry_length_increase (t[i], amount);
}

void resize_defaults (tree t, bool horizontal) {
  const int first= horizontal ? 1 : 2;
  geometry_replace_empty (t, first, tree (PLUS, horizontal ? "1l" : "1b", geometry_zero_unit ()));
  geometry_replace_empty (t, first + 2, tree (PLUS, horizontal ? "1r" : "1t", geometry_zero_unit ()));
}

bool handles (tree t, adjustment action) {
  switch (action) {
  case adjustment::speed:
    return geometry_var_space_context (t) || geometry_hspace_context (t) ||
      geometry_vspace_context (t) || geometry_vadjust_context (t) ||
      geometry_move_context (t) || geometry_resize_context (t) || is_func (t, IMAGE, 5);
  case adjustment::variant:
    return geometry_move_context (t) || geometry_resize_context (t);
  case adjustment::horizontal:
    return geometry_var_space_context (t) || geometry_hspace_context (t) ||
      geometry_move_context (t) || geometry_resize_context (t) || is_func (t, IMAGE, 5);
  case adjustment::vertical:
    return geometry_space_context (t) || geometry_vspace_context (t) ||
      geometry_vadjust_context (t) || geometry_move_context (t) ||
      geometry_resize_context (t) || is_func (t, IMAGE, 5);
  case adjustment::extremal:
    return geometry_resize_context (t);
  case adjustment::incremental:
    return geometry_space_context (t) || geometry_resize_context (t) || is_func (t, IMAGE, 5);
  }
  return false;
}

void adjust (tree t, adjustment action, bool forward, const char* command) {
  if (!handles (t, action)) { outward (t, command, object (forward)); return; }
  const int direction= forward ? 1 : -1;
  if (action == adjustment::variant) { geometry_circulate_unit (direction); return; }
  if (geometry_vadjust_context (t)) {
    if (N (t) < 2) return;
    if (action == adjustment::speed) geometry_length_increase_step (t[1], direction);
    else geometry_length_increase (t[1], direction);
    return;
  }
  focus_pointer focus (t);
  if (action == adjustment::speed) {
    int first= geometry_move_context (t) ? 1 : geometry_resize_context (t) ? 3 : 0;
    if (N (t) > first) {
      // Preserve the existing resize speed convention; dimensions remain independent.
      const int step= geometry_resize_context (t) ? -direction : direction;
      geometry_length_increase_step (t[first], step);
      if ((geometry_move_context (t) || geometry_resize_context (t)) &&
          N (t) > first + 1 && !geometry_lengths_consistent (t[first], t[first+1]))
        geometry_length_increase_step (t[first+1], step);
    }
  }
  else if (geometry_resize_context (t)) {
    if (N (t) < 5) return;
    const bool horizontal= action == adjustment::horizontal || action == adjustment::extremal;
    resize_defaults (t, horizontal);
    const int first= horizontal ? 1 : 2;
    const int amount= horizontal ? direction : -direction;
    const bool both= action == adjustment::extremal || action == adjustment::incremental;
    if (!both || geometry_lengths_consistent (t[first], t[first+2])) {
      if (both) geometry_length_increase (t[first], amount);
      geometry_length_increase (t[first+2], amount);
    }
  }
  else if (geometry_move_context (t)) {
    const int child= action == adjustment::horizontal ? 1 : 2;
    if (N (t) <= child) return;
    geometry_replace_empty (t, child, geometry_zero_unit ());
    geometry_length_increase (t[child], child == 1 ? direction : -direction);
  }
  else if (is_func (t, IMAGE, 5)) {
    const int child= action == adjustment::horizontal ? 1 : action == adjustment::vertical ? 2 : 4;
    if (N (t) <= child) return;
    geometry_replace_empty (t, child, child == 1 ? "1w" : child == 2 ? "1h" : "0h");
    geometry_length_increase (t[child], child == 4 ? -direction : direction);
  }
  else if (geometry_hspace_context (t) || geometry_vspace_context (t))
    rubber_increase (t, direction);
  else if (action == adjustment::horizontal) {
    if (N (t) > 0) geometry_length_increase (t[0], direction);
  }
  else {
    make_ternary (t);
    if (N (t) == 3) {
      bool both= action == adjustment::incremental;
      if (!both || geometry_lengths_consistent (t[1], t[2])) {
        if (both) geometry_length_increase (t[1], -direction);
        geometry_length_increase (t[2], -direction);
      }
    }
  }
  focus.restore ();
}

void focused (const char* command, bool argument) {
  call (command, object (focus_tree ()), object (argument));
}

bool graphical_text_context (tree t) {
  return is_graphical_text (t);
}

string graphical_attribute (tree t, string name) {
  return as_string (call ("graphical-get-attribute", object (t), object (name)));
}

void set_graphical_attribute (tree t, string name, string value) {
  (void) call ("graphical-set-attribute", object (t), object (name), object (value));
}

bool active_graphics_context () {
  editor ed= get_current_editor ();
  return !is_nil (ed) && ed->inside_graphics (false) &&
         !as_bool (call ("in-commutative-diagram?")) &&
         ed->get_env_string (PREAMBLE) == "false";
}

void graphical_text_horizontal (tree t, bool forwards) {
  const string old= graphical_attribute (t, "text-at-halign");
  string value;
  if (forwards) value= old == "right" ? "center" : "left";
  else value= old == "left" ? "center" : "right";
  set_graphical_attribute (t, "text-at-halign", value);
}

void graphical_text_vertical (tree t, bool down) {
  const string var= as_string (call ("graphics-valign-var", object (t)));
  const string old= graphical_attribute (t, var);
  string value;
  if (down) {
    if (old == "bottom") value= "base";
    else if (old == "base") value= "axis";
    else if (old == "axis") value= "center";
    else value= "top";
  }
  else {
    if (old == "top") value= "center";
    else if (old == "center") value= "axis";
    else if (old == "axis") value= "base";
    else value= "bottom";
  }
  set_graphical_attribute (t, var, value);
}

void insert_wrapper (tree t, string message, string context) {
  editor ed= get_current_editor ();
  const bool selection= ed->selection_active_small ();
  if (selection) ed->selection_cut ("wrapbuf");
  else ed->selection_cancel ();
  ed->var_insert_tree (t, path (0, 0));
  object left= call ("kbd-find-inv-system-binding", string_to_object ("(geometry-left)"));
  object right= call ("kbd-find-inv-system-binding", string_to_object ("(geometry-right)"));
  if (is_string (left) && is_string (right))
    message << " using " << as_string (left) << ", " << as_string (right) << ", etc. or ";
  else message << " using the keyboard or ";
  ed->set_message (message * "via the fields in the focus bar", context);
  if (selection) ed->selection_paste ("wrapbuf");
}

} // namespace

void geometry_speed (tree t, bool forward) { adjust (t, adjustment::speed, forward, "geometry-speed"); }
void geometry_variant (tree t, bool forward) { adjust (t, adjustment::variant, forward, "geometry-variant"); }
void geometry_horizontal (tree t, bool forward) {
  if (graphical_text_context (t)) { graphical_text_horizontal (t, forward); return; }
  adjust (t, adjustment::horizontal, forward, "geometry-horizontal");
}
void geometry_vertical (tree t, bool down) {
  if (graphical_text_context (t)) { graphical_text_vertical (t, down); return; }
  if (active_graphics_context ()) {
    (void) call ("graphics-change-geo-valign", object (down));
    return;
  }
  adjust (t, adjustment::vertical, down, "geometry-vertical");
}
void geometry_extremal (tree t, bool forward) {
  if (graphical_text_context (t)) {
    set_graphical_attribute (t, "text-at-halign", forward ? "left" : "right");
    return;
  }
  adjust (t, adjustment::extremal, forward, "geometry-extremal");
}
void geometry_incremental (tree t, bool down) {
  if (graphical_text_context (t)) {
    const string var= as_string (call ("graphics-valign-var", object (t)));
    set_graphical_attribute (t, var, down ? "top" : "bottom");
    return;
  }
  adjust (t, adjustment::incremental, down, "geometry-incremental");
}
void geometry_default (tree t) {
  tree parent;
  if (parent_tree (t, parent)) call ("geometry-default", object (parent));
}

void geometry_scale (tree t, double factor) {
  if (!std::isfinite (factor)) return;
  editor ed= get_current_editor ();
  if (!(geometry_var_space_context (t) || geometry_hspace_context (t) ||
        geometry_vspace_context (t) || is_func (t, IMAGE, 5))) {
    tree parent;
    if (parent_tree (t, parent)) call ("geometry-scale", object (parent), object (factor));
    else ed->geometry_pinch_scale= factor;
    return;
  }
  path ip= copy (obtain_ip (t));
  if (ed->geometry_pinch_modified) {
    ed->undo (0);
    tree root= ed->the_root ();
    if (!ip_attached (ip) || !has_subtree (root, reverse (ip))) return;
    t= subtree (root, reverse (ip));
  }
  tree before= copy (t);
  double scale= std::sqrt (std::abs (factor) + 0.000001);
  if (is_func (t, IMAGE, 5)) {
    if (N (t) < 3) return;
    double multiplier= is_empty (t[1]) || is_empty (t[2]) ? 0.1 : 0.001;
    geometry_length_scale (t[1], scale, multiplier);
    geometry_length_scale (t[2], scale, multiplier);
  }
  else {
    double multiplier= geometry_vspace_context (t) ? 0.2 : N (t) == 1 ? 1.0 : 0.001;
    for (int i= 0; i < N (t); ++i) geometry_length_scale (t[i], scale, multiplier);
  }
  ed->geometry_pinch_modified= t != before;
  if (ip_attached (ip)) ed->go_to_end (reverse (ip));
}

void geometry_rotate (tree t, double angle) {
  if (!std::isfinite (angle)) return;
  tree parent;
  if (parent_tree (t, parent)) call ("geometry-rotate", object (parent), object (angle));
  else get_current_editor ()->geometry_pinch_angle= angle;
}

void geometry_slower () { focused ("geometry-speed", false); }
void geometry_faster () { focused ("geometry-speed", true); }
void geometry_circulate (bool forward) { focused ("geometry-variant", forward); }
void geometry_reset () { call ("geometry-default", object (focus_tree ())); }
void geometry_left () { focused ("geometry-horizontal", false); }
void geometry_right () { focused ("geometry-horizontal", true); }
void geometry_up () { focused ("geometry-vertical", false); }
void geometry_down () { focused ("geometry-vertical", true); }
void geometry_start () { focused ("geometry-extremal", false); }
void geometry_end () { focused ("geometry-extremal", true); }
void geometry_top () { focused ("geometry-incremental", false); }
void geometry_bottom () { focused ("geometry-incremental", true); }

void geometry_pinch_clear () {
  editor ed= get_current_editor ();
  ed->geometry_pinch_modified= false;
  ed->geometry_pinch_scale= 1.0;
  ed->geometry_pinch_angle= 0.0;
}
void geometry_pinch_start () { geometry_pinch_clear (); }
void geometry_pinch_end () {
  double scale= get_current_editor ()->geometry_pinch_scale;
  if (scale > 1.05) call ("structured-maximize", object (focus_tree ()));
  else if (scale < 0.95) call ("structured-minimize", object (focus_tree ()));
  geometry_pinch_clear ();
}
void geometry_pinch_scale (double factor) { call ("geometry-scale", object (focus_tree ()), object (factor)); }
void geometry_pinch_rotate (double angle) { call ("geometry-rotate", object (focus_tree ()), object (angle)); }

void geometry_make_move (tree h, tree v) { insert_wrapper (tree (MOVE, "", h, v), "Adjust position", "move"); }
void geometry_make_shift (tree h, tree v) { insert_wrapper (tree (SHIFT, "", h, v), "Adjust position", "shift"); }
void geometry_make_resize (tree l, tree b, tree r, tree t) { insert_wrapper (tree (RESIZE, "", l, b, r, t), "Adjust extents", "resize"); }
void geometry_make_extend (tree l, tree b, tree r, tree t) { insert_wrapper (compound ("extend", "", l, b, r, t), "Adjust extension", "extend"); }
void geometry_make_clipped (tree l, tree b, tree r, tree t) { insert_wrapper (tree (CLIPPED, "", l, b, r, t), "Adjust clipping", "clipped"); }
void geometry_make_reduce_by (tree by) { insert_wrapper (compound ("reduce-by", "", by), "Reduce vertical size", "reduce-by"); }

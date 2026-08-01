/******************************************************************************
* MODULE     : concat_commutative_diagram.cpp
* DESCRIPTION: Native typesetting for ATHENA commutative diagrams
* COPYRIGHT  : (C) 2026  Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "concater.hpp"
#include "Boxes/construct.hpp"
#include "Boxes/graphics.hpp"
#include "curve.hpp"
#include "drd_std.hpp"

#include <cmath>

struct cd_point {
  double x, y;
  cd_point (double x2= 0.0, double y2= 0.0): x (x2), y (y2) {}
};

static cd_point operator + (cd_point a, cd_point b) {
  return cd_point (a.x + b.x, a.y + b.y);
}

static cd_point operator - (cd_point a, cd_point b) {
  return cd_point (a.x - b.x, a.y - b.y);
}

static cd_point operator * (cd_point a, double k) {
  return cd_point (a.x * k, a.y * k);
}

struct cd_geometry {
  cd_point p[4];
};

struct cd_named_geometry {
  string id;
  cd_geometry geometry;
};

struct cd_render_state {
  string selected_kind, selected_id;
  string hover_kind, hover_id;
  string target_id;
  bool has_drag;
  cd_geometry drag_geometry;

  cd_render_state (): has_drag (false) {}
};

struct cd_vertex_record {
  tree node;
  path formula_ip;
  string id;
  cd_point position;
};

struct cd_arrow_record {
  tree node;
  path formula_ip;
  string id;
};

static string
cd_string (tree t, string fallback= "") {
  return is_atomic (t)? t->label: fallback;
}

static double
cd_number (tree t, double fallback= 0.0) {
  if (!is_atomic (t) || !is_double (t->label)) return fallback;
  return as_double (t->label);
}

static string
cd_option (tree arrow, string key, string fallback) {
  if (!is_compound (arrow, "cd-arrow", 5) ||
      !is_func (arrow[4], TUPLE))
    return fallback;
  tree options= arrow[4];
  for (int i=0; i+1<N(options); i+=2)
    if (cd_string (options[i]) == key)
      return cd_string (options[i+1], fallback);
  return fallback;
}

static double
cd_option_number (tree arrow, string key, double fallback) {
  string value= cd_option (arrow, key, "");
  return is_double (value)? as_double (value): fallback;
}

static cd_point
cd_bezier_point (const cd_geometry& g, double t) {
  double u= 1.0 - t;
  return g.p[0] * (u*u*u) +
         g.p[1] * (3.0*u*u*t) +
         g.p[2] * (3.0*u*t*t) +
         g.p[3] * (t*t*t);
}

static cd_point
cd_bezier_tangent (const cd_geometry& g, double t) {
  double u= 1.0 - t;
  cd_point v= (g.p[1] - g.p[0]) * (3.0*u*u) +
              (g.p[2] - g.p[1]) * (6.0*u*t) +
              (g.p[3] - g.p[2]) * (3.0*t*t);
  double len= std::sqrt (v.x*v.x + v.y*v.y);
  if (len < 1.0e-9) return cd_point (1.0, 0.0);
  return v * (1.0 / len);
}

static cd_geometry
cd_shift_geometry (const cd_geometry& g, double amount) {
  cd_point tangent= cd_bezier_tangent (g, 0.5);
  cd_point normal (-tangent.y, tangent.x);
  cd_geometry shifted;
  for (int i=0; i<4; i++) shifted.p[i]= g.p[i] + normal * amount;
  return shifted;
}

static bool
cd_tree_point (tree t, cd_point& p) {
  if (!is_func (t, TUPLE, 2)) return false;
  p= cd_point (cd_number (t[0]), cd_number (t[1]));
  return true;
}

static void
cd_parse_layout (tree layout, array<cd_named_geometry>& geometries,
                 cd_render_state& state) {
  if (!is_func (layout, TUPLE)) return;
  for (int i=0; i<N(layout); i++) {
    tree entry= layout[i];
    if (!is_func (entry, TUPLE) || N(entry) == 0) continue;
    string kind= cd_string (entry[0]);
    if (kind == "selected" && N(entry) == 3) {
      state.selected_kind= cd_string (entry[1]);
      state.selected_id= cd_string (entry[2]);
    }
    else if (kind == "hover" && N(entry) == 3) {
      state.hover_kind= cd_string (entry[1]);
      state.hover_id= cd_string (entry[2]);
    }
    else if (kind == "target" && N(entry) == 2)
      state.target_id= cd_string (entry[1]);
    else if (kind == "arrow" && N(entry) == 6) {
      cd_named_geometry named;
      named.id= cd_string (entry[1]);
      bool valid= named.id != "";
      for (int j=0; j<4; j++)
        valid= cd_tree_point (entry[j+2], named.geometry.p[j]) && valid;
      if (valid) geometries << named;
    }
    else if (kind == "drag" && N(entry) == 3) {
      cd_point start, end;
      state.has_drag= cd_tree_point (entry[1], start) &&
                      cd_tree_point (entry[2], end);
      if (state.has_drag) {
        cd_point delta= end - start;
        state.drag_geometry.p[0]= start;
        state.drag_geometry.p[1]= start + delta * (1.0 / 3.0);
        state.drag_geometry.p[2]= start + delta * (2.0 / 3.0);
        state.drag_geometry.p[3]= end;
      }
    }
    else if (kind == "drag-curve" && N(entry) == 5) {
      state.has_drag= true;
      for (int j=0; j<4; j++)
        state.has_drag=
          cd_tree_point (entry[j+1], state.drag_geometry.p[j]) &&
          state.has_drag;
    }
  }
}

static bool
cd_find_geometry (array<cd_named_geometry> geometries, string id,
                  cd_geometry& geometry) {
  for (int i=0; i<N(geometries); i++)
    if (geometries[i].id == id) {
      geometry= geometries[i].geometry;
      return true;
    }
  return false;
}

class cd_box_builder {
public:
  edit_env env;
  path ip;
  frame fr;
  SI line_unit;
  array<box> boxes;
  array<SI> xs, ys;

  cd_box_builder (edit_env env2, path ip2, frame fr2):
    env (env2), ip (ip2), fr (fr2) {
    line_unit= max ((SI) 1, env->as_length ("1ln"));
  }

  point physical (cd_point p) {
    return fr (point (p.x, p.y));
  }

  void add (box b, SI x= 0, SI y= 0) {
    boxes << b;
    xs << x;
    ys << y;
  }

  void line (cd_point a, cd_point b, string color,
             SI width= 0) {
    point p= physical (a), q= physical (b);
    SI w= width == 0? line_unit: width;
    add (line_box (decorate (ip), (SI) p[0], (SI) p[1],
                   (SI) q[0], (SI) q[1],
                   pencil (named_color (color), w)));
  }

  void curve (const cd_geometry& geometry, string color, SI width,
              string dash= "solid") {
    array<point> points (4);
    for (int i=0; i<4; i++) points[i]= physical (geometry.p[i]);
    array<bool> style;
    SI style_unit= 0;
    if (dash == "dashed") {
      style << true << false;
      style_unit= 3 * line_unit;
    }
    else if (dash == "dotted") {
      style << true << false << false;
      style_unit= line_unit;
    }
    add (curve_box (decorate (ip), bezier (points), 1.0,
                    pencil (named_color (color), width),
                    style, array<point> (), style_unit,
                    brush (false), array<box> (0)));
  }

  void squiggle (const cd_geometry& geometry, string color, SI width) {
    array<point> points;
    array<path> paths;
    for (int i=0; i<=32; i++) {
      double t= ((double) i) / 32.0;
      cd_point p= cd_bezier_point (geometry, t);
      cd_point tangent= cd_bezier_tangent (geometry, t);
      cd_point normal (-tangent.y, tangent.x);
      double wave= 0.045 * std::sin (t * 16.0 * 3.141592653589793);
      points << physical (p + normal * wave);
      paths << decorate (ip);
    }
    add (curve_box (decorate (ip), poly_segment (points, paths), 1.0,
                    pencil (named_color (color), width),
                    array<bool> (), array<point> (), 0,
                    brush (false), array<box> (0)));
  }

  void circle (cd_point centre, double radius, string color,
               bool filled, SI width= 0) {
    point p= physical (centre);
    SI r= (SI) std::round (radius * fr->direct_scalar (1.0));
    SI w= width == 0? line_unit: width;
    pencil pen (named_color (color), w);
    brush fill= filled? brush (named_color (color)): brush (false);
    add (point_box (decorate (ip), p, r, pen, fill, "round"));
  }

  void formula (tree formula, path formula_ip, cd_point position,
                string color, bool small, string halo_color= "",
                bool strong_halo= false) {
    tree old_color= env->local_begin (COLOR, color);
    tree old_size;
    if (small) old_size= env->local_begin (FONT_SIZE, "0.84");
    box b= typeset_as_concat (env, formula, formula_ip);
    if (small) env->local_end (FONT_SIZE, old_size);
    env->local_end (COLOR, old_color);
    point p= physical (position);
    SI x= (SI) p[0] - ((b->x1 + b->x2) >> 1);
    SI y= (SI) p[1] - ((b->y1 + b->y2) >> 1);
    if (halo_color != "") {
      SI pad= max (3 * line_unit, env->as_length ("0.06cm"));
      array<SI> outline_x, outline_y;
      outline_x << b->x1 - pad << b->x2 + pad
                << b->x2 + pad << b->x1 - pad;
      outline_y << b->y1 - pad << b->y1 - pad
                << b->y2 + pad << b->y2 + pad;
      add (polygon_box (decorate (formula_ip), outline_x, outline_y,
                        brush (false),
                        pencil (named_color (halo_color),
                                strong_halo? 2 * line_unit: line_unit)),
           x, y);
    }
    add (b, x, y);
  }
};

static void
cd_add_open_head (cd_box_builder& b, cd_point tip, cd_point direction,
                  string color, double length= 0.18, double width= 0.11) {
  cd_point normal (-direction.y, direction.x);
  cd_point base= tip - direction * length;
  b.line (tip, base + normal * width, color);
  b.line (tip, base - normal * width, color);
}

static void
cd_add_bar (cd_box_builder& b, cd_point centre, cd_point direction,
            string color, double half= 0.11) {
  cd_point normal (-direction.y, direction.x);
  b.line (centre - normal * half, centre + normal * half, color);
}

static void
cd_add_markers (cd_box_builder& b, const cd_geometry& geometry,
                tree arrow, string color) {
  string tail= cd_option (arrow, "tail", "none");
  string head= cd_option (arrow, "head", "arrowhead");
  cd_point start= geometry.p[0], end= geometry.p[3];
  cd_point start_u= cd_bezier_tangent (geometry, 0.0);
  cd_point end_u= cd_bezier_tangent (geometry, 1.0);

  if (tail == "mono" || tail == "arrowhead")
    cd_add_open_head (b, start, start_u * -1.0, color);
  else if (tail == "maps-to")
    cd_add_bar (b, start, start_u, color, 0.13);

  if (head == "arrowhead")
    cd_add_open_head (b, end, end_u, color);
  else if (head == "epi") {
    cd_add_open_head (b, end, end_u, color);
    cd_add_open_head (b, end - end_u * 0.10, end_u, color);
  }
  else if (head == "top-harpoon" || head == "bottom-harpoon") {
    double sign= head == "top-harpoon"? 1.0: -1.0;
    cd_point normal (-end_u.y, end_u.x);
    b.line (end, end - end_u * 0.18 + normal * (sign * 0.11), color);
  }

  if (tail == "top-hook" || tail == "bottom-hook") {
    double sign= tail == "top-hook"? 1.0: -1.0;
    cd_point normal (-start_u.y, start_u.x);
    cd_point p1= start + start_u * 0.10;
    cd_point p2= start - start_u * 0.10 + normal * (sign * 0.13);
    cd_point p3= start + start_u * 0.12 + normal * (sign * 0.13);
    b.line (p1, p2, color);
    b.line (p2, p3, color);
  }
}

static void
cd_add_edge_symbol (cd_box_builder& b, const cd_geometry& geometry,
                    string type, string color) {
  cd_point centre= cd_bezier_point (geometry, 0.5);
  cd_point tangent= cd_bezier_tangent (geometry, 0.5);
  cd_point normal (-tangent.y, tangent.x);
  double half= 0.13;
  if (type == "adjunction") {
    b.line (centre - tangent * half, centre + tangent * half, color);
    b.line (centre + tangent * half - normal * half,
            centre + tangent * half + normal * half, color);
  }
  else {
    double side= type == "corner-inverse"? -1.0: 1.0;
    cd_point corner= centre + tangent * (side * half);
    b.line (centre, corner, color);
    b.line (corner, corner + normal * half, color);
    b.line (centre, centre + normal * half, color);
  }
}

static array<double>
cd_level_offsets (int level) {
  array<double> offsets;
  if (level <= 1) offsets << 0.0;
  else if (level == 2) offsets << -0.045 << 0.045;
  else if (level == 3) offsets << -0.09 << 0.0 << 0.09;
  else offsets << -0.135 << -0.045 << 0.045 << 0.135;
  return offsets;
}

static void
cd_add_arrow_body (cd_box_builder& b, const cd_geometry& geometry,
                   tree arrow) {
  string type= cd_option (arrow, "edge-type", "arrow");
  string body= cd_option (arrow, "body", "solid");
  string color= cd_option (arrow, "color", "black");
  int level= (int) std::round (cd_option_number (arrow, "level", 1.0));
  level= max (1, min (4, level));

  if (type != "arrow") {
    cd_add_edge_symbol (b, geometry, type, color);
    return;
  }

  if (body == "squiggly") {
    b.squiggle (geometry, color, b.line_unit);
    cd_add_markers (b, geometry, arrow, color);
  }
  else if (body == "none")
    cd_add_markers (b, geometry, arrow, color);
  else {
    array<double> offsets= cd_level_offsets (level);
    for (int i=0; i<N(offsets); i++) {
      cd_geometry shifted= cd_shift_geometry (geometry, offsets[i]);
      b.curve (shifted, color, b.line_unit, body);
      cd_add_markers (b, shifted, arrow, color);
    }
  }

  cd_point centre= cd_bezier_point (geometry, 0.5);
  cd_point tangent= cd_bezier_tangent (geometry, 0.5);
  cd_point normal (-tangent.y, tangent.x);
  if (body == "barred")
    cd_add_bar (b, centre, tangent, color);
  else if (body == "double-barred") {
    cd_add_bar (b, centre - tangent * 0.06, tangent, color);
    cd_add_bar (b, centre + tangent * 0.06, tangent, color);
  }
  else if (body == "bullet-solid")
    b.circle (centre, 0.055, color, true);
  else if (body == "bullet-hollow")
    b.circle (centre, 0.065, color, false);
  (void) normal;
}

void
concater_rep::typeset_commutative_diagram (tree t, path ip) {
  if (N(t) != 3) { typeset_error (t, ip); return; }

  double width= max (1.0, cd_number (t[0], 8.1));
  double height= max (1.0, cd_number (t[1], 3.1));
  SI unit= max ((SI) 1, env->as_length ("1cm"));
  SI box_width= (SI) std::round (width * unit);
  SI box_height= (SI) std::round (height * unit);
  frame fr= scaling ((double) unit, point (box_width / 2.0, 0.0));

  tree request (EXTERN, "commutative-diagram-layout", tree (QUOTE, t[2]));
  tree layout= env->rewrite (request);
  array<cd_named_geometry> geometries;
  cd_render_state state;
  cd_parse_layout (layout, geometries, state);

  array<cd_vertex_record> vertices;
  array<cd_arrow_record> arrows;
  tree body= t[2];
  if (is_compound (body, "cd-body") || is_func (body, DOCUMENT))
    for (int i=0; i<N(body); i++) {
      if (is_compound (body[i], "cd-vertex", 4)) {
        cd_vertex_record v;
        v.node= body[i];
        v.formula_ip= descend (descend (descend (ip, 2), i), 3);
        v.id= cd_string (body[i][0]);
        v.position= cd_point (cd_number (body[i][1]),
                              cd_number (body[i][2]));
        vertices << v;
      }
      else if (is_compound (body[i], "cd-arrow", 5)) {
        cd_arrow_record a;
        a.node= body[i];
        a.formula_ip= descend (descend (descend (ip, 2), i), 3);
        a.id= cd_string (body[i][0]);
        arrows << a;
      }
    }

  cd_box_builder builder (env, ip, fr);
  double xmin= -width / 2.0, xmax= width / 2.0;
  double ymin= -height / 2.0, ymax= height / 2.0;
  string grid_color= "#d8ddff";
  SI grid_width= max ((SI) 1, builder.line_unit >> 1);
  for (double x= std::floor (xmin * 2.0) / 2.0;
       x <= xmax + 1.0e-9; x+=0.5)
    builder.line (cd_point (x, ymin), cd_point (x, ymax),
                  grid_color, grid_width);
  for (double y= std::floor (ymin * 2.0) / 2.0;
       y <= ymax + 1.0e-9; y+=0.5)
    builder.line (cd_point (xmin, y), cd_point (xmax, y),
                  grid_color, grid_width);

  // Keep every selection halo behind every arrow, including at crossings.
  for (int i=0; i<N(arrows); i++) {
    cd_geometry geometry;
    if (!cd_find_geometry (geometries, arrows[i].id, geometry)) continue;
    bool selected= state.selected_kind == "arrow" &&
                   state.selected_id == arrows[i].id;
    bool hovered= state.hover_kind == "arrow" &&
                  state.hover_id == arrows[i].id;
    if (selected || hovered)
      builder.curve (geometry,
                     selected? "#9fc1ee": "#d0e1f7",
                     (selected? 10: 8) * builder.line_unit);
  }

  for (int i=0; i<N(arrows); i++) {
    cd_geometry geometry;
    if (!cd_find_geometry (geometries, arrows[i].id, geometry)) continue;
    bool selected= state.selected_kind == "arrow" &&
                   state.selected_id == arrows[i].id;
    bool hovered= state.hover_kind == "arrow" &&
                  state.hover_id == arrows[i].id;
    cd_add_arrow_body (builder, geometry, arrows[i].node);

    double label_t= 0.01 * max (0.0, min (100.0,
      cd_option_number (arrows[i].node, "label-position", 50.0)));
    cd_point label_position= cd_bezier_point (geometry, label_t);
    cd_point tangent= cd_bezier_tangent (geometry, label_t);
    cd_point normal (-tangent.y, tangent.x);
    string alignment= cd_option (arrows[i].node,
                                 "label-alignment", "left");
    if (alignment == "left") label_position= label_position + normal * 0.16;
    else if (alignment == "right")
      label_position= label_position - normal * 0.16;
    builder.formula (arrows[i].node[3], arrows[i].formula_ip,
                     label_position,
                     cd_option (arrows[i].node, "label-color", "black"),
                     true);
    if (selected || hovered) {
      string handle_color= selected? "#3976c5": "#82aee5";
      builder.circle (geometry.p[0], selected? 0.075: 0.060,
                      handle_color, true);
      builder.circle (geometry.p[3], selected? 0.075: 0.060,
                      handle_color, true);
    }
  }

  if (state.has_drag) {
    builder.curve (state.drag_geometry, "#3976c5",
                   2 * builder.line_unit, "dashed");
    cd_add_open_head (builder, state.drag_geometry.p[3],
                      cd_bezier_tangent (state.drag_geometry, 1.0),
                      "#3976c5");
  }

  for (int i=0; i<N(vertices); i++) {
    bool selected= state.selected_kind == "vertex" &&
                   state.selected_id == vertices[i].id;
    bool hovered= state.hover_kind == "vertex" &&
                  state.hover_id == vertices[i].id;
    bool target= state.target_id == vertices[i].id;
    builder.formula (vertices[i].node[3], vertices[i].formula_ip,
                     vertices[i].position, "black", false,
                     target? "#4c9f70":
                     selected? "#3976c5": hovered? "#82aee5": "",
                     selected || target);
  }

  box diagram= commutative_diagram_box (
    ip, builder.boxes, builder.xs, builder.ys,
    fr, box_width, box_height);
  array<tree> relay_args;
  relay_args << tree ("relay-with-frame")
             << tree ("commutative-diagram-handle")
             << t[0] << t[1] << t[2];
  box relayed= relay_box (ip, diagram, relay_args);
  tree spring (HTAB, "0fn");
  if (N(a) == 0)
    print (empty_box (decorate_left (ip), 0, 0, 0, env->fn->yx));
  print (space (0));
  control (spring, decorate_left (ip));
  print (relayed);
  print (space (0));
  control (spring, decorate_right (ip));
}

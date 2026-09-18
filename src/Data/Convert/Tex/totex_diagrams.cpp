/******************************************************************************
* MODULE     : totex_diagrams.cpp
* DESCRIPTION: Native commutative diagram support for LaTeX export
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "totex_internal.hpp"

#include <cmath>
#include <vector>

namespace {

using namespace latex_export_internal;

struct DiagramVertex {
  scheme_tree source;
  int x;
  int y;
};

scheme_tree
source_apply (string head, scheme_tree args) {
  scheme_tree out= stree_apply (head);
  if (stree_list (args))
    for (int i=0; i<N(args); ++i) out << args[i];
  return out;
}

scheme_tree
raw_latex (string value) {
  scheme_tree out= stree_apply ("!athena-latex-raw");
  out << stree_string (value);
  return out;
}

double
diagram_number (scheme_tree value, double fallback) {
  if (!is_atomic (value)) return fallback;
  string text= atom_text (value);
  return is_double (text) ? as_double (text) : fallback;
}

int
diagram_slot (scheme_tree value) {
  return (int) std::round (2.0 * diagram_number (value, 0.0));
}

string
vertex_id (scheme_tree vertex) {
  return func_is (vertex, "cd-vertex", 4) && string_atom (vertex[1])
           ? atom_text (vertex[1]) : string ("");
}

scheme_tree
diagram_body_items (scheme_tree body) {
  scheme_tree out (TUPLE);
  if (!head_is (body, "cd-body") && !head_is (body, "document")) return out;
  for (int i=1; i<N(body); ++i) out << body[i];
  return out;
}

std::vector<DiagramVertex>
diagram_vertices (scheme_tree body) {
  std::vector<DiagramVertex> out;
  scheme_tree items= diagram_body_items (body);
  for (int i=0; i<N(items); ++i) {
    scheme_tree vertex= items[i];
    if (!func_is (vertex, "cd-vertex", 4)) continue;
    out.push_back ({vertex, diagram_slot (vertex[2]), diagram_slot (vertex[3])});
  }
  return out;
}

std::vector<scheme_tree>
diagram_arrows (scheme_tree body) {
  std::vector<scheme_tree> out;
  scheme_tree items= diagram_body_items (body);
  for (int i=0; i<N(items); ++i)
    if (func_is (items[i], "cd-arrow", 5)) out.push_back (items[i]);
  return out;
}

const DiagramVertex*
find_vertex (const std::vector<DiagramVertex>& vertices, string id) {
  for (const auto& vertex: vertices)
    if (vertex_id (vertex.source) == id) return &vertex;
  return nullptr;
}

string
arrow_option (scheme_tree arrow, string key, string fallback) {
  if (!func_is (arrow, "cd-arrow", 5) || !func_is (arrow[5], "tuple"))
    return fallback;
  scheme_tree options= arrow[5];
  for (int i=1; i+1<N(options); i += 2)
    if (string_atom (options[i]) && atom_text (options[i]) == key)
      return string_atom (options[i+1]) ? atom_text (options[i+1]) : fallback;
  return fallback;
}

string
repeat_string (string value, int count) {
  string out;
  for (int i=0; i<count; ++i) out << value;
  return out;
}

string
arrow_direction (const DiagramVertex& source, const DiagramVertex& target) {
  int dx= target.x - source.x;
  int dy= target.y - source.y;
  return repeat_string (dy >= 0 ? "u" : "d", std::abs (dy)) *
         repeat_string (dx >= 0 ? "r" : "l", std::abs (dx));
}

string
loop_option (scheme_tree arrow) {
  double angle= diagram_number (
    stree_string (arrow_option (arrow, "loop-angle", "0")), 0.0);
  double normalized= angle - 360.0 * std::floor ((angle + 180.0) / 360.0);
  if (normalized >= -45.0 && normalized < 45.0) return "loop right";
  if (normalized >= 45.0 && normalized < 135.0) return "loop above";
  if (normalized >= 135.0 || normalized < -135.0) return "loop left";
  return "loop below";
}

string
color_option (string color) {
  if (color == "grey") color= "gray";
  static const char* known[]= {
    "black", "white", "red", "green", "blue", "cyan", "magenta",
    "yellow", "gray", "darkgray", "lightgray", "brown", "lime", "olive",
    "orange", "pink", "purple", "teal", "violet"
  };
  for (const char* candidate: known)
    if (color == candidate) return "draw=" * color;
  return "";
}

void
append_option (std::vector<string>& values, string value) {
  if (value != "") values.push_back (value);
}

std::vector<string>
static_arrow_options (scheme_tree arrow, string direction, bool self) {
  string tail= arrow_option (arrow, "tail", "none");
  string body= arrow_option (arrow, "body", "solid");
  string head= arrow_option (arrow, "head", "arrowhead");
  double level= diagram_number (stree_string (arrow_option (arrow, "level", "1")), 1.0);
  double curve= diagram_number (stree_string (arrow_option (arrow, "curve", "0")), 0.0);
  double offset= diagram_number (stree_string (arrow_option (arrow, "offset", "0")), 0.0);
  string color= color_option (arrow_option (arrow, "color", "black"));

  std::vector<string> out;
  if (self) append_option (out, loop_option (arrow));
  else append_option (out, direction);

  if (level > 1.0 && tail == "none" && head == "arrowhead") out.push_back ("Rightarrow");
  if (tail == "maps-to") out.push_back ("maps to");
  else if (tail == "top-hook") out.push_back ("hook");
  else if (tail == "bottom-hook") out.push_back ("hook'");
  else if (tail == "mono" || tail == "arrowhead") out.push_back ("tail");

  if (head == "none") out.push_back ("no head");
  else if (head == "epi") out.push_back ("two heads");
  else if (head == "top-harpoon") out.push_back ("harpoon");
  else if (head == "bottom-harpoon") out.push_back ("harpoon'");

  if (body == "dashed") out.push_back ("dashed");
  else if (body == "dotted") out.push_back ("dotted");

  if (!self && curve != 0.0)
    out.push_back ((curve > 0.0 ? "bend left=" : "bend right=") *
                   scheme_inexact_string (std::abs (curve * 10.0)));
  if (!self && offset != 0.0)
    out.push_back ((offset > 0.0 ? "shift left=" : "shift right=") *
                   scheme_inexact_string (std::abs (offset * 0.4)) * "ex");
  append_option (out, color);
  return out;
}

string
join_options (const std::vector<string>& values) {
  string out;
  for (size_t i=0; i<values.size (); ++i) {
    if (i) out << ", ";
    out << values[i];
  }
  return out;
}

scheme_tree
formula_content (scheme_tree formula) {
  return func_is (formula, "math", 1) ? formula[1] : formula;
}

scheme_tree
convert_formula (scheme_tree formula) {
  latex_export_env_set ("mode", object ("math"));
  scheme_tree out= tmtex_convert (formula_content (formula));
  latex_export_env_reset ("mode");
  return out;
}

scheme_tree
arrow_output (scheme_tree arrow, const DiagramVertex& source,
              const DiagramVertex& target) {
  bool self= string_atom (arrow[2]) && string_atom (arrow[3]) &&
             atom_text (arrow[2]) == atom_text (arrow[3]);
  string direction= arrow_direction (source, target);
  std::vector<string> options= static_arrow_options (arrow, direction, self);
  scheme_tree label= convert_formula (arrow[4]);
  string alignment= arrow_option (arrow, "label-alignment", "left");
  string prefix= "\\arrow[" * join_options (options);
  string suffix= alignment == "right" ? "\"'" :
                 (alignment == "centre" || alignment == "over")
                   ? "\" description" : "\"";

  scheme_tree out= stree_apply ("!concat");
  if (latex_export_latex_empty (label)) {
    out << raw_latex (prefix) << raw_latex ("]");
    return out;
  }
  out << raw_latex (prefix * ", \"") << label << raw_latex (suffix * "]");
  return out;
}

scheme_tree
diagram_cell (const DiagramVertex* record,
              const std::vector<DiagramVertex>& vertices,
              const std::vector<scheme_tree>& arrows) {
  if (record == nullptr) return stree_string ("");
  string id= vertex_id (record->source);
  scheme_tree out= stree_apply ("!concat");
  out << convert_formula (record->source[4]);
  for (scheme_tree arrow: arrows) {
    if (!string_atom (arrow[2]) || atom_text (arrow[2]) != id || !string_atom (arrow[3]))
      continue;
    const DiagramVertex* target= find_vertex (vertices, atom_text (arrow[3]));
    if (target != nullptr) out << arrow_output (arrow, *record, *target);
  }
  return out;
}

scheme_tree
diagram_matrix (const std::vector<DiagramVertex>& vertices,
                const std::vector<scheme_tree>& arrows) {
  if (vertices.empty ()) return stree_string ("");
  int min_x= vertices[0].x, max_x= vertices[0].x;
  int min_y= vertices[0].y, max_y= vertices[0].y;
  for (const auto& vertex: vertices) {
    min_x= std::min (min_x, vertex.x); max_x= std::max (max_x, vertex.x);
    min_y= std::min (min_y, vertex.y); max_y= std::max (max_y, vertex.y);
  }

  scheme_tree matrix= stree_apply ("!concat");
  bool first_row= true;
  for (int y=max_y; y>=min_y; --y) {
    if (!first_row) matrix << raw_latex (" \\\\" "\n");
    first_row= false;
    bool first_cell= true;
    for (int x=min_x; x<=max_x; ++x) {
      if (!first_cell) matrix << raw_latex (" & ");
      first_cell= false;
      const DiagramVertex* found= nullptr;
      for (const auto& vertex: vertices)
        if (vertex.x == x && vertex.y == y) { found= &vertex; break; }
      matrix << diagram_cell (found, vertices, arrows);
    }
  }
  return matrix;
}

scheme_tree
commutative_diagram_output (scheme_tree args) {
  scheme_tree source= source_apply ("commutative-diagram", args);
  if (!stree_list (args) || N(args) != 3)
    return latex_export_athena_data_wrap (source, stree_string (""));
  scheme_tree body= args[2];
  std::vector<DiagramVertex> vertices= diagram_vertices (body);
  std::vector<scheme_tree> arrows= diagram_arrows (body);
  scheme_tree begin= stree_apply ("!begin");
  begin << stree_string ("tikzcd");
  scheme_tree fallback (TUPLE);
  fallback << begin << diagram_matrix (vertices, arrows);
  return latex_export_athena_data_wrap (source, fallback);
}

} // namespace

scheme_tree
latex_export_commutative_diagram (scheme_tree args) {
  return commutative_diagram_output (args);
}

/******************************************************************************
* MODULE     : table_commands.cpp
* DESCRIPTION: Native high-level table editing commands
* COPYRIGHT  : (C) 2001 Joris van der Hoeven, 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* See the file LICENSE in the root directory.
******************************************************************************/

#include "table_commands.hpp"

#include "tree_search.hpp"

#include <algorithm>

namespace athena::table_commands {
namespace {

struct selection_keeper {
  editor_rep* ed= nullptr;
  bool active= false;
  observer left;
  observer right;

  explicit selection_keeper (editor_rep* editor): ed (editor) {
    if (ed == nullptr || !ed->selection_active_any ()) return;
    path start= ed->selection_get_start ();
    path end= ed->selection_get_end ();
    left= ed->position_new (start);
    right= ed->position_new (end);
    active= true;
  }

  ~selection_keeper () {
    if (!active) return;
    ed->position_delete (left);
    ed->position_delete (right);
  }

  void restore () const {
    if (active)
      ed->selection_set_paths (ed->position_get (left), ed->position_get (right));
  }
};

void
set_table_pairs (editor_rep* ed,
                 std::initializer_list<std::pair<string,string>> values) {
  if (ed == nullptr) return;
  for (const auto& [name, value]: values)
    ed->table_set_format (name, tree (value));
}

void
set_cell_pairs (editor_rep* ed,
                std::initializer_list<std::pair<string,string>> values) {
  if (ed == nullptr) return;
  selection_keeper selection (ed);
  for (const auto& [name, value]: values) {
    selection.restore ();
    ed->cell_set_format (name, tree (value));
  }
  selection.restore ();
}

bool
string_arg (const array<object>& args, int index, string& result) {
  if (index < 0 || index >= N(args) || !is_string (args[index])) return false;
  result= as_string (args[index]);
  return true;
}

bool
bool_arg (const array<object>& args, int index, bool& result) {
  if (index < 0 || index >= N(args) || !is_bool (args[index])) return false;
  result= as_bool (args[index]);
  return true;
}

bool
cell_range (editor_rep* ed, int r1, int r2, int c1, int c2,
            path& start, path& end) {
  if (ed == nullptr) return false;
  path first= ed->table_search_cell (r1, c1);
  path last= ed->table_search_cell (r2, c2);
  if (is_nil (first) || is_nil (last)) return false;
  start= first;
  end= path_up (last) * 1;
  return true;
}

void
select_cells (editor_rep* ed, int r1, int r2, int c1, int c2) {
  path start, end;
  if (cell_range (ed, r1, r2, c1, c2, start, end))
    ed->selection_set_paths (start, end);
}

enum border_bits: unsigned {
  OUTER_TOP= 1u << 0,
  OUTER_BOTTOM= 1u << 1,
  OUTER_LEFT= 1u << 2,
  OUTER_RIGHT= 1u << 3,
  INNER_TOP= 1u << 4,
  INNER_BOTTOM= 1u << 5,
  INNER_LEFT= 1u << 6,
  INNER_RIGHT= 1u << 7
};

void
set_borders (editor_rep* ed, string value, unsigned mask) {
  if (ed == nullptr) return;
  array<int> extents= ed->table_get_extents ();
  array<int> cells= ed->table_which_cells ();
  if (N(extents) != 2 || N(cells) != 4) return;
  const int rows= extents[0], cols= extents[1];
  const int r1= cells[0], r2= cells[1], c1= cells[2], c2= cells[3];
  selection_keeper original (ed);

  original.restore ();
  if (mask & INNER_TOP) ed->cell_set_format ("cell-tborder", tree (value));
  original.restore ();
  if (mask & INNER_BOTTOM) ed->cell_set_format ("cell-bborder", tree (value));
  original.restore ();
  if (mask & INNER_LEFT) ed->cell_set_format ("cell-lborder", tree (value));
  original.restore ();
  if (mask & INNER_RIGHT) ed->cell_set_format ("cell-rborder", tree (value));

  if (mask & OUTER_TOP) {
    select_cells (ed, r1, r1, c1, c2);
    ed->cell_set_format ("cell-tborder", tree (value));
    if (r1 > 1) {
      select_cells (ed, r1 - 1, r1 - 1, c1, c2);
      ed->cell_set_format ("cell-bborder", tree (value));
    }
  }
  if (mask & OUTER_BOTTOM) {
    select_cells (ed, r2, r2, c1, c2);
    ed->cell_set_format ("cell-bborder", tree (value));
    if (r2 < rows) {
      select_cells (ed, r2 + 1, r2 + 1, c1, c2);
      ed->cell_set_format ("cell-tborder", tree (value));
    }
  }
  if (mask & OUTER_LEFT) {
    select_cells (ed, r1, r2, c1, c1);
    ed->cell_set_format ("cell-lborder", tree (value));
    if (c1 > 1) {
      select_cells (ed, r1, r2, c1 - 1, c1 - 1);
      ed->cell_set_format ("cell-rborder", tree (value));
    }
  }
  if (mask & OUTER_RIGHT) {
    select_cells (ed, r1, r2, c2, c2);
    ed->cell_set_format ("cell-rborder", tree (value));
    if (c2 < cols) {
      select_cells (ed, r1, r2, c2 + 1, c2 + 1);
      ed->cell_set_format ("cell-lborder", tree (value));
    }
  }
  original.restore ();
}

bool
contains_eq_number (const tree& source, path base, path& found) {
  if (is_compound (source, "eq-number")) {
    found= base;
    return true;
  }
  if (!is_compound (source)) return false;
  for (int i=0; i<N(source); ++i)
    if (contains_eq_number (source[i], base * i, found)) return true;
  return false;
}

bool
is_eqnarray_target (tree target) {
  return is_compound (target, "eqnarray") || is_compound (target, "eqnarray*");
}

} // namespace

editor_rep*
current_editor () {
  editor ed= get_current_editor ();
  return is_nil (ed) ? nullptr : ed.operator-> ();
}

namespace {

tree
one_cell_tabular (tree body) {
  tree table (TABLE, tree (ROW, tree (CELL, body)));
  tree format (TFORMAT, table);
  return compound ("tabular", format);
}

void
insert_table_wrapper (editor_rep* ed, bool big) {
  if (ed == nullptr) return;
  tree body= "";
  if (ed->selection_active_normal ()) body= ed->selection_get_cut ();
  tree tabular= one_cell_tabular (body);
  if (big)
    ed->insert_tree (
      compound ("big-table", tabular, tree (DOCUMENT, "")),
      path (0, 0, 0, path (0, 0, 0)) * end (body));
  else
    ed->insert_tree (
      compound ("small-table", tabular, ""),
      path (0, 0, 0, path (0, 0, 0)) * end (body));
}

} // namespace

void insert_small_table (editor_rep* ed) { insert_table_wrapper (ed, false); }
void insert_big_table (editor_rep* ed) { insert_table_wrapper (ed, true); }

void
cell_set_format_star (editor_rep* ed, string var, string value) {
  if (ed == nullptr) return;
  selection_keeper selection (ed);
  selection.restore ();
  ed->cell_set_format (var, tree (value));
  selection.restore ();
  if (var == "cell-hmode" && value == "auto")
    ed->cell_set_format ("cell-width", tree (""));
  else if (var == "cell-width" && value != "" &&
           ed->cell_get_format ("cell-hmode") == "auto")
    ed->cell_set_format ("cell-hmode", tree ("exact"));
  else if (var == "cell-vmode" && value == "auto")
    ed->cell_set_format ("cell-height", tree (""));
  else if (var == "cell-height" && value != "" &&
           ed->cell_get_format ("cell-vmode") == "auto")
    ed->cell_set_format ("cell-vmode", tree ("exact"));
  selection.restore ();
}

void
table_set_format_star (editor_rep* ed, string var, string value) {
  if (ed == nullptr) return;
  ed->table_set_format (var, tree (value));
  if (var == "table-hmode" && value == "auto")
    ed->table_set_format ("table-width", tree (""));
  else if (var == "table-width" && value != "" &&
           ed->table_get_format ("table-hmode") == "auto")
    ed->table_set_format ("table-hmode", tree ("exact"));
  else if (var == "table-vmode" && value == "auto")
    ed->table_set_format ("table-height", tree (""));
  else if (var == "table-height" && value != "" &&
           ed->table_get_format ("table-vmode") == "auto")
    ed->table_set_format ("table-vmode", tree ("exact"));
}

void table_set_automatic_width (editor_rep* ed) {
  set_table_pairs (ed, {{"table-width", ""}, {"table-hmode", "auto"}});
}
void table_set_minimal_width (editor_rep* ed, string value) {
  set_table_pairs (ed, {{"table-width", value}, {"table-hmode", "max"}});
}
void table_set_exact_width (editor_rep* ed, string value) {
  set_table_pairs (ed, {{"table-width", value}, {"table-hmode", "exact"}});
}
void table_set_maximal_width (editor_rep* ed, string value) {
  set_table_pairs (ed, {{"table-width", value}, {"table-hmode", "min"}});
}
void table_set_automatic_height (editor_rep* ed) {
  set_table_pairs (ed, {{"table-height", ""}, {"table-vmode", "auto"}});
}
void table_set_minimal_height (editor_rep* ed, string value) {
  set_table_pairs (ed, {{"table-height", value}, {"table-vmode", "max"}});
}
void table_set_exact_height (editor_rep* ed, string value) {
  set_table_pairs (ed, {{"table-height", value}, {"table-vmode", "exact"}});
}
void table_set_maximal_height (editor_rep* ed, string value) {
  set_table_pairs (ed, {{"table-height", value}, {"table-vmode", "min"}});
}

bool table_test_parwidth (editor_rep* ed) {
  return ed != nullptr && ed->table_get_format ("table-width") == "1par";
}

void table_toggle_parwidth (editor_rep* ed) {
  if (table_test_parwidth (ed))
    set_table_pairs (ed, {{"table-width", ""}, {"table-hmode", ""}});
  else
    set_table_pairs (ed, {{"table-width", "1par"}, {"table-hmode", "exact"}});
}

void table_set_padding (editor_rep* ed, string value) {
  set_table_pairs (ed, {{"table-lsep", value}, {"table-rsep", value},
                        {"table-bsep", value}, {"table-tsep", value}});
}
void table_set_border (editor_rep* ed, string value) {
  set_table_pairs (ed, {{"table-lborder", value}, {"table-rborder", value},
                        {"table-bborder", value}, {"table-tborder", value}});
}
void table_set_halign (editor_rep* ed, string value) {
  table_set_format_star (ed, "table-halign", value);
}
void table_set_valign (editor_rep* ed, string value) {
  table_set_format_star (ed, "table-valign", value);
}

void cell_set_automatic_width (editor_rep* ed) {
  set_cell_pairs (ed, {{"cell-width", ""}, {"cell-hmode", "auto"}});
}
void cell_set_minimal_width (editor_rep* ed, string value) {
  set_cell_pairs (ed, {{"cell-width", value}, {"cell-hmode", "max"}});
}
void cell_set_exact_width (editor_rep* ed, string value) {
  set_cell_pairs (ed, {{"cell-width", value}, {"cell-hmode", "exact"}});
}
void cell_set_maximal_width (editor_rep* ed, string value) {
  set_cell_pairs (ed, {{"cell-width", value}, {"cell-hmode", "min"}});
}
void cell_set_automatic_height (editor_rep* ed) {
  set_cell_pairs (ed, {{"cell-height", ""}, {"cell-vmode", "auto"}});
}
void cell_set_minimal_height (editor_rep* ed, string value) {
  set_cell_pairs (ed, {{"cell-height", value}, {"cell-vmode", "max"}});
}
void cell_set_exact_height (editor_rep* ed, string value) {
  set_cell_pairs (ed, {{"cell-height", value}, {"cell-vmode", "exact"}});
}
void cell_set_maximal_height (editor_rep* ed, string value) {
  set_cell_pairs (ed, {{"cell-height", value}, {"cell-vmode", "min"}});
}
void cell_set_padding (editor_rep* ed, string value) {
  set_cell_pairs (ed, {{"cell-lsep", value}, {"cell-rsep", value},
                       {"cell-bsep", value}, {"cell-tsep", value}});
}
void cell_set_hpadding (editor_rep* ed, string value) {
  set_cell_pairs (ed, {{"cell-lsep", value}, {"cell-rsep", value}});
}
void cell_set_vpadding (editor_rep* ed, string value) {
  set_cell_pairs (ed, {{"cell-bsep", value}, {"cell-tsep", value}});
}
void cell_set_span (editor_rep* ed, string rows, string columns) {
  set_cell_pairs (ed, {{"cell-row-span", rows}, {"cell-col-span", columns}});
}

void cell_set_span_selection (editor_rep* ed) {
  if (ed == nullptr || !ed->selection_active_table ()) return;
  array<int> cells= ed->table_which_cells ();
  if (N(cells) != 4) return;
  ed->table_go_to (cells[0], cells[2]);
  ed->selection_cancel ();
  ed->cell_set_format (
    "cell-row-span", tree (as_string (cells[1] + 1 - cells[0])));
  ed->cell_set_format (
    "cell-col-span", tree (as_string (cells[3] + 1 - cells[2])));
}

void cell_set_halign (editor_rep* ed, string value) {
  cell_set_format_star (ed, "cell-halign", value);
}
void cell_set_valign (editor_rep* ed, string value) {
  cell_set_format_star (ed, "cell-valign", value);
}
void cell_set_background (editor_rep* ed, string value) {
  cell_set_format_star (ed, "cell-background", value);
}
void cell_set_vcorrect (editor_rep* ed, string value) {
  cell_set_format_star (ed, "cell-vcorrect", value);
}
void cell_set_hyphen (editor_rep* ed, string value) {
  cell_set_format_star (ed, "cell-hyphen", value);
}
void cell_set_block (editor_rep* ed, string value) {
  cell_set_format_star (ed, "cell-block", value);
}
void cell_toggle_wrap (editor_rep* ed) {
  if (ed == nullptr) return;
  cell_set_hyphen (ed, ed->cell_get_format ("cell-hyphen") == "n" ? "t" : "n");
}
void cell_halign_left (editor_rep* ed) {
  if (ed == nullptr) return;
  cell_set_halign (ed, ed->cell_get_format ("cell-halign") == "r" ? "c" : "l");
}
void cell_halign_right (editor_rep* ed) {
  if (ed == nullptr) return;
  cell_set_halign (ed, ed->cell_get_format ("cell-halign") == "l" ? "c" : "r");
}
void cell_valign_down (editor_rep* ed) {
  if (ed == nullptr) return;
  string old= ed->cell_get_format ("cell-valign");
  cell_set_valign (ed, old == "c" ? "B" : old == "t" ? "c" : "b");
}
void cell_valign_up (editor_rep* ed) {
  if (ed == nullptr) return;
  string old= ed->cell_get_format ("cell-valign");
  cell_set_valign (ed, old == "b" ? "B" : old == "B" ? "c" : "t");
}

void table_select_cells (editor_rep* ed, int r1, int r2, int c1, int c2) {
  select_cells (ed, r1, r2, c1, c2);
}

void cell_set_border (editor_rep* ed, string value) {
  set_borders (ed, value, 0xffu);
}
void cell_set_hborder (editor_rep* ed, string value) {
  set_borders (ed, value, OUTER_LEFT | OUTER_RIGHT | INNER_LEFT | INNER_RIGHT);
}
void cell_set_vborder (editor_rep* ed, string value) {
  set_borders (ed, value, OUTER_TOP | OUTER_BOTTOM | INNER_TOP | INNER_BOTTOM);
}
void cell_set_lborder (editor_rep* ed, string value) {
  set_borders (ed, value, OUTER_LEFT | INNER_LEFT);
}
void cell_set_rborder (editor_rep* ed, string value) {
  set_borders (ed, value, OUTER_RIGHT | INNER_RIGHT);
}
void cell_set_bborder (editor_rep* ed, string value) {
  set_borders (ed, value, OUTER_BOTTOM | INNER_BOTTOM);
}
void cell_set_tborder (editor_rep* ed, string value) {
  set_borders (ed, value, OUTER_TOP | INNER_TOP);
}

bool
eqnarray_numbered (editor_rep* ed, tree target) {
  if (ed == nullptr || !is_eqnarray_target (target)) return false;
  const int row= ed->table_which_row ();
  path cell= ed->table_search_cell (row, -1);
  tree root= ed->the_root ();
  if (is_nil (cell) || !has_subtree (root, cell)) return false;
  path found;
  return contains_eq_number (subtree (root, cell), cell, found);
}

bool
eqnarray_toggle_numbering (editor_rep* ed, tree target) {
  if (ed == nullptr || !is_eqnarray_target (target)) return false;
  const int row= ed->table_which_row ();
  path cell= ed->table_search_cell (row, -1);
  if (is_nil (cell) || !has_subtree (ed->the_root (), cell)) return false;
  path found;
  tree root= ed->the_root ();
  if (contains_eq_number (subtree (root, cell), cell, found)) {
    ed->cut (found);
    return true;
  }
  ed->go_to (end (root, cell));
  ed->insert_tree (compound ("eq-number"), path (0));
  return true;
}

bool
keyboard_call (string name, const array<object>& args, object& result) {
  editor_rep* ed= current_editor ();
  auto done= [&] { result= object (true); return true; };
  string value;
  bool direction;
  if (name == "table-set-halign" && N(args) == 1 && string_arg (args, 0, value)) {
    table_set_halign (ed, value); return done ();
  }
  if (name == "table-set-valign" && N(args) == 1 && string_arg (args, 0, value)) {
    table_set_valign (ed, value); return done ();
  }
  if (name == "table-set-automatic-width" && N(args) == 0) {
    table_set_automatic_width (ed); return done ();
  }
  if (name == "table-set-exact-width" && N(args) == 1 && string_arg (args, 0, value)) {
    table_set_exact_width (ed, value); return done ();
  }
  if (name == "table-format-center" && N(args) == 0) {
    if (ed) ed->table_format_center (); return done ();
  }
  if (name == "table-deactivate" && N(args) == 0) {
    if (ed) ed->table_deactivate (); return done ();
  }
  if (name == "table-test" && N(args) == 0) {
    if (ed) ed->table_test (); return done ();
  }
  if (name == "make-subtable" && N(args) == 0) {
    if (ed) ed->make_subtable (); return done ();
  }
  if (name == "set-cell-mode" && N(args) == 1 && string_arg (args, 0, value)) {
    if (ed) ed->set_cell_mode (value); return done ();
  }
  if (name == "cell-del-format" && N(args) == 1 && string_arg (args, 0, value)) {
    if (ed) ed->cell_del_format (value); return done ();
  }
  if (name == "cell-set-halign" && N(args) == 1 && string_arg (args, 0, value)) {
    cell_set_halign (ed, value); return done ();
  }
  if (name == "cell-set-valign" && N(args) == 1 && string_arg (args, 0, value)) {
    cell_set_valign (ed, value); return done ();
  }
  if (name == "cell-set-automatic-width" && N(args) == 0) {
    cell_set_automatic_width (ed); return done ();
  }
  if (name == "cell-set-automatic-height" && N(args) == 0) {
    cell_set_automatic_height (ed); return done ();
  }
  if (name == "cell-set-span-selection" && N(args) == 0) {
    cell_set_span_selection (ed); return done ();
  }
  if (name == "table-insert-row" && N(args) == 1 && bool_arg (args, 0, direction)) {
    if (ed) ed->table_insert_row (direction); return done ();
  }
  if (name == "table-remove-row" && N(args) == 1 && bool_arg (args, 0, direction)) {
    if (ed) ed->table_remove_row (direction); return done ();
  }
  if (name == "table-insert-column" && N(args) == 1 && bool_arg (args, 0, direction)) {
    if (ed) ed->table_insert_column (direction); return done ();
  }
  if (name == "table-remove-column" && N(args) == 1 && bool_arg (args, 0, direction)) {
    if (ed) ed->table_remove_column (direction); return done ();
  }
  if (name == "table-column-decoration" && N(args) == 1 && bool_arg (args, 0, direction)) {
    if (ed) ed->table_column_decoration (direction); return done ();
  }
  if (name == "table-row-decoration" && N(args) == 1 && bool_arg (args, 0, direction)) {
    if (ed) ed->table_row_decoration (direction); return done ();
  }
  if (name == "numbered-toggle" && N(args) == 1 && is_tree (args[0])) {
    if (eqnarray_toggle_numbering (ed, as_tree (args[0]))) return done ();
    return false;
  }
  using unary= void (*) (editor_rep*, string);
  const struct { const char* name; unary fn; } setters[]= {
    {"cell-set-border", cell_set_border}, {"cell-set-hborder", cell_set_hborder},
    {"cell-set-vborder", cell_set_vborder}, {"cell-set-lborder", cell_set_lborder},
    {"cell-set-rborder", cell_set_rborder}, {"cell-set-bborder", cell_set_bborder},
    {"cell-set-tborder", cell_set_tborder}
  };
  for (const auto& setter: setters)
    if (name == setter.name && N(args) == 1 && string_arg (args, 0, value)) {
      setter.fn (ed, value); return done ();
    }
  return false;
}

} // namespace athena::table_commands

#define ATHENA_TABLE_CURRENT() athena::table_commands::current_editor ()
void native_cell_set_format_star (string a, string b) { athena::table_commands::cell_set_format_star (ATHENA_TABLE_CURRENT(), a, b); }
void native_table_set_format_star (string a, string b) { athena::table_commands::table_set_format_star (ATHENA_TABLE_CURRENT(), a, b); }
void native_table_set_automatic_width () { athena::table_commands::table_set_automatic_width (ATHENA_TABLE_CURRENT()); }
void native_table_set_minimal_width (string v) { athena::table_commands::table_set_minimal_width (ATHENA_TABLE_CURRENT(), v); }
void native_table_set_exact_width (string v) { athena::table_commands::table_set_exact_width (ATHENA_TABLE_CURRENT(), v); }
void native_table_set_maximal_width (string v) { athena::table_commands::table_set_maximal_width (ATHENA_TABLE_CURRENT(), v); }
void native_table_set_automatic_height () { athena::table_commands::table_set_automatic_height (ATHENA_TABLE_CURRENT()); }
void native_table_set_minimal_height (string v) { athena::table_commands::table_set_minimal_height (ATHENA_TABLE_CURRENT(), v); }
void native_table_set_exact_height (string v) { athena::table_commands::table_set_exact_height (ATHENA_TABLE_CURRENT(), v); }
void native_table_set_maximal_height (string v) { athena::table_commands::table_set_maximal_height (ATHENA_TABLE_CURRENT(), v); }
bool native_table_test_parwidth () { return athena::table_commands::table_test_parwidth (ATHENA_TABLE_CURRENT()); }
void native_table_toggle_parwidth () { athena::table_commands::table_toggle_parwidth (ATHENA_TABLE_CURRENT()); }
void native_table_set_padding (string v) { athena::table_commands::table_set_padding (ATHENA_TABLE_CURRENT(), v); }
void native_table_set_border (string v) { athena::table_commands::table_set_border (ATHENA_TABLE_CURRENT(), v); }
void native_table_set_halign (string v) { athena::table_commands::table_set_halign (ATHENA_TABLE_CURRENT(), v); }
void native_table_set_valign (string v) { athena::table_commands::table_set_valign (ATHENA_TABLE_CURRENT(), v); }
void native_cell_set_automatic_width () { athena::table_commands::cell_set_automatic_width (ATHENA_TABLE_CURRENT()); }
void native_cell_set_minimal_width (string v) { athena::table_commands::cell_set_minimal_width (ATHENA_TABLE_CURRENT(), v); }
void native_cell_set_exact_width (string v) { athena::table_commands::cell_set_exact_width (ATHENA_TABLE_CURRENT(), v); }
void native_cell_set_maximal_width (string v) { athena::table_commands::cell_set_maximal_width (ATHENA_TABLE_CURRENT(), v); }
void native_cell_set_automatic_height () { athena::table_commands::cell_set_automatic_height (ATHENA_TABLE_CURRENT()); }
void native_cell_set_minimal_height (string v) { athena::table_commands::cell_set_minimal_height (ATHENA_TABLE_CURRENT(), v); }
void native_cell_set_exact_height (string v) { athena::table_commands::cell_set_exact_height (ATHENA_TABLE_CURRENT(), v); }
void native_cell_set_maximal_height (string v) { athena::table_commands::cell_set_maximal_height (ATHENA_TABLE_CURRENT(), v); }
void native_cell_set_padding (string v) { athena::table_commands::cell_set_padding (ATHENA_TABLE_CURRENT(), v); }
void native_cell_set_hpadding (string v) { athena::table_commands::cell_set_hpadding (ATHENA_TABLE_CURRENT(), v); }
void native_cell_set_vpadding (string v) { athena::table_commands::cell_set_vpadding (ATHENA_TABLE_CURRENT(), v); }
void native_cell_set_span (string r, string c) { athena::table_commands::cell_set_span (ATHENA_TABLE_CURRENT(), r, c); }
void native_cell_set_span_selection () { athena::table_commands::cell_set_span_selection (ATHENA_TABLE_CURRENT()); }
void native_cell_set_halign (string v) { athena::table_commands::cell_set_halign (ATHENA_TABLE_CURRENT(), v); }
void native_cell_set_valign (string v) { athena::table_commands::cell_set_valign (ATHENA_TABLE_CURRENT(), v); }
void native_cell_set_background (string v) { athena::table_commands::cell_set_background (ATHENA_TABLE_CURRENT(), v); }
void native_cell_set_vcorrect (string v) { athena::table_commands::cell_set_vcorrect (ATHENA_TABLE_CURRENT(), v); }
void native_cell_set_hyphen (string v) { athena::table_commands::cell_set_hyphen (ATHENA_TABLE_CURRENT(), v); }
void native_cell_set_block (string v) { athena::table_commands::cell_set_block (ATHENA_TABLE_CURRENT(), v); }
void native_cell_toggle_wrap () { athena::table_commands::cell_toggle_wrap (ATHENA_TABLE_CURRENT()); }
void native_cell_halign_left () { athena::table_commands::cell_halign_left (ATHENA_TABLE_CURRENT()); }
void native_cell_halign_right () { athena::table_commands::cell_halign_right (ATHENA_TABLE_CURRENT()); }
void native_cell_valign_down () { athena::table_commands::cell_valign_down (ATHENA_TABLE_CURRENT()); }
void native_cell_valign_up () { athena::table_commands::cell_valign_up (ATHENA_TABLE_CURRENT()); }
void native_table_select_cells (int a, int b, int c, int d) { athena::table_commands::table_select_cells (ATHENA_TABLE_CURRENT(), a, b, c, d); }
void native_cell_set_border (string v) { athena::table_commands::cell_set_border (ATHENA_TABLE_CURRENT(), v); }
void native_cell_set_hborder (string v) { athena::table_commands::cell_set_hborder (ATHENA_TABLE_CURRENT(), v); }
void native_cell_set_vborder (string v) { athena::table_commands::cell_set_vborder (ATHENA_TABLE_CURRENT(), v); }
void native_cell_set_lborder (string v) { athena::table_commands::cell_set_lborder (ATHENA_TABLE_CURRENT(), v); }
void native_cell_set_rborder (string v) { athena::table_commands::cell_set_rborder (ATHENA_TABLE_CURRENT(), v); }
void native_cell_set_bborder (string v) { athena::table_commands::cell_set_bborder (ATHENA_TABLE_CURRENT(), v); }
void native_cell_set_tborder (string v) { athena::table_commands::cell_set_tborder (ATHENA_TABLE_CURRENT(), v); }
#define ATHENA_TABLE_PROP_WRAPPER(fn, prop) \
  void fn (string v) { athena::table_commands::table_set_format_star (ATHENA_TABLE_CURRENT(), prop, v); }
#define ATHENA_CELL_PROP_WRAPPER(fn, prop) \
  void fn (string v) { athena::table_commands::cell_set_format_star (ATHENA_TABLE_CURRENT(), prop, v); }
ATHENA_TABLE_PROP_WRAPPER (native_table_set_height_value, "table-height")
ATHENA_TABLE_PROP_WRAPPER (native_table_set_lsep_value, "table-lsep")
ATHENA_TABLE_PROP_WRAPPER (native_table_set_rsep_value, "table-rsep")
ATHENA_TABLE_PROP_WRAPPER (native_table_set_bsep_value, "table-bsep")
ATHENA_TABLE_PROP_WRAPPER (native_table_set_tsep_value, "table-tsep")
ATHENA_TABLE_PROP_WRAPPER (native_table_set_lborder_value, "table-lborder")
ATHENA_TABLE_PROP_WRAPPER (native_table_set_rborder_value, "table-rborder")
ATHENA_TABLE_PROP_WRAPPER (native_table_set_bborder_value, "table-bborder")
ATHENA_TABLE_PROP_WRAPPER (native_table_set_tborder_value, "table-tborder")
ATHENA_TABLE_PROP_WRAPPER (native_table_set_row_origin_value, "table-row-origin")
ATHENA_TABLE_PROP_WRAPPER (native_table_set_col_origin_value, "table-col-origin")
ATHENA_TABLE_PROP_WRAPPER (native_table_set_min_rows_value, "table-min-rows")
ATHENA_TABLE_PROP_WRAPPER (native_table_set_min_cols_value, "table-min-cols")
ATHENA_TABLE_PROP_WRAPPER (native_table_set_max_rows_value, "table-max-rows")
ATHENA_TABLE_PROP_WRAPPER (native_table_set_max_cols_value, "table-max-cols")
ATHENA_CELL_PROP_WRAPPER (native_cell_set_hpart_value, "cell-hpart")
ATHENA_CELL_PROP_WRAPPER (native_cell_set_vpart_value, "cell-vpart")
ATHENA_CELL_PROP_WRAPPER (native_cell_set_lsep_value, "cell-lsep")
ATHENA_CELL_PROP_WRAPPER (native_cell_set_rsep_value, "cell-rsep")
ATHENA_CELL_PROP_WRAPPER (native_cell_set_bsep_value, "cell-bsep")
ATHENA_CELL_PROP_WRAPPER (native_cell_set_tsep_value, "cell-tsep")
#undef ATHENA_TABLE_PROP_WRAPPER
#undef ATHENA_CELL_PROP_WRAPPER
#undef ATHENA_TABLE_CURRENT

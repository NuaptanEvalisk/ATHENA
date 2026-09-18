/******************************************************************************
* MODULE     : totex_tables.cpp
* DESCRIPTION: Native TeXmacs table to LaTeX conversion
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "totex_internal.hpp"
#include "analyze.hpp"

namespace {

using namespace latex_export_internal;

bool
stree_true (scheme_tree value) {
  return is_atomic (value) && value->label == "#t";
}

scheme_tree
table_make_native (scheme_tree top_borders, scheme_tree bottom_borders,
                   scheme_tree rows) {
  scheme_tree out= stree_apply ("!table");
  if (!stree_list (rows)) return out;
  int nr= N(rows);
  for (int i=0; i<nr; ++i) {
    bool top= stree_list (top_borders) && i < N(top_borders) &&
              stree_true (top_borders[i]);
    bool previous_bottom= false;
    if (i == 0) previous_bottom= top;
    else previous_bottom= stree_list (bottom_borders) && i-1 < N(bottom_borders) &&
                          stree_true (bottom_borders[i-1]);
    if (top || previous_bottom) out << stree_apply ("hline");

    scheme_tree row= stree_apply ("!row");
    if (stree_list (rows[i]))
      for (int j=0; j<N(rows[i]); ++j) row << tmtex_convert (rows[i][j]);
    out << row;
  }
  if (nr > 0 && stree_list (bottom_borders) && nr-1 < N(bottom_borders) &&
      stree_true (bottom_borders[nr-1])) out << stree_apply ("hline");
  return out;
}

string
table_args_native (scheme_tree left_borders, scheme_tree right_borders,
                   scheme_tree halign) {
  if (!stree_list (halign)) return "";
  string out;
  for (int i=0; i<N(halign); ++i) {
    bool left= stree_list (left_borders) && i < N(left_borders) &&
               stree_true (left_borders[i]);
    bool previous_right= false;
    if (i == 0) previous_right= left;
    else previous_right= stree_list (right_borders) && i-1 < N(right_borders) &&
                         stree_true (right_borders[i-1]);
    if (left || previous_right) out << "|";
    out << atom_text (halign[i]);
  }
  if (N(halign) > 0 && stree_list (right_borders) &&
      N(halign)-1 < N(right_borders) && stree_true (right_borders[N(halign)-1]))
    out << "|";
  return out;
}

scheme_tree
table_cwith (string i1, string i2, string j1, string j2,
             string name, string value) {
  scheme_tree out= stree_apply ("cwith");
  out << stree_string (i1) << stree_string (i2)
      << stree_string (j1) << stree_string (j2)
      << stree_string (name) << stree_string (value);
  return out;
}

scheme_tree
table_defaults_native (string halign, bool borders) {
  scheme_tree out (TUPLE);
  if (halign == "rcl") {
    out << table_cwith ("1", "-1", "2", "-2", "cell-halign", "c")
        << table_cwith ("1", "-1", "1", "1", "cell-halign", "r")
        << table_cwith ("1", "-1", "-1", "-1", "cell-halign", "l");
  }
  else out << table_cwith ("1", "-1", "1", "-1", "cell-halign", halign);
  if (borders) {
    out << table_cwith ("1", "-1", "1", "1", "cell-lborder", "1ln")
        << table_cwith ("1", "1", "1", "-1", "cell-tborder", "1ln")
        << table_cwith ("1", "-1", "1", "-1", "cell-bborder", "1ln")
        << table_cwith ("1", "-1", "1", "-1", "cell-rborder", "1ln");
  }
  return out;
}

array<bool>
table_block_columns_native (scheme_tree t) {
  if (head_is (t, "tformat") && N(t) > 1)
    return table_block_columns_native (t[N(t)-1]);
  if (head_is (t, "table") && N(t) == 2)
    return table_block_columns_native (t[1]);
  if (head_is (t, "table") && N(t) > 2) {
    scheme_tree first= stree_apply ("table");
    first << t[1];
    scheme_tree rest= stree_apply ("table");
    for (int i=2; i<N(t); ++i) rest << t[i];
    array<bool> a= table_block_columns_native (first);
    array<bool> b= table_block_columns_native (rest);
    array<bool> out;
    int n= min (N(a), N(b));
    for (int i=0; i<n; ++i) out << (a[i] || b[i]);
    return out;
  }
  if (head_is (t, "row")) {
    array<bool> out;
    for (int i=1; i<N(t); ++i) {
      array<bool> cell= table_block_columns_native (t[i]);
      out << (N(cell) > 0 && cell[0]);
    }
    return out;
  }
  if (head_is (t, "cell") && N(t) > 1)
    return table_block_columns_native (t[N(t)-1]);
  array<bool> out;
  out << head_is (t, "document");
  return out;
}

scheme_tree
table_block_adjust_native (scheme_tree t) {
  if (head_is (t, "tformat") && N(t) > 1) {
    scheme_tree out= stree_apply ("tformat");
    for (int i=1; i<N(t)-1; ++i) out << t[i];
    out << table_block_adjust_native (t[N(t)-1]);
    return out;
  }
  if (!head_is (t, "table")) return t;
  array<bool> blocks= table_block_columns_native (t);
  int count= 0;
  for (int i=0; i<N(blocks); ++i) if (blocks[i]) ++count;
  if (count == 0) return t;
  scheme_tree out= stree_apply ("tformat");
  string width_number= as_string (12.0 / (double) count);
  if (!contains_text (width_number, ".") &&
      !contains_text (width_number, "e") &&
      !contains_text (width_number, "E"))
    width_number << ".0";
  string width= width_number * "cm";
  for (int i=0; i<N(blocks); ++i)
    if (blocks[i]) {
      string col= as_string (i + 1);
      out << table_cwith ("1", "-1", col, col, "cell-halign", "p{" * width * "}");
    }
  out << t;
  return out;
}

scheme_tree
table_replace_big_floats_native (scheme_tree t) {
  if (func_is (t, "big-figure", 2) || func_is (t, "big-table", 2)) {
    scheme_tree out= stree_apply ("tmfloat");
    out << stree_string ("h") << stree_string ("big")
        << stree_string (head_is (t, "big-figure") ? "figure" : "table")
        << t[1] << t[2];
    return out;
  }
  if (!stree_list (t)) return t;
  scheme_tree out (TUPLE);
  for (int i=0; i<N(t); ++i) out << table_replace_big_floats_native (t[i]);
  return out;
}

scheme_tree
table_adjust_native (scheme_tree t) {
  return table_replace_big_floats_native (table_block_adjust_native (t));
}

scheme_tree table_snapshot_native (scheme_tree x);

bool
table_props_native (string key, scheme_tree& before, string& halign,
                    scheme_tree& after, bool& borders) {
  before= stree_string ("");
  after = stree_string ("");
  borders= false;
  if (key == "block")       { halign= "l"; borders= true; return true; }
  if (key == "block*")      { halign= "c"; borders= true; return true; }
  if (key == "wide-block")  {
    before= stree_string ("{\\noindent}"); halign= "@{}X@{}";
    borders= true; return true;
  }
  if (key == "tabular")     { halign= "l"; return true; }
  if (key == "tabular*")    { halign= "c"; return true; }
  if (key == "wide-tabular") {
    before= stree_string ("{\\noindent}"); halign= "@{}X@{}"; return true;
  }
  if (key == "matrix") {
    before= stree_apply ("left("); halign= "c"; after= stree_apply ("right)");
    return true;
  }
  if (key == "det") {
    before= stree_apply ("left|"); halign= "c"; after= stree_apply ("right|");
    return true;
  }
  if (key == "bmatrix") {
    before= stree_apply ("left["); halign= "c"; after= stree_apply ("right]");
    return true;
  }
  if (key == "stack") { halign= "c"; return true; }
  if (key == "choice") {
    before= stree_apply ("left\\{"); halign= "l"; after= stree_apply ("right.");
    return true;
  }
  if (key == "tabbed" || key == "tabbed*") { halign= "l"; return true; }
  if (key == "rcl-table") {
    if (!latex_export_mathjax ()) {
      before= stree_string ("{\\setlength\\arraylinesep{0.4em}\\everymath={\\displaystyle}");
      after= stree_string ("}");
    }
    halign= "rcl";
    return true;
  }
  return false;
}

scheme_tree
table_apply_native (string key, scheme_tree args, scheme_tree x) {
  scheme_tree before, after;
  string halign;
  bool borders= false;
  bool found= table_props_native (key, before, halign, after, borders);
  bool wide= found && contains_text (halign, "X");

  if (key == "rcl-table") latex_export_add_latex_extra_package ("tabls");
  if (!math_mode () && !wide) x= table_adjust_native (x);

  if (!found) {
    scheme_tree begin= stree_apply ("!begin");
    begin << stree_string (key);
    if (stree_list (args))
      for (int i=0; i<N(args); ++i) begin << args[i];
    scheme_tree snapshot= table_snapshot_native (x);
    scheme_tree env (TUPLE);
    env << begin << table_make_native (snapshot[0], snapshot[1], snapshot[5]);
    return env;
  }

  scheme_tree defaults= table_defaults_native (halign, borders);
  scheme_tree formatted= stree_apply ("tformat");
  for (int i=0; i<N(defaults); ++i) formatted << defaults[i];
  formatted << x;
  scheme_tree snapshot= table_snapshot_native (formatted);
  string table_args= table_args_native (snapshot[2], snapshot[3], snapshot[4]);

  scheme_tree begin= stree_apply ("!begin");
  if (wide) {
    begin << stree_string ("tabularx") << stree_string ("1.0\\textwidth");
  }
  else begin << stree_string (math_mode () ? "array" : "tabular");
  begin << stree_string (table_args);

  scheme_tree env (TUPLE);
  env << begin << table_make_native (snapshot[0], snapshot[1], snapshot[5]);
  scheme_tree pieces (TUPLE);
  pieces << before << env << after;
  return latex_tex_concat (pieces);
}

scheme_tree
table_formats_native (scheme_tree x) {
  scheme_tree out (TUPLE);
  while (head_is (x, "tformat") && N(x) > 1) {
    for (int i=1; i<N(x)-1; ++i)
      if (func_is (x[i], "cwith", 6)) out << x[i];
    x= x[N(x)-1];
  }
  return out;
}

scheme_tree
table_cells_native (scheme_tree x) {
  while (!head_is (x, "table") && !head_is (x, "!arg") &&
         stree_list (x) && N(x) > 0)
    x= x[N(x)-1];

  scheme_tree rows (TUPLE);
  if (!head_is (x, "table")) {
    scheme_tree row (TUPLE);
    row << stree_string ("");
    rows << row;
    return rows;
  }
  for (int i=1; i<N(x); ++i) {
    scheme_tree row (TUPLE);
    scheme_tree source= x[i];
    if (head_is (source, "row")) {
      for (int j=1; j<N(source); ++j) {
        scheme_tree cell= source[j];
        row << (head_is (cell, "cell") && N(cell) > 1 ? cell[1] : cell);
      }
    }
    rows << row;
  }
  return rows;
}

int
table_column_count (scheme_tree rows) {
  int result= 0;
  if (!stree_list (rows)) return result;
  for (int i=0; i<N(rows); ++i)
    if (stree_list (rows[i])) result= max (result, N(rows[i]));
  return result;
}

int
table_decode_index (scheme_tree value, int count) {
  string s= string_atom (value) ? atom_text (value) : string ("0");
  int i= is_int (s) ? as_int (s) : 0;
  if (i < 0) return i + count;
  if (i > 0) return i - 1;
  return 0;
}

bool
table_format_on_row (scheme_tree f, int row, int nr, int nc, string name) {
  if (!func_is (f, "cwith", 6) || !string_atom (f[5]) || atom_text (f[5]) != name)
    return false;
  string j1= string_atom (f[3]) ? atom_text (f[3]) : "0";
  string j2= string_atom (f[4]) ? atom_text (f[4]) : "0";
  if (j1 != "1" || (j2 != "-1" && (!is_int (j2) || as_int (j2) != nc)))
    return false;
  int i1= table_decode_index (f[1], nr);
  int i2= table_decode_index (f[2], nr);
  return row >= i1 && row <= i2;
}

bool
table_format_on_column (scheme_tree f, int col, int nr, int nc, string name) {
  if (!func_is (f, "cwith", 6) || !string_atom (f[5]) || atom_text (f[5]) != name)
    return false;
  string i1= string_atom (f[1]) ? atom_text (f[1]) : "0";
  string i2= string_atom (f[2]) ? atom_text (f[2]) : "0";
  if (i1 != "1" || (i2 != "-1" && (!is_int (i2) || as_int (i2) != nr)))
    return false;
  int j1= table_decode_index (f[3], nc);
  int j2= table_decode_index (f[4], nc);
  return col >= j1 && col <= j2;
}

string
table_format_value (scheme_tree formats, bool row_axis, int index,
                    int nr, int nc, string name, string fallback) {
  if (!stree_list (formats)) return fallback;
  for (int i=N(formats)-1; i>=0; --i) {
    scheme_tree f= formats[i];
    bool applies= row_axis ? table_format_on_row (f, index, nr, nc, name)
                           : table_format_on_column (f, index, nr, nc, name);
    if (applies && N(f) > 6 && string_atom (f[6])) return atom_text (f[6]);
  }
  return fallback;
}

bool
table_length_nonzero (string value) {
  double length= 0.0;
  string unit;
  parse_length (value, length, unit);
  return length != 0.0;
}

scheme_tree
table_snapshot_native (scheme_tree x) {
  scheme_tree formats= table_formats_native (x);
  scheme_tree rows= table_cells_native (x);
  int nr= N(rows), nc= table_column_count (rows);
  scheme_tree top (TUPLE), bottom (TUPLE), left (TUPLE), right (TUPLE), align (TUPLE);
  for (int i=0; i<nr; ++i) {
    top << tree (table_length_nonzero (
      table_format_value (formats, true, i, nr, nc, "cell-tborder", "0")) ? "#t" : "#f");
    bottom << tree (table_length_nonzero (
      table_format_value (formats, true, i, nr, nc, "cell-bborder", "0")) ? "#t" : "#f");
  }
  for (int j=0; j<nc; ++j) {
    left << tree (table_length_nonzero (
      table_format_value (formats, false, j, nr, nc, "cell-lborder", "0")) ? "#t" : "#f");
    right << tree (table_length_nonzero (
      table_format_value (formats, false, j, nr, nc, "cell-rborder", "0")) ? "#t" : "#f");
    align << stree_string (table_format_value (
      formats, false, j, nr, nc, "cell-halign", "l"));
  }
  scheme_tree out (TUPLE);
  out << top << bottom << left << right << align << rows;
  return out;
}


} // namespace

scheme_tree
latex_export_table_apply (string key, scheme_tree args, scheme_tree table) {
  return table_apply_native (key, args, table);
}

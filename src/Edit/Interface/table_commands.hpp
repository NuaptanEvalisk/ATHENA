/******************************************************************************
* MODULE     : table_commands.hpp
* DESCRIPTION: Native high-level table editing commands
* COPYRIGHT  : (C) 2001 Joris van der Hoeven, 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* See the file LICENSE in the root directory.
******************************************************************************/
#pragma once

#include "editor.hpp"
#include "scheme.hpp"

namespace athena::table_commands {

editor_rep* current_editor ();
void insert_small_table (editor_rep* ed);
void insert_big_table (editor_rep* ed);

void cell_set_format_star (editor_rep* ed, string var, string value);
void table_set_format_star (editor_rep* ed, string var, string value);

void table_set_automatic_width (editor_rep* ed);
void table_set_minimal_width (editor_rep* ed, string value);
void table_set_exact_width (editor_rep* ed, string value);
void table_set_maximal_width (editor_rep* ed, string value);
void table_set_automatic_height (editor_rep* ed);
void table_set_minimal_height (editor_rep* ed, string value);
void table_set_exact_height (editor_rep* ed, string value);
void table_set_maximal_height (editor_rep* ed, string value);
bool table_test_parwidth (editor_rep* ed);
void table_toggle_parwidth (editor_rep* ed);
void table_set_padding (editor_rep* ed, string value);
void table_set_border (editor_rep* ed, string value);
void table_set_halign (editor_rep* ed, string value);
void table_set_valign (editor_rep* ed, string value);

void cell_set_automatic_width (editor_rep* ed);
void cell_set_minimal_width (editor_rep* ed, string value);
void cell_set_exact_width (editor_rep* ed, string value);
void cell_set_maximal_width (editor_rep* ed, string value);
void cell_set_automatic_height (editor_rep* ed);
void cell_set_minimal_height (editor_rep* ed, string value);
void cell_set_exact_height (editor_rep* ed, string value);
void cell_set_maximal_height (editor_rep* ed, string value);
void cell_set_padding (editor_rep* ed, string value);
void cell_set_hpadding (editor_rep* ed, string value);
void cell_set_vpadding (editor_rep* ed, string value);
void cell_set_span (editor_rep* ed, string rows, string columns);
void cell_set_span_selection (editor_rep* ed);
void cell_set_halign (editor_rep* ed, string value);
void cell_set_valign (editor_rep* ed, string value);
void cell_set_background (editor_rep* ed, string value);
void cell_set_vcorrect (editor_rep* ed, string value);
void cell_set_hyphen (editor_rep* ed, string value);
void cell_set_block (editor_rep* ed, string value);
void cell_toggle_wrap (editor_rep* ed);
void cell_halign_left (editor_rep* ed);
void cell_halign_right (editor_rep* ed);
void cell_valign_down (editor_rep* ed);
void cell_valign_up (editor_rep* ed);

void table_select_cells (editor_rep* ed, int row1, int row2,
                         int col1, int col2);
void cell_set_border (editor_rep* ed, string value);
void cell_set_hborder (editor_rep* ed, string value);
void cell_set_vborder (editor_rep* ed, string value);
void cell_set_lborder (editor_rep* ed, string value);
void cell_set_rborder (editor_rep* ed, string value);
void cell_set_bborder (editor_rep* ed, string value);
void cell_set_tborder (editor_rep* ed, string value);

bool eqnarray_numbered (editor_rep* ed, tree target);
bool eqnarray_toggle_numbering (editor_rep* ed, tree target);

// Direct JSON keymap dispatch. Fixed-value table editing actions do not touch
// Scheme; only generic interactive prompting remains outside this feature.
bool keyboard_call (string name, const array<object>& args, object& result);

} // namespace athena::table_commands

// Native glue entry points used by generic interactive UI and remaining
// Scheme callers. These mutate the current editor directly.
void native_cell_set_format_star (string var, string value);
void native_table_set_format_star (string var, string value);
void native_table_set_automatic_width ();
void native_table_set_minimal_width (string value);
void native_table_set_exact_width (string value);
void native_table_set_maximal_width (string value);
void native_table_set_automatic_height ();
void native_table_set_minimal_height (string value);
void native_table_set_exact_height (string value);
void native_table_set_maximal_height (string value);
bool native_table_test_parwidth ();
void native_table_toggle_parwidth ();
void native_table_set_padding (string value);
void native_table_set_border (string value);
void native_table_set_halign (string value);
void native_table_set_valign (string value);
void native_cell_set_automatic_width ();
void native_cell_set_minimal_width (string value);
void native_cell_set_exact_width (string value);
void native_cell_set_maximal_width (string value);
void native_cell_set_automatic_height ();
void native_cell_set_minimal_height (string value);
void native_cell_set_exact_height (string value);
void native_cell_set_maximal_height (string value);
void native_cell_set_padding (string value);
void native_cell_set_hpadding (string value);
void native_cell_set_vpadding (string value);
void native_cell_set_span (string rows, string columns);
void native_cell_set_span_selection ();
void native_cell_set_halign (string value);
void native_cell_set_valign (string value);
void native_cell_set_background (string value);
void native_cell_set_vcorrect (string value);
void native_cell_set_hyphen (string value);
void native_cell_set_block (string value);
void native_cell_toggle_wrap ();
void native_cell_halign_left ();
void native_cell_halign_right ();
void native_cell_valign_down ();
void native_cell_valign_up ();
void native_table_select_cells (int row1, int row2, int col1, int col2);
void native_cell_set_border (string value);
void native_cell_set_hborder (string value);
void native_cell_set_vborder (string value);
void native_cell_set_lborder (string value);
void native_cell_set_rborder (string value);
void native_cell_set_bborder (string value);
void native_cell_set_tborder (string value);
void native_table_set_height_value (string value);
void native_table_set_lsep_value (string value);
void native_table_set_rsep_value (string value);
void native_table_set_bsep_value (string value);
void native_table_set_tsep_value (string value);
void native_table_set_lborder_value (string value);
void native_table_set_rborder_value (string value);
void native_table_set_bborder_value (string value);
void native_table_set_tborder_value (string value);
void native_table_set_row_origin_value (string value);
void native_table_set_col_origin_value (string value);
void native_table_set_min_rows_value (string value);
void native_table_set_min_cols_value (string value);
void native_table_set_max_rows_value (string value);
void native_table_set_max_cols_value (string value);
void native_cell_set_hpart_value (string value);
void native_cell_set_vpart_value (string value);
void native_cell_set_lsep_value (string value);
void native_cell_set_rsep_value (string value);
void native_cell_set_bsep_value (string value);
void native_cell_set_tsep_value (string value);

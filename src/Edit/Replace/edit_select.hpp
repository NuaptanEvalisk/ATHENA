
/******************************************************************************
* MODULE     : select.hpp
* DESCRIPTION: for selection handling in TeXmacs
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef EDIT_SELECT_H
#define EDIT_SELECT_H
#include "editor.hpp"

#define DIRECT 0
#define CENTER 1
#define HORIZONTAL 2
#define VERTICAL 3

class edit_select_rep: virtual public editor_rep {
protected:
  range_set cur_sel;
  bool      selecting, shift_selecting;
  path      mid_p;
  string    selection_import;
  string    selection_export;
  path      focus_p;
  bool      focus_hold;
  hashmap<string,range_set> alt_sels;
  std::shared_ptr<const evaluation_environment_checkpoint> selection_environment;

protected:
  void with_selection_context (const std::function<void ()>& action,
    std::shared_ptr<const evaluation_environment_checkpoint> context= {});
  bool selection_in_source ();
  bool with_selection_navigation (const std::function<void (const cursor_navigation&)>&);
  bool selection_try_correct (path, path, path&, path&);
  void get_selection (path& start, path& end);
  void set_selection (path start, path end);
  void raw_cut (path start, path end);
  void selection_wrap (tree wrapper, path position);
  std::shared_ptr<native_paste_plan> prepare_paste (
    tree selection, std::shared_ptr<const evaluation_environment_checkpoint> context,
    bool graphics);
  void apply_paste (const native_paste_plan& prepared, bool local_transfer);

public:
  edit_select_rep ();
  ~edit_select_rep ();

  path formula_root (path p);

  void select (path p);
  void select (path start, path end);
  void select_all ();
  void select_line ();
  void select_from_cursor ();
  void select_from_cursor_if_active ();
  void select_from_keyboard (bool flag);
  void select_from_shift_keyboard ();
  void select_enlarge_text ();
  void select_enlarge ();
  void select_enlarge_environmental ();

  bool selection_active_any ();
  bool selection_active_normal ();
  bool selection_active_table (bool strict= true);
  bool selection_active_small ();
  bool selection_active_enlarging ();

  void selection_raw_set (string key, tree t);
  tree selection_raw_get (string key);
  void selection_correct (path i1, path i2, path& o1, path& o2);
  void selection_correct (path i1, path i2, path& o1, path& o2, const cursor_navigation&);
  path selection_get_subtable (int& row1, int& col1, int& row2, int& col2);
  selection compute_selection (path p1, path p2, const cursor_navigation&);
  selection compute_selection (range_set sel);
  void selection_get (selection& sel);
  bool selection_try_get (path& start, path& end);
  void selection_get (path& start, path& end);
  path selection_get_start ();
  path selection_get_end ();
  path selection_var_get_start ();
  path selection_var_get_end ();
  path selection_get_path ();
  path selection_get_cursor_path ();
  tree selection_get_env_value (string var);
  tree selection_get (string key);
  void selection_set (string key, tree t, bool persistant= false,
                      string move_token= "");
  void selection_set (tree t);
  void selection_set_start (path p= path());
  void selection_set_end (path p= path());
  void selection_set_paths (path start, path end);
  void selection_set_range_set (range_set sel);
  void selection_copy (string key= "primary");
  void selection_validate_paste (tree selection);
  std::shared_ptr<const native_paste_plan> selection_prepare_paste (tree selection);
  void selection_apply_paste (const native_paste_plan& prepared);
  void selection_paste (string key= "primary");
  void selection_clear (string key= "primary");
  void selection_cancel ();
  void selection_set_import (string fm);
  void selection_set_export (string fm);
  string selection_get_import ();
  string selection_get_export ();

  tree selection_get ();
  void selection_cut (string key= "primary");
  tree selection_get_cut ();
  void selection_move ();
  void cut (path p);
  void cut (path start, path end);
  path manual_focus_get ();
  void manual_focus_set (path p, bool force);
  void manual_focus_release ();
  bool focus_try_search (path p, bool skip_flag, bool up_flag, path& result);
  path focus_get (bool skip_flag);
  bool focus_try_get (path& result, bool skip_flag= true);

  void set_alt_selection (string s, range_set sel);
  range_set get_alt_selection (string s);
  void cancel_alt_selection (string s);
  void cancel_alt_selections ();
};

#endif // defined EDIT_SELECT_H

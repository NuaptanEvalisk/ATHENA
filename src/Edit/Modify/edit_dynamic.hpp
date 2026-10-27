
/******************************************************************************
* MODULE     : edit_dynamic.hpp
* DESCRIPTION: Main routines for the modification of dynamic content
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef EDIT_DYNAMIC_H
#define EDIT_DYNAMIC_H
#include "editor.hpp"
#include <vector>
#include <optional>

class edit_dynamic_rep: virtual public editor_rep {
  double hybrid_mark= 0;
  path hybrid_selection_start, hybrid_selection_end;
  tree hybrid_owner;
  struct hybrid_draft { tree input, selection; };
  std::vector<hybrid_draft> hybrid_drafts;
  std::optional<bool> source_mode_during_edit;
  std::shared_ptr<const evaluation_environment_checkpoint> macro_environment;
  std::shared_ptr<const evaluation_environment_checkpoint> macro_invocation_context ();
  void go_to_argument (path p, bool start_flag,
    const std::shared_ptr<const evaluation_environment_checkpoint>& prepared);
  bool is_multi_paragraph_macro (tree t, bool pure);
  void finish_hybrid (bool accepted= false);
  bool activate_latex (bool report_error, const std::optional<command>& resolved);
  void activate_hybrid (bool with_args_hint, const std::optional<command>& resolved);
public:
  edit_dynamic_rep ();
  ~edit_dynamic_rep ();

  bool in_source ();
  path find_dynamic (path p);

  void make_compound (tree_label l, int n);
  void activate ();

  void go_to_argument (path p, bool start_flag);
  void insert_argument (path p, bool forward);
  void insert_argument (bool forward);
  void remove_empty_argument (path p, bool forward);
  void remove_argument (path p, bool forward);
  void remove_argument (bool forward);

  void back_monolithic (path p);
  void back_general (path p, bool forward);
  void back_in_general (tree t, path p, bool forward);

  void make_with (string var, string val);
  void insert_with (path p, string var, tree val);
  void remove_with (path p, string var);
  void back_in_with (tree t, path p, bool forward);

  void make_mod_active (tree_label l);
  void insert_style_with (path p, string var, string val);
  void make_style_with (string var, string val);

  void make_hybrid ();
  void make_native_math_block (bool grid);
  bool hybrid_name_context ();
  bool hybrid_keypress (string key);
  bool prepare_hybrid ();
  void choose_hybrid (string name, bool activate);
  bool activate_latex ();
  bool activate_latex (bool report_error);
  void activate_hybrid (bool with_args_hint);
  void activate_symbol ();

  bool make_return_before ();
  bool make_return_after ();
  void ensure_trailing_proof_paragraph ();
};

#endif // defined EDIT_DYNAMIC_H

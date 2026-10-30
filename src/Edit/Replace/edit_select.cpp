
/******************************************************************************
* MODULE     : edit_select.cpp
* DESCRIPTION: Selection handling
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "Replace/edit_select.hpp"
#include "Interface/edit_interface.hpp"
#include "convert.hpp"
#include "tree_select.hpp"
#include "cursor_navigation.hpp"
#include "Data/NodeContracts/builtin_contracts.hpp"
#include "Typeset/Env/local_environment.hpp"
#include "tm_buffer.hpp"
#include "utf8_edit.hpp"
#include "Xml/clipboard_xml.hpp"
#include "ATHENA/Data/vault.hpp"
#include "ATHENA/Data/document_node_copy.hpp"
#include "ATHENA/buffer_actor.hpp"
#include "scheme_execution_context.hpp"

#include <filesystem>
#include <stdexcept>
#include <vector>

/******************************************************************************
* Constructor and destructor
******************************************************************************/

edit_select_rep::edit_select_rep ():
  cur_sel (no_ranges ()),
  selecting (false), shift_selecting (false), mid_p (),
  selection_import ("default"), selection_export ("default"),
  focus_p (), focus_hold (false) {}
edit_select_rep::~edit_select_rep () {}

void
edit_select_rep::with_selection_context (const std::function<void ()>& action,
    std::shared_ptr<const evaluation_environment_checkpoint> prepared) {
  if (!prepared && selection_environment) { action (); return; }
  if (!prepared) prepared= editor_rep::cursor_context_at (tp);
  if (!prepared) throw std::runtime_error ("Selection context is awaiting layout");
  const auto previous= selection_environment;
  selection_environment= std::move (prepared);
  try { action (); }
  catch (...) { selection_environment= previous; throw; }
  selection_environment= previous;
}

bool
edit_select_rep::selection_in_source () {
  if (!selection_environment) return in_source ();
  return selection_environment->effective_value (MODE) == "src" ||
         selection_environment->effective_value (PREAMBLE) == "true";
}

/******************************************************************************
* Finding a formula root for the Formula AST Viewer
******************************************************************************/

path
edit_select_rep::formula_root (path p) {
  std::shared_ptr<const evaluation_environment_checkpoint> document_context;
  while (p != rp) {
    path owner= path_up (p);
    tree st= subtree (et, owner);
    if (path_up (p) != rp && is_func (st, DOCUMENT, 1))
      st= subtree (et, owner= path_up (p, 2));
    if (is_func (st, CELL)) break;
    if (is_compound (st) && N(st) == 1) {
      const auto prepared= prepare_source_position (owner * 0, document_context);
      const athena::contracts::editing_query query {prepared->contract_context ()};
      tree env= query.environment (st, 0, tree (ATTR));
      tree mt= athena::contracts::field_environment_read (env, MODE);
      tree pt= athena::contracts::field_environment_read (env, "prog-language");
      if (mt == "math") break;
      if (mt == "prog" && pt == "minimal") break;
      if (mt == "text") return rp;
    }
    p= path_up (p);
  }
  return p;
}

/******************************************************************************
* Selecting particular things
******************************************************************************/

static bool
is_equation_array (tree t) {
  return is_compound (t, "eqnarray", 1) || is_compound (t, "eqnarray*", 1);
}

static bool
native_math_grid_row (tree document, path p) {
  return !is_nil (p) && is_func (subtree (document, p), ROW) &&
         is_func (subtree (document, path_up (p)), MATH_GRID);
}

static bool
native_math_grid_cell (tree document, path p) {
  return !is_nil (p) && is_func (subtree (document, p), CELL, 1) &&
         native_math_grid_row (document, path_up (p));
}

static path
equation_array_owner (tree document, path format) {
  while (!is_nil (format)) {
    path parent= path_up (format);
    tree t= subtree (document, parent);
    if (is_equation_array (t)) return parent;
    if (!is_func (t, DOCUMENT, 1) &&
        !(is_func (t, TFORMAT) && last_item (format) == N(t)-1)) break;
    format= parent;
  }
  return path ();
}

static tree
equation_array_clipboard (tree document, path p1, path p2, tree selection) {
  if ((!is_func (selection, TABLE) && !is_func (selection, TFORMAT)) ||
      !is_table_selection (document, p1, p2, false)) return selection;
  int r1, c1, r2, c2;
  path fp= find_subtable_selection (document, p1, p2, r1, c1, r2, c2);
  path owner= equation_array_owner (document, fp);
  if (is_nil (owner)) return selection;
  // Rebuild only the semantic/layout wrappers around the extracted subtable.
  // A nested matrix stops at its cell and never acquires the outer eqnarray.
  while (fp != owner) {
    tree parent= subtree (document, path_up (fp));
    tree wrapped (L(parent), N(parent));
    for (int i=0; i<N(parent); ++i)
      wrapped[i]= i == last_item (fp)? selection: copy (parent[i]);
    selection= wrapped;
    fp= path_up (fp);
  }
  return selection;
}

void
edit_select_rep::select (path p1, path p2) {
  //cout << "Select " << p1 << " -- " << p2 << "\n";
  if (!(rp <= p1 && rp <= p2)) return;
  auto text_boundary= [&] (path p, bool forwards) {
    const path parent= path_up (p);
    if (!has_subtree (et, parent)) return p;
    const tree leaf= subtree (et, parent);
    if (!is_atomic (leaf) || (!is_nil (parent) &&
        is_func (subtree (et, path_up (parent)), RAW_DATA))) return p;
    return parent * utf8_grapheme_snap (leaf->label, last_item (p), forwards);
  };
  // Expand a nonempty selection to whole graphemes, retaining drag direction.
  // A collapsed selection must stay collapsed, even at a stale byte offset.
  const bool reverse= path_less (p2, p1), empty= p1 == p2;
  p1= text_boundary (p1, reverse);
  p2= empty ? p1 : text_boundary (p2, !reverse);
  if (cur_sel == simple_range (p1, p2)) return;
  if (is_empty (cur_sel) && p1 == p2) {
    cur_sel= simple_range (p1, p2);
    return;
  }
  if (p1 != p2) {
    if (!selection_in_source () && is_table_selection (et, p1, p2, false)) {
      int r1, c1, r2, c2, rows, cols;
      path fp= find_subtable_selection (et, p1, p2, r1, c1, r2, c2);
      path owner= equation_array_owner (et, fp);
      if (!is_nil (owner)) {
        table_bound (fp, r1, c1, r2, c2);
        table_get_extents (fp, rows, cols);
        if (r1 == 0 && c1 == 0 && r2 == rows-1 && c2 == cols-1) {
          p1= owner * 0;
          p2= owner * 1;
        }
      }
    }
  }
  if (path_less (p1, p2))
    cur_sel= simple_range (p1, p2);
  else
    cur_sel= simple_range (p2, p1);
  notify_change (THE_SELECTION);
}

void
edit_select_rep::select (path p) {
  const auto entry= prepare_source_position (p * 0);
  with_selection_context ([&] {
    select (prepare_command_boundary (p, p, true, *entry),
            prepare_command_boundary (p, p, false, *entry));
  }, entry);
}

void
edit_select_rep::select_all () {
  tree buffer= the_buffer ();
  selection_set_paths (rp * 0, rp * right_index (buffer));
}

void
edit_select_rep::select_line () {
  select (search_parent_upwards (DOCUMENT));
}

void edit_select_rep::get_selection (path& p1, path& p2) {
  p1= start (cur_sel); p2= end (cur_sel); }
void edit_select_rep::set_selection (path p1, path p2) {
  select (p1, p2); }

/******************************************************************************
* For interface with cursor movement
******************************************************************************/

void
edit_select_rep::select_from_cursor () {
  if (selecting) {
    select (mid_p, tp);
    if (shift_selecting) selecting = false;
  }
}

void
edit_select_rep::select_from_cursor_if_active () {
  if (selecting) select (mid_p, tp);
  else selection_cancel ();
}

void
edit_select_rep::select_from_keyboard (bool flag) {
  selecting= flag;
  shift_selecting= false;
  if (flag) {
    cur_sel= simple_range (tp, tp);
    mid_p  = copy (tp);
  }
  else mid_p= rp;
}

void
edit_select_rep::select_from_shift_keyboard () {
  if (!shift_selecting || is_empty (cur_sel)) mid_p= copy (tp);
  selecting= true;
  shift_selecting= true;
}

/******************************************************************************
* Enlarging an existing selection
******************************************************************************/

static int
breaking_force (char c) {
  if (c == ' ') return 3;
  if (is_punctuation (c)) return 2;
  if (is_iso_alpha (c) || is_digit (c)) return 0;
  return 1;
}

void
edit_select_rep::select_enlarge_text () {
  path p= common (cur_sel);
  if (is_empty (cur_sel) && !is_nil (p)) p= path_up (p);
  tree st= subtree (et, p);
  ASSERT (is_atomic (st), "non textual tree");
  string s= st->label;
  const auto prepared= prepare_source_position (p * 0);
  const tree mode= prepared->effective_value (MODE);
  int i1= last_item (start (cur_sel)), j1= i1;
  int i2= last_item (end   (cur_sel)), j2= i2;
  path q= path_up (p);

  if (mode == "text" || mode == "src") {
    int i, f= 4;
    if (i1 > 0) {
      i= utf8_grapheme_previous (s, i1);
      f= min (f, breaking_force (s[i]));
    }
    if (i2 < N(s))
      f= min (f, breaking_force (s[i2]));

    while (i1 > 0) {
      i= utf8_grapheme_previous (s, i1);
      if (breaking_force (s[i]) > f) break;
      i1= i;
    }
    while (i2 < N(s)) {
      if (breaking_force (s[i2]) > f) break;
      i2= utf8_grapheme_next (s, i2);
    }

    if (i1 < i2 && (i1 != j1 || i2 != j2)) {
      if (is_concat (subtree (et, q)) && i1 == 0 && i2 == N(s))
        select (q * 0, q * 1);
      else select (p * i1, p * i2);
      return;
    }
  }

  if (is_concat (subtree (et, q)) || (i1 == 0 && i2 == N(s)))
    select (q * 0, q * 1);
  else select (p * 0, p * N(s));
}

bool
incomplete_script_selection (tree t, path lp, path rp) {
  if (!is_func (t, CONCAT)) return false;
  if (N(lp) < 2 || N(rp) < 2) return false;
  int l= lp->item, r= rp->item;
  if (is_func (t[l], RSUB) || is_func (t[l], RSUP)) return true;
  if (is_func (t[r], LSUB) || is_func (t[r], LSUP)) return true;
  if (l  >0    && (is_func (t[l-1], LSUB) || is_func (t[l-1], LSUP))) return true;
  if (r+1<N(t) && (is_func (t[r+1], RSUB) || is_func (t[r+1], RSUP))) return true;
  return false;
}

void
edit_select_rep::select_enlarge () {
  path sp, sq;
  if (is_empty (cur_sel) && !is_nil (start (cur_sel))) {
    sp= path_up (start (cur_sel));
    sq= sp;
  }
  else {
    sp= common (cur_sel);
    if (!(rp < sp)) {
      selection_cancel ();
      set_message ("", "");
      return;
    }
    sq= path_up (sp);
  }
  if (is_atomic (subtree (et, sp))) select_enlarge_text ();
  else select (sq * 0, sq * 1);

  path p = common (cur_sel);
  tree st= subtree (et, p);
  bool inner_border= false;
  if (is_compound (st) && !athena::contracts::builtin_label (as_string (L(st)))) {
    const auto prepared= prepare_source_position (p * 0);
    const athena::contracts::editing_query query {prepared->contract_context ()};
    inner_border= (query.describe (L(st)).border & BORDER_INNER) != 0;
  }
  if (inner_border ||
      is_func (st, TFORMAT) ||
      is_func (st, DOCUMENT, 1) ||
      is_script (st) ||
      incomplete_script_selection (st, start (cur_sel) / p, end (cur_sel) / p))
    select_enlarge ();
  else {
    tree s;
    if (is_atomic (st)) s= "text";
    else if (is_func (st, COMPOUND)) s= as_string (st[0]);
    else if (is_func (st, WITH)) s= concat ("with ", as_string (st[0]));
    else s= as_string (L(st));
    set_message (concat ("selected ", s), "enlarge selection");
  }
  selecting= shift_selecting= false;
}

static bool
stop_enlarge_environmental (tree t) {
  if (is_func (t, WITH, 3) && (t[0] == MODE) && (t[1] == "math")) return true;
  if (!is_extension (t)) return false;
  if (is_multi_paragraph (t)) return true;
  string s= as_string (L(t));
  return
    (s == "part") ||
    (s == "chapter") ||
    (s == "section") ||
    (s == "subsection") ||
    (s == "subsubsection") ||
    (s == "paragraph") ||
    (s == "subparagraph");
}

void
edit_select_rep::select_enlarge_environmental () {
  select_enlarge ();
  if (is_empty (cur_sel)) return;
  path p= common (cur_sel);
  tree st= subtree (et, p);
  if (stop_enlarge_environmental (st)) return;
  select_enlarge_environmental ();
}

/******************************************************************************
* Test whether selection is active
******************************************************************************/

bool
edit_select_rep::selection_active_any () {
  return !is_empty (cur_sel);
}

bool
edit_select_rep::selection_active_normal () {
  return selection_active_any () && (!selection_active_table ());
}

bool
edit_select_rep::selection_active_table (bool strict) {
  if (!selection_active_any ()) return false;
  return is_table_selection (et, start (cur_sel), end (cur_sel), strict);
}

bool
edit_select_rep::selection_active_small () {
  if (!selection_active_normal ()) return false;
  path p1, p2;
  selection_get (p1, p2);
  if (p2 == p1) return false;
  if (is_multi_paragraph (subtree (et, common (p1, p2)))) return false;
  return true;
}

bool
edit_select_rep::selection_active_enlarging () {
  return (selecting || !is_empty (cur_sel)) && (mid_p == tp);
}

/******************************************************************************
* Get the selection
******************************************************************************/

bool
edit_select_rep::with_selection_navigation (
    const std::function<void (const cursor_navigation&)>& action) {
  const auto context= selection_environment ? selection_environment : editor_rep::cursor_context_at (tp);
  if (!context) return false;
  return with_admitted_navigation (*context, action);
}

bool
edit_select_rep::selection_try_correct (path i1, path i2, path& o1, path& o2) {
  if (i1 == i2) { o1= i1; o2= i2; return true; }
  path first, last;
  if (!with_selection_navigation ([&] (const cursor_navigation& navigation) {
    selection_correct (i1, i2, first, last, navigation);
  })) return false;
  o1= first; o2= last;
  return true;
}

void
edit_select_rep::selection_correct (path i1, path i2, path& o1, path& o2) {
  if (!selection_try_correct (i1, i2, o1, o2))
    throw std::runtime_error ("Selection context is awaiting layout");
}

void
edit_select_rep::selection_correct (path i1, path i2, path& o1, path& o2,
                                    const cursor_navigation& navigation) {
  ASSERT (rp <= i1 && rp <= i2, "paths not inside document");
  const auto boundary= cursor_source_boundary ();
  ::selection_correct (navigation, subtree (et, rp), i1 / rp, i2 / rp, o1, o2,
    boundary && cursor_occurrence_admitted (i1, boundary),
    boundary && cursor_occurrence_admitted (i2, boundary));
  o1= rp * o1; o2= rp * o2;
}

path
edit_select_rep::selection_get_subtable (
  int& row1, int& col1, int& row2, int& col2)
{
  path fp= find_subtable_selection (et, start (cur_sel), end (cur_sel),
                                    row1, col1, row2, col2);
  table_bound (fp, row1, col1, row2, col2);
  return fp;
}

static path
selection_table_path (tree et, path p) {
  if (!is_nil (p)) p= path_up (p);
  while (!is_nil (p)) {
    if (is_func (subtree (et, p), TABLE)) return p;
    p= path_up (p);
  }
  return path ();
}

selection
edit_select_rep::compute_selection (path p1, path p2, const cursor_navigation& navigation) {
  if (is_table_selection (et, p1, p2, true)) {
    int row1, col1, row2, col2;
    path fp= find_subtable_selection (et, p1, p2, row1, col1, row2, col2);
    tree st= subtree (et, fp);
    table_bound (fp, row1, col1, row2, col2);

    int i, j;
    rectangle r (0, 0, 0, 0);
    for (i=row1; i<=row2; i++)
      for (j=col1; j<=col2; j++) {
        path cp= fp * ::table_search_cell (st, i, j);
        selection sel= eb->find_check_selection (cp * 0, cp * 1);
        if (sel->valid) {
          rectangles rs= sel->rs;
          if (r != rectangle (0, 0, 0, 0)) rs= rectangles (r, rs);
          if (!is_nil (rs)) r= least_upper_bound (rs);
        }
      }
    return selection (rectangles (r), fp * 0, fp * 1);
  }
  else {
    path p_start, p_end;
    //cout << "Find " << p1 << " -- " << p2 << "\n";
    selection_correct (p1, p2, p_start, p_end, navigation);
    //cout << "Find " << p_start << " -- " << p_end << "\n";
    const path row= common (p_start, p_end);
    if (native_math_grid_row (et, row)) {
      // The table box contains cells directly; there is no separate row box.
      // Resolve the row through its cells instead of nonexistent row borders.
      rectangles rs;
      tree source= subtree (et, row);
      for (int j=0; j<N(source); ++j) {
        const path cell= row * j;
        selection part= eb->find_check_selection (cell * 0, cell * 1);
        if (part->valid) rs << part->rs;
      }
      return selection (is_nil (rs) ? rs : rectangles (least_upper_bound (rs)),
                        p_start, p_end);
    }
    selection sel= eb->find_check_selection (p_start, p_end);
    path tf1= selection_table_path (et, p1);
    path tf2= selection_table_path (et, p2);
    // A promoted table boundary may have no direct cursor box, even though
    // find_selection returned the complete table-to-text geometry.
    if (!sel->valid && !is_nil (sel->rs) && tf1 != tf2 &&
        (!is_nil (tf1) || !is_nil (tf2)))
      sel->valid= true;
    //cout << "sel= " << sel << "\n";
    return sel;
  }
}

selection
edit_select_rep::compute_selection (range_set sel) {
  if (is_empty (sel)) return selection ();
  // Highlight source ranges without promoting a search hit to its macro.
  const cursor_navigation navigation ({athena::contracts::builtin_contracts (),
    athena::contracts::access_policy::source});
  rectangles rs;
  for (int i=0; i+1<N(sel); i+=2) {
    selection ssel= compute_selection (sel[i], sel[i+1], navigation);
    if (ssel->valid) rs << ssel->rs;
  }
  return selection (rs, start (sel), end (sel));
}

void
edit_select_rep::selection_get (selection& sel) {
  sel= compute_selection (cur_sel);
}

bool
edit_select_rep::selection_try_get (path& p1, path& p2) {
  if (is_empty (cur_sel)) {
    p1= start (cur_sel); p2= end (cur_sel);
    return true;
  }
  if (selection_active_table ()) {
    int row1, col1, row2, col2;
    path fp= find_subtable_selection (et, start (cur_sel), end (cur_sel),
                                      row1, col1, row2, col2);
    p1= fp * 0;
    p2= fp * 1;
  }
  else {
    if (!selection_try_correct (start (cur_sel), end (cur_sel), p1, p2)) return false;
    if (!selection_in_source () && p1 != p2) {
      const path grid= common (p1, p2);
      const cursor_navigation native_navigation ({athena::contracts::builtin_contracts (),
        athena::contracts::access_policy::normal});
      if (grid != rp && !is_nil (grid) && has_subtree (et, grid) &&
          is_func (subtree (et, grid), MATH_GRID) &&
          is_func (subtree (et, path_up (grid)), MATH_BLOCK, 1) &&
          ((p1 == grid * 0 && p2 == grid * 1) ||
           (p1 == native_navigation.start (et, grid) &&
            p2 == native_navigation.end (et, grid)))) {
        // A whole display grid carries the MathBlock numbering context. Move
        // the real enclosing object, not a synthetic wrapper with copied IDs.
        p1= path_up (grid) * 0;
        p2= path_up (grid) * 1;
      }
    }
  }
  return true;
}

void
edit_select_rep::selection_get (path& p1, path& p2) {
  if (!selection_try_get (p1, p2))
    throw std::runtime_error ("Selection context is awaiting layout");
}

path
edit_select_rep::selection_get_start () {
  return start (cur_sel);
}

path
edit_select_rep::selection_get_end () {
  return end (cur_sel);
}

path
edit_select_rep::selection_var_get_start () {
  path p1, p2;
  selection_get (p1, p2);
  return p1;
}

path
edit_select_rep::selection_var_get_end () {
  path p1, p2;
  selection_get (p1, p2);
  return p2;
}

static tree
selection_compute_without_source_root_identity (
    const cursor_navigation& navigation, tree document, path p1, path p2) {
  const path selected= common (p1, p2);
  if (native_math_grid_cell (document, selected))
    return copy (subtree (document, selected)[0]);
  if (native_math_grid_row (document, selected)) {
    // A row needs its native layout and numbering context on the clipboard.
    tree grid= subtree (document, path_up (selected));
    tree result (MATH_GRID, copy (subtree (document, selected)));
    auto metadata= *athena::node::get (grid);
    metadata.id.clear ();
    athena::node::set (result, metadata);
    const path owner= path_up (selected, 2);
    tree block= subtree (document, owner);
    if (is_func (block, MATH_BLOCK, 1)) {
      result= tree (MATH_BLOCK, result);
      auto block_metadata= *athena::node::get (block);
      block_metadata.id.clear ();
      athena::node::set (result, block_metadata);
    }
    return result;
  }
  if (const auto* header= athena::node::get (document);
      header && selection_covers_node (navigation, document, p1, p2)) {
    tree source= copy (document);
    auto metadata= *header;
    metadata.id.clear ();
    athena::node::set (source, metadata);
    return source;
  }
  return selection_compute (navigation, document, p1, p2);
}

tree
edit_select_rep::selection_get () {
  if (!selection_active_any ()) return "";
  if (selection_active_table ()) {
    int row1, col1, row2, col2;
    path fp= selection_get_subtable (row1, col1, row2, col2);
    return table_get_subtable (fp, row1, col1, row2, col2);
  }
  else {
    path p1, p2;
    // cout << "Selecting...\n";
    selection_get (p1, p2);
    // cout << "Between paths: " << p1 << " and " << p2 << "\n";
    tree t;
    if (!with_selection_navigation ([&] (const cursor_navigation& navigation) {
      t= selection_compute_without_source_root_identity (
        navigation, subtree (et, rp), p1 / rp, p2 / rp);
    })) throw std::runtime_error ("Selection extraction is awaiting layout");
    // cout << "Selection : " << t << "\n";
    return simplify_correct (t);
  }
}

path
edit_select_rep::selection_get_path () {
  path p1, p2;
  selection_get (p1, p2);
  if (p2 == p1 && !is_empty (cur_sel))
    return path_up (p1);
  return common (p1, p2);
}

path
edit_select_rep::selection_get_cursor_path () {
  if (!selection_active_any ()) return tp;
  return admitted_boundary (selection_get_path (), true);
}

tree
edit_select_rep::selection_get_env_value (string var) {
  const path selected= selection_get_cursor_path ();
  // Keep the selected physical occurrence when the selection's source is an
  // ancestor of the caret, including repeated macro-argument presentations.
  const auto context= editor_rep::cursor_context_at (selected);
  if (!context) throw std::runtime_error ("Selection context is awaiting layout");
  return context->effective_value (var);
}

/******************************************************************************
* Copy and paste
******************************************************************************/

namespace {

std::string
selection_std (string value) {
  return std::string (as_charp (value), (std::size_t) N(value));
}

std::string
source_move_vault_key (buffer_document_state* state) {
  namespace fs= std::filesystem;
  if (state == nullptr || !state->node_identities || !vault_active ())
    return {};
  string root_text= as_string (concretize (vault_get_root ()), URL_SYSTEM);
  string document_text= as_string (concretize (state->name), URL_SYSTEM);
  if (root_text == "" || document_text == "") return {};
  std::error_code error;
  fs::path root= fs::weakly_canonical (fs::path (selection_std (root_text)), error);
  if (error) return {};
  error.clear ();
  fs::path document=
    fs::weakly_canonical (fs::path (selection_std (document_text)), error);
  if (error) return {};
  fs::path relative= document.lexically_relative (root);
  if (relative.empty () || relative.is_absolute ()) return {};
  for (const fs::path& part: relative)
    if (part == "..") return {};
  return root.generic_string ();
}

bool
remote_source_move_marker_is_current (
    athena::document_node::source_move_endpoint endpoint, double marker) {
  struct response { bool current= false; };
  auto answer= std::make_shared<response> ();
  auto continuation= actor_continuation_registry::instance ().store (
    [answer, marker] {
      const auto* context= current_scheme_execution_context ();
      if (context != nullptr && context->editor != nullptr)
        answer->current=
          context->editor->source_move_cut_marker_present (marker);
    });
  if (!buffer_actor::invoke_on (
        endpoint.actor, actor_command_kind::run_native_continuation,
        endpoint.view, ATHENA_NO_BLOB, ATHENA_NO_BLOB, nullptr,
        SCHEME_CAPABILITY_BUFFER, continuation)) {
    actor_continuation_registry::instance ().discard (continuation);
    return false;
  }
  return answer->current;
}

std::string
source_move_token (const tree& selection) {
  if (!is_tuple (selection, "texmacs", 4) ||
      !is_tuple (selection[4], "athena-move-v1", 1) ||
      !is_atomic (selection[4][1]))
    return {};
  return selection_std (selection[4][1]->label);
}

tree
plain_clipboard_envelope (const tree& selection) {
  if (!is_tuple (selection, "texmacs", 4)) return selection;
  tree plain= tuple ("texmacs", selection[1], selection[2], selection[3]);
  athena::node::copy_metadata (selection, plain);
  return plain;
}

path
complete_identified_selection_path (const cursor_navigation& navigation,
                                    tree document, path p1, path p2) {
  path selected;
  // Extraction promotes a complete visible range to its first identified
  // ancestor. Cut must remove that same object, not only its innermost field.
  // The document root itself never moves onto the clipboard.
  for (path remaining= common (p1, p2); !is_nil (remaining);
       remaining= remaining->next) {
    selected= selected * remaining->item;
    tree value= subtree (document, selected);
    if (!athena::node::get (value)) continue;
    const auto local= is_atomic (value) ? navigation : navigation.at (selected);
    if (selection_covers_node (local, value, p1 / selected, p2 / selected))
      return selected;
  }
  return path ();
}

} // namespace

void
edit_select_rep::selection_raw_set (string key, tree t) {
  if ((is_tuple (t, "texmacs", 3) || is_tuple (t, "texmacs", 4)) &&
      !athena::document::clipboard_version (t)) {
    if (!buf) throw std::runtime_error ("Clipboard source has no storage version");
    t= athena::document::clipboard_with_version (t, buf->storage_version);
  }
  if (!::set_selection (key, t, "", "", "", "texmacs"))
    throw std::runtime_error ("Cannot publish clipboard selection");
}

tree
edit_select_rep::selection_raw_get (string key) {
  tree t; string s;
  (void) ::get_selection (key, t, s, "texmacs");
  return t;
}

void
edit_select_rep::selection_set_start (path p) {
  if (!selection_active_any ()) {
    if (rp < start (cur_sel)) select (start (cur_sel), start (cur_sel));
    else select (tp, tp);
  }
  if (is_nil (p)) selection_set_start (tp);
  else if (path_less_eq (end (cur_sel), p)) select (p, p);
  else if (rp < p) select (p, end (cur_sel));
}

void
edit_select_rep::selection_set_end (path p) {
  if (is_nil (p)) selection_set_end (tp);
  else if (path_less_eq (p, start (cur_sel))) select (p, p);
  else if (rp < p) select (start (cur_sel), p);
}

void
edit_select_rep::selection_set_paths (path p1, path p2) {
  if (is_nil (p1) || is_nil (p2)) selection_set_paths (tp, tp);
  else if (path_less_eq (p2, p1)) select (p1, p1);
  else if (rp < p1 && rp < p2) select (p1, p2);
}

void
edit_select_rep::selection_set_range_set (range_set sel) {
  selection_set_paths (start (sel), end (sel));
}

void
edit_select_rep::selection_set (
    string key, tree t, bool persistant, string move_token) {
  const path selected= selection_get_cursor_path ();
  const auto prepared= editor_rep::cursor_context_at (selected);
  if (!prepared) throw std::runtime_error ("Selection context is awaiting layout");
  string mode= as_string (prepared->effective_value (MODE));
  if (mode != "src" && (is_equation_array (t) || is_func (t, MATH_BLOCK)))
    mode= "text";
  string lan= as_string (prepared->effective_value (MODE_LANGUAGE (mode)));
  tree sel= move_token == "" ?
    tuple ("texmacs", t, mode, lan) :
    tuple ("texmacs", t, mode, lan,
           tuple ("athena-move-v1", move_token));
  if (!buf) throw std::runtime_error ("Clipboard source has no storage version");
  sel= athena::document::clipboard_with_version (sel, buf->storage_version);
  // Validate named and exported selections too: their in-process fast path
  // still contains native source, regardless of the external export format.
  const auto native_bytes= athena::document::write_clipboard_xml (sel);
  /* TODO: add mode="graphics" somewhere in the context of the <graphics>
     tag. To be done when implementing the different embeddings for
     nicely copying graphics into text, text into graphics, etc. */
  string s, sh, sv;
  if (key == "primary" || key == "mouse") {
    const path context= selected;
    if (selection_export == "verbatim") t= exec_verbatim (t, context);
    if (selection_export == "html") {
        if (mode == "math") 
            t= exec_html (compound ("math", t), context);
        else 
            t= exec_html (t, context);
    }
    if (selection_export == "latex") t= exec_latex (t, context);
    if ((selection_export == "latex") && (mode == "math"))
      t= compound ("math", t);
    if (selection_export == "verbatim")
      s= tree_to_verbatim (t, false, "utf-8");
    else if (selection_export != "default")
      s= tree_to_generic (t, selection_export * "-snippet");
    else {
      s= string (native_bytes.data (), static_cast<int> (native_bytes.size ()));
#ifdef QTTEXMACS
      tree tmp;
      tmp= exec_verbatim (t, context);
      sv= tree_to_verbatim (tmp, false, "utf-8");
      //tmp= exec_html (t, tp);
      //sh= tree_to_generic (tmp, "html-snippet");
#endif
    }
  }
  if (!::set_selection (key, sel, s, sv, sh, selection_export))
    throw std::runtime_error ("Cannot publish clipboard selection");
  selecting= shift_selecting= false;
  if (!persistant) selection_cancel ();
}

void
edit_select_rep::selection_set (tree t) {
  selection_set ("primary", t);
}

void
edit_select_rep::selection_copy (string key) {
  bool emacs= (get_preference ("look and feel", "default") == "emacs");
  if (inside_active_graphics ()) {
    tree t= native_graphics_copy_selection ();
    selection_set (key, t, !emacs);
    return;
  }
  if (selection_active_any ()) {
    tree t= selection_get ();
    if (!in_source ())
      t= equation_array_clipboard (et, start (cur_sel), end (cur_sel), t);
    selection_set (key, t, !emacs);
  }
}

tree
edit_select_rep::selection_get (string key) {
  tree t; string s;
  (void) ::get_selection (key, t, s, selection_import);
  return t;
}

struct native_paste_plan {
  struct table_destination {
    tree source;
    std::shared_ptr<const evaluation_environment_checkpoint> context;
  };
  tree selection;
  std::shared_ptr<const evaluation_environment_checkpoint> insertion_context;
  std::string move_token;
  string mode, language;
  bool graphics= false;
  std::vector<table_destination> tables;

  std::shared_ptr<const evaluation_environment_checkpoint> table_context (tree source) const {
    for (const auto& table: tables)
      if (strong_equal (table.source, source)) return table.context;
    throw std::runtime_error ("Paste destination table changed after preparation");
  }
};

std::shared_ptr<const native_paste_plan>
edit_select_rep::selection_prepare_paste (tree selection) {
  return prepare_paste (selection, editor_rep::cursor_context_at (tp), inside_active_graphics ());
}

std::shared_ptr<native_paste_plan>
edit_select_rep::prepare_paste (tree selection,
    std::shared_ptr<const evaluation_environment_checkpoint> context, bool graphics) {
  if (!buf) throw std::runtime_error ("Paste target has no storage version");
  if (buf->read_only) throw std::runtime_error ("This view is read-only");
  athena::document::validate_clipboard_paste (selection, buf->storage_version);
  auto prepared= std::make_shared<native_paste_plan> ();
  prepared->selection= copy (plain_clipboard_envelope (selection));
  prepared->move_token= source_move_token (selection);
  prepared->graphics= graphics;
  if (prepared->graphics) return prepared;

  if (!context) throw std::runtime_error ("Paste destination is awaiting layout");
  prepared->insertion_context= context;
  prepared->mode= as_string (context->effective_value (MODE));
  const string language= MODE_LANGUAGE (prepared->mode);
  prepared->language= as_string (context->effective_value (language));
  const tree content= selection[1];
  if (prepared->mode == "math" &&
      selection[2] == "text" &&
      !(is_compound (content, "math", 1) && !athena::node::get (content)))
    throw std::invalid_argument ("Cannot paste text into a formula");
  tree payload= content;
  if (is_compound (payload, "math", 1) || is_compound (payload, "text", 1))
    payload= payload[0];
  if (is_func (payload, TABLE) || is_func (payload, TFORMAT) ||
      is_equation_array (content)) {
    for (path owner= path_up (tp); rp < owner; owner= path_up (owner))
      if (is_func (subtree (et, owner), TABLE)) {
        const path format= ::table_search_format (et, owner);
        const auto entry= editor_rep::cursor_context_at (format * 0);
        if (!entry) throw std::runtime_error ("Paste destination table is awaiting layout");
        prepared->tables.push_back ({subtree (et, format), entry});
      }
  }
  return prepared;
}

void
edit_select_rep::selection_validate_paste (tree selection) {
  (void) selection_prepare_paste (selection);
}

void
edit_select_rep::selection_wrap (tree wrapper, path position) {
  const auto outer= editor_rep::cursor_context_at (selection_get_cursor_path ());
  if (!outer) {
    set_message ("Selection context is awaiting layout", "Wrap selection");
    return;
  }
  const auto destination= prepare_tree_position (wrapper, position, *outer);
  if (!destination) {
    set_message ("Insertion template does not expose this field", "Wrap selection");
    return;
  }

  tree content= selection_get ();
  string mode= as_string (outer->effective_value (MODE));
  if (mode != "src") {
    path first, last;
    selection_get (first, last);
    content= equation_array_clipboard (et, first, last, content);
    if (is_equation_array (content) || is_func (content, MATH_BLOCK)) mode= "text";
  }
  const tree language= outer->effective_value (MODE_LANGUAGE (mode));
  if (!buf) throw std::runtime_error ("Selection has no storage version");
  const tree envelope= athena::document::clipboard_with_version (
    tuple ("texmacs", content, mode, language), buf->storage_version);
  auto prepared= prepare_paste (envelope, destination, false);

  // A template can introduce its own table. Its entry is not the current
  // document's table entry or the environment inside the prospective cell.
  const path inner_table= ::table_search_format (wrapper, path_up (position));
  const tree candidate= subtree (wrapper, inner_table);
  std::shared_ptr<const evaluation_environment_checkpoint> template_table;
  if (is_func (candidate, TABLE) ||
      (is_func (candidate, TFORMAT) && N(candidate)>0 &&
       is_func (candidate[N(candidate)-1], TABLE))) {
    template_table= prepare_tree_position (wrapper, inner_table * 0, *outer);
    if (!template_table) {
      set_message ("Insertion template does not expose its table", "Wrap selection");
      return;
    }
  }

  selection_cut ("none");
  insert_tree (wrapper, position);
  if (template_table)
    prepared->tables.insert (prepared->tables.begin (),
      {subtree (et, search_format ()), std::move (template_table)});
  apply_paste (*prepared, true);
}

void
edit_select_rep::selection_paste (string key) {
  if (!buf) throw std::runtime_error ("Paste target has no storage version");
  bool read_only= buf != nullptr && buf->read_only;
  if (read_only) {
    set_message ("This view is read-only", "paste");
    return;
  }

  tree t; string s;
  if (!::get_selection (key, t, s, selection_import)) return;
  if (is_tuple (t, "texmacs", 3) || is_tuple (t, "texmacs", 4)) {
    const auto prepared= selection_prepare_paste (t);
    selection_apply_paste (*prepared);
    return;
  }
  const auto validate_import= [&] (const tree& content) {
    const tree envelope= athena::document::clipboard_with_version (
      tuple ("texmacs", content, "text", "english"), buf->storage_version);
    athena::document::validate_clipboard_paste (envelope, buf->storage_version);
  };
  if (inside_active_graphics ()) return;
  const auto context= editor_rep::cursor_context_at (tp);
  if (!context) throw std::runtime_error ("Paste destination is awaiting layout");
  const string mode= as_string (context->effective_value (MODE));
  if (is_tuple (t, "extern-utf8", 1)) {
    bool wrap= mode != "prog" &&
      get_preference ("verbatim->texmacs:wrap", "off") == "on";
    tree doc= verbatim_to_tree (as_string (t[1]), wrap, "utf-8");
    if (is_func (doc, DOCUMENT, 1)) doc= doc[0];
    validate_import (doc);
    if (!is_empty (doc)) insert_tree (doc);
    return;
  }
  if (is_tuple (t, "extern", 1)) {
    string s   = as_string (t[1]);
    if (mode == "prog")
      if (selection_import == "latex" || selection_import == "html")
        selection_import= "verbatim";
    if (mode == "math")
      if (selection_import == "latex") {
        while (starts (s, " ")) s= s(1, N(s));
        while (ends (s, " ")) s= s(0, N(s)-1);
        if (!starts (s, "$") && !ends (s, "$"))
          s= "$" * s * "$";
      }
    string fm;
    if (selection_import == "verbatim" && mode == "prog") fm= "code-snippet";
    else if (selection_import == "default") fm= "texmacs-snippet";
    else fm= selection_import * "-snippet";
    tree doc= generic_to_tree (s, fm);
    if (is_func (doc, DOCUMENT, 1)) doc= doc[0]; // temporary fix
    if (mode == "math" && is_compound (doc, "math", 1)) doc= doc[0];
    validate_import (doc);
    if (!is_empty (doc)) insert_tree (doc);
  }
}

void
edit_select_rep::selection_apply_paste (const native_paste_plan& prepared) {
  apply_paste (prepared, false);
}

void
edit_select_rep::apply_paste (const native_paste_plan& prepared, bool local_transfer) {
  tree t= copy (prepared.selection);
  const std::string& move_token= prepared.move_token;
  auto discard_move= [&] {
    if (!move_token.empty ())
      athena::document_node::invalidate_source_move (move_token);
  };
  if (prepared.graphics) {
    discard_move ();
    if (!local_transfer && athena::node::contains_metadata (t[1]))
      t[1]= athena::document_node::duplicate_source_nodes (t[1]);
    (void) native_graphics_paste_selection (t[1]);
    return;
  }
  const string mode= prepared.mode;
  const string lan= prepared.language;
  if (mode == "math" && is_equation_array (t[1])) {
    if (!local_transfer && athena::node::contains_metadata (t[1]))
      t[1]= athena::document_node::duplicate_source_nodes (t[1]);
    int row, col;
    path fp= search_format (row, col);
    tree content= t[1][0];
    while (is_func (content, DOCUMENT, 1)) content= content[0];
    if (!is_nil (fp) && !is_nil (equation_array_owner (et, fp)) &&
        (is_func (content, TFORMAT) || is_func (content, TABLE))) {
      discard_move ();
      table_write_subtable (fp, row, col, content,
        prepared.table_context (subtree (et, fp)));
      return;
    }
  }
  bool transformed= false;
  if (is_compound (t[1], "text", 1) && mode == "text" &&
      !athena::node::get (t[1])) {
    t= tuple ("texmacs", t[1][0], "text", lan);
    transformed= true;
  }
  if (is_compound (t[1], "math", 1) && mode == "math" &&
      !athena::node::get (t[1])) {
    t= tuple ("texmacs", t[1][0], "math", lan);
    transformed= true;
  }
  {
    const bool direct_mode=
      (t[2] == mode) || (t[2] == "src") || (mode == "src");
    tree wrapper;
    path wrapper_body;
    if (!direct_mode && ((t[2] == "math") || (mode == "math"))) {
      transformed= true;
      if (t[2] == "math") {
        wrapper= compound ("math", "");
        wrapper_body= path (0);
      }
      else {
        wrapper= tree (WITH, copy (MODE), copy (t[2]), "");
        wrapper_body= path (2);
      }
    }
    if (is_func (t[1], TFORMAT) || is_func (t[1], TABLE)) {
      if (!is_nil (wrapper_body)) insert_tree (wrapper, wrapper_body * 0);
      discard_move ();
      if (!local_transfer && athena::node::contains_metadata (t[1]))
        t[1]= athena::document_node::duplicate_source_nodes (t[1]);
      int row, col;
      path fp= search_format (row, col);
      if (is_nil (fp)) insert_tree (compound (copy (TABULAR), t[1]));
      else table_write_subtable (fp, row, col, t[1],
        prepared.table_context (subtree (et, fp)));
    }
    else {
      bool preserve_identity= local_transfer;
      if (!move_token.empty () && !transformed && direct_mode &&
          athena::node::contains_metadata (t[1]) &&
          buf != nullptr && buf->actor != nullptr &&
          node_identities_active ()) {
        const std::string vault_key= source_move_vault_key (buf);
        const athena::document_node::source_move_endpoint target {
          buf->actor->id (), runtime_view_id};
        auto claim= athena::document_node::reserve_source_move (
          move_token, t[1], vault_key, target);
        if (claim) {
          bool source_valid= false;
          if (claim->source.actor == target.actor) {
            // Two views of the same mutable document share one owner/history
            // domain; do not pretend that is a cross-document move.
            if (claim->source.view == target.view)
              source_valid=
                source_move_cut_marker_present (claim->marker);
          }
          else
            source_valid= remote_source_move_marker_is_current (
              claim->source, claim->marker);
          if (source_valid) {
            start_slave (claim->marker);
            source_move_paste_pending (
              string (claim->token.data (), (int) claim->token.size ()),
              claim->marker);
            preserve_identity= true;
          }
          else athena::document_node::invalidate_source_move (claim->token);
        }
      }
      if (!preserve_identity) {
        discard_move ();
        if (athena::node::contains_metadata (t[1]))
          t[1]= athena::document_node::duplicate_source_nodes (t[1]);
      }
      if (is_nil (wrapper_body)) insert_tree (t[1]);
      else {
        tree payload= copy (simplify_correct (t[1]));
        wrapper[wrapper_body->item]= payload;
        const path destination= prepare_tree_boundary (
          wrapper, wrapper_body, false, *prepared.insertion_context);
        insert_tree (wrapper, destination);
      }
    }
  }
}

void
edit_select_rep::selection_clear (string key) {
  ::clear_selection (key);
}

void
edit_select_rep::selection_cancel () {
  selecting= shift_selecting= false;
  if (is_empty (cur_sel)) return;
  select (start (cur_sel), start (cur_sel));
}

void
edit_select_rep::selection_set_import (string fm) {
  selection_import= fm;
}

void
edit_select_rep::selection_set_export (string fm) {
  selection_export= fm;
}

string
edit_select_rep::selection_get_import () {
  return selection_import;
}

string
edit_select_rep::selection_get_export () {
  return selection_export;
}

/******************************************************************************
* Cutting the selection
******************************************************************************/

void
edit_select_rep::cut (path p) {
  const auto entry= prepare_source_position (p * 0);
  cut (prepare_command_boundary (p, p, true, *entry),
       prepare_command_boundary (p, p, false, *entry));
}

void
edit_select_rep::cut (path p1, path p2) {
  path p = common (p1, p2);
  tree st= subtree (et, p);
  raw_cut (p1, p2);
  if (!is_func (st, TFORMAT) &&
      !is_func (st, TABLE) &&
      !is_func (st, ROW) &&
      !is_document (subtree (et, p)) &&
      is_concat (subtree (et, path_up (p))))
    correct_concat (path_up (p));
}

void
edit_select_rep::raw_cut (path p1, path p2) {
  if (p2 == p1) return;
  path p = common (p1, p2);
  tree t = subtree (et, p);
  int  n = N(p);
  int  i1= p1[n];
  int  i2= p2[n];

  if (native_math_grid_cell (et, p)) {
    assign (p * 0, "");
    return;
  }
  if (native_math_grid_row (et, p)) {
    const path grid_path= path_up (p);
    if (N(subtree (et, grid_path)) > 1) remove (p, 1);
    else {
      // A MathGrid must retain at least one rectangular row.
      tree empty (ROW, N(t));
      for (int j=0; j<N(t); ++j) empty[j]= tree (CELL, "");
      assign (p, empty);
    }
    return;
  }

  if (is_document (t) || is_concat (t)) {
    const auto boundary= [&] (int child, bool at_start) {
      const path owner= p * child;
      if (is_atomic (t[child])) return owner * (at_start ? 0 : N(t[child]->label));
      const auto entry= prepare_source_position (owner * 0);
      // Keep the child's own border. CONCAT-wide correction can move its
      // beginning to the previous sibling, repeating this same cut range.
      return prepare_command_boundary (owner, owner, at_start, *entry);
    };
    path q1= boundary (i1, false);
    path q2= boundary (i2, true);
    raw_cut (q2, p2);
    if (i2>i1+1) remove (p * (i1+1), i2-i1-1);
    raw_cut (p1, q1);
    if (is_concat (t)) correct_concat (p);
    else remove_return (p * i1);
    return;
  }

  if (is_func (t, TFORMAT) || is_func (t, TABLE) || is_func (t, ROW)) {
    path fp= ::table_search_format (et, p);
    tree st= subtree (et, fp);
    int row1, col1, row2, col2;
    table_search_coordinates (st, tail (p1, N(fp)), row1, col1);
    table_search_coordinates (st, tail (p2, N(fp)), row2, col2);
    if (row1>row2) { int tmp= row1; row1= row2; row2= tmp; }
    if (col1>col2) { int tmp= col1; col1= col2; col2= tmp; }
    table_cut_selection (fp, row1, col1, row2, col2);
    return;
  }

  if (is_compound (t) && (!is_format (t))) {
    assign (p, "");
    return;
  }

  if ((N(p1) != (N(p)+1)) || (N(p2) != (N(p)+1))) {
    failed_error << "t = " << t << "\n";
    failed_error << "p = " << p << "\n";
    failed_error << "p1= " << p1 << "\n";
    failed_error << "p2= " << p2 << "\n";
    FAILED ("invalid cut");
  }

  if (is_atomic (t)) {
    int pos= last_item (p1);
    int nr = last_item (p2)-pos;
    if (nr>0) remove (p1, nr);
  }
  else {
    if ((last_item (p1) != 0) || (last_item (p2) != 1)) {
      failed_error << "t = " << t << "\n";
      failed_error << "p = " << p << "\n";
      failed_error << "p1= " << p1 << "\n";
      failed_error << "p2= " << p2 << "\n";
      FAILED ("invalid object cut");
    }
    assign (p, "");
  }
}

void
edit_select_rep::selection_cut (string key) {
  if (inside_active_graphics ()) {
    if (key != "none") {
      tree t= native_graphics_copy_selection ();
      selection_set (key, t);
      (void) native_graphics_cut_selection ();
    }
    else (void) native_graphics_cut_selection ();
  }
  else if (selection_active_any ()) {
    path p1, p2;
    if (selection_active_table ()) {
      p1= start (cur_sel); p2= end (cur_sel);
      if(key != "none") {
        tree sel= selection_get ();
        if (!in_source ()) sel= equation_array_clipboard (et, p1, p2, sel);
        selection_set (key, sel);
      }
      cut (p1, p2);
    }
    else {
      selection_get (p1, p2);
      if (p2 == p1) { go_to (p2); return; }
      path p= common (p1, p2);
      bool stable_right_boundary= false;
      if (N(p1) > N(p) && N(p2) > N(p)) {
        tree t= subtree (et, p);
        int i1= p1[N(p)], i2= p2[N(p)];
        stable_right_boundary=
          (is_document (t) || is_concat (t)) && i1 < i2 &&
          p1 == p * i1 * 0 && p2 == p * i2 * 0;
      }

      // Normally the left boundary follows a partially retained prefix.  For
      // a half-open range of complete children, however, that boundary is
      // deleted and may reattach to the wrong side of an adjacent math
      // operator.  Its right boundary belongs to the first surviving child.
      path complete_object;
      tree sel;
      if (!with_selection_navigation ([&] (const cursor_navigation& navigation) {
        tree body= subtree (et, rp);
        if (!native_math_grid_row (et, p) && !native_math_grid_cell (et, p)) {
          const path selected= complete_identified_selection_path (
            navigation, body, p1 / rp, p2 / rp);
          if (!is_nil (selected)) complete_object= rp * selected;
        }
        if (key != "none") sel= selection_compute_without_source_root_identity (
          navigation, body, p1 / rp, p2 / rp);
      })) throw std::runtime_error ("Selection extraction is awaiting layout");
      if (key != "none") {
        tree clipboard= simplify_correct (sel);
        athena::document_node::source_move_ticket move;
        if (selection_export == "default" && node_identities_active () &&
            athena::node::contains_metadata (clipboard) &&
            buf != nullptr && buf->actor != nullptr) {
          const std::string vault_key= source_move_vault_key (buf);
          move= athena::document_node::issue_source_move (
            clipboard, vault_key,
            {buf->actor->id (), runtime_view_id});
        }
        try {
          selection_set (
            key, clipboard, false,
            move.token.empty () ? string () :
              string (move.token.data (), (int) move.token.size ()));
        }
        catch (...) {
          if (!move.token.empty ())
            athena::document_node::invalidate_source_move (move.token);
          throw;
        }
        if (!move.token.empty ()) start_slave (move.marker);
      }
      // Clipboard metadata must use the selected display before repositioning
      // the caret for the actual cut.
      go_to (p2);
      observer pos= position_new (stable_right_boundary? p2: p1);
      if (!is_nil (complete_object)) {
        const path parent_path= path_up (complete_object);
        const tree parent= subtree (et, parent_path);
        if (is_document (parent) || is_concat (parent))
          remove (complete_object, 1);
        else {
          // Fixed-arity owners keep their slot, but not the moved identity.
          const tree value= subtree (et, complete_object);
          assign (complete_object, is_document (value) ?
            tree (DOCUMENT, "") : tree (""));
        }
        // A complete identified paragraph is removed as an object so its UUID
        // can move rather than being duplicated on the empty remainder.  Keep
        // the containing document editable, though: ordinary range cuts have
        // always left an empty paragraph here, and wrapper insertion relies on
        // that document invariant.
        if (has_subtree (et, parent_path)) {
          tree parent= subtree (et, parent_path);
          if (is_document (parent) && N(parent) == 0)
            insert (parent_path * 0, tree (DOCUMENT, ""));
        }
      }
      else cut (p1, p2);
      go_to (position_get (pos));
      position_delete (pos);
    }
  }
}

tree
edit_select_rep::selection_get_cut () {
  tree t= selection_get ();
  selection_cut ("none");
  return t;
}

void
edit_select_rep::selection_move () {
  observer pos= position_new (tp);
  tree t= selection_get_cut ();
  go_to (position_get (pos));
  insert_tree (t);
  position_delete (pos);
}

/******************************************************************************
* Focus related routines
******************************************************************************/

path
edit_select_rep::manual_focus_get () {
  return focus_p;
}

void
edit_select_rep::manual_focus_set (path p, bool force) {
  //cout << "Set focus " << p << ", " << force << ", " << focus_hold << "\n";
  if (is_nil (p) && focus_hold && !force) return;
  focus_p= p;
  focus_hold= !is_nil (p);
}

void
edit_select_rep::manual_focus_release () {
  focus_hold= false;
}

bool
edit_select_rep::focus_try_search (path p, bool skip_flag, bool up_flag, path& result) {
  //cout << "Search focus " << p << ", " << skip_flag << ", " << up_flag << "\n";
  if (!(rp < p)) { result= rp; return true; }
  tree st= subtree (et, p);
  if (!skip_flag) { result= p; return true; }
  if (is_compound (st) && p == path_up (tp) && last_item (tp) != 0) {
    const auto entry= editor_rep::cursor_context_at (p * 0);
    if (!entry) return false;
    if (athena::contracts::editing_query {entry->contract_context ()}.no_fields_editable (L(st))) {
      result= p;
      return true;
    }
  }
  if (is_atomic (st) ||
      is_func (st, DOCUMENT) ||
      is_func (st, CONCAT) ||
      is_func (st, TFORMAT) ||
      is_func (st, TABLE) ||
      is_func (st, ROW) ||
      is_func (st, CELL) ||
      is_compound (st, "shown") ||
      is_func (st, HIDDEN) ||
      is_compound (st, "slide") ||
      is_compound (st, "with-screen-color") ||
      up_flag)
    return focus_try_search (path_up (p), skip_flag, false, result);
  result= p;
  return true;
}

bool
edit_select_rep::focus_try_get (path& result, bool skip_flag) {
  //cout << "Search focus " << focus_p << ", " << skip_flag << "\n";
  if (!is_nil (focus_p))
    return focus_try_search (focus_p, skip_flag, false, result);
  if (selection_active_any ()) {
    path first, last;
    if (!selection_try_get (first, last)) return false;
    const path selected= first == last && !is_empty (cur_sel) ? path_up (first) : common (first, last);
    return focus_try_search (selected, skip_flag, false, result);
  }
  else {
    tree st= subtree (et, path_up (tp));
    if (is_compound (st, "float")) skip_flag= false;
    if (is_compound (st, "wide-float")) skip_flag= false;
    if (is_compound (st, "footnote")) skip_flag= false;
    if (is_compound (st, "footnote-anchor")) skip_flag= false;
    if (is_compound (st, "wide-footnote")) skip_flag= false;
    if (is_compound (st, "note-footnote")) skip_flag= false;
    if (is_compound (st, "note-footnote*")) skip_flag= false;
    if (is_compound (st, "folded-comment")) skip_flag= false;
    if (is_compound (st, "cite-detail")) skip_flag= false;
    return focus_try_search (path_up (tp), skip_flag, true, result);
  }
}

path
edit_select_rep::focus_get (bool skip_flag) {
  path result;
  if (!focus_try_get (result, skip_flag))
    throw std::runtime_error ("Focus context is awaiting layout");
  return result;
}

/******************************************************************************
* Alternative selections
******************************************************************************/

void
edit_select_rep::set_alt_selection (string name, range_set sel) {
  if (alt_sels[name] != sel) {
    alt_sels (name)= sel;
    notify_change (THE_SELECTION);
  }
}

range_set
edit_select_rep::get_alt_selection (string name) {
  return alt_sels[name];
}

range_set
current_alt_selection (string name) {
  return get_current_editor()->get_alt_selection (name);
}

void
edit_select_rep::cancel_alt_selection (string name) {
  if (alt_sels->contains (name)) {
    alt_sels->reset (name);
    notify_change (THE_SELECTION);
  }
}

void
edit_select_rep::cancel_alt_selections () {
  if (N(alt_sels) > 0) {
    alt_sels= hashmap<string,range_set> ();
    notify_change (THE_SELECTION);
  }
}

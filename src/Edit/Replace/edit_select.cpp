
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

/******************************************************************************
* Constructor and destructor
******************************************************************************/

edit_select_rep::edit_select_rep ():
  cur_sel (no_ranges ()),
  selecting (false), shift_selecting (false), mid_p (),
  selection_import ("default"), selection_export ("default"),
  focus_p (), focus_hold (false) {}
edit_select_rep::~edit_select_rep () {}

/******************************************************************************
* Finding a formula root for the Formula AST Viewer
******************************************************************************/

path
edit_select_rep::formula_root (path p) {
  const auto prepared= editor_rep::cursor_context_at (p);
  const athena::contracts::editing_query query {
    prepared ? prepared->contract_context () : contract_state.contracts};
  while (p != rp) {
    tree st= subtree (et, path_up (p));
    if (path_up (p) != rp && is_func (st, DOCUMENT, 1))
      st= subtree (et, path_up (p, 2));
    if (is_func (st, CELL)) break;
    if (is_compound (st) && N(st) == 1) {
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
    if (!in_source () && is_table_selection (et, p1, p2, false)) {
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
  select (start (et, p), end (et, p));
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
  string mode= get_env_string (MODE);
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
  const auto prepared= editor_rep::cursor_context_at (tp);
  const athena::contracts::editing_query query {
    prepared ? prepared->contract_context () : contract_state.contracts};
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
  if (((query.describe (L(st)).border &
        BORDER_INNER) != 0 && !athena::contracts::builtin_label (as_string (L(st)))) ||
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

void
edit_select_rep::selection_correct (path i1, path i2, path& o1, path& o2) {
  using athena::contracts::access_policy;
  const auto context= editor_rep::cursor_context_at (tp);
  const auto access= in_source () ? access_policy::source : access_policy::normal;
  selection_correct (i1, i2, o1, o2,
    cursor_navigation ({context ? context->contract_context () : contract_state.contracts, access}));
}

void
edit_select_rep::selection_correct (path i1, path i2, path& o1, path& o2,
                                    const cursor_navigation& navigation) {
  ASSERT (rp <= i1 && rp <= i2, "paths not inside document");
  ::selection_correct (navigation, subtree (et, rp), i1 / rp, i2 / rp, o1, o2);
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
edit_select_rep::compute_selection (path p1, path p2) {
  using athena::contracts::access_policy;
  const auto context= editor_rep::cursor_context_at (tp);
  const auto access= in_source () ? access_policy::source : access_policy::normal;
  return compute_selection (p1, p2,
    cursor_navigation ({context ? context->contract_context () : contract_state.contracts, access}));
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
  const cursor_navigation navigation ({contract_state.contracts, athena::contracts::access_policy::source});
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

void
edit_select_rep::selection_get (path& p1, path& p2) {
  if (selection_active_table ()) {
    int row1, col1, row2, col2;
    path fp= selection_get_subtable (row1, col1, row2, col2);
    p1= fp * 0;
    p2= fp * 1;
  }
  else {
    selection_correct (start (cur_sel), end (cur_sel), p1, p2);
    if (!in_source () && p1 != p2) {
      const path grid= common (p1, p2);
      if (grid != rp && !is_nil (grid) && has_subtree (et, grid) &&
          is_func (subtree (et, grid), MATH_GRID) &&
          is_func (subtree (et, path_up (grid)), MATH_BLOCK, 1) &&
          ((p1 == grid * 0 && p2 == grid * 1) ||
           (p1 == ::start (et, grid) && p2 == ::end (et, grid)))) {
        // A whole display grid carries the MathBlock numbering context. Move
        // the real enclosing object, not a synthetic wrapper with copied IDs.
        p1= path_up (grid) * 0;
        p2= path_up (grid) * 1;
      }
    }
  }
  /*
  selection sel; selection_get (sel);
  p1= sel->start;
  p2= sel->end;
  */
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
    tree document, path root, path p1, path p2) {
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
  if (!is_nil (root) && has_subtree (document, root) &&
      p1 == start (document, root) && p2 == end (document, root)) {
    tree source= copy (subtree (document, root));
    if (const auto* header= athena::node::get (source)) {
      auto metadata= *header;
      metadata.id.clear ();
      athena::node::set (source, metadata);
      return selection_compute (source, start (source), end (source));
    }
  }
  return selection_compute (document, p1, p2);
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
    tree t= selection_compute_without_source_root_identity (et, rp, p1, p2);
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
  return start (et, selection_get_path ());
}

tree
edit_select_rep::selection_get_env_value (string var) {
  if (!selection_active_any ()) return get_env_value (var);
  const path selected= selection_get_cursor_path ();
  // Keep the selected physical occurrence when the selection's source is an
  // ancestor of the caret, including repeated macro-argument presentations.
  const auto context= editor_rep::cursor_context_at (selected);
  if (context) {
    const tree value= context->value (var);
    return is_func (value, BACKUP, 2) ? value[0] : value;
  }
  return get_env_value (var, selected);
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
complete_identified_selection_path (tree document, path p1, path p2, path root) {
  path selected= common (p1, p2);
  if (selected == root || is_nil (selected) || !has_subtree (document, selected))
    return path ();
  tree value= subtree (document, selected);
  if (!athena::node::get (value) ||
      p1 != start (document, selected) || p2 != end (document, selected))
    return path ();
  path parent= path_up (selected);
  if (is_nil (parent) || !has_subtree (document, parent)) return path ();
  tree container= subtree (document, parent);
  if (is_document (container) || is_concat (container)) return selected;
  // A complete identified range in a fixed content slot must also move its
  // header. Ordinary character/range cuts retain it on the empty remainder.
  return (is_atomic (value) || is_document (value) || is_concat (value)) ?
    selected : path ();
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
  selecting= shift_selecting= false;
  string mode= as_string (selection_get_env_value (MODE));
  if (mode != "src" && (is_equation_array (t) || is_func (t, MATH_BLOCK)))
    mode= "text";
  string lan = as_string (selection_get_env_value (MODE_LANGUAGE (mode)));
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
    const path context= selection_get_cursor_path ();
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
    path old_tp= tp;
    selection sel; selection_get (sel);
    go_to (sel->end);
    tree t= selection_get ();
    if (!in_source ())
      t= equation_array_clipboard (et, start (cur_sel), end (cur_sel), t);
    go_to (sel->start);
    selection_set (key, t, !emacs);
    go_to (old_tp);
  }
}

tree
edit_select_rep::selection_get (string key) {
  tree t; string s;
  (void) ::get_selection (key, t, s, selection_import);
  return t;
}

void
edit_select_rep::selection_validate_paste (tree selection) {
  if (!buf) throw std::runtime_error ("Paste target has no storage version");
  if (buf->read_only) throw std::runtime_error ("This view is read-only");
  athena::document::validate_clipboard_paste (selection, buf->storage_version);
  const tree content= selection[1];
  if (!inside_active_graphics () && get_env_string (MODE) == "math" &&
      selection[2] == "text" &&
      !(is_compound (content, "math", 1) && !athena::node::get (content)))
    throw std::invalid_argument ("Cannot paste text into a formula");
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
  if (is_tuple (t, "texmacs", 3) || is_tuple (t, "texmacs", 4))
    selection_validate_paste (t);
  const auto validate_import= [&] (const tree& content) {
    const tree envelope= athena::document::clipboard_with_version (
      tuple ("texmacs", content, "text", "english"), buf->storage_version);
    athena::document::validate_clipboard_paste (envelope, buf->storage_version);
  };
  const std::string move_token= source_move_token (t);
  auto discard_move= [&] {
    if (!move_token.empty ())
      athena::document_node::invalidate_source_move (move_token);
  };
  t= plain_clipboard_envelope (t);
  if (inside_active_graphics ()) {
    discard_move ();
    if (is_tuple (t, "texmacs", 3)) {
      if (athena::node::contains_metadata (t[1]))
        t[1]= athena::document_node::duplicate_source_nodes (t[1]);
      (void) native_graphics_paste_selection (t[1]);
    }
    return;
  }
  if (is_tuple (t, "extern-utf8", 1)) {
    bool wrap= get_env_string (MODE) != "prog" &&
      get_preference ("verbatim->texmacs:wrap", "off") == "on";
    tree doc= verbatim_to_tree (as_string (t[1]), wrap, "utf-8");
    if (is_func (doc, DOCUMENT, 1)) doc= doc[0];
    validate_import (doc);
    if (!is_empty (doc)) insert_tree (doc);
    return;
  }
  if (is_tuple (t, "extern", 1)) {
    string mode= get_env_string (MODE);
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
  if (is_tuple (t, "texmacs", 3)) {
    string mode= get_env_string (MODE);
    string lan = get_env_string (MODE_LANGUAGE (mode));
    if (mode == "math" && is_equation_array (t[1])) {
      if (athena::node::contains_metadata (t[1]))
        t[1]= athena::document_node::duplicate_source_nodes (t[1]);
      int row, col;
      path fp= search_format (row, col);
      tree content= t[1][0];
      while (is_func (content, DOCUMENT, 1)) content= content[0];
      if (!is_nil (fp) && !is_nil (equation_array_owner (et, fp)) &&
          (is_func (content, TFORMAT) || is_func (content, TABLE))) {
        discard_move ();
        table_write_subtable (fp, row, col, content);
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
    if (mode == "math" && t[2] == "text") {
      discard_move ();
      set_message ("Error: invalid paste of text into a formula", "paste");
    }
    else {
      const bool direct_mode=
        (t[2] == mode) || (t[2] == "src") || (mode == "src");
      if (!direct_mode && ((t[2] == "math") || (mode == "math"))) {
        transformed= true;
        if (t[2] == "math")
          insert_tree (compound ("math", ""), path (0, 0));
        else if (t[2] == "text")
          insert_tree (compound ("text", ""), path (0, 0));
        else
          insert_tree (tree (WITH, copy (MODE), copy (t[2]), ""), path (2, 0));
      }
      if (is_func (t[1], TFORMAT) || is_func (t[1], TABLE)) {
        discard_move ();
        if (athena::node::contains_metadata (t[1]))
          t[1]= athena::document_node::duplicate_source_nodes (t[1]);
        int row, col;
        path fp= search_format (row, col);
        if (is_nil (fp)) insert_tree (compound (copy (TABULAR), t[1]));
        else table_write_subtable (fp, row, col, t[1]);
      }
      else {
        bool preserve_identity= false;
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
        insert_tree (t[1]);
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
  cut (start (et, p), end (et, p));
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
    path q1= copy (p); q1 << path (i1, end (t[i1]));
    path q2= copy (p); q2 << path (i2, start (t[i2]));
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
      const path complete_object=
        (native_math_grid_row (et, p) || native_math_grid_cell (et, p)) ? path () :
        complete_identified_selection_path (et, p1, p2, rp);
      if (key != "none") {
        tree sel= selection_compute_without_source_root_identity (et, rp, p1, p2);
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

path
edit_select_rep::focus_search (path p, bool skip_flag, bool up_flag) {
  //cout << "Search focus " << p << ", " << skip_flag << ", " << up_flag << "\n";
  if (!(rp < p)) return rp;
  tree st= subtree (et, p);
  if (!skip_flag) return p;
  if (none_accessible (st) && p == path_up (tp) && last_item (tp) != 0)
    return p;
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
    return focus_search (path_up (p), skip_flag, false);
  return p;
}

path
edit_select_rep::focus_get (bool skip_flag) {
  //cout << "Search focus " << focus_p << ", " << skip_flag << "\n";
  if (!is_nil (focus_p))
    return focus_search (focus_p, skip_flag, false);
  if (selection_active_any ())
    return focus_search (selection_get_path (), skip_flag, false);
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
    return focus_search (path_up (tp), skip_flag, true);
  }
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

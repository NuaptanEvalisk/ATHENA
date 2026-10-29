
/******************************************************************************
* MODULE     : cursor.cpp
* DESCRIPTION: cursor handling
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "edit_cursor.hpp"
#include "Data/NodeContracts/editing_query.hpp"
#include "iterator.hpp"
#include "tm_buffer.hpp"
#include "tree_traverse.hpp"
#include "cursor_navigation.hpp"
#include "analyze.hpp"
#include "Typeset/Env/local_environment.hpp"
#include "Data/NodeContracts/builtin_contracts.hpp"

/******************************************************************************
* Constructor and destructor
******************************************************************************/

edit_cursor_rep::edit_cursor_rep ():
  cu (0, 0), mv (0, 0), mv_status (0) {}
edit_cursor_rep::~edit_cursor_rep () {}
cursor& edit_cursor_rep::the_cursor () { return cu; }
cursor& edit_cursor_rep::the_ghost_cursor () { return mv; }

/******************************************************************************
* Cursor movement
******************************************************************************/

#define DELTA (1<<23)

path
edit_cursor_rep::make_cursor_accessible (path p, bool forwards) {
  const auto boundary= p == tp ? cursor_source_boundary () : nullptr;
  const auto context= editor_rep::cursor_context_at (p);
  if (!context) return path ();
  path result;
  if (!with_admitted_navigation (*context, [&] (const cursor_navigation& navigation) {
    if (boundary && cursor_occurrence_admitted (p, boundary) &&
        navigation.is_mapped_cursor (subtree (et, rp), p / rp)) result= p;
    else result= make_cursor_accessible (p, forwards, navigation);
  })) return path ();
  return result;
}

path
edit_cursor_rep::make_cursor_accessible (
    path p, bool forwards, const cursor_navigation& navigation) {
  path start_p= p;
  bool inverse= false;
  tree st= subtree (et, rp);
  while (navigation.contracts ().access != athena::contracts::access_policy::source &&
         !navigation.is_accessible_cursor (st, p / rp)) {
    ASSERT (rp <= p, "path outside document");
    path sp = p / rp;
    path pp = sp;
    int  dir= (forwards ^ inverse)? 1: -1;
    path cp = navigation.closest_accessible_inside (st, sp, dir);
    if (cp != sp) {
      sp= cp;
    }
    else sp= navigation.move_valid (st, cp, dir > 0);
    if ((dir > 0 && !path_less (pp, sp)) ||
        (dir < 0 && !path_less (sp, pp))) {
      if (inverse) {
        p= rp * navigation.closest_accessible_inside (st, sp, forwards? 1: -1); break; }
      else {
        p= start_p; inverse= true; }
    }
    else p= rp * sp;
  }
  return p;
}

path
edit_cursor_rep::tree_path (path sp, SI x, SI y, SI delta, bool source) {
  return tree_path (sp, x, y, delta, nullptr, nullptr, source);
}

path
edit_cursor_rep::tree_path (path sp, SI x, SI y, SI delta,
                            athena::text::caret_affinity* affinity, path* box_path,
                            bool source) {
  path bp= find_scrolled_box_path (eb, sp, x, y, delta);
  path stp= eb->find_tree_path (bp);
  if (!(rp < stp)) return path ();
  const auto boundary= eb->find_source_boundary (bp);
  const auto context= source ? std::shared_ptr<const evaluation_environment_checkpoint> {} :
    display_context_at (stp, bp);
  using athena::contracts::access_policy;
  path p= stp;
  const auto resolve= [&] (const cursor_navigation& navigation) {
    tree body= subtree (et, rp);
    if (!(boundary && context && cursor_occurrence_admitted (stp, boundary) &&
          navigation.is_mapped_cursor (body, stp / rp))) {
      p= rp * navigation.correct_cursor (body, stp / rp);
      p= make_cursor_accessible (p, searching_forwards, navigation);
    }
  };
  if (source)
    resolve (cursor_navigation ({athena::contracts::builtin_contracts (), access_policy::source}));
  else if (!context || !with_admitted_navigation (*context, resolve, bp)) return path ();
  if (affinity)
    *affinity= p == stp ? eb->find_cursor (bp)->affinity :
      athena::text::caret_affinity::downstream;
  if (box_path) *box_path= p == stp ? bp : path ();
  return p;
}

std::optional<bool>
edit_cursor_rep::cursor_move_sub (SI& x0, SI& y0, SI& d0, SI dx, SI dy) {
  struct pending_hit {};
  const SI original_x= x0, original_y= y0, original_delta= d0;
  try {
  path sp= find_innermost_scroll (eb, tp);
  searching_forwards= dx == 1 || dy == -1;

  int i,d;
  athena::text::caret_affinity ref_affinity;
  path ref_bp;
  path ref_p= tree_path (sp, x0, y0, d0, &ref_affinity, &ref_bp);
  if (is_nil (ref_p)) return {};
  const auto same_position= [&] (SI x, SI y, SI delta) {
    athena::text::caret_affinity side;
    path bp;
    const path p= tree_path (sp, x, y, delta, &side, &bp);
    if (is_nil (p)) throw pending_hit {};
    return p == ref_p && side == ref_affinity &&
      (is_nil (bp) || is_nil (ref_bp) ||
       eb->find_source_boundary (bp) == eb->find_source_boundary (ref_bp));
  };
  if (ref_p != tp) {
    if (!searching_forwards && path_less (tp, ref_p));
    else if (searching_forwards && path_less (ref_p, tp));
    else {
      tp= ref_p;
      cu->affinity= ref_affinity;
      remember_cursor_display (ref_bp);
      return true;
    }
  }
  
  // cout << "ref_p = " << ref_p << "\n";
  if (same_position (x0, y0, d0+ dx*DELTA)) {
    for (i=1; i<DELTA; i=i<<1)
      if (!same_position (x0+ dx*i, y0+ dy*i, d0+ dx*DELTA))
        break;
    if (i>=DELTA) return false;
    for (d=i>>2; d>=1; d=d>>1)
      if (!same_position (x0+ dx*(i-d), y0+ dy*(i-d), d0+ dx*DELTA))
        i-=d;

    x0 += dx*i;
    y0 += dy*i;
  }
  
  // cout << "path  = " << tree_path (sp, x0, y0, d0) << "\n";
  if (dx!=0) {
    if (same_position (x0, y0, d0)) {
      for (i=1; i<DELTA; i=i<<1)
        if (!same_position (x0, y0, d0+ dx*i)) break;
      if (i>=DELTA)
        FAILED ("inconsistent cursor handling");
      for (d=i>>2; d>=1; d=d>>1)
        if (!same_position (x0, y0, d0+ dx*(i-d))) i-=d;
      d0 += dx*i;
    }
    else {
      for (i=1; i<DELTA; i=i<<1)
        if (same_position (x0, y0, d0- dx*i)) break;
      if (i<DELTA) {
        for (d=i>>2; d>=1; d=d>>1)
          if (same_position (x0, y0, d0- dx*(i-d))) i-=d;
        i--;
        d0 -= dx*i;
      }
      else {  // exceptional case
        ref_p= tree_path (sp, x0, y0, d0- dx*DELTA, &ref_affinity, &ref_bp);
        if (is_nil (ref_p)) throw pending_hit {};
        for (i=1; i<DELTA; i=i<<1)
          if (same_position (x0, y0, d0- dx*i)) break;
        for (d=i>>2; d>=1; d=d>>1)
          if (same_position (x0, y0, d0- dx*(i-d))) i-=d;
        d0 -= dx*i;
      }
    }
  }

  path bp;
  const path target= tree_path (sp, x0, y0, d0, &cu->affinity, &bp);
  if (is_nil (target)) throw pending_hit {};
  tp= target;
  remember_cursor_display (bp);
  return true;
  }
  catch (const pending_hit&) {
    x0= original_x; y0= original_y; d0= original_delta;
    return {};
  }
}

std::optional<bool>
edit_cursor_rep::cursor_move (SI dx, SI dy) {
  return cursor_move_sub (mv->ox, mv->oy, mv->delta, dx, dy);
}

/******************************************************************************
* Routines affecting both the cursor and the ghost cursor
******************************************************************************/

void
edit_cursor_rep::adjust_ghost_cursor (int status) {
  if (status==mv_status) {
    if (status!=HORIZONTAL) {
      mv->ox   = cu->ox;
      mv->delta= cu->delta;
    }
    if (status!=VERTICAL) {
      SI dy= (cu->y1 + cu->y2) >> 1;
      mv->oy= cu->oy + dy;
    }
  }
}

void
edit_cursor_rep::notify_cursor_moved (int status) {
  mv_status= status;
  cu= current_display_cursor (cu->affinity);
  if (cu->valid) prepare_cursor_environment ();
  notify_change (THE_CURSOR);
  if (cu->valid && cursor_context_at (tp, cursor_source_boundary ()))
    call ("notify-cursor-moved", object (status));
}

void
edit_cursor_rep::go_to (SI x, SI y, bool absolute) {
  (void) move_to_physical (x, y, absolute);
}

bool
edit_cursor_rep::move_to_physical (SI x, SI y, bool absolute) {
  if (has_changed (THE_TREE+THE_ENVIRONMENT)) return false;
  path bp;
  const path target= tree_path (absolute? path (): find_innermost_scroll (eb, tp),
                              x, y, 0, &cu->affinity, &bp);
  if (is_nil (target)) return false;
  tp= target;
  remember_cursor_display (bp);
  notify_cursor_moved (CENTER);
  mv->ox   = x;
  mv->oy   = y;
  mv->delta= 0;
  return true;
}

void
edit_cursor_rep::go_left_physical () {
  (void) move_horizontal_physical (false);
}

void
edit_cursor_rep::go_right_physical () {
  (void) move_horizontal_physical (true);
}

bool
edit_cursor_rep::move_horizontal_physical (bool forwards) {
  if (has_changed (THE_TREE+THE_ENVIRONMENT)) return false;
  const cursor original= copy (mv);
  adjust_ghost_cursor (VERTICAL);
  if (!cursor_move (forwards ? 1 : -1, 0)) { mv= original; return false; }
  notify_cursor_moved (HORIZONTAL);
  select_from_cursor_if_active ();
  return true;
}

void
edit_cursor_rep::go_up () {
  if (has_changed (THE_TREE+THE_ENVIRONMENT)) return;
  path scroll_p= find_innermost_scroll (eb, tp);
  path start_p= tree_path (scroll_p, -(1 << 30), 1 << 30, 0);
  if (is_nil (start_p) || tp == start_p) return;
  path old_p= tp;
  const cursor original= copy (mv);
  adjust_ghost_cursor (HORIZONTAL);
  if (!cursor_move (0, 1)) { mv= original; return; }
  notify_cursor_moved (VERTICAL);
  if (tp == old_p) tp= start_p;
  select_from_cursor_if_active ();
}

void
edit_cursor_rep::go_down () {
  if (has_changed (THE_TREE+THE_ENVIRONMENT)) return;
  path scroll_p= find_innermost_scroll (eb, tp);
  path end_p= tree_path (scroll_p, 1 << 30, -(1 << 30), 0);
  if (is_nil (end_p) || tp == end_p) return;
  path old_p= tp;
  const cursor original= copy (mv);
  adjust_ghost_cursor (HORIZONTAL);
  if (!cursor_move (0, -1)) { mv= original; return; }
  notify_cursor_moved (VERTICAL);
  if (tp == old_p) tp= end_p;
  select_from_cursor_if_active ();
}

void
edit_cursor_rep::go_page_up () {
  if (has_changed (THE_TREE+THE_ENVIRONMENT)) return;
  path sp= find_innermost_scroll (eb, tp);
  if (is_nil (sp)) go_to (mv->ox, min (mv->oy + get_visible_height (), eb->y2));
  else {
    SI x, y, sx, sy;
    rectangle outer, inner;
    box b= eb[path_up (sp)];
    find_canvas_info (eb, sp, x, y, sx, sy, outer, inner);
    go_to (mv->ox, min (mv->oy + b->h (), y + sy + inner->y2), false);
  }
  select_from_cursor_if_active ();
}

void
edit_cursor_rep::go_page_down () {
  if (has_changed (THE_TREE+THE_ENVIRONMENT)) return;
  path sp= find_innermost_scroll (eb, tp);
  if (is_nil (sp)) go_to (mv->ox, max (mv->oy - get_visible_height (), eb->y1));
  else {
    SI x, y, sx, sy;
    rectangle outer, inner;
    box b= eb[path_up (sp)];
    find_canvas_info (eb, sp, x, y, sx, sy, outer, inner);
    go_to (mv->ox, max (mv->oy - b->h (), y + sy + inner->y1), false);
  }
  select_from_cursor_if_active ();
}

/******************************************************************************
* Adapt physical horizontal cursor movement to line breaking
******************************************************************************/

void
edit_cursor_rep::go_left () {
  go_horizontal (false);
}

void
edit_cursor_rep::go_right () {
  go_horizontal (true);
}

void
edit_cursor_rep::go_horizontal (bool forwards) {
  if (has_changed (THE_TREE+THE_ENVIRONMENT)) return;
  const path old_tp= copy (tp), parent= path_up (tp);
  const auto old_context= editor_rep::cursor_context_at (old_tp);
  if (!old_context) return;
  const bool source= old_context->effective_value (MODE) == "src" ||
                    old_context->effective_value (PREAMBLE) == "true";
  const std::optional<path> display= cursor_display_root == eb &&
      cursor_display_source == tp && !is_nil (cursor_display_path) ?
    std::optional<path> (cursor_display_path) : std::nullopt;
  const cursor old_cu= copy (cu), old_mv= copy (mv);
  const auto old_affinity= cu->affinity;
  const auto old_boundary= cursor_source_boundary ();
  const tree st= subtree (et, parent);
  bool has_accessible_child= false;
  if (!source && is_compound (st) && !with_admitted_navigation (*old_context,
      [&] (const cursor_navigation& navigation) {
        const auto entry= navigation.at (parent / rp);
        for (int i=0; i<N(st); ++i)
          has_accessible_child |= entry.contracts ().accessible (st, i);
      }, display)) return;
  adjust_ghost_cursor (VERTICAL);
  if (!cursor_move (forwards ? 1 : -1, 0)) { mv= old_mv; return; }
  const path near_border= parent * (forwards ? 0 : right_index (st));
  const path far_border= parent * (forwards ? right_index (st) : 0);
  const bool skipped_child= !source && has_accessible_child &&
    old_tp == near_border && tp == far_border;
  // Physical movement already admitted the hit and its field permissions.
  if ((tp == old_tp && (cu->affinity != old_affinity ||
                       cursor_source_boundary () != old_boundary)) ||
      (tp != old_tp && !skipped_child && inside_contiguous_document (et, old_tp, tp))) {
    notify_cursor_moved (HORIZONTAL);
    select_from_cursor_if_active ();
    return;
  }
  path target;
  if (!with_admitted_navigation (*old_context, [&] (const cursor_navigation& navigation) {
    tree body= subtree (et, rp);
    if (!source && !has_accessible_child && old_tp == near_border &&
        navigation.is_accessible_cursor (body, far_border / rp)) target= far_border;
    else target= make_cursor_accessible (
      rp * navigation.move_valid (body, old_tp / rp, forwards), forwards, navigation);
  }, display)) {
    tp= old_tp; cu= old_cu; mv= old_mv;
    remember_cursor_display (display ? *display : path ());
    return;
  }
  if (rp < target) go_to (target);
  else notify_cursor_moved (HORIZONTAL);
  select_from_cursor_if_active ();
}

void
edit_cursor_rep::go_start_line () {
  if (has_changed (THE_TREE+THE_ENVIRONMENT)) return;
  bool first_pass= true;
  path orig_tp= copy (tp);
  while (true) {
    cursor old_cu= copy (cu);
    cursor old_mv= copy (mv);
    path   old_tp= copy (tp);
    path old_display= cursor_display_path;
    const auto old_boundary= cursor_source_boundary ();
    adjust_ghost_cursor (VERTICAL);
    if (!cursor_move (-1, 0)) { mv= old_mv; return; }
    if (first_pass) notify_cursor_moved (HORIZONTAL);
    if ((tp == old_tp && cu->affinity == old_cu->affinity &&
         cursor_source_boundary () == old_boundary) ||
        !inside_same_or_more (et, tp, orig_tp, DOCUMENT)) {
      cu= old_cu;
      mv= old_mv;
      tp= old_tp;
      remember_cursor_display (old_display);
      notify_cursor_moved (HORIZONTAL);
      select_from_cursor_if_active ();
      return;
    }
    first_pass= false;
  }
}

void
edit_cursor_rep::go_end_line () {
  if (has_changed (THE_TREE+THE_ENVIRONMENT)) return;
  bool first_pass= true;
  path orig_tp= copy (tp);
  while (true) {
    cursor old_cu= copy (cu);
    cursor old_mv= copy (mv);
    path   old_tp= copy (tp);
    path old_display= cursor_display_path;
    const auto old_boundary= cursor_source_boundary ();
    adjust_ghost_cursor (VERTICAL);
    if (!cursor_move (1, 0)) { mv= old_mv; return; }
    if (first_pass) notify_cursor_moved (HORIZONTAL);
    if ((tp == old_tp && cu->affinity == old_cu->affinity &&
         cursor_source_boundary () == old_boundary) ||
        !inside_same_or_more (et, tp, orig_tp, DOCUMENT)) {
      cu= old_cu;
      mv= old_mv;
      tp= old_tp;
      remember_cursor_display (old_display);
      notify_cursor_moved (HORIZONTAL);
      select_from_cursor_if_active ();
      return;
    }
    first_pass= false;
  }
}

/******************************************************************************
* Logical cursor changes
******************************************************************************/

void
edit_cursor_rep::adjust_cursor () {
  path sp= find_innermost_scroll (eb, tp);
  cursor mv= copy (cu);
  SI dx= PIXEL << 8, ddelta= 0;
  path p= tree_path (sp, mv->ox, mv->oy, mv->delta);
  if (is_nil (p)) return;
  if (p != tp) {
    // cout << "Cursors don't match\n";
    while (dx != 0 || ddelta != 0) {
      // cout << "  " << tp << ", " << p << "\n";
      p= tree_path (sp, mv->ox, mv->oy, mv->delta);
      if (is_nil (p)) return;
      int eps= (path_inf (p, tp)? 1: -1);
      if (p == tp) eps= (mv->ox < cu->ox? 1: -1);
      if (p == tp && mv->ox == cu->ox) eps= (mv->delta < cu->delta? 1: -1);
      if (dx > 0) {
        const path candidate= p != tp ? p : tree_path (sp, mv->ox + eps * dx, mv->oy, mv->delta);
        if (is_nil (candidate)) return;
        if (p != tp || candidate == tp)
          mv->ox += eps * dx;
        dx >>= 1;
        if (dx == 0) ddelta= DELTA;
      }
      else if (ddelta > 0) {
        const path candidate= p != tp ? p : tree_path (sp, mv->ox, mv->oy, mv->delta + eps * ddelta);
        if (is_nil (candidate)) return;
        if (p != tp || candidate == tp)
          mv->delta += eps * ddelta;
        ddelta >>= 1;
      }
    }
  }
  if (p == tp) cu= mv;
}

void
edit_cursor_rep::go_to_here () {
  cu= current_display_cursor (cu->affinity);
  const auto context= editor_rep::cursor_context_at (tp);
  path corrected= tp;
  if (context && with_admitted_navigation (*context, [&] (const cursor_navigation& navigation) {
    tree body= subtree (et, rp);
    if (!cu->valid || !navigation.valid_cursor (body, corrected / rp))
      corrected= rp * navigation.super_correct (body, corrected / rp);
    if (!cu->valid || !navigation.valid_cursor (body, corrected / rp))
      corrected= make_cursor_accessible (corrected, false, navigation);
  }) && corrected != tp) {
    tp= corrected;
    cu= current_display_cursor ();
  }
  if (cu->valid) adjust_cursor ();
  if (mv_status == DIRECT) {
    mv= copy (cu);
    if (mv->slope > 0 &&
        is_atomic (subtree (et, path_up (tp)))) {
      mv->ox += (SI) round (mv->slope * min (mv->y1, -PIXEL));
      mv->delta = 0;
    }
  }
  if (cu->valid) prepare_cursor_environment ();
  notify_change (THE_CURSOR);
  if (cu->valid && cursor_context_at (tp, cursor_source_boundary ()))
    call ("notify-cursor-moved", object (DIRECT));
}

void
edit_cursor_rep::go_to (path p) {
  bool must_move= tp != p;
  if (rp <= p) {
    //if (tp != p) cout << "Go to " << p << "\n";
    if (must_move) cu->affinity= athena::text::caret_affinity::downstream;
    tp= p;
    mv_status= DIRECT;
    if (!has_changed (THE_TREE+THE_ENVIRONMENT)) {
      cu= current_display_cursor (cu->affinity);
      if (cu->valid) adjust_cursor ();
      mv= copy (cu);
      if (mv->slope > 0 &&
          is_atomic (subtree (et, path_up (tp)))) {
        mv->ox += (SI) round (mv->slope * min (mv->y1, -PIXEL));
        mv->delta = 0;
      }
    }
    if (cu->valid && !has_changed (THE_TREE+THE_ENVIRONMENT))
      prepare_cursor_environment ();
    if (must_move)
      notify_change (THE_CURSOR, THE_FREEZE);
    else
      notify_change (THE_CURSOR);
    // A mutation may leave a valid old box but no current source context.
    // apply_changes() sends the hook through go_to_here() after layout.
    if (cu->valid && cursor_context_at (tp, cursor_source_boundary ()))
      call ("notify-cursor-moved", object (DIRECT));
  }
}

void
edit_cursor_rep::go_to_correct (path p) {
  // Mutation observers need structural correction before a new layout exists.
  // Invocation-specific borders are applied by go_to_here after admission.
  const cursor_navigation structural ({athena::contracts::builtin_contracts ()});
  go_to (rp * structural.correct_cursor (subtree (et, rp), p / rp));
}

void
edit_cursor_rep::go_to_start (path p) {
  go_to (start (et, p));
}

void
edit_cursor_rep::go_to_end (path p) {
  go_to (end (et, p));
}

void
edit_cursor_rep::go_to_border (path p, bool at_start) {
  if (at_start) go_to_start (p);
  else go_to_end (p);
}

void
edit_cursor_rep::go_start () {
  go_to (correct_cursor (et, rp * 0));
  select_from_cursor_if_active ();
}

void
edit_cursor_rep::go_end () {
  go_to (correct_cursor (et, rp * 1));
  select_from_cursor_if_active ();
}

void
edit_cursor_rep::go_start_paragraph () {
  path p= search_parent_upwards (DOCUMENT);
  go_to (start (et, p));
  select_from_cursor_if_active ();
}

void
edit_cursor_rep::go_end_paragraph () {
  path p= search_parent_upwards (DOCUMENT);
  go_to (end (et, p));
  select_from_cursor_if_active ();
}

void
edit_cursor_rep::go_start_of (tree_label what) {
  path p= search_upwards (what);
  if (!is_nil (p)) go_to (start (et, p));
}

void
edit_cursor_rep::go_end_of (tree_label what) {
  path p= search_upwards (what);
  if (!is_nil (p)) go_to (end (et, p));
}

void
edit_cursor_rep::go_start_with (string var, string val) {
  path p= search_upwards_with (var, val);
  if (!is_nil (p)) go_to (start (et, p));
}

void
edit_cursor_rep::go_end_with (string var, string val) {
  path p= search_upwards_with (var, val);
  if (!is_nil (p)) go_to (end (et, p));
}

/******************************************************************************
* Jumping to a label
******************************************************************************/

tree
edit_cursor_rep::get_labels () {
  tree r (TUPLE);
  hashmap<string,tree> h= buf->data->ref;
  iterator<string> it= iterate (h);
  while (it->busy ()) {
    string ref= it->next ();
    r << ref;
  }
  return r;
}

static path
search_label (tree t, string which) {
  if (is_atomic (t)) return path ();
  else if (t == tree (LABEL, which)) return path (1);
  else if (is_compound (t, "tag", 2) && t[0] == which)
    return path (1, start (t[1]));
  else {
    int i, n=N(t);
    for (i=0; i<n; i++) {
      path q= search_label (t[i], which);
      if (!is_nil (q)) return path (i, q);
    }
    return path ();
  }
}

bool
edit_cursor_rep::cursor_is_accessible () {
  const auto boundary= cursor_source_boundary ();
  const auto context= editor_rep::cursor_context_at (tp);
  bool accessible= false;
  if (!context || !with_admitted_navigation (*context, [&] (const cursor_navigation& navigation) {
    tree body= subtree (et, rp);
    accessible= boundary && cursor_occurrence_admitted (tp, boundary) ?
      navigation.is_mapped_cursor (body, tp / rp) : navigation.is_accessible_cursor (body, tp / rp);
  })) throw std::runtime_error ("Cursor accessibility is awaiting layout");
  return accessible;
}

path
edit_cursor_rep::search_label (string s, bool local) {
  path p= ::search_label (subtree (et, rp), s);
  if (!is_nil (p)) return (rp * p);
  if (!is_nil (eb)) {
    p= eb->find_tag (s);
    if (!is_nil (p) &&
        !is_func (subtree (et, path_up (p)), INCLUDE))
      return p;
  }
  tree val= buf->data->ref[s];
  url u;
  if (is_func (val, TUPLE, 3) && is_atomic (val[2]) &&
      !starts (val[2]->label, "#"))
    u= relative (buf->name, url (val[2]->label));
  if (local || is_none (u)) return path ();
  if (u != buf->name) {
    url vw= get_passive_view (u);
    if (is_none (vw)) return path ();
    return view_to_editor (vw) -> search_label (s, true);
  }
  return path ();
}

void
edit_cursor_rep::go_to_label (string s) {
  path p= search_label (s, true);
  if (!is_nil (p)) {
    go_to (p);
    show_cursor_if_hidden ();
    return;
  }
  tree val= buf->data->ref[s];
  if (is_func (val, TUPLE, 3) && is_atomic (val[2])) {
    string extra= val[2]->label;
    if (starts (extra, "#")) {
      string part= extra (1, N (extra));
      int i= search_forwards (".", part);
      if (i >= 0) part= part (0, i);
      string show= "(show-hidden-part " * scm_quote (part) * ")";
      string jump= "(go-to-label " * scm_quote (s) * ")";
      exec_delayed (scheme_cmd ("(if " * show * " (delayed " * jump * "))"));
    }
    else {
      url u= relative (buf->name, url (extra));
      if (u != buf->name) {
        string new_buf = scm_quote (as_string (u));
        string load_buf= "(load-buffer (system->url " * new_buf * "))";
        string jump_to = "(go-to-label " * scm_quote (s) * ")";
        exec_delayed (scheme_cmd ("(begin " * load_buf * " " * jump_to * ")"));
      }
    }
  }
}

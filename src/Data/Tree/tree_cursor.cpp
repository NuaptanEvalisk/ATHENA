
/******************************************************************************
* MODULE     : tree_cursor.cpp
* DESCRIPTION: abstract cursor handling
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "cursor_navigation.hpp"
#include "analyze.hpp"
#include "utf8_edit.hpp"
#include "vars.hpp"

using athena::contracts::access_policy;

/******************************************************************************
* Finding a closest cursor inside a tree
******************************************************************************/

bool
is_inside (tree t, path p) {
  if (is_nil (p)) return false;
  else if (is_atomic (t)) {
    return is_atom (p) && utf8_grapheme_boundary (t->label, p->item);
  }
  else if (is_atom (p))
    return p->item == 0 || p->item == 1;
  else if (is_func (t, RAW_DATA, 1))
    return p == path (0, 0);
  else return p->item >= 0 && p->item < N(t) &&
              is_inside (t[p->item], p->next);
}

path
closest_inside (tree t, path p) {
  // Arbitrary paths may be outside the tree.
  // This routine returns a closest path to p inside the tree t
  if (is_nil (p)) return path (0);
  else if (is_atomic (t)) {
    return path (utf8_grapheme_snap (t->label, p->item, true));
  }
  else if (is_atom (p) || p->item < 0 || p->item >= N(t))
    return path (max (0, min (1, p->item)));
  else if (is_func (t, RAW_DATA, 1))
    return path (0, 0);
  else if (is_func (t, AROUND, 3) && p->item == 0) return path (0);
  else if (is_func (t, AROUND, 3) && p->item == 2) return path (1);
  else return path (p->item, closest_inside (t[p->item], p->next));
}

bool
cursor_navigation::is_modified_accessible (tree t, path p, bool activate, bool persistent,
                                           bool mapped) const {
  if (p->item != 0) return false;
  t= t[0]; p= p->next;
  if (is_atomic (t)) return accessible_cursor (t, p, mapped);
  else if (is_atom (p) && !persistent) return false;
  else {
    const auto local= query.with_access (activate? access_policy::normal: access_policy::source);
    if (persistent) return cursor_navigation (local).accessible_cursor (t, p, mapped);
    return (mapped ? local.mapped_accessible (t, p->item) : local.accessible (t, p->item)) &&
      accessible_cursor (t[p->item], p->next, mapped);
  }
}

static bool
next_without_border (tree t, path p) {
  return false;
}

int
cursor_navigation::lowest_accessible_child (tree t) const {
  for (int i=0; i<N(t); i++)
    if (query.accessible (t, i))
      return i;
  return 0;
}

int
cursor_navigation::highest_accessible_child (tree t) const {
  for (int i=N(t)-1; i>=0; i--)
    if (query.accessible (t, i))
      return i;
  return N(t) - 1;
}

static bool
graphics_in_path (tree t, path p) {
  // FIXME: when in the cursor is inside graphics,
  // it cannot be at the start/end.  There should be
  // a more robust way to ensure this.
  if (is_nil (p) || is_atom (p)) return false;
  if (is_atomic (t) || p->item < 0 || p->item >= N(t)) return false;
  if (is_func (t, GRAPHICS)) return true;
  return graphics_in_path (t[p->item], p->next);
}

bool
cursor_navigation::is_accessible_cursor (tree t, path p) const {
  return accessible_cursor (t, p, false);
}

bool
cursor_navigation::is_mapped_cursor (tree t, path p) const {
  return is_inside (t, p) && accessible_cursor (t, p, true);
}

bool
cursor_navigation::accessible_cursor (tree t, path p, bool mapped) const {
  if (is_atomic (t) || is_atom (p)) {
    if (query.input_only && query.access != access_policy::source)
      return false;
    else if (is_atomic (t))
      return is_inside (t, p);
    else return !query.child_enforcing (t);
  }
  else if (0 > p->item || p->item >= N(t)) return false;
  else if (query.parent_enforcing (t) &&
           query.access != access_policy::source &&
           !graphics_in_path (t, p) &&
           ((p->item == lowest_accessible_child (t) &&
             p->next == start (t[p->item])) ||
            (p->item == highest_accessible_child (t) &&
             p->next == end (t[p->item]))))
    return false;
  else switch (L(t)) {
    case CONCAT:
      if (!accessible_cursor (t[p->item], p->next, mapped)) return false;
      else return !next_without_border (t, p);
    case ACTIVE:
      return is_modified_accessible (t, p, true, false, mapped);
    case VAR_ACTIVE:
      return is_modified_accessible (t, p, true, true, mapped);
    case INACTIVE:
      return is_modified_accessible (t, p, false, false, mapped);
    case VAR_INACTIVE:
      return is_modified_accessible (t, p, false, true, mapped);
    default:
      if (!(mapped ? query.mapped_accessible (t, p->item) : query.accessible (t, p->item)))
        return false;
      const auto child= query.inside (t, p->item, mapped);
      if (is_func (t, RAW_DATA, 1))
        return p == path (0, 0) && (!child.input_only || child.access == access_policy::source);
      return cursor_navigation (child).accessible_cursor (t[p->item], p->next, mapped);
    }
}

path
cursor_navigation::closest_accessible (tree t, path p, int dir) const {
  // Given a path p inside t, the path may be unaccessible
  // This routine returns the closest path to p inside t which is accessible
  // dir in {-1, 0, 1} specifies the search direction
  // The routine returns nil if there exists no accessible path inside t
  if (is_atomic (t)) return p;
  else if (is_nil (p)) return closest_accessible (t, path (0), dir);
  else if (is_atom (p) && !query.child_enforcing (t)) return p;
  else if (is_func (t, AROUND) && (p->item == 0 || p->item == 2)) {
    if (p->item == 0 && dir < 0) return path (0);
    if (p->item == 2 && dir > 0) return path (1);
    if (p->item == 0) return path (1, start (t[1]));
    if (p->item == 2) return path (1, end (t[1]));
  }
  else {
    int i, k= p->item, n= N(t);
    if (p == path (1)) k= max (0, n-1);
    for (i=0; i<n; i++) {
      int j;
      if (dir == 0)
        j= ((i&1) == 0? (k+(i>>1) % n): (k+n-((i+1)>>1) % n));
      else if (dir == 1) {
        j= k+i;
        if (j >= n) {
          if (query.child_enforcing (t)) break;
          return path (1);
        }
      }
      else {
        j= k-i;
        if (j < 0) {
          if (query.child_enforcing (t)) break;
          return path (0);
        }
      }
      if (query.accessible (t, j)) {
        // FIXME: certain tags modify source accessability props
        // FIXME: cells with non-trivial span may lead to unaccessability
        // FIXME: very dynamic markup should be treated after typesetting
        if (is_atom (p) && is_atomic (t[j]))
          return path (j, p->item * (j < k? N (t[j]->label): 0));
        int  ind = right_index (t[j]);
        path sp  = (j == k? p->next: (j < k? path (ind): path (0)));
        int  sdir= (j == k? dir: (j < k? -1: 1));
        path prop= (p == path (0)? p: path (ind));
        path sp2 = (is_nil (sp)? prop: sp);
        path r   = closest_accessible (t[j], sp2, sdir);
        if (!is_nil (r)) {
          r= path (j, r);
          if (!is_concat (t) || !next_without_border (t, r)) {
            if (query.parent_enforcing (t) &&
                !graphics_in_path (t, p) &&
                !is_accessible_cursor (t, p)) {
              if (r->item == lowest_accessible_child (t)) return path (0);
              if (r->item == highest_accessible_child (t)) return path (1);
            }
            return r;
          }
        }
      }
    }
    return path ();
  }
}

path
cursor_navigation::closest_accessible_inside (tree t, path p, int dir) const {
  path q= closest_accessible (t, p, dir);
  if (!is_nil (q)) return q;
  if (dir <= 0) return path (0);
  else return path (right_index (t));
}

/******************************************************************************
* Shifting the cursor
******************************************************************************/

bool
is_shifted (tree t, path p, int dir= -1, bool flag= false) {
  if (dir == 0) return true;
  else if (is_atom (p)) {
    if (flag) {
      if (dir < 0) return p->item != 0;
      else return p->item != right_index (t);
    }
    else return true;
  }
  else if (is_concat (t)) {
    int  i    = p->item;
    bool sflag= flag || (dir<0? i>0: i<N(t)-1);
    return is_shifted (t[i], p->next, dir, sflag);
  }
  else return is_shifted (t[p->item], p->next, dir, false);
}

path
cursor_navigation::extreme (tree t, int dir) const {
  if (dir < 0) return start (t);
  else return end (t);
}

path
cursor_navigation::shift (tree t, path p, int dir) const {
  if (dir == 0 || is_nil (p) || is_atom (p)) return p;
  else if (is_concat (t) && p->next == extreme (t[p->item], dir)) {
    for (int i= p->item + dir; i >= 0 && i < N(t); i += dir)
      if (query.accessible (t, i))
        return path (i, extreme (t[i], -dir));
    return p;
  }
  else return path (p->item, shift (t[p->item], p->next, dir));
}

/******************************************************************************
* Subroutines for cursor paths in trees
******************************************************************************/

bool
cursor_navigation::valid_cursor (tree t, path p, bool start_flag) const {
  if ((!is_nil (p)) && (!is_atom (p)) &&
      ((p->item < 0) || (p->item >= arity (t)))) {
    failed_error << "Testing valid cursor " << p << " in " << t << "\n";
    FAILED ("bad path");
  }

  if (is_nil (p)) return false;
  if (is_atomic (t) && !is_inside (t, p)) return false;
  if (is_atom (p)) {
    if (query.child_enforcing (t)) return false;
    if (start_flag) return (p->item!=0);
    return true;
  }
  if (query.parent_enforcing (t) &&
      !graphics_in_path (t, p) &&
      ((p->item == lowest_accessible_child (t) &&
        p->next == start (t[p->item])) ||
       (p->item == highest_accessible_child (t) &&
        p->next == end (t[p->item]))))
    return false;
  if (is_concat (t)) {
    if (next_without_border (t, p)) return false;
    return valid_cursor (t[p->item], p->next, start_flag || (p->item!=0));
  }
  if (is_mod_active_once (t))
    return is_atomic (t[0]) || (!is_atom (p->next));
  if (is_prime (t)) return false;
  // FIXME: hack for treating VAR_EXPAND "math"
  if (is_compound (t, "input", 2) && (N(p) == 2) &&
      is_compound (t[1], "math", 1) && (p->item == 1))
    return false;
  if (is_func (t, BIG_AROUND) && p->item == 1) {
    if (p == path (1, 0) && is_right_script_prime (t[1])) return false;
    if (p == path (1, 0, 0) && is_concat (t[1]) &&
        N(t[1]) > 0 && is_right_script_prime (t[1][0])) return false;
  }
  if (is_func (t, RAW_DATA, 1)) return p == path (0, 0);
  return valid_cursor (t[p->item], p->next, false);
}

static path
label_boundary_for_invalid_cursor (tree t, path p) {
  if (is_nil (p)) return path (0);
  if (is_atom (p)) return path (p->item <= 0 ? 0 : 1);
  if (p->item < 0) return path (0);
  if (p->item >= arity (t)) return path (1);
  if (is_atomic (t[p->item]) && is_atom (p->next))
    return path (p->next->item <= 0 ? 0 : 1);
  return path (p->item <= 0 ? 0 : 1);
}

static bool
invalid_cursor_inside_label (tree t, path p) {
  if (!is_func (t, LABEL) || is_nil (p)) return false;
  if (is_atom (p)) return p->item < 0 || p->item > 1;
  if (p->item < 0 || p->item >= arity (t)) return true;
  return is_atomic (t[p->item]) && is_atom (p->next) &&
         (p->next->item < 0 || p->next->item > N(t[p->item]->label));
}

path
cursor_navigation::pre_correct (tree t, path p) const {
  //cout << "Precorrect " << p << " in " << t << "\n";
  if (invalid_cursor_inside_label (t, p)) {
    // Labels are zero-width anchors.  Stale cursor/selection/scroll positions
    // may land inside their atomic payload after structural edits; normalize
    // those positions to the nearest label boundary instead of treating this as
    // a document warning.
    return label_boundary_for_invalid_cursor (t, p);
  }
  if ((!is_nil (p)) && (!is_atom (p)) && ((p->item < 0) || (p->item >= arity (t)))) {
    if (is_func (t, GRAPHICS)) {
      std_warning << "Precorrecting " << p << " in " << t << "\n";
      p= path (1);
    }
    else {
      failed_error << "Precorrecting " << p << " in " << t << "\n";
      FAILED ("bad path");
    }
  }

  if (is_nil (p)) return pre_correct (t, path (0));
  if (is_atom (p)) {
    if (!is_atomic (t) && query.child_enforcing (t)) {
      if (p->item == 0) {
        for (int i=0; i<N(t); i++)
          if (i == N(t)-1 || query.accessible (t, i))
            return path (i, pre_correct (t[i], path (0)));
      }
      else {
        for (int i=N(t)-1; i>=0; i--)
          if (i == 0 || query.accessible (t, i))
            return path (i, pre_correct (t[i], path (right_index (t[i]))));
      }
      FAILED ("nullary tree with no border");
    }
    if (is_atomic (t) && (p->item < 0 || p->item > N(t->label))) {
      std_warning << "Precorrecting " << p << " in " << t << "\n";
      FAILED ("bad path");
    }
    return p;
  }
  if (is_mod_active_once (t) && is_compound (t[0]) && is_atom (p->next)) {
    if (N (t[0]) == 0) return path (0);
    t= t[0]; p= p->next;
    if (p->item==0) return path (0, path (0, pre_correct (t[0], path (0))));
    else {
      int l=N(t)-1;
      return path (0, path (l, pre_correct (t[l], path (right_index (t[l])))));
    }
  }
  if (is_prime (t)) {
    if (p->next->item == 0) return path (0);
    else return path (1);
  }
  // FIXME: hack for treating VAR_EXPAND "math"
  if (is_compound (t, "input", 2) && (N(p) == 2) &&
      is_compound (t[1], "math", 1) && (p->item == 1))
    {
      int i= (p->next->item == 0? 0: right_index (t[1][0]));
      return path (1, 0, pre_correct (t[1][0], path (i)));
    }
  path r (p->item, pre_correct (t[p->item], p->next));
  if (query.parent_enforcing (t) &&
      !graphics_in_path (t, p)) {
    if (r->item == lowest_accessible_child (t) &&
        !valid_cursor (t, p, false))
      return path (0);
    if (r->item == highest_accessible_child (t) &&
        !valid_cursor (t, p, false))
      return path (1);      
  }
  return r;
}

static bool
left_most (tree t, path p) {
  if (is_nil (p)) FAILED ("invalid nil path");
  if ((!is_atom (p)) && ((p->item < 0) || (p->item >= arity (t)))) {
    if (is_func (t, LABEL)) return false;
    failed_error << "Left most " << p << " in " << t << "\n";
    FAILED ("bad path");
  }

  int i=p->item;
  if (is_atom (p)) return i==0;
  if (is_concat (p)) return (i==0) && left_most (t[0], p->next);
  return false;
}

path
cursor_navigation::left_correct (tree t, path p) const {
  //cout << "Left correct " << p << " in " << t << "\n";
  if (is_nil (p)) FAILED ("invalid nil path");
  if (invalid_cursor_inside_label (t, p))
    return label_boundary_for_invalid_cursor (t, p);
  if ((!is_atom (p)) && ((p->item < 0) || (p->item >= arity (t)))) {
    failed_error << "Left correcting " << p << " in " << t << "\n";
    FAILED ("bad path");
  }
  if (is_atom (p) && is_atomic (t) &&
      (p->item < 0 || p->item > N(t->label))) {
    failed_error << "Left correcting " << p << " in " << t << "\n";
    FAILED ("bad path");
  }

  int i=p->item;
  if (is_atom (p)) return p;
  if (is_concat (t) && (i>0) && left_most (t[i], p->next))
    return path (i-1, pre_correct (t[i-1], path (right_index (t[i-1]))));
  if (is_prime (t)) return path (0);
  return path (i, left_correct (t[i], p->next));
}

static bool
right_most (tree t, path p) {
  if (is_nil (p)) FAILED ("invalid nil path");
  if ((!is_atom (p)) && ((p->item < 0) || (p->item >= arity (t)))) {
    if (is_func (t, LABEL)) return false;
    failed_error << "Right most " << p << " in " << t << "\n";
    FAILED ("bad path");
  }

  int i=p->item;
  if (is_atom (p)) return i==right_index (t);
  if (is_concat (p)) return (i==1) && right_most (t[N(t)-1], p->next);
  return false;
}

path
cursor_navigation::right_correct (tree t, path p) const {
  if (is_nil (p)) FAILED ("invalid nil path");
  if (invalid_cursor_inside_label (t, p))
    return label_boundary_for_invalid_cursor (t, p);
  if ((!is_atom (p)) && ((p->item < 0) || (p->item >= arity (t)))) {
    failed_error << "Right correcting " << p << " in " << t << "\n";
    FAILED ("bad path");
  }
  if (is_atom (p) && is_atomic (t) &&
      (p->item < 0 || p->item > N(t->label))) {
    failed_error << "Right correcting " << p << " in " << t << "\n";
    FAILED ("bad path");
  }

  int i=p->item;
  if (is_atom (p)) return p;
  if (is_concat (t) && (i<N(t)-1) && right_most (t[i], p->next))
    return path (i+1, pre_correct (t[i+1], path (0)));
  if (is_prime (t)) return path (1);
  return path (i, right_correct (t[i], p->next));
}

static path
keep_positive (path p) {
  if (is_nil (p)) return p;
  if (p->item < 0) return path ();
  return path (p->item, keep_positive (p->next));
}

/******************************************************************************
* Exported routines for cursor paths in trees
******************************************************************************/

path
cursor_navigation::correct_cursor (tree t, path p, bool forwards) const {
  //cout << "Correct cursor " << p << " in " << t << ", " << forwards << "\n";
  p= keep_positive (p);
  path pp= pre_correct (t, p);
  const path parent= path_up (pp);
  const tree leaf= subtree (t, parent);
  const bool raw= !is_nil (parent) &&
    is_func (subtree (t, path_up (parent)), RAW_DATA);
  if (is_atomic (leaf) && !raw)
    pp= parent * utf8_grapheme_snap (leaf->label, last_item (pp), forwards);
  if (forwards) return right_correct (t, pp);
  else return left_correct (t, pp);
}

path
cursor_navigation::start (tree t, path p) const {
  //cout << "Start " << p << " in " << t << "\n";
  if ((!is_nil (p)) && (arity (parent_subtree (t, p)) == 0)) return p;
  return correct_cursor (t, p * 0);
}

path
cursor_navigation::end (tree t, path p) const {
  //cout << "End " << p << " in " << t << "\n";
  if ((!is_nil (p)) && (arity (parent_subtree (t, p)) == 0)) return p;
  return correct_cursor (t, p * right_index (subtree (t, p)));
}

path cursor_navigation::start (tree t) const { return start (t, path ()); }
path cursor_navigation::end (tree t) const { return end (t, path ()); }

path
cursor_navigation::up_correct (tree t, path p, bool active) const {
  //cout << "Up correct " << p << " in " << t << "\n";
  if (is_nil (p)) return p;
  if ((p->item<0) || (p->item>=N(t))) return path ();
  if (active && !query.accessible (t, p->item)) return path ();
  return path (p->item,
               up_correct (t[p->item], p->next, !is_mod_active_once (t)));
}

path
cursor_navigation::super_correct (tree t, path p, bool forwards) const {
  path q= path_up (p);
  path r= up_correct (t, q);
  if (q != r) {
#ifdef SANITY_CHECKS
    ASSERT (!is_nil (r), "unexpected situation");
#else
    if (is_nil (r)) return path (0);
#endif
    int last= (forwards? 1: 0);
    if (is_atomic (subtree (t, r))) p= path_up (r) * last;
    else p= r * last;
  }
  return correct_cursor (t, p, forwards);
}


/******************************************************************************
* MODULE     : tree_traverse.cpp
* DESCRIPTION: abstract cursor movement and tree traversal
* COPYRIGHT  : (C) 2005  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "tree_traverse.hpp"
#include "Data/NodeContracts/editing_query.hpp"
#include "cursor_navigation.hpp"
#include "Data/NodeContracts/execution_state.hpp"
#include "analyze.hpp"
#include "utf8_edit.hpp"
#include "hashset.hpp"
#include "scheme.hpp"
#include "Data/NodeContracts/node_groups.hpp"
#include "Data/NodeContracts/builtin_contracts.hpp"
#include "Data/NodeContracts/native_fields.hpp"

/******************************************************************************
* Accessability
******************************************************************************/

bool
exists_accessible_inside (tree t,
    const athena::contracts::source_contract_preparation& prepare, path source) {
  if (is_atomic (t)) return true;
  // Native editability is independent of styles. Only non-native fields need
  // an evaluated entry; ordinary deletion must not prepare a whole buffer.
  using namespace athena::contracts;
  const bool native= native_fields (L(t)) != nullptr;
  const auto context= native ? nullptr : prepare (source);
  const editing_query query {native ? builtin_contracts () : *context};
  if (!query.child_enforcing (t)) return true;
  for (int i=0; i<N(t); i++)
    if (query.accessible (t, i) &&
        exists_accessible_inside (t[i], prepare, source * i))
      return true;
  return false;
}

/******************************************************************************
* Traversal of a tree
******************************************************************************/

path
cursor_navigation::move_any (tree t, path p, bool forward) const {
  path q = path_up (p);
  int  l = last_item (p);
  tree st= subtree (t, q);
  if (!is_nil (q) && is_func (subtree (t, path_up (q)), RAW_DATA)) {
    if (forward) return path_up (q) * 1;
    else return path_up (q) * 0;
  }
  if (is_atomic (st)) {
    string s= st->label;
#ifdef SANITY_CHECKS
    ASSERT (l >= 0 && l <= N(s), "out of range");
#else
    l= max (min (l, N(s)), 0);
#endif
    if (forward) {
      if (l<N(s)) {
        l= utf8_grapheme_next (s, l);
        return q * l;
      }
    }
    else {
      if (l>0) {
        l= utf8_grapheme_previous (s, l);
        return q * l;
      }
    }
  }
  else if ((forward && l==0) || (!forward && l==1)) {
    const auto entry= at (q);
    int i, n= N(st);
    if (forward) {
      for (i=0; i<n; i++)
        if (entry.contracts ().accessible (st, i))
          return q * path (i, 0);
    }
    else {
      for (i=n-1; i>=0; i--)
        if (entry.contracts ().accessible (st, i))
          return q * path (i, right_index (st[i]));
    }
    return q * (1-l);
  }
  else if (is_nil (q)) return p;

  l = last_item (q);
  q = path_up (q);
  st= subtree (t, q);
  const auto parent= at (q);
  int i, n= N(st);
  if (forward) {
    for (i=l+1; i<n; i++)
      if (parent.contracts ().accessible (st, i))
        return q * path (i, 0);
  }
  else {
    for (i=l-1; i>=0; i--)
      if (parent.contracts ().accessible (st, i)) {
        return q * path (i, right_index (st[i]));
    }
  }
  return q * (forward? 1: 0);
}

/******************************************************************************
* Traversal of a valid cursor positions inside a tree
******************************************************************************/

path
closest_up (tree t, path p) {
  if (is_atomic (t)) {
    if (is_nil (p)) return path (0);
    else return path (utf8_grapheme_snap (t->label, p->item, false));
  }
  else if (is_func (t, RAW_DATA, 1) && !is_atom (p)) return path (0, 0);
  else if (is_concat (t) || is_document (t)) {
    if (N(t) == 0) return path (0);
    else if (is_nil (p) || is_atom (p) || p->item < 0)
      return path (0, closest_up (t[0], path ()));
    else if (p->item >= N(t))
      return path (N(t)-1, closest_up (t[N(t)-1], path ()));
    else return path (p->item, closest_up (t[p->item], p->next));
  }
  else {
    if (is_nil (p)) return path (0);
    else if (is_atom (p)) return path (max (0, min (p->item, 1)));
    else if (p->item < 0) return path (0);
    else if (p->item >= N(t)) return path (1);
    else return path (p->item, closest_up (t[p->item], p->next));
  }
}

static path
move_valid_sub (const cursor_navigation& navigation, tree t, path p, bool forward) {
#ifdef SANITY_CHECKS
  ASSERT (is_inside (t, p), "invalid cursor [move_valid]");
#else
  if (!is_inside (t, p)) p= closest_up (t, p);
#endif
  path q= p;
  while (true) {
    path r= navigation.move_any (t, q, forward);
    if (r == q) return p;
    if (navigation.valid_cursor (t, r)) return r;
    q= r;
  }
}

static bool
inside_graphics (tree t, path p) {
  if (is_func (t, GRAPHICS)) return true;
  if (is_nil (p) || is_nil (p->next)) return false;
  return inside_graphics (t[p->item], p->next);
}

path
cursor_navigation::move_valid (tree t, path p, bool forward) const {
  // NOTE: extra hook for moving inside graphical text
  auto traversal= *this;
  if (!path_accessible (t, p))
    traversal.query.access= athena::contracts::access_policy::source;
  path q= move_valid_sub (traversal, t, p, forward);
  if (!inside_graphics (t, p)) return q;
  if (traversal.inside_contiguous_document (t, p, q)) return q;
  return false;
}


path
cursor_navigation::move_accessible (tree t, path p, bool forward) const {
#ifdef SANITY_CHECKS
  ASSERT (is_inside (t, p), "invalid cursor [move_accessible]");
#else
  if (!is_inside (t, p)) p= closest_up (t, p);
#endif
  path q= p;
  while (true) {
    path r= move_any (t, q, forward);
    if (r == q) return p;
    if (is_accessible_cursor (t, r)) return r;
    q= r;
  }
}


/******************************************************************************
* Word based traversal of a tree
******************************************************************************/

static inline bool
is_iso_alphanum (char c) {
  return is_iso_alpha (c) || is_digit (c);
}

static bool
at_border (tree t, path p, bool forward) {
  tree st= subtree (t, path_up (p));
  int l= last_item (p), n= N(st);
  if (!is_concat (st) && !is_document (st)) return true;
  if ((forward && l!=n-1) || (!forward && l!=0)) return false;
  return at_border (t, path_up (p), forward);
}

static bool
next_is_word (tree t, path p) {
  tree st= subtree (t, path_up (p));
  int l= last_item (p), n= N(st);
  if (!is_concat (st) || l+1 >= n) return false;
  if (is_compound (st[l+1])) return true;
  return st[l+1] != "" && is_iso_alphanum (st[l+1]->label[0]);
}

path
cursor_navigation::move_word (tree t, path p, bool forward) const {
  while (true) {
    path q= move_accessible (t, p, forward);
    int l= last_item (q);
    if (q == p) return p;
    tree st= subtree (t, path_up (q));
    if (is_atomic (st)) {
      string s= st->label;
      int n= N(s);
      if (s == "") return q;
      if (forward && l>0 &&
          (is_iso_alphanum (s[l-1]) || (l<n && s[l] == ' ') ||
           (l==n && at_border (t, path_up (q), forward))) &&
          (l==n || !is_iso_alphanum (s[l])))
        return q;
      if (!forward && l<n &&
          (is_iso_alphanum (s[l]) || (l>0 && s[l-1] == ' ') ||
           (l==0 && at_border (t, path_up (q), forward))) &&
          (l==0 || !is_iso_alphanum (s[l-1])))
        return q;
      if (!forward && l==n && next_is_word (t, path_up (q)))
        return q;
    }
    else {
      if (forward && l==1) return q;
      if (!forward && l==0) return q;
      if (!forward && next_is_word (t, path_up (q)))
        return q;
    }
    p= q;
  }
}

/******************************************************************************
* Node based traversal of a tree
******************************************************************************/

path
cursor_navigation::move_node (tree t, path p, bool forward) const {
  tree st= subtree (t, path_up (p));
  if (is_atomic (st)) {
    if (forward) p= path_up (p) * N (st->label);
    else p= path_up (p) * 0;
  }
  p= move_accessible (t, p, forward);
  st= subtree (t, path_up (p));
  if (is_atomic (st) && last_item (p) > 0)
    p= path_up (p) * N (st->label);
  return p;
}

/******************************************************************************
* Tag based traversal of a tree
******************************************************************************/

static int
tag_border (const cursor_navigation& navigation, tree t, path p) {
  const path source= path_up (p);
  const tree st= subtree (t, source);
  if (is_compound (st) && navigation.at (source).contracts ().no_fields_editable (L(st))) {
    if (last_item (p) == 0) return -1;
    if (last_item (p) == 1) return 1;
  }
  return 0;
}

static bool
distinct_tag_or_argument (const cursor_navigation& navigation,
                          tree t, path p, path q, hashset<int> labs) {
  path c= common (p, q);
  path r= path_up (q);
  if (labs->contains ((int) L (subtree (t, r))) && tag_border (navigation, t, q) > 0)
    return true;
  while (!is_nil (r) && (r != c)) {
    r= path_up (r);
    if (labs->contains ((int) L (subtree (t, r))) &&
        !navigation.at (r).contracts ().no_fields_editable (L(subtree (t, r))))
      return true;
  }
  return false;
}

static bool
acceptable_border (const cursor_navigation& navigation,
                   tree t, path p, path q, hashset<int> labs) {
  if (tag_border (navigation, t, q) == 0) return true;
  if (!labs->contains ((int) L (subtree (t, path_up (q))))) return true;
  if (tag_border (navigation, t, q) < 0) return false;
  return tag_border (navigation, t, p) > 0;
}

static bool
acceptable_child (const cursor_navigation& navigation, tree t, path p, hashset<int> labs) {
  tree st= subtree (t, path_up (p));
  if (is_compound (st) && labs->contains ((int) L(st)))
    if (last_item (p) == 0 || last_item (p) == 1)
      return true;
  p= path_up (p);
  while (!is_nil (p)) {
    st= subtree (t, path_up (p));
    if (labs->contains ((int) L(st)))
      if (navigation.at (path_up (p)).contracts ().accessible (st, last_item (p)))
        return true;
    p= path_up (p);
  }
  return false;
}

static int
tag_index (tree t, path p, hashset<int> labs) {
  p= path_up (p);
  while (!is_nil (p)) {
    if (labs->contains ((int) L (subtree (t, path_up (p)))))
      return last_item (p);
    else p= path_up (p);
  }
  return -1;
}

static bool
tag_inside (tree t, path p, hashset<int> labs) {
  return !labs->contains ((int) L (subtree (t, path_up (p))));
}

static hashset<int> get_labels (scheme_tree t);

path
cursor_navigation::move_tag (tree t, path p, scheme_tree labels,
                             bool forward, bool preserve) const {
  const auto labs= get_labels (labels);
  path q= p;
  while (true) {
    path r= move_node (t, q, forward);
    if (r == q) return p;
    if (!tag_inside (t, r, labs) && !tag_inside (t, p, labs) &&
        last_item (r) == last_item (p))
      return r;
    if (distinct_tag_or_argument (*this, t, p, r, labs) &&
        acceptable_border (*this, t, p, r, labs) &&
        acceptable_child (*this, t, r, labs) &&
        tag_inside (t, r, labs) == tag_inside (t, p, labs) &&
        (!preserve || tag_index (t, r, labs) == tag_index (t, p, labs)))
      return r;
    q= r;
  }
}

static hashset<int>
get_labels (scheme_tree t) {
  hashset<int> labs;
  if (is_atomic (t))
    labs->insert ((int) as_tree_label (t->label));
  else {
    int i, n= N(t);
    for (i=0; i<n; i++)
      if (is_atomic (t[i]))
        labs->insert ((int) as_tree_label (t[i]->label));
  }
  return labs;
}

/******************************************************************************
* Traverse the children of a node
******************************************************************************/

path
cursor_navigation::move_argument (tree t, path p, bool forward) const {
  path q = path_up (p);
  int  l = last_item (p);
  tree st= subtree (t, q);
  const auto entry= at (q);
  int i, n= N(st);
  if (forward) {
    for (i=l+1; i<n; i++)
      if (entry.contracts ().accessible (st, i))
        return start (t, q * i);
  }
  else {
    for (i=l-1; i>=0; i--)
      if (entry.contracts ().accessible (st, i))
        return end (t, q * i);
  }
  return path ();
}

/******************************************************************************
* Other routines
******************************************************************************/

path
search_common (tree t, path p, path q, tree_label which) {
  if (is_atomic (t)) return path ();
  if (is_nil (p) || is_nil (q) || p->item != q->item) return path ();
  if (0 > p->item || p->item >= N(t)) return path ();
  path c= search_common (t[p->item], p->next, q->next, which);
  if (L(subtree (t[p->item], c)) == which) return path (p->item, c);
  return path ();
}

bool
no_other (tree t, path p, tree_label which) {
  if (L(t) == which) return false;
  if (is_nil (p)) return true;
  if (0 > p->item || p->item >= N(t)) return false;
  return no_other (t[p->item], p->next, which);
}

bool
inside_same (tree t, path p, path q, tree_label which) {
  path d= search_common (t, path_up (p), path_up (q), which);
  if (L(subtree (t, d)) != which) return true;
  path sp= p / d;
  path sq= q / d;
  if (is_atom (sp) || is_atom (sq)) return false;
  if (sp->item != sq->item) return false;
  if (0 > sp->item || sp->item >= N(subtree (t, d))) return false;
  tree st= subtree (t, d) [sp->item];
  return no_other (st, path_up (sq->next), which) ||
         no_other (st, path_up (sp->next), which);
}

bool
inside_same_or_more (tree t, path p, path q, tree_label which) {
  path d= search_common (t, path_up (p), path_up (q), which);
  if (L(subtree (t, d)) != which) return true;
  path sp= p / d;
  path sq= q / d;
  if (is_atom (sp) || is_atom (sq)) return false;
  if (sp->item != sq->item) return false;
  if (0 > sp->item || sp->item >= N(subtree (t, d))) return false;
  tree st= subtree (t, d) [sp->item];
  return no_other (st, path_up (sq->next), which);
}

bool
is_boundary (tree t, path p) {
  if (is_func (subtree (t, p), DOCUMENT)) return true;
  if (is_func (subtree (t, p), GRAPHICS)) return true;
  return false;
}

bool
cursor_navigation::inside_contiguous_document (tree t, path op, path oq) const {
  if (!inside_same (t, op, oq, DOCUMENT)) return false;
  path p= path_up (op), q= path_up (oq);
  while (!is_nil (p) && !is_boundary (t, p)) p= path_up (p);
  while (!is_nil (q) && !is_boundary (t, q)) q= path_up (q);
  if (p == q) return true;
  if (q <= p) return inside_contiguous_document (t, oq, op);
  if (!(p <= q)) return false;
  const auto local= at (q);
  const tree source= subtree (t, q);
  if (path_less (op, oq))
    return (oq / q) == local.start (source);
  else
    return (oq / q) == local.end (source);
}

/******************************************************************************
* Find sections in document
******************************************************************************/

// BufferActors traverse sections independently. Even contains() walks
// reference-counted list buckets, so these lazy caches cannot be shared.
static thread_local hashset<tree_label> section_traverse_tags;
static thread_local hashset<tree_label> section_tags;

void
init_sections () {
  if (N(section_traverse_tags) == 0) {
    section_traverse_tags= hashset<tree_label> ();
    section_traverse_tags->insert (DOCUMENT);
    section_traverse_tags->insert (CONCAT);
    section_traverse_tags->insert (make_tree_label ("ignore"));
    section_traverse_tags->insert (make_tree_label ("show-part"));
    section_traverse_tags->insert (make_tree_label ("hide-part"));
  }
  if (N(section_tags) == 0) {
    for (const auto* group: {"section-tag", "section*-tag"})
      for (const auto& name: athena::contracts::standard_node_groups ().members (group))
        section_tags->insert (as_tree_label (string (name.c_str ())));
  }
}

path
previous_section_impl (tree t, path p) {
  if (is_atomic (t)) return path ();
  else if (N(t) == 1 && section_tags->contains (L(t)))
    return p * 0;
  else if (section_traverse_tags->contains (L(t))) {
    int i= is_nil (p)? N(t)-1: p->item;
    for (; i>=0; i--) {
      if (!is_nil (p) && i == p->item) {
        path r= previous_section_impl (t[i], p->next);
        if (!is_nil (r)) return path (i, r);
      }
      else {
        path r= previous_section_impl (t[i], path ());
        if (!is_nil (r)) return path (i, r);
      }
    }
  }
  return path ();
}

path
previous_section (tree t, path p) {
  init_sections ();
  path r= previous_section_impl (t, p);
  if (is_nil (r)) return p;
  return path_up (r);
}

void
search_sections (array<tree>& a, tree t) {
  if (is_atomic (t)) return;
  else if (N(t) == 1 && section_tags->contains (L(t))) a << t;
  else if (section_traverse_tags->contains (L(t)))
    for (int i=0; i<N(t); i++)
      search_sections (a, t[i]);
}

array<tree>
search_sections (tree t) {
  init_sections ();
  array<tree> a;
  search_sections (a, t);
  return a;
}

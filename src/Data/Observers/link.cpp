
/******************************************************************************
* MODULE     : hard_link.cpp
* DESCRIPTION: Persistent hard_links between trees
* COPYRIGHT  : (C) 2006  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "modification.hpp"
#include "link.hpp"
#include "iterator.hpp"
#include "vars.hpp"
#include "boot.hpp"
#include "new_document.hpp"
#include <mutex>

// Live loci belong to the document's execution owner, just like its observers.
static thread_local hashmap<string,list<observer> > id_resolve;
static thread_local hashmap<observer,list<string> > pointer_resolve;

// Repositories own the links. The shared index only borrows them while locked;
// copying soft_link handles here would race their owner-local reference counts.
static std::mutex link_mutex;
static hashmap<tree,list<const soft_link_rep*> > vertex_occurrences;
static hashmap<string,int> type_count (0);

static std::mutex visited_mutex;
static hashset<string> visited_table;

/******************************************************************************
* Soft links
******************************************************************************/

void
register_pointer (string id, observer which) {
  // cout << "Register: " << id << " -> " << which << "\n";
  // cout << "Register: " << id << " -> " << obtain_tree (which) << "\n";
  list<observer>& l1= id_resolve (id);
  l1= list<observer> (which, l1);
  list<string>& l2= pointer_resolve (which);
  l2= list<string> (id, l2);
}

void
unregister_pointer (string id, observer which) {
  // cout << "Unregister: " << id << " -> " << which << "\n";
  // cout << "Unregister: " << id << " -> " << obtain_tree (which) << "\n";
  list<observer>& l1= id_resolve (id);
  l1= remove (l1, which);
  if (is_nil (l1)) id_resolve->reset (id);
  list<string>& l2= pointer_resolve (which);
  l2= remove (l2, id);
  if (is_nil (l2)) pointer_resolve->reset (which);
}

static void
register_vertex (const tree& v, const soft_link_rep* ln) {
  // A new key must not retain an actor-owned tree after its actor unregisters.
  if (!vertex_occurrences->contains (v))
    vertex_occurrences (copy (v))= list<const soft_link_rep*> (ln);
  else {
    list<const soft_link_rep*>& l= vertex_occurrences (v);
    l= list<const soft_link_rep*> (ln, l);
  }
}

static void
unregister_vertex (const tree& v, const soft_link_rep* ln) {
  list<const soft_link_rep*>& l= vertex_occurrences (v);
  l= remove (l, ln);
  if (is_nil (l)) vertex_occurrences->reset (v);
}

static void
register_link (const soft_link& ln) {
  std::lock_guard<std::mutex> lock (link_mutex);
  // cout << "Register: " << ln->t << "\n";
  int i, n= N(ln->t);
  if (is_atomic (ln->t[0]))
    type_count (ln->t[0]->label) ++;
  for (i=1; i<n; i++)
    register_vertex (ln->t[i], ln.operator->());
}

static void
unregister_link (const soft_link& ln) {
  std::lock_guard<std::mutex> lock (link_mutex);
  // cout << "Unregister: " << ln->t << "\n";
  int i, n= N(ln->t);
  if (is_atomic (ln->t[0])) {
    type_count (ln->t[0]->label) --;
    if (type_count (ln->t[0]->label) == 0)
      type_count->reset (ln->t[0]->label);
  }
  for (i=1; i<n; i++)
    unregister_vertex (ln->t[i], ln.operator->());
}

/******************************************************************************
* Link repositories
******************************************************************************/

link_repository_rep::link_repository_rep () {}

link_repository_rep::~link_repository_rep () {
  while (!is_nil (loci)) {
    tree t= obtain_tree (loci->item);
    unregister_pointer (ids->item, loci->item);
    detach_observer (t, loci->item);
    ids= ids->next;
    loci= loci->next;
  }
  while (!is_nil (links)) {
    unregister_link (links->item);
    links= links->next;
  }
}

void
link_repository_rep::insert_locus (string id, tree t) {
  observer obs= tree_pointer (t, true);
  register_pointer (id, obs);
  attach_observer (t, obs);
  ids= list<string> (id, ids);
  loci= list<observer> (obs, loci);
}

void
link_repository_rep::insert_locus (string id, tree t, string cb) {
  observer obs= scheme_observer (t, cb);
  register_pointer (id, obs);
  attach_observer (t, obs);
  ids= list<string> (id, ids);
  loci= list<observer> (obs, loci);
}

void
link_repository_rep::insert_link (soft_link ln) {
  register_link (ln);
  links= list<soft_link> (ln, links);
}

/******************************************************************************
* Routines for navigation
******************************************************************************/

list<string>
get_ids (list<observer> l) {
  if (is_nil (l)) return list<string> ();
  return pointer_resolve [l->item] * get_ids (l->next);
}

list<string>
get_ids (tree t) {
  if (is_nil (t->obs)) return list<string> ();
  list<observer> l= t->obs->get_tree_pointers ();
  return reverse (get_ids (l));
}

list<tree>
as_trees (list<observer> l) {
  if (is_nil (l)) return list<tree> ();
  else return list<tree> (obtain_tree (l->item), as_trees (l->next));
}

list<tree>
get_trees (string id) {
  return reverse (as_trees (id_resolve [id]));
}

static list<tree>
as_tree_list (list<const soft_link_rep*> l) {
  if (is_nil (l)) return list<tree> ();
  // Returned metadata may outlive the lock and its originating repository.
  else return list<tree> (copy (l->item->t), as_tree_list (l->next));
}

list<tree>
get_links (tree v) {
  std::lock_guard<std::mutex> lock (link_mutex);
  return reverse (as_tree_list (vertex_occurrences [v]));
}

list<string>
all_link_types () {
  std::lock_guard<std::mutex> lock (link_mutex);
  list<string> l;
  iterator<string> it= iterate (type_count);
  while (it->busy()) {
    string s= it->next();
    l= list<string> (s, l);
  }
  return l;
}

/******************************************************************************
* Locus rendering
******************************************************************************/

void
set_locus_rendering (string var, string val) {
  set_user_preference (var, val);
}

string
get_locus_rendering (string var) {
  if (var == "locus-on-paper")
    return get_user_preference (var, "change");
  if (var == LOCUS_COLOR)
    return get_user_preference (var, "#404080");
  if (var == VISITED_COLOR)
    return get_user_preference (var, "#702070");
  if (var == RADIOACTIVE_LINK_COLOR)
    return get_user_preference (var, "#a04400");
  return "";
}

void
declare_visited (string id) {
  std::lock_guard<std::mutex> lock (visited_mutex);
  visited_table->insert (id);
}

bool
has_been_visited (string id) {
  std::lock_guard<std::mutex> lock (visited_mutex);
  return visited_table->contains (id);
}

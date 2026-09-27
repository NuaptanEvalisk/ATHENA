
/******************************************************************************
* MODULE     : modification.hpp
* DESCRIPTION: elementary tree modifications
* COPYRIGHT  : (C) 2008  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "modification.hpp"
#include "node_metadata.hpp"

tree
node_header (const tree& t) {
  tree r= is_atomic (t)? tree (""): tree (L(t));
  athena::node::copy_metadata (t, r);
  return r;
}

void
apply_node_header (tree& t, const tree& header) {
  ASSERT (is_atomic (t) == is_atomic (header), "incompatible node header");
  if (is_compound (t)) LR(t)= L(header);
  athena::node::copy_metadata (header, t);
}

modification
mod_set_metadata (path p, tree carrier) {
  tree data (TUPLE);
  athena::node::copy_metadata (carrier, data);
  return modification (MOD_SET_METADATA, p, data);
}

bool
restores_child_header (modification mod) {
  return (mod->k == MOD_INSERT_NODE && is_func (mod->t, UNINIT, 2)) ||
    (mod->k == MOD_REMOVE_NODE && is_func (mod->t, TUPLE, 1));
}

tree
single_node_header (modification mod) {
  ASSERT ((mod->k == MOD_JOIN || mod->k == MOD_REMOVE_NODE) &&
          is_func (mod->t, TUPLE, 1), "single node header expected");
  return mod->t[0];
}

tree
inserted_node_template (modification mod) {
  ASSERT (mod->k == MOD_INSERT_NODE, "insert_node modification expected");
  return restores_child_header (mod)? mod->t[0]: mod->t;
}

static bool
compatible_header (tree source, tree header) {
  if (is_generic (header) || is_atomic (source) != is_atomic (header)) return false;
  return is_atomic (header)? header->label == "": N(header) == 0;
}

bool
has_node_headers (modification mod) {
  if (mod->k == MOD_SPLIT) return is_func (mod->t, TUPLE, 2);
  if (mod->k == MOD_JOIN) return is_func (mod->t, TUPLE, 1);
  return false;
}

static bool
empty_header_payload (tree payload) {
  return is_atomic (payload) && payload->label == "" &&
    !athena::node::get (payload);
}

static bool
metadata_content_empty (tree t) {
  if (is_atomic (t)) return N(t->label) == 0;
  if (L(t) != CONCAT && L(t) != DOCUMENT) return false;
  for (int i=0; i<N(t); i++)
    if (!metadata_content_empty (t[i])) return false;
  return true;
}

static bool
metadata_fragment_empty (tree t, int begin, int end) {
  if (begin == end) return true;
  if (is_atomic (t)) return false;
  if (L(t) != CONCAT && L(t) != DOCUMENT) return false;
  for (int i=begin; i<end; i++)
    if (!metadata_content_empty (t[i])) return false;
  return true;
}

void
prepare_modification (tree t, modification mod) {
  if (mod->k == MOD_SPLIT && !has_node_headers (mod)) {
    tree source= subtree (t, root (mod) * index (mod));
    const auto* metadata= athena::node::get (source);
    if (!metadata) return;
    int length= is_atomic (source)? N(source->label): N(source);
    int at= argument (mod);
    bool keep_right= metadata_fragment_empty (source, 0, at) &&
      !metadata_fragment_empty (source, at, length);
    tree retained= node_header (source);
    // Duplicate only the header's identity graph, including rich-text properties.
    tree fresh= athena::node::duplicate (retained);
    mod->t= keep_right? tree (TUPLE, fresh, retained):
      tree (TUPLE, retained, fresh);
  }
  else if (mod->k == MOD_JOIN && !has_node_headers (mod)) {
    tree source= subtree (t, root (mod));
    tree left= source[index (mod)], right= source[index (mod)+1];
    if (athena::node::get (left) || athena::node::get (right) || L(left) != L(right))
      mod->t= tree (TUPLE, node_header (left));
  }
  else if (mod->k == MOD_REMOVE_NODE && !restores_child_header (mod)) {
    ASSERT (is_applicable (t, mod), "cannot discard annotated wrapper");
    tree source= subtree (t, root (mod));
    tree child= source[index (mod)];
    if (!athena::node::get (source)) return;
    tree header= node_header (child);
    if (N(source) == 1 && athena::node::get (source) &&
        !athena::node::get (child))
      athena::node::copy_metadata (source, header);
    mod->t= tree (TUPLE, header);
  }
}

/******************************************************************************
* Equality and Output
******************************************************************************/

bool
operator == (modification m1, modification m2) {
  return m1->k == m2->k && m1->p == m2->p && m1->t == m2->t;
}

bool
operator != (modification m1, modification m2) {
  return m1->k != m2->k || m1->p != m2->p || m1->t != m2->t;
}

tm_ostream&
operator << (tm_ostream& out, modification mod) {
  switch (mod->k) {
  case MOD_ASSIGN:
    return out << "assign (" << root (mod)
	       << ", " << mod->t << ")";
  case MOD_INSERT:
    return out << "insert (" << root (mod)
	       << ", " << index (mod) << ", " << mod->t << ")";
  case MOD_REMOVE:
    return out << "remove (" << root (mod)
	       << ", " << index (mod) << ", " << argument (mod) << ")";
  case MOD_SPLIT:
    return out << "split (" << root (mod)
	       << ", " << index (mod) << ", " << argument (mod) << ")";
  case MOD_JOIN:
    return out << "join (" << root (mod)
	       << ", " << index (mod) << ")";
  case MOD_ASSIGN_NODE:
    return out << "assign_node (" << root (mod)
	       << ", " << mod->t << ")";
  case MOD_INSERT_NODE:
    return out << "insert_node (" << root (mod)
	       << ", " << argument (mod) << ", " << mod->t << ")";
  case MOD_REMOVE_NODE:
    return out << "remove_node (" << root (mod)
	       << ", " << index (mod) << ")";
  case MOD_SET_CURSOR:
    return out << "set_cursor (" << root (mod)
	       << ", " << index (mod) << ", " << mod->t << ")";
  case MOD_SET_METADATA:
    return out << "set_metadata (" << root (mod) << ", " << mod->t << ")";
  default: FAILED ("invalid modification type");
    return out;
  }
}

/******************************************************************************
* Accessors
******************************************************************************/

path
root (modification mod) {
  switch (mod->k) {
  case MOD_ASSIGN: return mod->p;
  case MOD_INSERT: return path_up (mod->p);
  case MOD_REMOVE: return path_up (path_up (mod->p));
  case MOD_SPLIT: return path_up (path_up (mod->p));
  case MOD_JOIN: return path_up (mod->p);
  case MOD_ASSIGN_NODE: return mod->p;
  case MOD_INSERT_NODE: return path_up (mod->p);
  case MOD_REMOVE_NODE: return path_up (mod->p);
  case MOD_SET_CURSOR: return path_up (mod->p);
  case MOD_SET_METADATA: return mod->p;
  default: FAILED ("invalid modification type");
  }
  return path ();
}

int
index (modification mod) {
  switch (mod->k) {
  case MOD_INSERT: return last_item (mod->p);
  case MOD_REMOVE: return last_item (path_up (mod->p));
  case MOD_SPLIT: return last_item (path_up (mod->p));
  case MOD_JOIN: return last_item (mod->p);
  case MOD_REMOVE_NODE: return last_item (mod->p);
  case MOD_SET_CURSOR: return last_item (mod->p);
  default: FAILED ("invalid modification type");
  }
  return 0;
}

int
argument (modification mod) {
  switch (mod->k) {
  case MOD_REMOVE: return last_item (mod->p);
  case MOD_SPLIT: return last_item (mod->p);
  case MOD_INSERT_NODE: return last_item (mod->p);
  default: FAILED ("invalid modification type");
  }
  return 0;
}

tree_label
L (modification mod) {
  ASSERT (mod->k == MOD_ASSIGN_NODE, "assign_node modification expected");
  return L (mod->t);
}

/******************************************************************************
* Constructor and accessors for scheme interface
******************************************************************************/

modification
make_modification (string s, path p, tree t) {
  modification_type k= MOD_ASSIGN;
  if (s == "assign") k= MOD_ASSIGN;
  else if (s == "insert") k= MOD_INSERT;
  else if (s == "remove") k= MOD_REMOVE;
  else if (s == "split") k= MOD_SPLIT;
  else if (s == "join") k= MOD_JOIN;
  else if (s == "assign-node") k= MOD_ASSIGN_NODE;
  else if (s == "insert-node") k= MOD_INSERT_NODE;
  else if (s == "remove-node") k= MOD_REMOVE_NODE;
  else if (s == "set-cursor") k= MOD_SET_CURSOR;
  else if (s == "set-metadata") k= MOD_SET_METADATA;
  return modification (k, p, t);
}

string
get_type (modification mod) {
  switch (mod->k) {
  case MOD_ASSIGN: return "assign";
  case MOD_INSERT: return "insert";
  case MOD_REMOVE: return "remove";
  case MOD_SPLIT: return "split";
  case MOD_JOIN: return "join";
  case MOD_ASSIGN_NODE: return "assign-node";
  case MOD_INSERT_NODE: return "insert-node";
  case MOD_REMOVE_NODE: return "remove-node";
  case MOD_SET_CURSOR: return "set-cursor";
  case MOD_SET_METADATA: return "set-metadata";
  default: FAILED ("invalid modification type");
  }
  return "none";
}

path
get_path (modification mod) {
  return mod->p;
}

tree
get_tree (modification mod) {
  return mod->t;
}

/******************************************************************************
* Test applicability of modifications
******************************************************************************/

bool
can_assign (tree t, path p, tree u) {
  (void) u;
  return has_subtree (t, p);
}

bool
can_insert (tree t, path p, int pos, tree u) {
  if (!has_subtree (t, p)) return false;
  tree st= subtree (t, p);
  if (is_atomic (st)) return pos >= 0 && pos <= N(st->label) && is_atomic (u);
  else return pos >= 0 && pos <= N(st) && is_compound (u);
}

bool
can_remove (tree t, path p, int pos, int nr) {
  if (!has_subtree (t, p)) return false;
  tree st= subtree (t, p);
  if (is_atomic (st)) return pos >= 0 && pos+nr <= N(st->label);
  else return pos >= 0 && pos+nr <= N(st);
}

bool
can_split (tree t, path p, int pos, int at) {
  if (!has_subtree (t, p * pos)) return false;
  tree st= subtree (t, p * pos);
  if (is_atomic (st)) return at >= 0 && at <= N(st->label);
  else return at >= 0 && at <= N(st);
}

bool
can_join (tree t, path p, int pos) {
  if (!has_subtree (t, p)) return false;
  tree st= subtree (t, p);
  if (pos < 0 || pos+1 >= N(st)) return false;
  if (is_atomic (st[pos]) && is_atomic (st[pos+1])) return true;
  if (is_compound (st[pos]) && is_compound (st[pos+1])) return true;
  return false;
}

bool
can_assign_node (tree t, path p, tree_label op) {
  (void) op;
  return has_subtree (t, p) && is_compound (subtree (t, p));
}

bool
can_insert_node (tree t, path p, int pos, tree u) {
  return has_subtree (t, p) && is_compound (u) && pos >= 0 && pos <= N(u);
}

bool
can_remove_node (tree t, path p, int pos) {
  if (!has_subtree (t, p * pos)) return false;
  tree source= subtree (t, p);
  if (!athena::node::get (source)) return true;
  return N(source) == 1 && !athena::node::get (source[pos]);
}

bool
can_set_cursor (tree t, path p, int pos, tree data) {
  (void) data;
  if (!has_subtree (t, p)) return false;
  return pos >= 0 && pos <= right_index (subtree (t, p));
}

bool
is_applicable (tree t, modification mod) {
  switch (mod->k) {
  case MOD_ASSIGN:
    return can_assign (t, root (mod), mod->t);
  case MOD_INSERT:
    return can_insert (t, root (mod), index (mod), mod->t);
  case MOD_REMOVE:
    return can_remove (t, root (mod), index (mod), argument (mod));
  case MOD_SPLIT:
    if (!can_split (t, root (mod), index (mod), argument (mod))) return false;
    if (empty_header_payload (mod->t)) return true;
    if (!has_node_headers (mod)) return false;
    for (int i=0; i<2; i++) {
      tree source= subtree (t, root (mod) * index (mod));
      tree header= mod->t[i];
      if (!compatible_header (source, header)) return false;
    }
    return true;
  case MOD_JOIN:
    if (!can_join (t, root (mod), index (mod))) return false;
    if (!has_node_headers (mod)) return empty_header_payload (mod->t);
    return compatible_header (subtree (t, root (mod) * index (mod)),
                              single_node_header (mod));
  case MOD_ASSIGN_NODE:
    return can_assign_node (t, root (mod), L (mod));
  case MOD_INSERT_NODE:
    if (!can_insert_node (t, root (mod), argument (mod),
                          inserted_node_template (mod))) return false;
    return !restores_child_header (mod) ||
      compatible_header (subtree (t, root (mod)), mod->t[1]);
  case MOD_REMOVE_NODE:
    if (restores_child_header (mod))
      return has_subtree (t, root (mod) * index (mod)) &&
        compatible_header (subtree (t, root (mod) * index (mod)),
                           single_node_header (mod));
    return empty_header_payload (mod->t) &&
      can_remove_node (t, root (mod), index (mod));
  case MOD_SET_CURSOR:
    return can_set_cursor (t, root (mod), index (mod), mod->t);
  case MOD_SET_METADATA:
    return has_subtree (t, mod->p) &&
      !is_generic (subtree (t, mod->p)) &&
      L(subtree (t, mod->p)) != UNINIT && is_func (mod->t, TUPLE, 0);
  default:
    return false;
  }
}

/******************************************************************************
* Functional application of modifications
******************************************************************************/

tree
clean_assign (tree t, path p, tree u) {
  if (is_nil (p)) return copy (u);
  else {
    int i, j= p->item, n= N(t);
    if (j >= n) FAILED("clean_remove(): Invalid path."); //return copy(u);  // FIXME? check whether this is the right return value.
    tree r (t, n);
    athena::node::copy_metadata (t, r);
    for (i=0; i<j; i++) r[i]= t[i];
    r[j]= clean_assign (t[j], p->next, u);
    for (i++; i<n; i++) r[i]= t[i];
    return r;
  }
}

tree
clean_insert (tree t, path p, tree u) {
  if (is_nil (p->next) && is_atomic (t)) {
    string s= t->label;
    tree r= s (0, p->item) * u->label * s (p->item, N(s));
    athena::node::copy_metadata (t, r);
    return r;
  }
  else if (is_nil (p->next)) {
    int i, j= p->item, n= N(t), nr= N(u);
    tree r (t, n+nr);
    athena::node::copy_metadata (t, r);
    for (i=0; i<j; i++) r[i]= t[i];
    for (; i<n; i++) r[i+nr]= t[i];
    for (i=0; i<nr; i++) r[j+i]= copy (u[i]);
    return r;
  }
  else {
    int i, j= p->item, n= N(t);
    tree r (t, n);
    for (i=0; i<j; i++) r[i]= t[i];
    r[j]= clean_insert (t[j], p->next, u);
    for (i++; i<n; i++) r[i]= t[i];
    athena::node::copy_metadata (t, r);
    return r;
  }  
}

tree
clean_remove (tree t, path p, int nr) {
  if (is_nil (p->next) && is_atomic (t)) {
    string s= t->label;
    if (N(s) < p->item+nr)
      FAILED ("clean_remove: Invalid remove from atomic tree");
    tree r= s (0, p->item) * s (p->item+nr, N(s));
    athena::node::copy_metadata (t, r);
    return r;
  }
  else if (is_nil (p->next)) {
    int i, j= p->item, n= N(t);
    tree r (t, n-nr);
    athena::node::copy_metadata (t, r);
    for (i=0; i<j; i++) r[i]= t[i];
    for (i+=nr; i<n; i++) r[i-nr]= t[i];
    return r;
  }
  else {
    int i, j= p->item, n= N(t);
    if (j >= n) FAILED ("clean_remove: Invalid path"); //return t;  // FIXME? check whether this is the right return value.
    tree r (t, n);
    for (i=0; i<j; i++) r[i]= t[i];
    r[j]= clean_remove (t[j], p->next, nr);
    for (i++; i<n; i++) r[i]= t[i];
    athena::node::copy_metadata (t, r);
    return r;
  }  
}

tree
clean_split (tree t, path p, tree headers) {
  if (is_nil (p->next->next)) {
    tree u= t[p->item];
    int i, n1= p->next->item, n2= N(u)-n1;
    tree s1, s2;
    if (is_atomic (u)) {
      s1= u->label (0, n1);
      s2= u->label (n1, N(u->label));
    }
    else {
      s1= tree (u, n1);
      s2= tree (u, n2);
      for (i=0; i<n1; i++) s1[i]= u[i];
      for (i=0; i<n2; i++) s2[i]= u[n1+i];
    }
    if (is_func (headers, TUPLE, 2)) {
      apply_node_header (s1, headers[0]);
      apply_node_header (s2, headers[1]);
    }

    int j= p->item, n= N(t);
    tree r (t, n+1);
    athena::node::copy_metadata (t, r);
    for (i=0; i<j; i++) r[i]= t[i];
    r[j]= s1; r[j+1]= s2;
    for (i++; i<n; i++) r[i+1]= t[i];
    return r;
  }
  else {
    int i, j= p->item, n= N(t);
    tree r (t, n);
    for (i=0; i<j; i++) r[i]= t[i];
    r[j]= clean_split (t[j], p->next, headers);
    for (i++; i<n; i++) r[i]= t[i];
    athena::node::copy_metadata (t, r);
    return r;
  }  
}

tree
clean_join (tree t, path p, tree payload) {
  if (is_nil (p->next)) {
    int i, j= p->item;
    tree s1= t[j], s2= t[j+1], u;
    if (is_atomic (s1))
      u= tree (s1->label * s2->label);
    else {
      int n1= N(s1), n2= N(s2);
      u= tree (s1, n1+n2);
      for (i=0; i<n1; i++) u[i]= s1[i];
      for (i=0; i<n2; i++) u[n1+i]= s2[i];
    }
    if (is_func (payload, TUPLE, 1)) apply_node_header (u, payload[0]);

    int n= N(t);
    tree r (t, n-1);
    athena::node::copy_metadata (t, r);
    for (i=0; i<j; i++) r[i]= t[i];
    r[j]= u;
    for (i+=2; i<n; i++) r[i-1]= t[i];
    return r;
  }
  else {
    int i, j= p->item, n= N(t);
    tree r (t, n);
    for (i=0; i<j; i++) r[i]= t[i];
    r[j]= clean_join (t[j], p->next, payload);
    for (i++; i<n; i++) r[i]= t[i];
    athena::node::copy_metadata (t, r);
    return r;
  }  
}

tree
clean_assign_node (tree t, path p, tree_label op) {
  if (is_nil (p)) {
    int i, n= N(t);
    tree r (op, n);
    athena::node::copy_metadata (t, r);
    for (i=0; i<n; i++) r[i]= t[i];
    return r;
  }
  else {
    int i, j= p->item, n= N(t);
    tree r (t, n);
    for (i=0; i<j; i++) r[i]= t[i];
    r[j]= clean_assign_node (t[j], p->next, op);
    for (i++; i<n; i++) r[i]= t[i];
    athena::node::copy_metadata (t, r);
    return r;
  }  
}

static tree
clean_with_header (tree t, tree header) {
  if (L(t) == L(header) && athena::node::equal_metadata (t, header)) return t;
  tree r= is_atomic (t)? tree (t->label): tree (L(header), N(t));
  if (is_compound (t))
    for (int i=0; i<N(t); i++) r[i]= t[i];
  athena::node::copy_metadata (header, r);
  return r;
}

tree
clean_insert_node (tree t, path p, tree u, tree child_header, bool restore) {
  if (is_nil (p->next)) {
    int i, j= p->item, n= N(u);
    tree r (u, n+1);
    athena::node::copy_metadata (u, r);
    for (i=0; i<j; i++) r[i]= u[i];
    r[j]= restore? clean_with_header (t, child_header): t;
    for (; i<n; i++) r[i+1]= u[i];
    return r;
  }
  else {
    int i, j= p->item, n= N(t);
    tree r (t, n);
    for (i=0; i<j; i++) r[i]= t[i];
    r[j]= clean_insert_node (t[j], p->next, u, child_header, restore);
    for (i++; i<n; i++) r[i]= t[i];
    athena::node::copy_metadata (t, r);
    return r;
  }  
}

tree
clean_remove_node (tree t, path p, tree payload) {
  if (is_nil (p->next))
    return is_func (payload, TUPLE, 1)?
      clean_with_header (t[p->item], payload[0]): t[p->item];
  else {
    int i, j= p->item, n= N(t);
    tree r (t, n);
    for (i=0; i<j; i++) r[i]= t[i];
    r[j]= clean_remove_node (t[j], p->next, payload);
    for (i++; i<n; i++) r[i]= t[i];
    athena::node::copy_metadata (t, r);
    return r;
  }  
}

tree
clean_set_cursor (tree t, path p, tree data) {
  (void) p; (void) data;
  return t;
}

tree
clean_set_metadata (tree t, path p, tree carrier) {
  if (is_nil (p)) {
    tree r= is_atomic (t)? tree (t->label): tree (L(t), N(t));
    if (is_compound (t))
      for (int i=0; i<N(t); i++) r[i]= t[i];
    athena::node::copy_metadata (carrier, r);
    return r;
  }
  tree r (t, N(t));
  athena::node::copy_metadata (t, r);
  for (int i=0; i<N(t); i++)
    r[i]= i == p->item? clean_set_metadata (t[i], p->next, carrier): t[i];
  return r;
}

tree
clean_apply (tree t, modification mod) {
  ASSERT (is_applicable (t, mod), "invalid modification");
  prepare_modification (t, mod);
  switch (mod->k) {
  case MOD_ASSIGN:
    return clean_assign (t, mod->p, mod->t);
  case MOD_INSERT:
    return clean_insert (t, mod->p, mod->t);
  case MOD_REMOVE:
    return clean_remove (t, path_up (mod->p), last_item (mod->p));
  case MOD_SPLIT:
    return clean_split (t, mod->p, mod->t);
  case MOD_JOIN:
    return clean_join (t, mod->p, mod->t);
  case MOD_ASSIGN_NODE:
    return clean_assign_node (t, mod->p, L(mod));
  case MOD_INSERT_NODE:
    return clean_insert_node (t, mod->p, inserted_node_template (mod),
                             restores_child_header (mod)? mod->t[1]: mod->t,
                             restores_child_header (mod));
  case MOD_REMOVE_NODE:
    return clean_remove_node (t, mod->p, mod->t);
  case MOD_SET_CURSOR:
    return clean_set_cursor (t, mod->p, mod->t);
  case MOD_SET_METADATA:
    return clean_set_metadata (t, mod->p, mod->t);
  default:
    FAILED ("invalid modification type");
    return "";
  }
}

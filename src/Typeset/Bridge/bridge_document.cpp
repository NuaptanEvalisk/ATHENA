
/******************************************************************************
* MODULE     : bridge_document.cpp
* DESCRIPTION: Bridge between logical and physically typesetted document
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "bridge.hpp"
#include "scheme.hpp"
#include "tm_timer.hpp"

static string
athena_labels_mode (edit_env env) {
  if (env->get_string (PAGE_PRINTED) == "true") return "hidden";
  return get_preference ("vault labels mode", "visible");
}

bridge bridge_docrange (typesetter ttt, tree st, path ip, array<bridge>& brs,
			int begin, int end, bool divide);

class bridge_document_rep: public bridge_rep {
protected:
  array<bridge> brs;
  bridge acc; // binary splitting acceleration for long documents
  int progressive_end;
  bool progressive_finalized;
  bool progressive_pass_pending;

public:
  bridge_document_rep (typesetter ttt, tree st, path ip);
  void initialize ();
  void initialize_acc ();

  void notify_assign (path p, tree u);
  void notify_insert (path p, tree u);
  void notify_remove (path p, int nr);
  bool notify_macro  (int type, string var, int l, path p, tree u);
  void notify_change ();

  void my_exec_until (path p);
  bool my_typeset_will_be_complete ();
  void my_typeset (int desired_status);
};

bridge_document_rep::bridge_document_rep (typesetter ttt, tree st, path ip):
  bridge_rep (ttt, st, ip)
{
  initialize ();
}

void
bridge_document_rep::initialize () {
  int i, n= N(st);
  progressive_end= 0;
  progressive_finalized= false;
  progressive_pass_pending= false;
  brs= array<bridge> (n);
  for (i=0; i<n; i++)
    brs[i]= make_bridge (ttt, st[i], descend (ip, i));
  initialize_acc ();
}

void
bridge_document_rep::initialize_acc () {
  if (true || ttt->paper) acc= bridge ();
  else acc= bridge_docrange (ttt, st, ip, brs, 0, N(st), true);
}

bridge
bridge_document (typesetter ttt, tree st, path ip) {
  return tm_new<bridge_document_rep> (ttt, st, ip);
}

/******************************************************************************
* Event notification
******************************************************************************/

void
bridge_document_rep::notify_assign (path p, tree u) {
  // cout << "Assign " << p << ", " << u << " in " << st << "\n";
  ASSERT (!is_nil (p) || is_func (u, DOCUMENT) || is_func (u, PARA),
	  "nil path");
  if (is_nil (p)) {
    bool same_source= st == u;
    int previous_end= progressive_end;
    bool previous_finalized= progressive_finalized;
    st= u;
    initialize ();
    if (same_source) {
      progressive_end= min (previous_end, N(st));
      progressive_finalized=
        previous_finalized && progressive_end == N(st);
    }
  }
  else {
    if (is_atom (p)) {
      replace_bridge (brs[p->item], u, descend (ip, p->item));
      st= substitute (st, p->item, brs[p->item]->st);
    }
    else {
      brs[p->item]->notify_assign (p->next, u);
      st= substitute (st, p->item, brs[p->item]->st);
    }
    if (!is_nil (acc)) acc->notify_assign (p, u);
  }
  status= CORRUPTED;
}

void
bridge_document_rep::notify_insert (path p, tree u) {
  //cout << "Insert " << p << ", " << u << " in " << st << "\n";
  ASSERT (!is_nil (p), "nil path");
  if (is_atom (p)) {
    int i, j, n= N(brs), pos= p->item, nr= N(u);
    bool was_complete= progressive_end >= n;
    array<bridge> brs2 (n+nr);
    if (pos>0) brs[pos-1]->notify_change (); // touch in case of surroundings
    if (pos<n) brs[pos  ]->notify_change (); // touch in case of surroundings
    for (i=0; i<pos; i++) brs2[i]= brs[i];
    for (j=0; j<nr ; j++) brs2[i+j]= make_bridge (ttt, u[j], descend (ip,i+j));
    for (; i<n; i++) {
      brs2[i+nr]= brs[i];
      brs2[i+nr]->ip->item += nr;
    }
    brs= brs2;
    st = (st (0, p->item) * u) * st (p->item, N(st));
    if (was_complete) progressive_end= n + nr;
    else if (pos < progressive_end) progressive_end += nr;
    progressive_finalized= was_complete;
    if (!is_nil (acc)) acc->notify_insert (p, u);
    // initialize_acc ();
  }
  else {
    brs[p->item]->notify_insert (p->next, u);
    st= substitute (st, p->item, brs[p->item]->st);
    if (!is_nil (acc)) acc->notify_assign (p->item, st[p->item]);
  }
  status= CORRUPTED;
}

void
bridge_document_rep::notify_remove (path p, int nr) {
  // cout << "Remove " << p << ", " << nr << " in " << st << "\n";
  ASSERT (!is_nil (p), "nil path");
  if (is_atom (p)) {
    int i, n= N(brs), pos= p->item;
    bool was_complete= progressive_end >= n;
    array<bridge> brs2 (n-nr);
    for (i=0; i<pos ; i++) brs2[i]= brs[i];
    for (; i<n-nr; i++) {
      brs2[i]= brs[i+nr];
      brs2[i]->ip->item -= nr;
    }
    bool change_flag= false;
    for (i=pos; i<pos+nr; i++)
      change_flag |= !brs[i]->changes->empty();
    brs= brs2;
    n -= nr;
    st = st (0, pos) * st (pos+nr, N(st));
    if (was_complete) progressive_end= n;
    else if (pos < progressive_end)
      progressive_end= max (pos, progressive_end - nr);
    progressive_finalized= was_complete;
    if (pos>0) brs[pos-1]->notify_change (); // touch in case of surroundings
    if (pos<n) brs[pos  ]->notify_change (); // touch in case of surroundings
    if (change_flag) // touch brs[pos..n] for correct ``changes handling''
      for (i=pos; i<n; i++)
	brs[i]->notify_change ();
    if (!is_nil (acc)) acc->notify_remove (p, nr);
    // initialize_acc ();
  }
  else {
    brs[p->item]->notify_remove (p->next, nr);
    st= substitute (st, p->item, brs[p->item]->st);
    if (!is_nil (acc)) acc->notify_assign (p->item, st[p->item]);
  }
  status= CORRUPTED;
}

bool
bridge_document_rep::notify_macro (int tp, string var, int l, path p, tree u) {
  bool flag= false;
  int i, n= N(brs);
  for (i=0; i<n; i++)
    flag= brs[i]->notify_macro (tp, var, l, p, u) || flag;
  if (flag) {
    status= CORRUPTED;
    if (!is_nil (acc)) acc->notify_change ();
  }
  return flag;
}

void
bridge_document_rep::notify_change () {
  status= CORRUPTED;
  if (!is_nil (acc)) acc->notify_change ();
  if (N(brs)>0) brs[0]->notify_change ();
  if (N(brs)>1) brs[N(brs)-1]->notify_change ();
}

/******************************************************************************
* Typesetting
******************************************************************************/

void
bridge_document_rep::my_exec_until (path p) {
  if (is_nil (acc)) {
    int i;
    for (i=0; i<p->item; i++)
      brs[i]->exec_until (path (right_index (brs[i]->st)), true);
    if (i<N(st)) brs[i]->exec_until (p->next);
  }
  else acc->my_exec_until (p);
}

bool
bridge_document_rep::my_typeset_will_be_complete () {
  bool root_document= ttt->br.operator-> () == this;
  bool progressive= ttt->progressive && !ttt->paper &&
                    (root_document? N(st) >= 96:
                     ttt->progressive_root_active && N(st) >= 32);
  if (root_document) ttt->progressive_root_active= progressive;
  if (progressive && progressive_end < N(st)) return false;
  if (is_nil (acc)) {
    int i, n= N(brs);
    for (i=0; i<n; i++)
      if (!brs[i]->my_typeset_will_be_complete ()) return false;
    return true;
  }
  else return acc->my_typeset_will_be_complete ();
}

static bool
athena_document_child_visible (tree t, string mode, bool printed) {
  if (is_compound (t, "folded-hidden")) return false;
  return !(mode == "hidden" && !printed &&
           is_only_labels_and_white (t) && has_label (t));
}

static SI
athena_progressive_placeholder_height (array<page_item> lines,
                                       int completed, int total,
                                       edit_env env) {
  SI measured= 0;
  for (int i=0; i<N(lines); i++)
    if (lines[i]->type == PAGE_LINE_ITEM)
      measured += max ((SI) 1, lines[i]->b->h ()) + lines[i]->spc->def;
  SI fallback= max ((SI) 1, 2 * env->fn->yx);
  SI average= completed > 0? measured / completed: fallback;
  average= max (average, fallback);
  double estimate= ((double) average) * ((double) (total - completed));
  return (SI) min (estimate, (double) (1 << 29));
}

void
bridge_document_rep::my_typeset (int desired_status) {
  //cout << INDENT;
  if (is_nil (acc)) {
    int i, n= N(st);
    array<line_item> a= ttt->a;
    array<line_item> b= ttt->b;
    string mode = athena_labels_mode (env);
    bool printed= env->get_string (PAGE_PRINTED) == "true";
    bool root_document= ttt->br.operator-> () == this;
    bool progressive= ttt->progressive && !ttt->paper &&
                      (root_document? n >= 96:
                       ttt->progressive_root_active && n >= 32);
    if (root_document) ttt->progressive_root_active= progressive;
    progressive_pass_pending= false;

    int minimum= n;
    if (progressive) {
      if (root_document) ttt->progressive_initial= progressive_end == 0;
      minimum= ttt->progressive_advance? (root_document? 8: 1): 1;
      if (root_document)
        minimum= max (minimum, ttt->progressive_required + 1);
      minimum= max (minimum, progressive_end);
      minimum= min (minimum, n);
    }

    int first_visible = -1;
    int last_visible = -1;
    for (i=0; i<n; i++) {
      if (!athena_document_child_visible (st[i], mode, printed)) continue;
      if (first_visible == -1) first_visible = i;
      last_visible = i;
    }

    if (first_visible == -1) return;

    int end= 0;
    for (i=0; i<n; i++) {
      bool visible= athena_document_child_visible (st[i], mode, printed);
      if (!visible) {
        end= i + 1;
        continue;
      }
      //cout << "Typesetting " << st[i] << LF;
      int wanted= (!progressive && i==last_visible?
                   desired_status & WANTED_MASK: WANTED_PARAGRAPH);
      ttt->a= (i==first_visible  ? a: array<line_item> ());
      ttt->b= (!progressive && i==last_visible? b: array<line_item> ());
      brs[i]->typeset (PROCESSED+ wanted);
      end= i + 1;
      if (progressive && end >= minimum &&
          (!ttt->progressive_advance ||
           texmacs_time () >= ttt->progressive_deadline_ms))
        break;
    }

    if (progressive) {
      progressive_end= max (progressive_end, end);
      if (progressive_end < n) {
        SI height= athena_progressive_placeholder_height (
          ttt->l, progressive_end, n, env);
        array<page_item> placeholder (1);
        placeholder[0]= page_item (empty_box (
          decorate (ip), 0, -height, 0, 0));
        ttt->insert_stack (placeholder, stack_border ());
        progressive_pass_pending= true;
      }
      else if (!progressive_finalized) {
        progressive_pass_pending= true;
        progressive_finalized= true;
      }
      ttt->progressive_pending |= progressive_pass_pending;
      if (progressive_pass_pending)
        ttt->progressive_pending_generation++;
    }
    else {
      progressive_end= n;
      progressive_finalized= true;
    }
  }
  else acc->my_typeset (desired_status);
  //cout << UNINDENT;
}

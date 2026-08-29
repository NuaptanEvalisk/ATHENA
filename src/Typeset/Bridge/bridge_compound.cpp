
/******************************************************************************
* MODULE     : bridge_compound.cpp
* DESCRIPTION: Bridge between logical and physical long macro expansions
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "bridge.hpp"
#include "drd_std.hpp"
#include "enunciation_surround.hpp"
#include "radioactive_link_scope.hpp"
#include "scheme.hpp"

tree insert_at (tree, path, tree);
tree remove_at (tree, path, int);

static bool
athena_enunciation_surround_body_edit (tree st, path p) {
  if (!athena_is_enunciation_surround (st) || is_nil (p) ||
      is_nil (p->next))
    return false;
  int d= athena_enunciation_surround_delta (st);
  return p->item == d + 2;
}

static bool
athena_enunciation_surround_display (edit_env env, tree st) {
  if (!athena_is_enunciation_surround (st)) return false;
  int d= athena_enunciation_surround_delta (st);
  return athena_enunciation_starts_display (env, st[d + 2]);
}

static path
athena_enunciation_surround_body_path (bool display, path p) {
  return path (display? 1: 2, p->next);
}

static void
athena_mark_enunciation_background (
  array<page_item>& items, tree color, int alpha)
{
  brush bg (color, alpha);
  for (int i=0; i<N(items); i++)
    if (items[i]->type == PAGE_LINE_ITEM) {
      page_item item= copy (items[i]);
      if (item->block_bg->get_type () == brush_none)
        item->block_bg= bg;
      items[i]= item;
    }
}

class bridge_compound_rep: public bridge_rep {
protected:
  bool   valid;
  bridge body;
  int    delta;
  tree   fun;

public:
  bridge_compound_rep (typesetter ttt, tree st, path ip);
  void initialize (tree body_t, int delta, tree fun);

  void notify_assign (path p, tree u);
  void notify_insert (path p, tree u);
  void notify_remove (path p, int nr);
  bool notify_macro  (int type, string var, int level, path p, tree u);
  void notify_change ();

  bool my_typeset_will_be_complete ();
  void my_typeset (int desired_status);
};

bridge_compound_rep::bridge_compound_rep (typesetter ttt, tree st, path ip):
  bridge_rep (ttt, st, ip)
{
  valid= false;
}

void
bridge_compound_rep::initialize (tree body_t, int delta2, tree fun2) {
  if ((!valid) || (body->st != body_t) || (delta != delta2) || (fun != fun2)) {
    valid= true;
    if (is_nil (body)) body= make_bridge (ttt, attach_right (body_t, ip));
    else replace_bridge (body, attach_right (body_t, ip));
    delta= delta2;
    fun  = fun2;
  }
}

bridge
bridge_compound (typesetter ttt, tree st, path ip) {
  return tm_new<bridge_compound_rep> (ttt, st, ip);
}

/******************************************************************************
* Event notification
******************************************************************************/

void
bridge_compound_rep::notify_assign (path p, tree u) {
  // cout << "Assign " << p << ", " << u << " in " << st << "\n";
  ASSERT (!is_nil (p) || L(u) >= START_EXTENSIONS, "nil path");
  if (athena_is_enunciation_background (st)) {
    int d= athena_enunciation_background_delta (st);
    if (!is_nil (p) && p->item == d + 1 && !is_nil (p->next) &&
        !is_nil (body)) {
      body->notify_assign (p->next, u);
      st= substitute (st, d + 1, body->st);
    }
    else {
      st= substitute (st, p, u);
      valid= false;
    }
    status= CORRUPTED;
    return;
  }
  if (athena_is_enunciation_surround (st)) {
    bool old_display= athena_enunciation_surround_display (env, st);
    tree new_st= substitute (st, p, u);
    bool new_display= athena_enunciation_surround_display (env, new_st);
    if (valid && old_display == new_display &&
        athena_enunciation_surround_body_edit (st, p)) {
      body->notify_assign (
        athena_enunciation_surround_body_path (old_display, p), u);
      st = new_st;
      fun= athena_enunciation_surround_rewrite (env, st);
    }
    else {
      st= new_st;
      valid= false;
    }
    status= CORRUPTED;
    return;
  }
  if (athena_is_proof_qed_layout (st)) {
    st= substitute (st, p, u);
    valid= false;
    status= CORRUPTED;
    return;
  }
  if (is_nil (p) || (p->item == 0) || is_nil (body)) {
    st= substitute (st, p, u);
    valid= false;
  }
  else {
    // bool mp_flag= is_multi_paragraph (st);
    if (is_func (fun, XMACRO, 2))
      notify_macro (MACRO_ASSIGN, fun[0]->label, -1, p, u);
    else if (is_applicable (fun) && (p->item < N(fun)))
      notify_macro (MACRO_ASSIGN, fun[p->item-delta]->label, -1, p->next, u);
    st= substitute (st, p, u);
    // if (mp_flag != is_multi_paragraph (st)) valid= false;
  }
  status= CORRUPTED;
}

void
bridge_compound_rep::notify_insert (path p, tree u) {
  // cout << "Insert " << p << ", " << u << " in " << st << "\n";
  ASSERT (!is_nil (p), "nil path");
  if (athena_is_enunciation_background (st)) {
    int d= athena_enunciation_background_delta (st);
    if (!is_atom (p) && p->item == d + 1 && !is_nil (body)) {
      body->notify_insert (p->next, u);
      st= substitute (st, d + 1, body->st);
    }
    else {
      st= insert_at (st, p, u);
      valid= false;
    }
    status= CORRUPTED;
    return;
  }
  if (athena_is_enunciation_surround (st)) {
    bool old_display= athena_enunciation_surround_display (env, st);
    tree new_st= insert_at (st, p, u);
    bool new_display= athena_enunciation_surround_display (env, new_st);
    if (valid && old_display == new_display &&
        athena_enunciation_surround_body_edit (st, p)) {
      body->notify_insert (
        athena_enunciation_surround_body_path (old_display, p), u);
      st = new_st;
      fun= athena_enunciation_surround_rewrite (env, st);
    }
    else {
      st= new_st;
      valid= false;
    }
    status= CORRUPTED;
    return;
  }
  if (athena_is_proof_qed_layout (st)) {
    st= insert_at (st, p, u);
    valid= false;
    status= CORRUPTED;
    return;
  }
  if (is_atom (p) || is_nil (body)) bridge_rep::notify_insert (p, u);
  else {
    // bool mp_flag= is_multi_paragraph (st);
    if (is_func (fun, XMACRO, 2))
      notify_macro (MACRO_INSERT, fun[0]->label, -1, p, u);
    else if (is_applicable (fun) && (p->item < N(fun)))
      notify_macro (MACRO_INSERT, fun[p->item-delta]->label, -1, p->next, u);
    st= insert_at (st, p, u);
    // if (mp_flag != is_multi_paragraph (st)) valid= false;
  }
  status= CORRUPTED;
}

void
bridge_compound_rep::notify_remove (path p, int nr) {
  // cout << "Remove " << p << ", " << nr << " in " << st << "\n";
  ASSERT (!is_nil (p), "nil path");
  if (athena_is_enunciation_background (st)) {
    int d= athena_enunciation_background_delta (st);
    if (!is_atom (p) && p->item == d + 1 && !is_nil (body)) {
      body->notify_remove (p->next, nr);
      st= substitute (st, d + 1, body->st);
    }
    else {
      st= remove_at (st, p, nr);
      valid= false;
    }
    status= CORRUPTED;
    return;
  }
  if (athena_is_enunciation_surround (st)) {
    bool old_display= athena_enunciation_surround_display (env, st);
    tree new_st= remove_at (st, p, nr);
    bool new_display= athena_enunciation_surround_display (env, new_st);
    if (valid && old_display == new_display &&
        athena_enunciation_surround_body_edit (st, p)) {
      body->notify_remove (
        athena_enunciation_surround_body_path (old_display, p), nr);
      st = new_st;
      fun= athena_enunciation_surround_rewrite (env, st);
    }
    else {
      st= new_st;
      valid= false;
    }
    status= CORRUPTED;
    return;
  }
  if (athena_is_proof_qed_layout (st)) {
    st= remove_at (st, p, nr);
    valid= false;
    status= CORRUPTED;
    return;
  }
  if (is_atom (p) || is_nil (body)) bridge_rep::notify_remove (p, nr);
  else {
    // bool mp_flag= is_multi_paragraph (st);
    if (is_func (fun, XMACRO, 2))
      notify_macro (MACRO_REMOVE, fun[0]->label, -1, p, tree (as_string (nr)));
    else if (is_applicable (fun) && (p->item < N(fun)))
      notify_macro (MACRO_REMOVE, fun[p->item-delta]->label, -1, p->next,
		    tree (as_string (nr)));
    st= remove_at (st, p, nr);
    // if (mp_flag != is_multi_paragraph (st)) valid= false;
  }
  status= CORRUPTED;
}

bool
bridge_compound_rep::notify_macro (
  int type, string var, int l, path p, tree u)
{
  /*
  cout << "Macro argument " << var << " [action=" << type
       << ", level=" << l << "] " << p << ", " << u << " in " << st << "\n";
  */

  bool flag;
  if (athena_is_enunciation_background (st)) {
    int d= athena_enunciation_background_delta (st);
    bool color_dep= env->depends (st[d], var, l);
    bool body_dep = env->depends (st[d + 1], var, l);
    if (valid && body_dep && !color_dep && !is_nil (body)) {
      flag= body->notify_macro (type, var, l, p, u);
      status= CORRUPTED;
      return flag || body_dep;
    }
    valid= false;
    status= CORRUPTED;
    return body_dep || color_dep;
  }
  if (athena_is_enunciation_surround (st)) {
    int d= athena_enunciation_surround_delta (st);
    bool body_dep= env->depends (st[d + 2], var, l);
    bool wrapper_dep=
      env->depends (st[d], var, l) || env->depends (st[d + 1], var, l) ||
      (athena_enunciation_surround_has_color (st) &&
       env->depends (st[d + 3], var, l));
    if (valid && body_dep && !wrapper_dep && !is_nil (body)) {
      flag= body->notify_macro (type, var, l, p, u);
      status= CORRUPTED;
      return flag || body_dep;
    }
    valid= false;
    status= CORRUPTED;
    return body_dep || wrapper_dep;
  }
  if (athena_is_proof_qed_layout (st)) {
    int d= athena_proof_qed_layout_delta (st);
    bool dep= env->depends (st[d], var, l) ||
              env->depends (st[d + 1], var, l) ||
              env->depends (st[d + 2], var, l) ||
              env->depends (st[d + 3], var, l);
    valid= false;
    status= CORRUPTED;
    return dep;
  }
  if (valid) {
    int i, n=N(fun)-1, m=N(st);
    env->macro_arg= list<hashmap<string,tree> > (
      hashmap<string,tree> (UNINIT), env->macro_arg);
    env->macro_src= list<hashmap<string,path> > (
      hashmap<string,path> (path (DECORATION)), env->macro_src);
    if (L(fun) == XMACRO) {
      if (is_atomic (fun[0])) {
        string var= fun[0]->label;
        env->macro_arg->item (var)= st;
        env->macro_src->item (var)= ip;
      }
    }
    else for (i=0; i<n; i++)
      if (is_atomic (fun[i])) {
        string var= fun[i]->label;
        env->macro_arg->item (var)=
        i+delta<m? st[i+delta]:
        attach_dip (tree (UNINIT), decorate_right (ip));
        env->macro_src->item (var)=
        i+delta<m? descend (ip,i+delta):
        decorate_right(ip);
      }
    flag= body->notify_macro (type, var, l+1, p, u);
    env->macro_arg= env->macro_arg->next;
    env->macro_src= env->macro_src->next;
  }
  else flag= env->depends (st, var, l);
  if (flag) status= CORRUPTED;
  return flag;
}

void
bridge_compound_rep::notify_change () {
  status= CORRUPTED;
  if (!is_nil (body)) body->notify_change ();
}

/******************************************************************************
* Typesetting
******************************************************************************/

bool
bridge_compound_rep::my_typeset_will_be_complete () {
  return !valid;
}

void
bridge_compound_rep::my_typeset (int desired_status) {
  if (athena_is_enunciation_background (st)) {
    int d= athena_enunciation_background_delta (st);
    tree body_t= st[d + 1];
    initialize (body_t, 0, body_t);
    if (!the_drd->is_child_enforcing (st))
      ttt->insert_marker (st, ip);
    array<page_item> items;
    stack_border border;
    ttt->local_start (items, border);
    body->typeset (desired_status);
    ttt->local_end (items, border);
    athena_mark_enunciation_background (items, st[d], env->alpha);
    ttt->insert_stack (items, border);
    return;
  }
  if (athena_is_enunciation_surround (st)) {
    tree r= athena_enunciation_surround_rewrite (env, st);
    initialize (r, 0, r);
    if (!the_drd->is_child_enforcing (st))
      ttt->insert_marker (st, ip);
    tree color= athena_enunciation_surround_color (st);
    if (is_atomic (color) && color->label == "none")
      body->typeset (desired_status);
    else {
      array<page_item> items;
      stack_border border;
      ttt->local_start (items, border);
      body->typeset (desired_status);
      ttt->local_end (items, border);
      athena_mark_enunciation_background (items, color, env->alpha);
      ttt->insert_stack (items, border);
    }
    return;
  }
  if (athena_is_proof_qed_layout (st)) {
    tree r= athena_proof_qed_layout_rewrite (env, st);
    initialize (r, 0, r);
    if (!the_drd->is_child_enforcing (st))
      ttt->insert_marker (st, ip);
    body->typeset (desired_status);
    return;
  }

  int d;
  tree f;
  string macro_name;
  if (L(st) == COMPOUND) {
    d= 1;
    f= st[0];
    if (is_compound (f)) f= env->exec (f);
    if (is_atomic (f)) {
      macro_name= f->label;
      if (env->provides (macro_name)) f= env->read (macro_name);
      else f= tree (_ERROR, st);
    }
  }
  else {
    macro_name= as_string (L(st));
    if (env->provides (macro_name)) f= env->read (macro_name);
    else f= tree (_ERROR, st);
    d= 0;
  }

  if (is_applicable (f)) {
    int i, n=N(f)-1, m=N(st)-d;
    
    // WYVERN EDITION: Inject background colors for enunciations
    string var= macro_name;
    
    if (is_enunciation_type (var)) {
      string col = get_preference ("vault " * var * " color", "none");
      if (col != "none") {
        f = copy(f);
        f[n] = tree(WITH, "vault-enunciation-color", col, f[n]);
      }
    }
    else if (var == "render-enunciation" || var == "render-proof") {
      if (env->provides("vault-enunciation-color")) {
        string col = as_string(env->read("vault-enunciation-color"));
        if (col != "none") {
          f = copy(f);
          tree body= copy (f[n]);
          bool found= false;
          if (var == "render-enunciation")
            body= athena_set_enunciation_surround_color (
              body, tree (col), found);
          if (found) f[n]= body;
          else {
            if (var == "render-proof")
              body= tree(WITH, "vault-enunciation-color", "none", body);
            tree bg= tree (COMPOUND,
                           tree ("athena-enunciation-background"),
                           tree (col), body);
            f[n] = tree(COMPOUND, tree("padded*"), bg);
          }
        }
      }
    }

    env->macro_arg= list<hashmap<string,tree> > (
      hashmap<string,tree> (UNINIT), env->macro_arg);
    env->macro_src= list<hashmap<string,path> > (
      hashmap<string,path> (path (DECORATION)), env->macro_src);
    if (L(f) == XMACRO) {
      if (is_atomic (f[0])) {
        string var= f[0]->label;
        env->macro_arg->item (var)= st;
        env->macro_src->item (var)= ip;
      }
    }
    else for (i=0; i<n; i++)
      if (is_atomic (f[i])) {
        string var= f[i]->label;
        env->macro_arg->item (var)=
        i<m? st[i+d]: attach_dip (tree (UNINIT), decorate_right (ip));
        env->macro_src->item (var)=
        i<m? descend (ip,i+d): decorate_right(ip);
      }
    initialize (f[n], d, f);
    // /*IF_NON_CHILD_ENFORCING(st)*/ ttt->insert_marker (st, ip);
    if (!the_drd->is_child_enforcing (st))
      ttt->insert_marker (st, ip);
    bool suppress_links= athena_suppresses_radioactive_links (macro_name);
    tree old_radioactive_scope;
    if (suppress_links)
      old_radioactive_scope=
        env->local_begin ("athena-radioactive-links-suppressed", "true");
    body->typeset (desired_status);
    if (suppress_links)
      env->local_end ("athena-radioactive-links-suppressed",
                      old_radioactive_scope);
    env->macro_arg= env->macro_arg->next;
    env->macro_src= env->macro_src->next;
  }
  else {
    initialize (f, d, f);
    // /*IF_NON_CHILD_ENFORCING(st)*/ ttt->insert_marker (st, ip);
    if (!the_drd->is_child_enforcing (st))
      ttt->insert_marker (st, ip);
    body->typeset (desired_status);
  }
}

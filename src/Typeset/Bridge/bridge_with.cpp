
/******************************************************************************
* MODULE     : bridge_with.cpp
* DESCRIPTION: Bridge between logical and physical local enviroment changes
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "bridge.hpp"
#include "Env/local_environment.hpp"

class bridge_with_rep: public bridge_rep {
protected:
  int    last;
  bridge body;
  athena::contracts::evaluation_boundary_handle body_entry;
  athena::contracts::contract_event_log prefix_source, body_prefix;

public:
  bridge_with_rep (typesetter ttt, tree st, path ip);
  void initialize ();

  void notify_assign (path p, tree u);
  void notify_insert (path p, tree u);
  void notify_remove (path p, int nr);
  bool notify_macro  (int type, string var, int level, path p, tree u);
  void notify_change ();
  bool refresh_radioactive_children () override {
    return !is_nil (body) && body->refresh_radioactive_links ();
  }

  void my_exec_until (path p);
  bool my_typeset_will_be_complete ();
  void my_typeset (int desired_status);
};

bridge_with_rep::bridge_with_rep (typesetter ttt, tree st, path ip):
  bridge_rep (ttt, st, ip)
{
  initialize ();
}

void
bridge_with_rep::initialize () {
  last= N(st)-1;
  if (is_nil(body)) body= make_bridge (ttt, st[last], descend (ip, last));
  else replace_bridge (body, st[last], descend (ip, last));
}

bridge
bridge_with (typesetter ttt, tree st, path ip) {
  return tm_new<bridge_with_rep> (ttt, st, ip);
}

/******************************************************************************
* Event notification
******************************************************************************/

void
bridge_with_rep::notify_assign (path p, tree u) {
  // cout << "Assign " << p << ", " << u << " in " << st << "\n";
  ASSERT (!is_nil (p) || L(u) == L(st), "nil path");
  if (is_nil (p)) {
    st=u;
    initialize ();
  }
  else {
    bool mp_flag= is_multi_paragraph (st);
    if (p->item == last) {
      if (is_atom (p)) replace_bridge (body, u, descend (ip, last));
      else body->notify_assign (p->next, u);
      st= substitute (st, p->item, body->st);
    }
    else {
      st= substitute (st, p, u);
      body->notify_change ();
    }
    if (mp_flag != is_multi_paragraph (st)) initialize ();
  }
  status= CORRUPTED;
}

void
bridge_with_rep::notify_insert (path p, tree u) {
  // cout << "Insert " << p << ", " << u << " in " << st << "\n";
  ASSERT (!is_nil (p), "nil path");
  if (is_atom (p) || (p->item != last)) bridge_rep::notify_insert (p, u);
  else {
    bool mp_flag= is_multi_paragraph (st);
    body->notify_insert (p->next, u);
    st= substitute (st, last, body->st);
    if (mp_flag != is_multi_paragraph (st)) initialize ();
  }
  status= CORRUPTED;
}

void
bridge_with_rep::notify_remove (path p, int nr) {
  // cout << "Remove " << p << ", " << nr << " in " << st << "\n";
  ASSERT (!is_nil (p), "nil path");
  if (is_atom (p) || (p->item != last)) bridge_rep::notify_remove (p, nr);
  else {
    bool mp_flag= is_multi_paragraph (st);
    body->notify_remove (p->next, nr);
    st= substitute (st, last, body->st);
    if (mp_flag != is_multi_paragraph (st)) initialize ();
  }
  status= CORRUPTED;
}

bool
bridge_with_rep::notify_macro (int type, string var, int l, path p, tree u) {
  bool flag= body->notify_macro (type, var, l, p, u);
  if (flag) status= CORRUPTED;
  return flag;
}

void
bridge_with_rep::notify_change () {
  status= CORRUPTED;
  body->notify_change ();
}

/******************************************************************************
* Typesetting
******************************************************************************/

void
bridge_with_rep::my_exec_until (path p) {
  if (((last&1) != 0) || (p->item != last)) return;
  if (prefix_source != contract_changes) {
    body_prefix= athena::contracts::contract_event_prefix (contract_changes, body_entry);
    prefix_source= contract_changes;
  }
  if (body_prefix && env->replay_contract_events (body_prefix, false)) {
    body->exec_until (p->next);
    return;
  }
  if (L(st) == WITH_PACKAGE) {
    scoped_package_environment scope (*env.operator-> (), st[0]);
    body->exec_until (p->next);
    scope.finish (true);
    return;
  }
  local_environment_bindings bindings;
  if (!evaluate_local_environment (*env.operator-> (), st, bindings)) return;
  local_environment_scope scope (*env.operator-> (), bindings, true);
  body->exec_until (p->next);
  scope.finish (true);
}

bool
bridge_with_rep::my_typeset_will_be_complete () {
  if (status != CORRUPTED) return false;
  return body->my_typeset_will_be_complete ();
}

void
bridge_with_rep::my_typeset (int desired_status) {
  if (L(st) == WITH_PACKAGE) {
    if (N(st) != 2) {
      ttt->insert_paragraph (tree (_ERROR, "bad with-package"), ip);
      return;
    }
    scoped_package_environment scope (*env.operator-> (), st[0]);
    ttt->insert_marker (st, ip);
    body_entry= env->mark_execution_boundary ();
    prefix_source.reset ();
    body_prefix.reset ();
    body->typeset (desired_status);
    scope.finish ();
    return;
  }
  local_environment_bindings bindings;
  if (!evaluate_local_environment (*env.operator-> (), st, bindings)) {
    ttt->insert_paragraph (tree (_ERROR, "bad with"), ip);
    return;
  }
  local_environment_scope scope (*env.operator-> (), bindings);
  ttt->insert_marker (st, ip);
  body_entry= env->mark_execution_boundary ();
  prefix_source.reset ();
  body_prefix.reset ();
  body->typeset (desired_status);
  scope.finish ();
}

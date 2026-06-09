/******************************************************************************
* MODULE     : bridge_transclude.cpp
* DESCRIPTION: Bridge for vault transclusions
* COPYRIGHT  : (C) 2026
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "bridge.hpp"
#include "drd_std.hpp"
#include "glue.hpp"
#include "scheme.hpp"

static array<string> active_transclusion_bridges;

static tree
transclusion_error_tree (tree t, string message) {
  string hint= N(t) > 1 ? as_string (t[1]) : string ("");
  return tree (WITH, "color", "red",
               tree (CONCAT, "Broken Transclusion: " * message *
                     " (" * hint * ")."));
}

struct TranscludeBridgeCycleLock {
  bool ok;
  TranscludeBridgeCycleLock (string uuid) {
    ok= true;
    for (int i=0; i<N(active_transclusion_bridges); i++)
      if (active_transclusion_bridges[i] == uuid) ok= false;
    if (ok) active_transclusion_bridges << uuid;
  }
  ~TranscludeBridgeCycleLock () {
    if (ok) {
      int n= N(active_transclusion_bridges);
      if (n > 0) {
        array<string> next;
        for (int i=0; i<n-1; i++) next << active_transclusion_bridges[i];
        active_transclusion_bridges= next;
      }
    }
  }
};

static tree
resolve_transclusion_tree (tree t) {
  if (N(t) != 4)
    return transclusion_error_tree (t, "Malformed transclusion");

  string uuid= as_string (t[0]);
  TranscludeBridgeCycleLock lock (uuid);
  if (!lock.ok)
    return transclusion_error_tree (t, "Cyclic transclusion detected");

  static tmscm fun= scm_lookup_string ("vault-resolve-transclude");
  tmscm res_scm= call_scheme (fun,
                              tree_to_tmscm (t[0]),
                              tree_to_tmscm (t[1]),
                              tree_to_tmscm (t[2]),
                              tree_to_tmscm (t[3]));
  tree content= tmscm_to_content (res_scm);
  if (is_compound (content, DOCUMENT) && N(content) > 0)
    content= content[0];
  return content;
}

class bridge_transclude_rep: public bridge_rep {
protected:
  tree   bt;
  bridge body;

public:
  bridge_transclude_rep (typesetter ttt, tree st, path ip);
  void initialize (tree body_t);

  void notify_assign (path p, tree u);
  bool notify_macro  (int type, string var, int l, path p, tree u);
  void notify_change ();

  void my_typeset (int desired_status);
};

bridge_transclude_rep::bridge_transclude_rep (typesetter ttt, tree st,
                                              path ip):
  bridge_rep (ttt, st, ip) {}

void
bridge_transclude_rep::initialize (tree body_t) {
  if (is_nil (body)) body= make_bridge (ttt, attach_right (body_t, ip));
  else replace_bridge (body, path (), bt, attach_right (body_t, ip));
  bt= copy (body_t);
}

bridge
bridge_transclude (typesetter ttt, tree st, path ip) {
  return tm_new<bridge_transclude_rep> (ttt, st, ip);
}

/******************************************************************************
* Event notification
******************************************************************************/

void
bridge_transclude_rep::notify_assign (path p, tree u) {
  status= CORRUPTED;
  st= substitute (st, p, u);
}

bool
bridge_transclude_rep::notify_macro (int tp, string var, int l, path p,
                                     tree u) {
  (void) tp; (void) var; (void) l; (void) p; (void) u;
  return false;
}

void
bridge_transclude_rep::notify_change () {
  status= CORRUPTED;
}

/******************************************************************************
* Typesetting
******************************************************************************/

void
bridge_transclude_rep::my_typeset (int desired_status) {
  initialize (env->rewrite (resolve_transclusion_tree (st)));
  if (!the_drd->is_child_enforcing (st))
    ttt->insert_marker (st, ip);
  body->typeset (desired_status);
}

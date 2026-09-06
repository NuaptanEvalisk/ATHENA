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
#include "ATHENA/Data/artifact_radioactive_links.hpp"
#include "ATHENA/Data/transclusion_cache.hpp"
#include "drd_std.hpp"

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
resolve_transclusion_tree (tree t, string* cache_key) {
  if (N(t) != 4)
    return transclusion_error_tree (t, "Malformed transclusion");

  string uuid= as_string (t[0]);
  TranscludeBridgeCycleLock lock (uuid);
  if (!lock.ok)
    return transclusion_error_tree (t, "Cyclic transclusion detected");

  return athena_resolve_transclusion_display (t, cache_key);
}

class bridge_transclude_rep: public bridge_rep {
protected:
  tree   bt;
  tree   resolved;
  string resolved_key;
  bool   has_resolved;
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
  bridge_rep (ttt, st, ip), has_resolved (false) {}

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
  resolved= tree ();
  resolved_key= "";
  has_resolved= false;
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
  string next_key;
  tree next= resolve_transclusion_tree (st, &next_key);
  if (resolved_key != next_key || !has_resolved) {
    resolved=
      athena_artifact_radioactive_suppress_definitions (next);
    resolved_key= next_key;
    has_resolved= true;
  }
  initialize (env->rewrite (resolved));
  if (!the_drd->is_child_enforcing (st))
    ttt->insert_marker (st, ip);
  tree old_radioactive_scope= env->local_begin (
    "athena-radioactive-links-in-transclusion", "true");
  body->typeset (desired_status);
  env->local_end ("athena-radioactive-links-in-transclusion",
                  old_radioactive_scope);
}


/******************************************************************************
* MODULE     : object.cpp
* DESCRIPTION: Implementation of scheme objects
* COPYRIGHT  : (C) 1999-2011 Joris van der Hoeven and Massimiliano Gubinelli
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "object.hpp"
#include "glue.hpp"

#include "config.h"
#include "array.hpp"
#include "hashmap.hpp"
#include "promise.hpp"
#include "widget.hpp"
#include "boot.hpp"
#include "editor.hpp"
#include "modification.hpp"
#include "patch.hpp"
#include "colors.hpp"
#include "tm_ostream.hpp"
#include "actor_ui_bridge.hpp"
#include "buffer_actor.hpp"
#include "guile_tm.hpp"
#include "scheme_execution_context.hpp"

/******************************************************************************
* The object representation class
******************************************************************************/

tmscm_object_rep::tmscm_object_rep (tmscm obj):
  handle (tmscm_root_acquire (obj)) {}

tmscm_object_rep::~tmscm_object_rep () {
  // This may run from Guile's finalizer thread.  Releasing only publishes the
  // slot; the owner unroots it at a normal Guile safe point.
  tmscm_root_release (handle);
}


/******************************************************************************
* Routines on objects
******************************************************************************/

tm_ostream&
operator << (tm_ostream& out, object obj) {
  out.flush ();
  if (out == cout) call ("write", obj);
  else if (out == cerr) call ("write-err", obj);
  else {
    object ret= call ("object->string", obj);
    return out << as_string (ret);
  }
  call ("force-output");
  return out;
}

bool
operator == (object obj1, object obj2) {
  tmscm o1= object_to_tmscm (obj1), o2= object_to_tmscm (obj2);
  return tmscm_is_equal (o1, o2);
}

bool
operator != (object obj1, object obj2) {
  return !(obj1 == obj2);
}

int
hash (object obj) {
  return as_int (call ("hash", obj, object (1234567)));
}


/******************************************************************************
* Utilities
******************************************************************************/

object null_object () {
  return tmscm_to_object (tmscm_null ()); }
object cons (object obj1, object obj2) {
  return tmscm_to_object (tmscm_cons (object_to_tmscm (obj1), object_to_tmscm (obj2))); }
object list_object (object obj1) {
  return cons (obj1, null_object ()); }
object list_object (object obj1, object obj2) {
  return cons (obj1, cons (obj2, null_object ())); }
object list_object (object obj1, object obj2, object obj3) {
  return cons (obj1, cons (obj2, cons (obj3, null_object ()))); }
object as_list_object (array<object> objs) {
  object r= null_object ();
  for (int i=N(objs)-1; i>=0; i--) r= cons (objs[i], r);
  return r; }
object symbol_object (string s) {
  return tmscm_to_object ( symbol_to_tmscm (s) ); }
object car (object obj) {
  return tmscm_to_object (tmscm_car (object_to_tmscm (obj))); }
object cdr (object obj) {
  return tmscm_to_object (tmscm_cdr (object_to_tmscm (obj))); }
object caar (object obj) {
  return tmscm_to_object (tmscm_caar (object_to_tmscm (obj))); }
object cdar (object obj) {
  return tmscm_to_object (tmscm_cdar (object_to_tmscm (obj))); }
object cadr (object obj) {
  return tmscm_to_object (tmscm_cadr (object_to_tmscm (obj))); }
object cddr (object obj) {
  return tmscm_to_object (tmscm_cddr (object_to_tmscm (obj))); }
object caddr (object obj) {
  return tmscm_to_object (tmscm_caddr (object_to_tmscm (obj))); }
object cadddr (object obj) {
  return tmscm_to_object (tmscm_cadddr (object_to_tmscm (obj))); }


/******************************************************************************
* Predicates
******************************************************************************/

bool is_null (object obj) { return tmscm_is_null (object_to_tmscm (obj)); }
bool is_list (object obj) { return tmscm_is_list (object_to_tmscm (obj)); }
bool is_bool (object obj) { return tmscm_is_bool (object_to_tmscm (obj)); }
bool is_int (object obj) { return tmscm_is_int (object_to_tmscm (obj)); }
bool is_double (object obj) { return tmscm_is_double (object_to_tmscm (obj)); }
bool is_string (object obj) { return tmscm_is_string (object_to_tmscm (obj)); }
bool is_symbol (object obj) { return tmscm_is_symbol (object_to_tmscm (obj)); }
bool is_tree (object obj) { return tmscm_is_tree (object_to_tmscm (obj)); }
bool is_path (object obj) { return tmscm_is_path (object_to_tmscm (obj)); }
bool is_url (object obj) { return tmscm_is_url (object_to_tmscm (obj)); }
bool is_array_double (object obj) {
  return tmscm_is_array_double (object_to_tmscm (obj)); }
bool is_widget (object obj) { return tmscm_is_widget (object_to_tmscm (obj)); }
bool is_patch (object obj) { return tmscm_is_patch (object_to_tmscm (obj)); }
bool is_modification (object obj) {
  return tmscm_is_modification (object_to_tmscm (obj)); }

/******************************************************************************
* Basic conversions
******************************************************************************/

object::object (tmscm_object_rep* o): rep (static_cast<object_rep*>(o)) {}
object::object (): rep (tm_new<tmscm_object_rep> (tmscm_null ())) {}
object::object (bool b): rep (tm_new<tmscm_object_rep> (bool_to_tmscm (b))) {}
object::object (int i): rep (tm_new<tmscm_object_rep> (int_to_tmscm (i))) {}
object::object (double x):
  rep (tm_new<tmscm_object_rep> (double_to_tmscm (x))) {}
object::object (const char* s):
  rep (tm_new<tmscm_object_rep> (string_to_tmscm (string (s)))) {}
object::object (string s):
  rep (tm_new<tmscm_object_rep> (string_to_tmscm (s))) {}
object::object (tree t):
  rep (tm_new<tmscm_object_rep> (tree_to_tmscm (t))) {}
object::object (list<string> l):
  rep (tm_new<tmscm_object_rep> (list_string_to_tmscm (l))) {}
object::object (list<tree> l):
  rep (tm_new<tmscm_object_rep> (list_tree_to_tmscm (l))) {}
object::object (path p): rep (tm_new<tmscm_object_rep> (path_to_tmscm (p))) {}
object::object (url u): rep (tm_new<tmscm_object_rep> (url_to_tmscm (u))) {}
object::object (array<double> a):
  rep (tm_new<tmscm_object_rep> (array_double_to_tmscm (a))) {}
object::object (patch m):
  rep (tm_new<tmscm_object_rep> (patch_to_tmscm (m))) {}
object::object (modification m):
  rep (tm_new<tmscm_object_rep> (modification_to_tmscm (m))) {}

bool
as_bool (object obj) {
  tmscm b= object_to_tmscm (obj);
  if (!tmscm_is_bool (b)) return false;
  return tmscm_to_bool (b);
}

int
as_int (object obj) {
  tmscm i= object_to_tmscm (obj);
  if (!tmscm_is_int (i)) return 0;
  return tmscm_to_int (i);
}

double
as_double (object obj) {
  tmscm x= object_to_tmscm (obj);
  if (!tmscm_is_double (x)) return 0.0;
  return tmscm_to_double (x);
}

string
as_string (object obj) {
  tmscm s= object_to_tmscm (obj);
  if (!tmscm_is_string (s)) return "";
  return tmscm_to_string (s);
}

string
as_symbol (object obj) {
  tmscm s= object_to_tmscm (obj);
  if (!tmscm_is_symbol (s)) return "";
  return tmscm_to_symbol (s);
}

tree
as_tree (object obj) {
  tmscm t= object_to_tmscm (obj);
  if (!tmscm_is_tree (t)) return tree ();
  return tmscm_to_tree (t);
}

scheme_tree
as_scheme_tree (object obj) {
  tmscm t= object_to_tmscm (obj);
  return tmscm_to_scheme_tree (t);
}

list<string>
as_list_string (object obj) {
  tmscm l= object_to_tmscm (obj);
  if (!tmscm_is_list_string (l)) return list<string> ();
  return tmscm_to_list_string (l);
}

list<tree>
as_list_tree (object obj) {
  tmscm l= object_to_tmscm (obj);
  if (!tmscm_is_list_tree (l)) return list<tree> ();
  return tmscm_to_list_tree (l);
}

path
as_path (object obj) {
  tmscm t= object_to_tmscm (obj);
  if (!tmscm_is_path (t)) return path ();
  return tmscm_to_path (t);
}

array<object>
as_array_object (object obj) {
  ASSERT (is_list (obj), "list expected");
  array<object> ret;
  while (!is_null (obj)) {
    ret << car (obj);
    obj= cdr (obj);
  }
  return ret;
}

url
as_url (object obj) {
  tmscm t= object_to_tmscm (obj);
  if (!tmscm_is_url (t)) return url ("");
  return tmscm_to_url (t);
}

array<double>
as_array_double (object obj) {
  ASSERT (is_array_double (obj), "array<double> expected");
  tmscm t= object_to_tmscm (obj);
  return tmscm_to_array_double (t);
}

modification
as_modification (object obj) {
  tmscm m= object_to_tmscm (obj);
  if (!tmscm_is_modification (m))
    return mod_assign (path (), "");
  return tmscm_to_modification (m);
}

patch
as_patch (object obj) {
  tmscm p= object_to_tmscm (obj);
  if (!tmscm_is_patch (p))
    return patch (array<patch> ());
  return tmscm_to_patch (p);
}

widget
as_widget (object obj) {
  tmscm w= object_to_tmscm (obj);
  if (!tmscm_is_widget (w)) return widget ();
  return tmscm_to_widget (w);
}

object
tree_to_stree (scheme_tree t) {
  return call ("tree->stree", t);
}

tree
stree_to_tree (object obj) {
  return as_tree (call ("stree->tree", obj));
}

tree
content_to_tree (object obj) {
  return tmscm_to_content (object_to_tmscm (obj));
    // return as_tree (call ("content->tree", obj));
}

object
string_to_object (string s) {
  return call ("string->object", s);
}

string
object_to_string (object obj) {
  return as_string (call ("object->string", obj));
}

object
scheme_cmd (const char* s) {
  return eval ("(lambda () " * string (s) * ")");
}

object
scheme_cmd (string s) {
  return eval ("(lambda () " * s * ")");
}

object
scheme_cmd (object cmd) {
  cmd= cons (cmd, null_object ());
  cmd= cons (null_object (), cmd);
  cmd= cons (eval ("'lambda"), cmd);
  return eval (cmd);
}

/******************************************************************************
* Conversions to functional objects
******************************************************************************/

static inline array<tmscm >
array_lookup (array<object> a) {
  const int n=N(a);
  array<tmscm > tmscm (n);
  int i;
  for (i=0; i<n; i++) tmscm [i]= object_to_tmscm (a[i]);
  return tmscm ;
}

class object_command_rep: public command_rep {
  object obj;
  athena_actor_id actor_id;
  athena_view_id view_id;
  athena_scheme_handle_id handle;
public:
  object_command_rep (object obj2):
    obj (), actor_id (ATHENA_NO_ACTOR), view_id (ATHENA_NO_VIEW),
    handle (ATHENA_NO_SCHEME_HANDLE) {
    const SchemeExecutionContext* context= current_scheme_execution_context ();
    if (context != nullptr && context->actor_id != ATHENA_NO_ACTOR &&
        context->view_id != ATHENA_NO_VIEW) {
      actor_id= context->actor_id;
      view_id= context->view_id;
      handle= scheme_command_handle_acquire (object_to_tmscm (obj2));
    }
    else obj= obj2;
  }
  ~object_command_rep () override {
    if (handle == ATHENA_NO_SCHEME_HANDLE) return;
    actor_command_ticket ticket= buffer_actor::submit_to (
      actor_id, actor_command_kind::release_scheme_handle, view_id,
      ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER, handle);
    if (!ticket) scheme_command_handle_release (handle);
  }
  void apply () override {
    if (handle == ATHENA_NO_SCHEME_HANDLE) {
      (void) call_scheme (object_to_tmscm (obj));
      return;
    }
    (void) buffer_actor::submit_to (
      actor_id, actor_command_kind::invoke_scheme_handle, view_id,
      ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER, handle);
  }
  void apply (object args) override {
    if (handle == ATHENA_NO_SCHEME_HANDLE) {
      (void) call_scheme (object_to_tmscm (obj),
                          array_lookup (as_array_object (args)));
      return;
    }
    athena_scheme_handle_id arguments=
      scheme_command_handle_acquire (object_to_tmscm (args));
    actor_command_ticket ticket= buffer_actor::submit_to (
      actor_id, actor_command_kind::invoke_scheme_handle, view_id,
      ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER,
      handle, arguments);
    if (!ticket) scheme_command_handle_release (arguments);
  }
  tm_ostream& print (tm_ostream& out) override {
    if (handle != ATHENA_NO_SCHEME_HANDLE)
      return out << "<actor-command " << actor_id << ":" << view_id << ">";
    object bis= call ("sourcify", obj);
    return out << "<command " << bis << ">"; }
};

command
as_command (object obj) {
  return tm_new<object_command_rep> (obj);
}

class object_promise_widget_rep: public promise_rep<widget> {
  object obj;
  athena_actor_id actor_id;
  athena_view_id view_id;
  athena_scheme_handle_id handle;
public:
  object_promise_widget_rep (object obj2):
    obj (), actor_id (ATHENA_NO_ACTOR), view_id (ATHENA_NO_VIEW),
    handle (ATHENA_NO_SCHEME_HANDLE) {
    const SchemeExecutionContext* context= current_scheme_execution_context ();
    if (context != nullptr && context->actor_id != ATHENA_NO_ACTOR &&
        context->view_id != ATHENA_NO_VIEW) {
      actor_id= context->actor_id;
      view_id= context->view_id;
      handle= scheme_command_handle_acquire (object_to_tmscm (obj2));
    }
    else obj= obj2;
  }
  ~object_promise_widget_rep () override {
    if (handle == ATHENA_NO_SCHEME_HANDLE) return;
    actor_command_ticket ticket= buffer_actor::submit_to (
      actor_id, actor_command_kind::release_scheme_handle, view_id,
      ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER, handle);
    if (!ticket) scheme_command_handle_release (handle);
  }
  tm_ostream& print (tm_ostream& out) override {
    if (handle != ATHENA_NO_SCHEME_HANDLE)
      return out << "<actor-widget-promise " << actor_id << ":" << view_id
                 << ">";
    return out << obj;
  }
  widget eval () override {
    if (handle != ATHENA_NO_SCHEME_HANDLE) {
      actor_command_record result;
      if (buffer_actor::invoke_on (
            actor_id, actor_command_kind::evaluate_widget_handle, view_id,
            ATHENA_NO_BLOB, ATHENA_NO_BLOB, &result,
            SCHEME_CAPABILITY_BUFFER, handle))
        return actor_ui_take_widget (result.argument[0]);
      FAILED ("BufferActor rejected widget promise evaluation");
      return glue_widget ();
    }
    tmscm result= call_scheme (object_to_tmscm (obj));
    if (tmscm_is_widget (result)) return tmscm_to_widget (result);
    else {
      FAILED ("widget expected");
      return glue_widget ();
    }
  }
};

promise<widget>
as_promise_widget (object obj) {
  return tm_new<object_promise_widget_rep> (obj);
}

/******************************************************************************
* Evaluation and function calls
******************************************************************************/

object eval (const char* expr) {
  return tmscm_to_object (eval_scheme (expr)); }
object eval (string expr) {
  return tmscm_to_object (eval_scheme (expr)); }
object eval (object expr) {
  return call ("eval", expr); }
object eval_secure (string expr) {
  return eval ("(wrap-eval-secure " * expr * ")"); }
object eval_file (string name) {
  return tmscm_to_object (eval_scheme_file (name)); }
bool exec_file (url u) {
  object ret= eval_file (materialize (u));
  return ret != object ("#<unspecified>"); }

object call (const char* fun) {
  return tmscm_to_object (call_scheme (eval_scheme(fun))); }
object call (const char* fun, object a1) {
  return tmscm_to_object (call_scheme (eval_scheme(fun), object_to_tmscm (a1))); }
object call (const char* fun, object a1, object a2) {
  return tmscm_to_object (call_scheme (eval_scheme(fun), object_to_tmscm (a1), object_to_tmscm (a2))); }
object call (const char* fun, object a1, object a2, object a3) {
  return tmscm_to_object (call_scheme (eval_scheme(fun), object_to_tmscm (a1),
                                       object_to_tmscm (a2), object_to_tmscm (a3))); }
object call (const char* fun, object a1, object a2, object a3, object a4) {
  return tmscm_to_object (call_scheme (eval_scheme(fun), object_to_tmscm (a1),
                                       object_to_tmscm (a2), object_to_tmscm (a3), object_to_tmscm (a4))); }
object call (const char* fun, array<object> a) {
  return tmscm_to_object (call_scheme (eval_scheme(fun), array_lookup(a))); }

object call (string fun) {
  return tmscm_to_object (call_scheme (eval_scheme(fun))); }
object call (string fun, object a1) {
  return tmscm_to_object (call_scheme (eval_scheme(fun), object_to_tmscm (a1))); }
object call (string fun, object a1, object a2) {
  return tmscm_to_object (call_scheme (eval_scheme(fun), object_to_tmscm (a1), object_to_tmscm (a2))); }
object call (string fun, object a1, object a2, object a3) {
  return tmscm_to_object (call_scheme (eval_scheme(fun), object_to_tmscm (a1),
                                       object_to_tmscm (a2), object_to_tmscm (a3))); }
object call (string fun, object a1, object a2, object a3, object a4) {
  return tmscm_to_object (call_scheme (eval_scheme(fun), object_to_tmscm (a1),
                                       object_to_tmscm (a2), object_to_tmscm (a3), object_to_tmscm (a4))); }
object call (string fun, array<object> a) {
  return tmscm_to_object (call_scheme (eval_scheme(fun), array_lookup(a))); }

object call (object fun) {
  return tmscm_to_object (call_scheme (object_to_tmscm (fun))); }
object call (object fun, object a1) {
  return tmscm_to_object (call_scheme (object_to_tmscm (fun), object_to_tmscm (a1))); }
object call (object fun, object a1, object a2) {
  return tmscm_to_object (call_scheme (object_to_tmscm (fun), object_to_tmscm (a1), object_to_tmscm (a2))); }
object call (object fun, object a1, object a2, object a3) {
  return tmscm_to_object (call_scheme (object_to_tmscm (fun), object_to_tmscm (a1),
                                       object_to_tmscm (a2), object_to_tmscm (a3))); }
object call (object fun, object a1, object a2, object a3, object a4) {
  return tmscm_to_object (call_scheme (object_to_tmscm (fun), object_to_tmscm (a1),
                                       object_to_tmscm (a2), object_to_tmscm (a3), object_to_tmscm (a4))); }
object call (object fun, array<object> a) {
  return tmscm_to_object (call_scheme (object_to_tmscm (fun), array_lookup(a))); }

/******************************************************************************
* User preferences
******************************************************************************/

static bool preferences_ok= false;
bool aofm_converter_mode = false;
static hashmap<string, string> aofm_pref_cache ("");

void
aofm_enable_converter_mode (bool enable) {
  aofm_converter_mode = enable;
}

extern void aofm_cache_latex_commands();

void
aofm_cache_preferences () {
  cout << "AOFM] Caching preferences..." << LF;
  array<string> prefs;
  prefs << string ("latex->texmacs:align-to-aligned")
        << string ("latex->texmacs:operator-d-is-differential")
        << string ("latex->texmacs:roman-d-is-differential")
        << string ("latex->texmacs:text-d-is-differential")
        << string ("latex->texmacs:parse-bbbk")
        << string ("latex->texmacs:parse-bbbi-as-mathi")
        << string ("latex->texmacs:text-operators")
        << string ("latex->texmacs:matrix-recognition")
        << string ("latex->texmacs:aligned-to-eqnarray")
        << string ("latex->texmacs:intelligent-formula-cleaner")
        << string ("latex->texmacs:intelligent-formula-cleaner-model")
        << string ("latex->texmacs:source-tracking")
        << string ("latex->texmacs:transparent-source-tracking")
        << string ("latex->texmacs:conservative")
        << string ("remove superfluous invisible")
        << string ("homoglyph correct")
        << string ("insert missing invisible")
        << string ("zealous invisible correct")
        << string ("manual remove superfluous invisible")
        << string ("manual homoglyph correct")
        << string ("manual insert missing invisible")
        << string ("manual zealous invisible correct")
        << string ("vault preferred font");
  for (int i=0; i<N(prefs); i++) {
    string val = get_preference (prefs[i], "default");
    aofm_pref_cache (prefs[i]) = val;
    cout << "AOFM]   " << prefs[i] << " -> " << val << LF;
  }
  
  aofm_cache_latex_commands();
}

void
notify_preferences_booted () {
  preferences_ok= true;
  gui_cursor_color = named_color (get_preference ("gui cursor color", "red"));
  gui_selection_color = named_color (get_preference ("gui selection color", "red"));
  gui_focus_color = named_color (get_preference ("gui focus color", "#0ff"));
  gui_focus_border_width =
    max (as_int (get_preference ("gui focus border width", "1")), 1);
}

void
set_preference (string var, string val) {
  set_user_preference (var, val);
  if (preferences_ok) save_user_preferences ();
}

void
notify_preference (string var) {
  if (preferences_ok) (void) call ("notify-preference", var);
}

string
get_preference (string var, string def) {
  if (aofm_converter_mode && aofm_pref_cache->contains (var)) {
    // cout << "AOFM] (cache hit) " << var << endl;
    string pref = aofm_pref_cache[var];
    if (pref == "default") return def; else return pref;
  }
  string pref= get_user_preference (var, def);
  if (pref == "default") return def; else return pref;
}

void
protected_call (object cmd) {
#ifdef USE_EXCEPTIONS
  try {
#endif
    get_current_editor()->before_menu_action ();
    call (cmd);
    get_current_editor()->after_menu_action ();
#ifdef USE_EXCEPTIONS
  }
  catch (string s) {
    get_current_editor()->cancel_menu_action ();
  }
  handle_exceptions ();
#endif
}

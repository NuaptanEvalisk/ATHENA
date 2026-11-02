
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
#include "native_dispatch.hpp"
#include "glue.hpp"
#include "convert.hpp"

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
#include "new_view.hpp"
#include "application_preferences.hpp"
#include <variant>
#include <cmath>
#include <cstring>
#include <stdexcept>

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

enum class native_object_kind {
  boolean, integer, real, text, symbol, keyword, source_tree, location,
  change, patch_value
};

class native_object_rep: public object_rep {
public:
  native_object_kind kind;
  std::variant<bool,int,double,string,tree,url,modification,patch> value;
  tmscm_root_handle* bridge= nullptr;

  template<class T> native_object_rep (native_object_kind kind, const T& value):
    kind (kind), value (value) {}
  ~native_object_rep () { if (bridge) tmscm_root_release (bridge); }

  tmscm legacy_value () {
    if (bridge) return tmscm_root_value (bridge);
    tmscm result= TMSCM_UNSPECIFIED;
    switch (kind) {
    case native_object_kind::boolean: result= bool_to_tmscm (std::get<bool> (value)); break;
    case native_object_kind::integer: result= int_to_tmscm (std::get<int> (value)); break;
    case native_object_kind::real: result= double_to_tmscm (std::get<double> (value)); break;
    case native_object_kind::text: result= string_to_tmscm (std::get<string> (value)); break;
    case native_object_kind::symbol: result= symbol_to_tmscm (std::get<string> (value)); break;
    case native_object_kind::keyword: result= keyword_to_tmscm (std::get<string> (value)); break;
    case native_object_kind::source_tree: result= tree_to_tmscm (std::get<tree> (value)); break;
    case native_object_kind::location: result= url_to_tmscm (std::get<url> (value)); break;
    case native_object_kind::change: result= modification_to_tmscm (std::get<modification> (value)); break;
    case native_object_kind::patch_value: result= patch_to_tmscm (std::get<patch> (value)); break;
    }
    bridge= tmscm_root_acquire (result);
    return result;
  }
};

static native_object_rep* native_value (object value) {
  return dynamic_cast<native_object_rep*> (value.operator->());
}

tmscm object_to_tmscm (object value) {
  if (auto* native= native_value (value)) return native->legacy_value ();
  return tmscm_root_value (static_cast<tmscm_object_rep*> (value.operator->())->handle);
}


/******************************************************************************
* Routines on objects
******************************************************************************/

tm_ostream&
operator << (tm_ostream& out, object obj) {
  out.flush ();
  out << object_to_string (obj);
  out.flush ();
  return out;
}

bool
operator == (object obj1, object obj2) {
  if (obj1.operator->() == obj2.operator->()) return true;
  auto* first= native_value (obj1);
  auto* second= native_value (obj2);
  if (first && second) {
    if (first->kind != second->kind) return false;
    if (first->kind == native_object_kind::text)
      return as_string (obj1) == as_string (obj2);
    if (first->kind == native_object_kind::real) {
      const double x= std::get<double> (first->value), y= std::get<double> (second->value);
      return std::memcmp (&x, &y, sizeof (double)) == 0 || (std::isnan (x) && std::isnan (y));
    }
    return first->value == second->value;
  }
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
  return tm_new<native_object_rep> (native_object_kind::symbol, s); }
object keyword_object (string s) {
  return tm_new<native_object_rep> (native_object_kind::keyword, s); }
string keyword_name (object keyword) {
  if (auto* value= native_value (keyword)) {
    ASSERT (value->kind == native_object_kind::keyword, "keyword expected");
    return std::get<string> (value->value);
  }
  return tmscm_to_symbol (scm_keyword_to_symbol (object_to_tmscm (keyword))); }
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

bool is_null (object obj) { return !native_value (obj) && tmscm_is_null (object_to_tmscm (obj)); }
bool is_list (object obj) { return !native_value (obj) && tmscm_is_list (object_to_tmscm (obj)); }
bool is_bool (object obj) {
  if (auto* n= native_value (obj)) return n->kind == native_object_kind::boolean;
  return tmscm_is_bool (object_to_tmscm (obj));
}
bool is_int (object obj) {
  if (auto* n= native_value (obj)) {
    if (n->kind == native_object_kind::integer) return true;
    if (n->kind != native_object_kind::real) return false;
    const double x= std::get<double> (n->value);
    return std::isfinite (x) && x == std::floor (x);
  }
  return tmscm_is_int (object_to_tmscm (obj));
}
bool is_double (object obj) {
  if (auto* n= native_value (obj))
    return n->kind == native_object_kind::real || n->kind == native_object_kind::integer;
  return tmscm_is_double (object_to_tmscm (obj));
}
bool is_string (object obj) {
  if (auto* n= native_value (obj)) return n->kind == native_object_kind::text;
  return tmscm_is_string (object_to_tmscm (obj));
}
bool is_symbol (object obj) {
  if (auto* n= native_value (obj)) return n->kind == native_object_kind::symbol;
  return tmscm_is_symbol (object_to_tmscm (obj));
}
bool is_tree (object obj) {
  if (auto* n= native_value (obj)) return n->kind == native_object_kind::source_tree;
  return tmscm_is_tree (object_to_tmscm (obj));
}
bool is_path (object obj) { return !native_value (obj) && tmscm_is_path (object_to_tmscm (obj)); }
bool is_url (object obj) {
  if (auto* n= native_value (obj))
    return n->kind == native_object_kind::location || n->kind == native_object_kind::text;
  return tmscm_is_url (object_to_tmscm (obj));
}
bool is_array_double (object obj) {
  return !native_value (obj) && tmscm_is_array_double (object_to_tmscm (obj)); }
bool is_widget (object obj) { return !native_value (obj) && tmscm_is_widget (object_to_tmscm (obj)); }
bool is_patch (object obj) {
  if (auto* n= native_value (obj)) return n->kind == native_object_kind::patch_value;
  return tmscm_is_patch (object_to_tmscm (obj));
}
bool is_modification (object obj) {
  if (auto* n= native_value (obj)) return n->kind == native_object_kind::change;
  return tmscm_is_modification (object_to_tmscm (obj)); }
bool is_procedure (object obj) {
  if (native_value (obj)) return false;
  return SCM_NFALSEP (scm_procedure_p (object_to_tmscm (obj))); }
object procedure_source (object obj) {
  if (!is_procedure (obj)) return object (false);
  return tmscm_to_object (scm_procedure_source (object_to_tmscm (obj)));
}

/******************************************************************************
* Basic conversions
******************************************************************************/

object::object (tmscm_object_rep* o): rep (static_cast<object_rep*>(o)) {}
object::object (native_object_rep* o): rep (static_cast<object_rep*>(o)) {}
object::object (): rep (tm_new<tmscm_object_rep> (tmscm_null ())) {}
object::object (bool b): rep (tm_new<native_object_rep> (native_object_kind::boolean, b)) {}
object::object (int i): rep (tm_new<native_object_rep> (native_object_kind::integer, i)) {}
object::object (double x):
  rep (tm_new<native_object_rep> (native_object_kind::real, x)) {}
object::object (const char* s):
  object (string (s)) {}
object::object (string s):
  rep (tm_new<native_object_rep> (native_object_kind::text, s)) {}
object::object (tree t):
  rep (tm_new<native_object_rep> (native_object_kind::source_tree, t)) {}
object::object (list<string> l):
  rep (tm_new<tmscm_object_rep> (list_string_to_tmscm (l))) {}
object::object (list<tree> l):
  rep (tm_new<tmscm_object_rep> (list_tree_to_tmscm (l))) {}
object::object (path p): rep (tm_new<tmscm_object_rep> (path_to_tmscm (p))) {}
object::object (url u): rep (tm_new<native_object_rep> (native_object_kind::location, u)) {}
object::object (array<double> a):
  rep (tm_new<tmscm_object_rep> (array_double_to_tmscm (a))) {}
object::object (patch m):
  rep (tm_new<native_object_rep> (native_object_kind::patch_value, m)) {}
object::object (modification m):
  rep (tm_new<native_object_rep> (native_object_kind::change, m)) {}

bool
as_bool (object obj) {
  if (auto* n= native_value (obj))
    return n->kind == native_object_kind::boolean && std::get<bool> (n->value);
  tmscm b= object_to_tmscm (obj);
  if (!tmscm_is_bool (b)) return false;
  return tmscm_to_bool (b);
}

int
as_int (object obj) {
  if (auto* n= native_value (obj)) {
    if (n->kind == native_object_kind::integer) return std::get<int> (n->value);
    if (is_int (obj)) throw std::invalid_argument ("Expected an exact integer");
    return 0;
  }
  tmscm i= object_to_tmscm (obj);
  if (!tmscm_is_int (i)) return 0;
  return tmscm_to_int (i);
}

double
as_double (object obj) {
  if (auto* n= native_value (obj)) {
    if (n->kind == native_object_kind::real) return std::get<double> (n->value);
    if (n->kind == native_object_kind::integer) return std::get<int> (n->value);
    return 0.0;
  }
  tmscm x= object_to_tmscm (obj);
  if (!tmscm_is_double (x)) return 0.0;
  return tmscm_to_double (x);
}

string
as_string (object obj) {
  if (auto* n= native_value (obj)) {
    if (n->kind != native_object_kind::text) return "";
    // Strings exported to the remaining Scheme runtime may be mutated there.
    return n->bridge ? tmscm_to_string (tmscm_root_value (n->bridge)) : std::get<string> (n->value);
  }
  tmscm s= object_to_tmscm (obj);
  if (!tmscm_is_string (s)) return "";
  return tmscm_to_string (s);
}

string
as_symbol (object obj) {
  if (auto* n= native_value (obj))
    return n->kind == native_object_kind::symbol ? std::get<string> (n->value) : string ("");
  tmscm s= object_to_tmscm (obj);
  if (!tmscm_is_symbol (s)) return "";
  return tmscm_to_symbol (s);
}

tree
as_tree (object obj) {
  if (auto* n= native_value (obj))
    return n->kind == native_object_kind::source_tree ? std::get<tree> (n->value) : tree ();
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
  if (native_value (obj)) return list<string> ();
  tmscm l= object_to_tmscm (obj);
  if (!tmscm_is_list_string (l)) return list<string> ();
  return tmscm_to_list_string (l);
}

list<tree>
as_list_tree (object obj) {
  if (native_value (obj)) return list<tree> ();
  tmscm l= object_to_tmscm (obj);
  if (!tmscm_is_list_tree (l)) return list<tree> ();
  return tmscm_to_list_tree (l);
}

path
as_path (object obj) {
  if (native_value (obj)) return path ();
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
  if (auto* n= native_value (obj)) {
    if (n->kind == native_object_kind::location) return std::get<url> (n->value);
    if (n->kind != native_object_kind::text) return url ("");
#ifdef OS_MINGW
    return url_system (as_string (obj));
#else
    return url (as_string (obj));
#endif
  }
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
  if (auto* n= native_value (obj)) {
    return n->kind == native_object_kind::change ?
      std::get<modification> (n->value) : mod_assign (path (), "");
  }
  tmscm m= object_to_tmscm (obj);
  if (!tmscm_is_modification (m))
    return mod_assign (path (), "");
  return tmscm_to_modification (m);
}

patch
as_patch (object obj) {
  if (auto* n= native_value (obj))
    return n->kind == native_object_kind::patch_value ? std::get<patch> (n->value) : patch (array<patch> ());
  tmscm p= object_to_tmscm (obj);
  if (!tmscm_is_patch (p))
    return patch (array<patch> ());
  return tmscm_to_patch (p);
}

widget
as_widget (object obj) {
  if (native_value (obj)) return widget ();
  tmscm w= object_to_tmscm (obj);
  if (!tmscm_is_widget (w)) return widget ();
  return tmscm_to_widget (w);
}

widget
take_widget (object obj) {
  if (native_value (obj)) return widget ();
  tmscm w= object_to_tmscm (obj);
  if (!tmscm_is_widget (w)) return widget ();
  return tmscm_take_widget (w);
}

object
tree_to_stree (scheme_tree t) {
  return tmscm_to_object (
    scheme_tree_to_tmscm (tree_to_scheme_tree (t)));
}

tree
stree_to_tree (object obj) {
  return scheme_tree_to_tree (as_scheme_tree (obj));
}

tree
content_to_tree (object obj) {
  if (is_tree (obj)) return as_tree (obj);
  if (is_string (obj)) return tree (as_string (obj));
  if (native_value (obj)) return "?";
  return tmscm_to_content (object_to_tmscm (obj));
    // return as_tree (call ("content->tree", obj));
}

object
string_to_object (string s) {
  return tmscm_to_object (scm_read (scm_open_input_string (string_to_tmscm (s))));
}

string
object_to_string (object obj) {
  return tmscm_to_string (scm_object_to_string (object_to_tmscm (obj), SCM_UNDEFINED));
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

class local_object_command_rep: public command_rep {
  object obj;
  athena_actor_id actor_id;
  athena_view_id view_id;
  SchemeCapabilitySet capabilities;
public:
  local_object_command_rep (object obj2):
    obj (obj2), actor_id (ATHENA_NO_ACTOR), view_id (ATHENA_NO_VIEW),
    capabilities (SCHEME_CAPABILITY_NONE) {
    const SchemeExecutionContext* context= current_scheme_execution_context ();
    if (context != nullptr && context->actor_id != ATHENA_NO_ACTOR) {
      actor_id= context->actor_id;
      view_id= context->view_id;
      capabilities= context->capabilities;
    }
  }
  void apply () override {
    if (actor_id != ATHENA_NO_ACTOR) {
      const SchemeExecutionContext* context= current_scheme_execution_context ();
      ASSERT (context != nullptr && context->actor_id == actor_id &&
              context->view_id == view_id && context->capabilities == capabilities,
              "execution-local Scheme command invoked outside its owner context");
    }
    (void) call_scheme (object_to_tmscm (obj));
  }
  void apply (object args) override {
    if (actor_id != ATHENA_NO_ACTOR) {
      const SchemeExecutionContext* context= current_scheme_execution_context ();
      ASSERT (context != nullptr && context->actor_id == actor_id &&
              context->view_id == view_id && context->capabilities == capabilities,
              "execution-local Scheme command invoked outside its owner context");
    }
    (void) call_scheme (object_to_tmscm (obj),
                        array_lookup (as_array_object (args)));
  }
  tm_ostream& print (tm_ostream& out) override {
    if (actor_id != ATHENA_NO_ACTOR)
      return out << "<local-actor-command " << actor_id << ":" << view_id << ">";
    object source= procedure_source (obj);
    object bis= source == object (false) ? obj : source;
    return out << "<command " << bis << ">"; }
};

command
as_command (object obj) {
  return tm_new<local_object_command_rep> (obj);
}

class actor_object_command_rep: public command_rep {
  object obj;
  athena_actor_id actor_id;
  athena_view_id view_id;
  SchemeCapabilitySet capabilities;
  athena_scheme_handle_id handle;
public:
  actor_object_command_rep (object obj2):
    obj (), actor_id (ATHENA_NO_ACTOR), view_id (ATHENA_NO_VIEW),
    capabilities (SCHEME_CAPABILITY_NONE), handle (ATHENA_NO_SCHEME_HANDLE) {
    const SchemeExecutionContext* context= current_scheme_execution_context ();
    if (context != nullptr && context->actor_id != ATHENA_NO_ACTOR) {
      actor_id= context->actor_id;
      view_id= context->view_id;
      capabilities= context->capabilities;
      handle= scheme_command_handle_acquire (object_to_tmscm (obj2));
    }
    else obj= obj2;
  }
  ~actor_object_command_rep () override {
    if (handle == ATHENA_NO_SCHEME_HANDLE) return;
    scheme_command_handle_release (handle);
  }
  void apply () override {
    if (handle == ATHENA_NO_SCHEME_HANDLE) {
      (void) call_scheme (object_to_tmscm (obj));
      return;
    }
    if (!scheme_command_handle_retain (handle)) return;
    actor_command_ticket ticket= buffer_actor::submit_to (
      actor_id, actor_command_kind::invoke_scheme_handle, view_id,
      ATHENA_NO_BLOB, ATHENA_NO_BLOB, capabilities, handle);
    if (!ticket) scheme_command_handle_release (handle);
  }
  void apply (object args) override {
    if (handle == ATHENA_NO_SCHEME_HANDLE) {
      (void) call_scheme (object_to_tmscm (obj),
                          array_lookup (as_array_object (args)));
      return;
    }
    athena_scheme_handle_id arguments=
      scheme_command_handle_acquire (object_to_tmscm (args));
    if (!scheme_command_handle_retain (handle)) {
      scheme_command_handle_release (arguments);
      return;
    }
    actor_command_ticket ticket= buffer_actor::submit_to (
      actor_id, actor_command_kind::invoke_scheme_handle, view_id,
      ATHENA_NO_BLOB, ATHENA_NO_BLOB, capabilities,
      handle, arguments);
    if (!ticket) {
      scheme_command_handle_release (handle);
      scheme_command_handle_release (arguments);
    }
  }
  tm_ostream& print (tm_ostream& out) override {
    if (handle != ATHENA_NO_SCHEME_HANDLE)
      return out << "<actor-command " << actor_id << ":" << view_id << ">";
    object source= procedure_source (obj);
    object bis= source == object (false) ? obj : source;
    return out << "<command " << bis << ">"; }
};

command
as_actor_command (object obj) {
  return tm_new<actor_object_command_rep> (obj);
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
    scheme_command_handle_release (handle);
  }
  tm_ostream& print (tm_ostream& out) override {
    if (handle != ATHENA_NO_SCHEME_HANDLE)
      return out << "<actor-widget-promise " << actor_id << ":" << view_id
                 << ">";
    return out << obj;
  }
  widget eval () override {
    if (handle != ATHENA_NO_SCHEME_HANDLE) {
      if (!scheme_command_handle_retain (handle))
        FAILED ("Scheme widget promise handle is no longer live");
      actor_command_record result;
      if (buffer_actor::invoke_on (
            actor_id, actor_command_kind::evaluate_widget_handle, view_id,
            ATHENA_NO_BLOB, ATHENA_NO_BLOB, &result,
            SCHEME_CAPABILITY_BUFFER, handle))
        return actor_ui_take_widget (result.argument[0]);
      scheme_command_handle_release (handle);
      FAILED ("BufferActor rejected widget promise evaluation");
      return glue_widget ();
    }
    tmscm result= call_scheme (object_to_tmscm (obj));
    if (tmscm_is_widget (result)) return tmscm_to_widget (result);
    // Match actor-owned widget promises: Scheme already reports evaluation
    // failures, so a bad extension must not escalate into a second fatal UI
    // exception.  Keep the lazy menu tree usable with an empty placeholder.
    return glue_widget ();
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
  return call ("primitive-eval", expr); }
object eval_file (string name) {
  return tmscm_to_object (eval_scheme_file (name)); }
bool exec_file (url u) {
  object ret= eval_file (materialize (u));
  return ret != object ("#<unspecified>"); }

namespace {
template<typename... Arguments>
object named_call (string name, const Arguments&... values) {
  array<object> arguments;
  (..., (arguments << values));
  return call (name, arguments);
}
}

object call (const char* fun) { return named_call (fun); }
object call (const char* fun, object a1) { return named_call (fun, a1); }
object call (const char* fun, object a1, object a2) { return named_call (fun, a1, a2); }
object call (const char* fun, object a1, object a2, object a3) { return named_call (fun, a1, a2, a3); }
object call (const char* fun, object a1, object a2, object a3, object a4) { return named_call (fun, a1, a2, a3, a4); }
object call (const char* fun, array<object> a) { return call (string (fun), a); }

object call (string fun) { return named_call (fun); }
object call (string fun, object a1) { return named_call (fun, a1); }
object call (string fun, object a1, object a2) { return named_call (fun, a1, a2); }
object call (string fun, object a1, object a2, object a3) { return named_call (fun, a1, a2, a3); }
object call (string fun, object a1, object a2, object a3, object a4) { return named_call (fun, a1, a2, a3, a4); }
object call (string fun, array<object> a) {
  object result;
  if (native_dispatch_call (fun, a, result)) return result;
  // Runtime builtins and remaining closures are removed by subsequent stages.
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
  if (!preferences_ok) return;
  if (get_user_preference_callback (var) == "notify-viewport-follow") {
    notify_viewport_follow ();
    return;
  }
  application_preference_notify (var);
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

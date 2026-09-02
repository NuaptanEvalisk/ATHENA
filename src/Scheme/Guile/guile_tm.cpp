
/******************************************************************************
* MODULE     : guile_tm.cpp
* DESCRIPTION: Interface to Guile
* COPYRIGHT  : (C) 1999-2011  Joris van der Hoeven and Massimiliano Gubinelli
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifdef OS_MINGW
  //FIXME: if this include is not here we have compilation problems on mingw32
  //       (probably name clashes with Windows headers)
  //#include "tree.hpp"
#endif
  //#include "Glue/glue.hpp"

#include "guile_tm.hpp"
#include "blackbox.hpp"
#include "file.hpp"
#include "../Scheme/glue.hpp"
#include "convert.hpp" // tree_to_texmacs (should not belong here)
#include "scheme_execution_context.hpp"

#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

/******************************************************************************
 * Installation of guile and initialization of guile
 ******************************************************************************/
static void (*old_call_back) (int, char**)= NULL;
static scheme_compile_callback old_compile_callback= nullptr;
static SCM execution_context_fluid= SCM_UNDEFINED;
static std::atomic<bool> scheme_runtime_initialized (false);
static thread_local const SchemeExecutionContext* fluid_execution_context=
  nullptr;

struct scheme_execution_request {
  const SchemeExecutionContext* context;
  scheme_execution_callback callback;
  void* data;
};

class fluid_execution_scope {
  const SchemeExecutionContext* previous;

public:
  explicit fluid_execution_scope (const SchemeExecutionContext* context):
    previous (fluid_execution_context) {
    fluid_execution_context= context;
  }

  ~fluid_execution_scope () { fluid_execution_context= previous; }
};

static SCM
invoke_scheme_execution_request (void* raw) {
  scheme_execution_request* request=
    static_cast<scheme_execution_request*> (raw);
  SchemeExecutionScope scope (*request->context);
  fluid_execution_scope fluid_scope (request->context);
  return request->callback (request->data);
}

tmscm
scheme_with_execution_context (const SchemeExecutionContext& context,
                               scheme_execution_callback callback,
                               void* data) {
  if (fluid_execution_context == &context) return callback (data);
  ASSERT (!scm_is_eq (execution_context_fluid, SCM_UNDEFINED),
          "Scheme execution context fluid is not initialized");
  scheme_execution_request request= { &context, callback, data };
  SCM value= scm_from_pointer (
    const_cast<SchemeExecutionContext*> (&context), nullptr);
  return scm_c_with_fluid (execution_context_fluid, value,
                           invoke_scheme_execution_request, &request);
}

bool
scheme_runtime_is_initialized () noexcept {
  return scheme_runtime_initialized.load (std::memory_order_acquire);
}

static SCM
invoke_in_current_execution_context (scheme_execution_callback callback,
                                     void* data) {
  const SchemeExecutionContext* context= current_scheme_execution_context ();
  if (context == nullptr) return callback (data);
  return scheme_with_execution_context (*context, callback, data);
}

static void
athena_auto_compile_callback (const char* source, int compiling, void* data) {
  (void) data;
  if (old_compile_callback != nullptr)
    old_compile_callback (compiling != 0,
                          source == nullptr ? string () : string (source));
}

static void
new_call_back (void *closure, int argc, char** argv) {
  (void) closure;
  
  old_call_back (argc, argv);
}


int guile_argc;
char **guile_argv;

/******************************************************************************
 * Owner-affine Scheme roots and deferred C++ destruction
 ******************************************************************************/

struct scheme_execution_shard;

struct tmscm_root_handle {
  tmscm value;
  scheme_execution_shard* owner;
  tmscm_root_handle* next;
  std::atomic<unsigned char> state;

  explicit tmscm_root_handle (scheme_execution_shard* owner2):
    value (SCM_UNDEFINED), owner (owner2), next (nullptr), state (0) {}
};

struct deferred_blackbox {
  blackbox value;
  scheme_execution_shard* owner;
  deferred_blackbox* next;

  deferred_blackbox (blackbox value2, scheme_execution_shard* owner2):
    value (value2), owner (owner2), next (nullptr) {}
};

struct scheme_execution_shard {
  std::thread::id owner_thread;
  std::atomic<tmscm_root_handle*> released_roots;
  std::atomic<deferred_blackbox*> released_blackboxes;
  std::vector<tmscm_root_handle*> roots;
  std::vector<tmscm_root_handle*> free_roots;

  scheme_execution_shard ():
    owner_thread (std::this_thread::get_id ()), released_roots (nullptr),
    released_blackboxes (nullptr) {}
};

struct scheme_shard_registry {
  std::mutex lock;
  std::vector<scheme_execution_shard*> shards;
};

static scheme_shard_registry&
scheme_shards () {
  static scheme_shard_registry* registry= new scheme_shard_registry;
  return *registry;
}

static thread_local scheme_execution_shard* current_scheme_shard= nullptr;

static scheme_execution_shard*
scheme_current_shard () {
  if (current_scheme_shard != nullptr) return current_scheme_shard;
  scheme_execution_shard* shard= new scheme_execution_shard;
  scheme_shard_registry& registry= scheme_shards ();
  {
    std::lock_guard<std::mutex> guard (registry.lock);
    registry.shards.push_back (shard);
  }
  current_scheme_shard= shard;
  return shard;
}

template<typename Node>
static void
scheme_publish_release (std::atomic<Node*>& head, Node* node) noexcept {
  Node* previous= head.load (std::memory_order_relaxed);
  do node->next= previous;
  while (!head.compare_exchange_weak (
    previous, node, std::memory_order_release, std::memory_order_relaxed));
}

static void
scheme_drain_shard (scheme_execution_shard* shard) {
  ASSERT (shard->owner_thread == std::this_thread::get_id (),
          "Scheme execution shard must be drained by its owner");
  deferred_blackbox* boxes=
    shard->released_blackboxes.exchange (nullptr, std::memory_order_acquire);
  while (boxes != nullptr) {
    deferred_blackbox* next= boxes->next;
    tm_delete (boxes);
    boxes= next;
  }

  tmscm_root_handle* roots=
    shard->released_roots.exchange (nullptr, std::memory_order_acquire);
  while (roots != nullptr) {
    tmscm_root_handle* next= roots->next;
    scm_gc_unprotect_object (roots->value);
    roots->value= SCM_UNDEFINED;
    roots->next= nullptr;
    roots->state.store (0, std::memory_order_release);
    shard->free_roots.push_back (roots);
    roots= next;
  }
}

void
scheme_runtime_safe_point () {
  scheme_drain_shard (scheme_current_shard ());
}

void
scheme_runtime_drain_all () {
  scheme_shard_registry& registry= scheme_shards ();
  std::vector<scheme_execution_shard*> snapshot;
  {
    std::lock_guard<std::mutex> guard (registry.lock);
    snapshot= registry.shards;
  }
  const std::thread::id current= std::this_thread::get_id ();
  for (scheme_execution_shard* shard: snapshot) {
    if (shard->owner_thread == current) scheme_drain_shard (shard);
    else {
      ASSERT (shard->released_roots.load (std::memory_order_acquire) == nullptr,
              "foreign Scheme shard retained deferred roots at shutdown");
      ASSERT (shard->released_blackboxes.load (std::memory_order_acquire) == nullptr,
              "foreign Scheme shard retained deferred C++ objects at shutdown");
    }
  }
}

tmscm_root_handle*
tmscm_root_acquire (tmscm obj) {
  scheme_execution_shard* shard= scheme_current_shard ();
  scheme_drain_shard (shard);
  tmscm_root_handle* handle;
  if (!shard->free_roots.empty ()) {
    handle= shard->free_roots.back ();
    shard->free_roots.pop_back ();
  }
  else {
    handle= new tmscm_root_handle (shard);
    shard->roots.push_back (handle);
  }
  handle->value= scm_gc_protect_object (obj);
  handle->state.store (1, std::memory_order_release);
  return handle;
}

tmscm
tmscm_root_value (const tmscm_root_handle* handle) {
  ASSERT (handle != nullptr &&
          handle->state.load (std::memory_order_acquire) == 1,
          "live Scheme root expected");
  return handle->value;
}

void
tmscm_root_release (tmscm_root_handle* handle) noexcept {
  if (handle == nullptr) return;
  unsigned char expected= 1;
  if (!handle->state.compare_exchange_strong (
        expected, 2, std::memory_order_acq_rel, std::memory_order_relaxed))
    return;
  scheme_publish_release (handle->owner->released_roots, handle);
}

namespace {

struct scheme_command_handle_registry {
  std::mutex lock;
  std::unordered_map<athena_scheme_handle_id, tmscm_root_handle*> handles;
  athena_scheme_handle_id next_id= 1;
};

scheme_command_handle_registry&
command_handle_registry () {
  static scheme_command_handle_registry registry;
  return registry;
}

} // namespace

athena_scheme_handle_id
scheme_command_handle_acquire (tmscm command) {
  tmscm_root_handle* root= tmscm_root_acquire (command);
  scheme_command_handle_registry& registry= command_handle_registry ();
  std::lock_guard<std::mutex> guard (registry.lock);
  athena_scheme_handle_id id= registry.next_id++;
  if (id == ATHENA_NO_SCHEME_HANDLE) id= registry.next_id++;
  registry.handles.emplace (id, root);
  return id;
}

tmscm
scheme_command_handle_value (athena_scheme_handle_id id) {
  scheme_command_handle_registry& registry= command_handle_registry ();
  std::lock_guard<std::mutex> guard (registry.lock);
  auto found= registry.handles.find (id);
  if (found == registry.handles.end ()) return SCM_UNDEFINED;
  return tmscm_root_value (found->second);
}

void
scheme_command_handle_release (athena_scheme_handle_id id) noexcept {
  if (id == ATHENA_NO_SCHEME_HANDLE) return;
  tmscm_root_handle* root= nullptr;
  scheme_command_handle_registry& registry= command_handle_registry ();
  {
    std::lock_guard<std::mutex> guard (registry.lock);
    auto found= registry.handles.find (id);
    if (found == registry.handles.end ()) return;
    root= found->second;
    registry.handles.erase (found);
  }
  tmscm_root_release (root);
}

void
start_scheme (int argc, char** argv, void (*call_back) (int, char**),
              scheme_compile_callback compile_callback) {
  guile_argc = argc;
  guile_argv = argv;
  old_call_back= call_back;
  old_compile_callback= compile_callback;
  scm_athena_set_auto_compile_callback (athena_auto_compile_callback, nullptr);
  scm_boot_guile (argc, argv, new_call_back, 0);
  scm_athena_set_auto_compile_callback (nullptr, nullptr);
  old_compile_callback= nullptr;
}



/******************************************************************************
 * Catching errors (with thanks to Dale P. Smith)
 ******************************************************************************/

SCM
TeXmacs_lazy_catcher (void *data, SCM tag, SCM throw_args) {
  SCM eport= scm_current_error_port();
  scm_handle_by_message_noexit (data, tag, throw_args);
  scm_force_output (eport);
  scm_ithrow (tag, throw_args, 1);
  return SCM_UNSPECIFIED; /* never returns */
}

SCM
TeXmacs_catcher (void *data, SCM tag, SCM args) {
  (void) data;
  return scm_cons (tag, args);
}

/******************************************************************************
 * Evaluation of files
 ******************************************************************************/

#ifndef DEBUG_ON
static SCM
TeXmacs_lazy_eval_file (char *file) {
  return scm_internal_lazy_catch (SCM_BOOL_T,
                                  (scm_t_catch_body) scm_c_primitive_load, file,
                                  (scm_t_catch_handler) TeXmacs_lazy_catcher, file);
}
#endif

static SCM
TeXmacs_eval_file (char *file) {
#ifndef DEBUG_ON
  return scm_internal_catch (SCM_BOOL_T,
                             (scm_t_catch_body) TeXmacs_lazy_eval_file, file,
                             (scm_t_catch_handler) TeXmacs_catcher, file);
#else
  return 	scm_c_primitive_load (file);										 
#endif
}

static SCM
TeXmacs_eval_file_in_context (void* file) {
  return TeXmacs_eval_file (static_cast<char*> (file));
}

SCM
eval_scheme_file (string file) {
    //static int cumul= 0;
    //timer tm;
  if (DEBUG_STD) debug_std << "Evaluating " << file << "...\n";
  scheme_runtime_safe_point ();
  c_string _file (file);
  SCM result= invoke_in_current_execution_context (
    TeXmacs_eval_file_in_context, _file);
  scheme_runtime_safe_point ();
    //int extra= tm->watch (); cumul += extra;
    //cout << extra << "\t" << cumul << "\t" << file << "\n";
  return result;
}

/******************************************************************************
 * Evaluation of strings
 ******************************************************************************/

#ifndef DEBUG_ON
static SCM
TeXmacs_lazy_eval_string (char *s) {
  return scm_internal_lazy_catch (SCM_BOOL_T,
                                  (scm_t_catch_body) scm_c_eval_string, s,
                                  (scm_t_catch_handler) TeXmacs_lazy_catcher, s);
}
#endif

static SCM
TeXmacs_eval_string (char *s) {
#ifndef DEBUG_ON
  return scm_internal_catch (SCM_BOOL_T,
                             (scm_t_catch_body) TeXmacs_lazy_eval_string, s,
                             (scm_t_catch_handler) TeXmacs_catcher, s);
#else
  return  scm_c_eval_string(s);
#endif
}

static SCM
TeXmacs_eval_string_in_context (void* source) {
  return TeXmacs_eval_string (static_cast<char*> (source));
}

SCM
eval_scheme (string s) {
    // cout << "Eval] " << s << "\n";
  scheme_runtime_safe_point ();
  c_string _s (s);
  SCM result= invoke_in_current_execution_context (
    TeXmacs_eval_string_in_context, _s);
  scheme_runtime_safe_point ();
  return result;
}

/******************************************************************************
 * Using scheme objects as functions
 ******************************************************************************/

struct arg_list { int  n; SCM* a; };

static SCM
TeXmacs_call (arg_list* args) {
  switch (args->n) {
    case 0: return scm_call_0 (args->a[0]); break;
    case 1: return scm_call_1 (args->a[0], args->a[1]); break;
    case 2: return scm_call_2 (args->a[0], args->a[1], args->a[2]); break;
    case 3:
      return scm_call_3 (args->a[0], args->a[1], args->a[2], args->a[3]); break;
    default:
    {
      int i;
      SCM l= SCM_NULL;
      for (i=args->n; i>=1; i--)
        l= scm_cons (args->a[i], l);
      return scm_apply_0 (args->a[0], l);
    }
  }
}

#ifndef DEBUG_ON
static SCM
TeXmacs_lazy_call_scm (arg_list* args) {
  return scm_internal_lazy_catch (SCM_BOOL_T,
                                  (scm_t_catch_body) TeXmacs_call, (void*) args,
                                  (scm_t_catch_handler) TeXmacs_lazy_catcher, (void*) args);
}
#endif

static SCM
TeXmacs_call_scm_unbound (arg_list *args) {
  scheme_runtime_safe_point ();
#ifndef DEBUG_ON
  SCM result= scm_internal_catch (
    SCM_BOOL_T, (scm_t_catch_body) TeXmacs_lazy_call_scm, (void*) args,
    (scm_t_catch_handler) TeXmacs_catcher, (void*) args);
#else
  SCM result= TeXmacs_call(args);
#endif
  scheme_runtime_safe_point ();
  return result;
}

static SCM
TeXmacs_call_scm_in_context (void* raw) {
  return TeXmacs_call_scm_unbound (static_cast<arg_list*> (raw));
}

static SCM
TeXmacs_call_scm (arg_list* args) {
  return invoke_in_current_execution_context (TeXmacs_call_scm_in_context,
                                               args);
}

SCM
call_scheme (SCM fun) {
// uncomment block to display scheme call
/*
  SCM ENDLscm= scm_from_locale_string ("\n");
  SCM source=scm_procedure_source(fun);
  scm_call_2(scm_c_eval_string("display*"), source, ENDLscm);
  scm_call_2(scm_c_eval_string("display*"),  scm_procedure_environment(fun), ENDLscm);
  scm_call_2(scm_c_eval_string("display*"),  scm_procedure_properties(fun), ENDLscm);
  //DBGFMT1(debug_tmwidgets, source);
*/
  SCM a[]= { fun }; arg_list args= { 0, a };
  return TeXmacs_call_scm (&args);
}

SCM
call_scheme (SCM fun, SCM a1) {
  SCM a[]= { fun, a1 }; arg_list args= { 1, a };
  return TeXmacs_call_scm (&args);
}

SCM
call_scheme (SCM fun, SCM a1, SCM a2) {
  SCM a[]= { fun, a1, a2 }; arg_list args= { 2, a };
  return TeXmacs_call_scm (&args);
}

SCM
call_scheme (SCM fun, SCM a1, SCM a2, SCM a3) {
  SCM a[]= { fun, a1, a2, a3 }; arg_list args= { 3, a };
  return TeXmacs_call_scm (&args);
}

SCM
call_scheme (SCM fun, SCM a1, SCM a2, SCM a3, SCM a4) {
  SCM a[]= { fun, a1, a2, a3, a4 }; arg_list args= { 4, a };
  return TeXmacs_call_scm (&args);
}

SCM
call_scheme (SCM fun, array<SCM> a) {
  const int n= N(a);
  STACK_NEW_ARRAY(scm, SCM, n+1);
  int i;
  scm[0]= fun;
  for (i=0; i<n; i++) scm[i+1]= a[i];
  arg_list args= { n, scm };
  SCM ret= TeXmacs_call_scm (&args);
  STACK_DELETE_ARRAY(scm);
  return ret;
}


/******************************************************************************
 * Miscellaneous routines for use by glue only
 ******************************************************************************/

string
scheme_dialect () {
  return "guile-d";
}

#define SET_SMOB(smob,data,type)   \
SCM_NEWSMOB (smob, type, data);
#define GET_SMOB_DATA(smob) ((void*) SCM_SMOB_DATA (smob))


/******************************************************************************
 * Booleans
 ******************************************************************************/


SCM
bool_to_scm (bool flag) {
  return scm_bool2scm (flag);
}

/******************************************************************************
 * Integers
 ******************************************************************************/

SCM
int_to_scm (int i) {
  return scm_long2scm ((long) i);
}

SCM
long_to_scm (long l) {
  return scm_long2scm (l);
}

/******************************************************************************
 * Floating point numbers
 ******************************************************************************/
SCM
double_to_scm (double i) {
  return scm_double2scm (i);
}

/******************************************************************************
 * Strings
 ******************************************************************************/

static char*
athena_scm_string_to_bytes (SCM value, size_t* length) {
  size_t count= scm_c_string_length (value);
  for (size_t i=0; i<count; ++i)
    if (SCM_CHAR (scm_c_string_ref (value, i)) > 0xff)
      return scm_to_utf8_stringn (value, length);
  return scm_to_latin1_stringn (value, length);
}

tmscm
string_to_tmscm (string s) {
  c_string _s (s);
  // TeXmacs strings are byte strings (typically Cork or UTF-8), matching
  // Guile 1.8 semantics.  Locale decoding corrupts both under embedded
  // Guile 3 when the process still has the C locale.
  SCM r= scm_from_latin1_stringn (_s, N(s));
  return r;
}

string
tmscm_to_string (tmscm s) {
  guile_str_size_t len_r;
  char* _r= athena_scm_string_to_bytes (s, &len_r);
  string r (_r, len_r);
#ifdef OS_WIN32
  scm_must_free(_r);
#else
  free (_r);
#endif
  return r;
}

/******************************************************************************
 * Symbols
 ******************************************************************************/

tmscm
symbol_to_tmscm (string s) {
  c_string _s (s);
  SCM r= scm_from_latin1_symboln (_s, N(s));
  return r;
}

string
tmscm_to_symbol (tmscm s) {
  guile_str_size_t len_r;
  char* _r= athena_scm_string_to_bytes (scm_symbol_to_string (s), &len_r);
  string r (_r, len_r);
#ifdef OS_WIN32
  scm_must_free(_r);
#else
  free (_r);
#endif
  return r;
}

/******************************************************************************
 * Blackbox
 ******************************************************************************/

static scm_t_bits blackbox_tag;
#define SCM_BLACKBOXP(t) SCM_SMOB_PREDICATE (blackbox_tag, t)

bool
tmscm_is_blackbox (tmscm t) {
  return SCM_BLACKBOXP (t);
}

tmscm
blackbox_to_tmscm (blackbox b) {
  scheme_execution_shard* shard= scheme_current_shard ();
  scheme_drain_shard (shard);
  SCM blackbox_smob;
  deferred_blackbox* payload= tm_new<deferred_blackbox> (b, shard);
  SET_SMOB (blackbox_smob, (void*) payload, blackbox_tag);
  return blackbox_smob;
}

blackbox
tmscm_to_blackbox (tmscm blackbox_smob) {
  deferred_blackbox* payload=
    (deferred_blackbox*) GET_SMOB_DATA (blackbox_smob);
  ASSERT (payload != nullptr, "live blackbox expected");
  return payload->value;
}

static SCM
mark_blackbox (SCM blackbox_smob) {
  (void) blackbox_smob;
  return SCM_BOOL_F;
}

static size_t
free_blackbox (SCM blackbox_smob) {
  deferred_blackbox* payload=
    (deferred_blackbox*) GET_SMOB_DATA (blackbox_smob);
  if (payload == nullptr) return 0;
  SCM_SET_SMOB_DATA (blackbox_smob, 0);
  scheme_publish_release (payload->owner->released_blackboxes, payload);
  return 0;
}

int
print_blackbox (SCM blackbox_smob, SCM port, scm_print_state *pstate) {
  (void) pstate;
  string s = "<blackbox>";
  int type_ = type_box (tmscm_to_blackbox(blackbox_smob)) ;
  if (type_ == type_helper<tree>::id) {
    tree t= tmscm_to_tree (blackbox_smob);
    s= "<tree " * tree_to_texmacs (t) * ">";
  }
  else if (type_ == type_helper<observer>::id) {
    s= "<observer>";
  }
  else if (type_ == type_helper<widget>::id) {
    s= "<widget>";
  }
  else if (type_ == type_helper<promise<widget> >::id) {
    s= "<promise-widget>";
  }
  else if (type_ == type_helper<command>::id) {
    command cmd= tmscm_to_command (blackbox_smob);
    s= print_to_string<command> (cmd);
  }
  else if (type_ == type_helper<url>::id) {
    url u= tmscm_to_url (blackbox_smob);
    s= "<url " * as_string (u) * ">";
  }
  else if (type_ == type_helper<modification>::id) {
    s= "<modification>";
  }
  else if (type_ == type_helper<patch>::id) {
    s= "<patch>";
  }
  
  scm_display (string_to_tmscm (s), port);
  return 1;
}

static SCM
cmp_blackbox (SCM t1, SCM t2) {
  return scm_bool2scm (tmscm_to_blackbox (t1) == tmscm_to_blackbox (t2));
}



/******************************************************************************
 * Initialization
 ******************************************************************************/


#ifdef SCM_NEWSMOB
void
initialize_smobs () {
  blackbox_tag= scm_make_smob_type (const_cast<char*> ("blackbox"), 0);
  scm_set_smob_mark (blackbox_tag, mark_blackbox);
  scm_set_smob_free (blackbox_tag, free_blackbox);
  scm_set_smob_print (blackbox_tag, print_blackbox);
  scm_set_smob_equalp (blackbox_tag, cmp_blackbox);
}

#else

scm_smobfuns blackbox_smob_funcs = {
  mark_blackbox, free_blackbox, print_blackbox, cmp_blackbox
};


void
initialize_smobs () {
  blackbox_tag= scm_newsmob (&blackbox_smob_funcs);
}

#endif

void
initialize_scheme () {
  const char* init_prg =
#ifdef DEBUG_ON
  "(debug-enable 'backtrace)\n"
#endif
  "\n"
  "(define (display-to-string obj)\n"
  "  (call-with-output-string\n"
  "    (lambda (port) (display obj port))))\n"
  "(define (object->string obj)\n"
  "  (call-with-output-string\n"
  "    (lambda (port) (write obj port))))\n"
  "\n"
  "(define (texmacs-version) \"" ATHENA_VERSION "\")\n"
  "(define (texmacs-compat-version) \"" TEXMACS_COMPAT_VERSION "\")\n"
  "(define (texmacs-build-user) \"" BUILD_USER "\")\n"
  "(define (texmacs-build-date) \"" BUILD_DATE "\")\n"
  "(define (texmacs-host-os) \"" HOST_OS "\")\n"
  "(define (texmacs-host-vendor) \"" HOST_VENDOR "\")\n"
  "(define (texmacs-host-cpu) \"" HOST_CPU "\")\n"
  "(define (texmacs-build-info)\n"
  "  `((version . ,(texmacs-version))\n"
  "    (compat-version . ,(texmacs-compat-version))\n"
  "    (build-user . ,(texmacs-build-user))\n"
  "    (build-date . ,(texmacs-build-date))\n"
  "    (host-os . ,(texmacs-host-os))\n"
  "    (host-vendor . ,(texmacs-host-vendor))\n"
  "    (host-cpu . ,(texmacs-host-cpu))))";
  
  execution_context_fluid= scm_make_thread_local_fluid (SCM_BOOL_F);
  scm_c_define ("%athena-execution-context-fluid", execution_context_fluid);
  scm_c_eval_string (init_prg);
  initialize_smobs ();
  initialize_glue ();
  scheme_runtime_initialized.store (true, std::memory_order_release);
  
    // uncomment to have a guile repl available at startup	
    //	gh_repl(guile_argc, guile_argv);
    //scm_shell (guile_argc, guile_argv);
  
  
}

void
finalize_scheme_bootstrap () {
  scheme_runtime_drain_all ();
  scm_athena_flush_deferred_auto_compilation ();
}

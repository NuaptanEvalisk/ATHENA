/* Native runtime state for ATHENA's Scheme module and definition system.

   Copyright (C) 2026 Nuaptan Felix Evalisk

   This file is part of ATHENA's Guile runtime and is distributed under the
   GNU Lesser General Public License version 3 or later.  */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdlib.h>
#include <string.h>

#include "libguile/athena-runtime.h"
#include "libguile/boolean.h"
#include "libguile/dynwind.h"
#include "libguile/error.h"
#include "libguile/eq.h"
#include "libguile/eval.h"
#include "libguile/foreign.h"
#include "libguile/gsubr.h"
#include "libguile/gc.h"
#include "libguile/hashtab.h"
#include "libguile/keywords.h"
#include "libguile/list.h"
#include "libguile/load.h"
#include "libguile/macros.h"
#include "libguile/modules.h"
#include "libguile/pairs.h"
#include "libguile/ports.h"
#include "libguile/procprop.h"
#include "libguile/procs.h"
#include "libguile/strports.h"
#include "libguile/strings.h"
#include "libguile/symbols.h"
#include "libguile/syntax.h"
#include "libguile/throw.h"
#include "libguile/threads.h"
#include "libguile/variable.h"

static SCM athena_root_module;
static SCM athena_module_records;
static SCM athena_sequential_modules;
static SCM athena_definition_sources;
static SCM athena_definition_names;
static SCM athena_definition_modules;
static SCM athena_property_contexts;
static SCM athena_lazy_modules;
static SCM athena_resolve_interface_proc;
static SCM athena_make_fresh_user_module_proc;
static SCM athena_module_use_proc;
static SCM athena_module_use_interfaces_proc;
static SCM athena_module_modified_proc;
static SCM athena_module_add_proc;
static SCM athena_module_name_proc;
static SCM athena_syntax_to_datum_proc;
static SCM athena_datum_to_syntax_proc;
static SCM athena_set_module_binder_proc;
static SCM athena_set_module_declarative_proc;
static SCM athena_set_module_duplicates_proc;
static SCM athena_lookup_duplicates_handlers_proc;
static SCM athena_global_binder_proc;
static SCM athena_original_module_export_proc;
static SCM athena_initial_root_bindings;
static SCM athena_module_construction_lock;

enum athena_module_state
{
  ATHENA_MODULE_UNLOADED,
  ATHENA_MODULE_LOADING,
  ATHENA_MODULE_LOADED,
  ATHENA_MODULE_FAILED
};

struct athena_runtime_thread;

struct athena_module_record
{
  SCM name;
  SCM module;
  SCM source_file;
  SCM interface;
  SCM failure_tag;
  SCM failure_args;
  enum athena_module_state state;
  struct athena_runtime_thread *owner;
  scm_i_pthread_cond_t changed;
};

struct athena_runtime_thread
{
  struct athena_module_record *waiting_on;
};

static scm_i_pthread_mutex_t athena_module_registry_lock =
  SCM_I_PTHREAD_MUTEX_INITIALIZER;
static scm_i_pthread_mutex_t athena_publication_lock =
  SCM_I_PTHREAD_MUTEX_INITIALIZER;
static SCM_THREAD_LOCAL struct athena_runtime_thread *athena_runtime_thread;

static struct athena_runtime_thread *
athena_current_runtime_thread (void)
{
  if (athena_runtime_thread == NULL)
    {
      athena_runtime_thread = calloc (1, sizeof (*athena_runtime_thread));
      if (athena_runtime_thread == NULL)
        scm_misc_error ("ATHENA runtime",
                        "could not allocate thread state", SCM_EOL);
    }
  return athena_runtime_thread;
}

static struct athena_module_record *
athena_module_record (SCM name, int create)
{
  SCM pointer;
  struct athena_module_record *record;

  scm_dynwind_begin (0);
  scm_dynwind_pthread_mutex_lock (&athena_module_registry_lock);
  pointer = scm_hash_ref (athena_module_records, name, SCM_BOOL_F);
  if (scm_is_true (pointer))
    {
      record = scm_to_pointer (pointer);
      scm_dynwind_end ();
      return record;
    }
  if (!create)
    {
      scm_dynwind_end ();
      return NULL;
    }

  record = scm_gc_malloc (sizeof (*record), "ATHENA module record");
  memset (record, 0, sizeof (*record));
  record->name = name;
  record->module = SCM_BOOL_F;
  record->source_file = SCM_BOOL_F;
  record->interface = SCM_BOOL_F;
  record->failure_tag = SCM_BOOL_F;
  record->failure_args = SCM_EOL;
  record->state = ATHENA_MODULE_UNLOADED;
  scm_i_pthread_cond_init (&record->changed, NULL);
  scm_hash_set_x (athena_module_records, name,
                  scm_from_pointer (record, NULL));
  scm_dynwind_end ();
  return record;
}

struct athena_module_wait
{
  scm_i_pthread_cond_t *condition;
  scm_i_pthread_mutex_t *mutex;
  int status;
};

static void *
athena_module_wait_without_guile (void *data)
{
  struct athena_module_wait *wait = data;
  wait->status = scm_i_pthread_cond_wait (wait->condition, wait->mutex);
  return NULL;
}

static int
athena_module_wait_cycle_p (struct athena_runtime_thread *thread,
                            struct athena_module_record *target)
{
  struct athena_runtime_thread *owner = target->owner;
  while (owner != NULL)
    {
      if (owner == thread)
        return 1;
      target = owner->waiting_on;
      if (target == NULL)
        return 0;
      owner = target->owner;
    }
  return 0;
}

/* Guile compiles a module file before its leading module declaration has
   established imports.  ATHENA module files use `texmacs-module' in that
   position, so the private runtime makes the compatibility forms core syntax
   for every compiler environment.  This is deliberately runtime-wide: it
   avoids rewriting thousands of Guile 1.8-era source files.  */
static void
athena_install_core_syntax (SCM runtime, SCM core, const char *name)
{
  SCM variable = scm_c_module_lookup (runtime, name);
  SCM symbol = scm_from_utf8_symbol (name);

  scm_c_module_define (core, name, scm_variable_ref (variable));
  scm_module_export (core, scm_list_1 (symbol));
}

static SCM
athena_table_prepend (SCM table, SCM key, SCM value)
{
  SCM old = scm_hashq_ref (table, key, SCM_EOL);
  return scm_hashq_set_x (table, key, scm_cons (value, old));
}

static SCM
athena_symbol (const char *name)
{
  return scm_from_utf8_symbol (name);
}

/* Guile 1.8's prefix keyword reader turns :option into a keyword, whereas
   Guile 3's native reader leaves it as a colon-prefixed symbol.  ATHENA source
   accepts both spellings, but Guile's own bootstrap must retain the native
   reader.  Normalize at the language boundary instead of changing the global
   reader while building Guile itself. */
static SCM
athena_option_key (SCM value)
{
  if (scm_is_keyword (value))
    return scm_keyword_to_symbol (value);
  if (scm_is_symbol (value))
    {
      char *name = scm_to_utf8_string (scm_symbol_to_string (value));
      SCM result = SCM_BOOL_F;
      if (name[0] == ':' && name[1] != '\0')
        result = athena_symbol (name + 1);
      free (name);
      return result;
    }
  return SCM_BOOL_F;
}

static int
athena_option_form_p (SCM value)
{
  return scm_is_pair (value)
         && scm_is_true (athena_option_key (scm_car (value)));
}

static int
athena_variable_bound_p (SCM variable)
{
  return scm_is_true (variable)
         && scm_is_true (scm_variable_bound_p (variable));
}

static SCM
athena_record_initial_binding (void *closure, SCM name, SCM variable,
                               SCM result)
{
  SCM bindings = *(SCM *) closure;
  if (athena_variable_bound_p (variable))
    scm_hashq_set_x (bindings, name, SCM_BOOL_T);
  return result;
}

static void
athena_record_visible_module_bindings (SCM module, SCM visited)
{
  SCM uses;

  if (scm_is_true (scm_hashq_ref (visited, module, SCM_BOOL_F)))
    return;
  scm_hashq_set_x (visited, module, SCM_BOOL_T);
  scm_internal_hash_fold (athena_record_initial_binding,
                          &athena_initial_root_bindings, SCM_UNSPECIFIED,
                          SCM_MODULE_OBARRAY (module));
  uses = SCM_MODULE_USES (module);
  while (scm_is_pair (uses))
    {
      athena_record_visible_module_bindings (scm_car (uses), visited);
      uses = scm_cdr (uses);
    }
}

static void
athena_snapshot_initial_root_bindings (void)
{
  SCM visited = scm_c_make_hash_table (127);
  athena_initial_root_bindings = scm_c_make_hash_table (1021);
  scm_gc_protect_object (athena_initial_root_bindings);
  athena_record_visible_module_bindings (athena_root_module, visited);
}

static int
athena_compiling_scheme_bytecode_p (void)
{
  const char *value = getenv ("ATHENA_SCHEME_COMPILE");
  return value != NULL && strcmp (value, "1") == 0;
}

static int
athena_module_p (SCM module)
{
  SCM name;
  SCM registered = SCM_BOOL_F;
  struct athena_module_record *record;
  int sequential;

  scm_dynwind_begin (0);
  scm_dynwind_pthread_mutex_lock (&athena_module_registry_lock);
  sequential = scm_is_true
    (scm_hashq_ref (athena_sequential_modules, module, SCM_BOOL_F));
  scm_dynwind_end ();
  if (sequential)
    return 1;
  name = scm_call_1 (athena_module_name_proc, module);
  if (scm_is_false (name))
    return 0;
  record = athena_module_record (name, 0);
  scm_i_pthread_mutex_lock (&athena_module_registry_lock);
  if (record != NULL)
    registered = record->module;
  scm_i_pthread_mutex_unlock (&athena_module_registry_lock);
  return scm_is_eq (registered, module);
}

static void
athena_share_exported_bindings (SCM module, SCM names)
{
  SCM cursor = names;
  SCM root_names = SCM_EOL;

  if (!athena_module_p (module))
    return;
  scm_dynwind_begin (0);
  scm_dynwind_pthread_mutex_lock (&athena_publication_lock);
  while (scm_is_pair (cursor))
    {
      SCM spec = scm_car (cursor);
      SCM internal_name = scm_is_pair (spec) ? scm_car (spec) : spec;
      SCM external_name = scm_is_pair (spec) ? scm_cdr (spec) : spec;
      SCM variable = scm_module_local_variable (module, internal_name);

      if (scm_is_true (variable))
        {
          SCM root_variable = scm_module_local_variable (athena_root_module,
                                                         external_name);

          /* Source compiled before a module import may already hold the root
             variable for an unresolved or legacy global reference.  Replacing
             the root obarray entry would make later lookups see the imported
             binding while existing bytecode continued reading the stale
             variable.  The root variable must also remain independent from
             the provider: aliasing it would let a later root assignment mutate
             the provider itself, leaving nothing for a subsequent import to
             restore. */
          if (athena_variable_bound_p (variable))
            {
              if (scm_is_true (root_variable))
                scm_variable_set_x (root_variable,
                                    scm_variable_ref (variable));
              else
                scm_module_define (athena_root_module, external_name,
                                   scm_variable_ref (variable));
            }
          else if (scm_is_false (root_variable))
            scm_call_3 (athena_module_add_proc, athena_root_module,
                        external_name, scm_make_undefined_variable ());
          root_names = scm_cons (external_name, root_names);
        }
      cursor = scm_cdr (cursor);
    }
  if (!scm_is_null (root_names))
    scm_apply_2 (athena_original_module_export_proc, athena_root_module,
                 root_names, SCM_EOL);
  scm_dynwind_end ();
}

SCM_DEFINE (scm_athena_module_export_x, "%athena-module-export!", 2, 0, 1,
            (SCM module, SCM names, SCM rest),
            "Export bindings and share ATHENA module exports with its root.")
#define FUNC_NAME s_scm_athena_module_export_x
{
  SCM result = scm_apply_2 (athena_original_module_export_proc,
                            module, names, rest);
  athena_share_exported_bindings (module, names);
  return result;
}
#undef FUNC_NAME

static SCM
athena_quote (SCM value)
{
  return scm_list_2 (athena_symbol ("quote"), value);
}

static int
athena_proper_list_p (SCM value)
{
  while (scm_is_pair (value))
    value = scm_cdr (value);
  return scm_is_null (value);
}

static int
athena_formals_p (SCM value)
{
  while (scm_is_pair (value))
    {
      if (!scm_is_symbol (scm_car (value)))
        return 0;
      value = scm_cdr (value);
    }
  return scm_is_null (value) || scm_is_symbol (value);
}

static SCM
athena_listify_tail (SCM value)
{
  if (scm_is_pair (value))
    return scm_cons (scm_car (value), athena_listify_tail (scm_cdr (value)));
  return scm_list_1 (value);
}

static SCM
athena_head_name (SCM head)
{
  while (scm_is_pair (head))
    head = scm_car (head);
  if (!scm_is_symbol (head))
    scm_misc_error ("tm-define", "invalid definition head ~S",
                    scm_list_1 (head));
  return head;
}

SCM_DEFINE (scm_athena_public_define_if_absent_x,
            "%athena-public-define-if-absent!", 3, 0, 0,
            (SCM module, SCM name, SCM value),
            "Define and export @var{name} from @var{module} if absent.")
#define FUNC_NAME s_scm_athena_public_define_if_absent_x
{
  SCM result;
  if (!scm_is_symbol (name))
    scm_wrong_type_arg_msg (FUNC_NAME, 2, name, "symbol");
  scm_dynwind_begin (0);
  scm_dynwind_pthread_mutex_lock (&athena_publication_lock);
  if (!athena_variable_bound_p (scm_module_variable (module, name)))
    scm_module_define (module, name, value);
  scm_module_export (module, scm_list_1 (name));
  result = scm_variable_ref (scm_module_variable (module, name));
  scm_dynwind_end ();
  return result;
}
#undef FUNC_NAME

SCM_DEFINE (scm_athena_root_binding_set_x,
            "%athena-root-binding-set!", 2, 0, 0,
            (SCM name, SCM value),
            "Set and export an ATHENA root-module binding.")
#define FUNC_NAME s_scm_athena_root_binding_set_x
{
  if (!scm_is_symbol (name))
    scm_wrong_type_arg_msg (FUNC_NAME, 1, name, "symbol");
  scm_dynwind_begin (0);
  scm_dynwind_pthread_mutex_lock (&athena_publication_lock);
  scm_module_define (athena_root_module, name, value);
  scm_module_export (athena_root_module, scm_list_1 (name));
  scm_dynwind_end ();
  return value;
}
#undef FUNC_NAME

SCM_DEFINE (scm_athena_root_binding_ref,
            "%athena-root-binding-ref", 1, 0, 0,
            (SCM name), "Read an ATHENA root-module binding.")
#define FUNC_NAME s_scm_athena_root_binding_ref
{
  SCM variable;
  SCM result;
  if (!scm_is_symbol (name))
    scm_wrong_type_arg_msg (FUNC_NAME, 1, name, "symbol");
  scm_dynwind_begin (0);
  scm_dynwind_pthread_mutex_lock (&athena_publication_lock);
  variable = scm_module_local_variable (athena_root_module, name);
  if (!athena_variable_bound_p (variable))
    scm_misc_error (FUNC_NAME, "unbound ATHENA state variable ~S",
                    scm_list_1 (name));
  result = scm_variable_ref (variable);
  scm_dynwind_end ();
  return result;
}
#undef FUNC_NAME

static SCM
athena_lambda_form (SCM head, SCM body)
{
  SCM lambda = athena_symbol ("lambda");
  while (scm_is_pair (head))
    {
      SCM form = scm_cons (lambda, scm_cons (scm_cdr (head), body));
      body = scm_list_1 (form);
      head = scm_car (head);
    }
  if (!scm_is_symbol (head) || !scm_is_pair (body))
    scm_misc_error ("tm-define", "invalid definition body for ~S",
                    scm_list_1 (head));
  return scm_car (body);
}

static SCM
athena_apply_form (SCM function, SCM head)
{
  if (scm_is_pair (head) && athena_proper_list_p (head))
    return scm_cons (athena_apply_form (function, scm_car (head)),
                     scm_cdr (head));
  if (scm_is_pair (head))
    {
      SCM cons_star = scm_cons (athena_symbol ("cons*"),
                                athena_listify_tail (scm_cdr (head)));
      return scm_list_3 (athena_symbol ("apply"),
                         athena_apply_form (function, scm_car (head)),
                         cons_star);
    }
  return function;
}

static SCM
athena_condition_expression (SCM condition)
{
  if (scm_is_pair (condition)
      && scm_is_eq (scm_car (condition), athena_symbol ("lambda"))
      && scm_is_pair (scm_cdr (condition))
      && scm_is_pair (scm_cdr (scm_cdr (condition))))
    return scm_car (scm_cdr (scm_cdr (condition)));
  return scm_list_1 (condition);
}

static SCM
athena_condition_conjunction (SCM conditions)
{
  SCM mapped = SCM_EOL;
  SCM cursor = conditions;
  while (scm_is_pair (cursor))
    {
      mapped = scm_cons (athena_condition_expression (scm_car (cursor)),
                         mapped);
      cursor = scm_cdr (cursor);
    }
  if (!scm_is_null (cursor))
    scm_misc_error ("tm-define", "invalid condition list ~S",
                    scm_list_1 (conditions));
  mapped = scm_reverse_x (mapped, SCM_EOL);
  if (scm_is_pair (mapped) && scm_is_null (scm_cdr (mapped)))
    return scm_car (mapped);
  return scm_cons (athena_symbol ("and"), mapped);
}

static SCM
athena_begin_expression (SCM body)
{
  if (scm_is_pair (body) && scm_is_null (scm_cdr (body)))
    return scm_car (body);
  return scm_cons (athena_symbol ("begin"), body);
}

struct athena_module_resolution
{
  SCM name;
  SCM relative_file;
  struct athena_module_record *record;
  int private_module;
  SCM failure_tag;
  SCM failure_args;
};

static SCM
athena_module_relative_file (SCM name)
{
  SCM pieces = SCM_EOL;
  SCM cursor = name;
  SCM relative;

  while (scm_is_pair (cursor))
    {
      SCM component = scm_car (cursor);
      if (!scm_is_symbol (component))
        return SCM_BOOL_F;
      pieces = scm_cons (scm_symbol_to_string (component), pieces);
      cursor = scm_cdr (cursor);
      if (scm_is_pair (cursor))
        pieces = scm_cons (scm_from_utf8_string ("/"), pieces);
    }
  if (!scm_is_null (cursor))
    return SCM_BOOL_F;

  relative = scm_string_append (scm_reverse_x (pieces, SCM_EOL));
  relative = scm_string_append
    (scm_list_2 (relative, scm_from_utf8_string (".scm")));
  return relative;
}

static SCM
athena_module_source_file (SCM name)
{
  SCM relative = athena_module_relative_file (name);
  if (scm_is_false (relative))
    return SCM_BOOL_F;
  return scm_sys_search_load_path (relative);
}

static SCM
athena_load_module_body (void *data)
{
  struct athena_module_resolution *resolution = data;
  SCM module;
  SCM interface;

  if (!resolution->private_module)
    return scm_call_1 (athena_resolve_interface_proc, resolution->name);

  scm_dynwind_begin (0);
  scm_dynwind_current_module
    (scm_call_0 (athena_make_fresh_user_module_proc));
  scm_primitive_load_path (resolution->relative_file);
  scm_dynwind_end ();

  scm_i_pthread_mutex_lock (&athena_module_registry_lock);
  module = resolution->record->module;
  interface = resolution->record->interface;
  scm_i_pthread_mutex_unlock (&athena_module_registry_lock);
  if (scm_is_false (module))
    scm_misc_error ("module-provide", "no code for ATHENA module ~S",
                    scm_list_1 (resolution->name));
  if (scm_is_false (interface))
    interface = scm_module_public_interface (module);
  return scm_is_true (interface) ? interface : module;
}

static SCM
athena_resolve_interface_pre_unwind (void *data, SCM tag, SCM throw_args)
{
  (void) data;
  (void) tag;
  (void) throw_args;
  return SCM_UNSPECIFIED;
}

static SCM
athena_resolve_interface_handler (void *data, SCM tag, SCM throw_args)
{
  struct athena_module_resolution *resolution = data;
  resolution->failure_tag = tag;
  resolution->failure_args = throw_args;
  return SCM_BOOL_F;
}

static SCM
athena_module_provide_internal (SCM name, int rethrow)
{
  struct athena_module_resolution resolution;
  struct athena_runtime_thread *thread = athena_current_runtime_thread ();
  struct athena_module_record *record;
  SCM registered = SCM_BOOL_F;
  SCM result = SCM_BOOL_F;
  SCM failure_tag = SCM_BOOL_F;
  SCM failure_args = SCM_EOL;

  record = athena_module_record (name, 1);

  /* Loaded modules are the normal runtime case.  Keep that path off the
     construction lock; the registry lock publishes the completed module. */
  scm_i_pthread_mutex_lock (&athena_module_registry_lock);
  if (record->state == ATHENA_MODULE_LOADED)
    {
      result = scm_is_true (record->interface)
                 ? record->interface : record->module;
      scm_i_pthread_mutex_unlock (&athena_module_registry_lock);
      return result;
    }
  if (record->state == ATHENA_MODULE_FAILED)
    {
      failure_tag = record->failure_tag;
      failure_args = record->failure_args;
      scm_i_pthread_mutex_unlock (&athena_module_registry_lock);
      if (rethrow && scm_is_true (failure_tag))
        scm_ithrow (failure_tag, failure_args, 1);
      return SCM_BOOL_F;
    }
  scm_i_pthread_mutex_unlock (&athena_module_registry_lock);

  /* Separate module files have private modules, but their legacy top-level
     definitions and exports are published into the same ATHENA root module.
     Guile's module obarrays are not concurrent hash tables, so constructing
     two modules in parallel corrupts that shared publication boundary.  This
     recursive lock serializes first-time module construction only; recursive
     imports on the loader thread remain valid, while loaded-module execution
     stays concurrent through the fast path above. */
  scm_dynwind_begin (0);
  scm_dynwind_lock_mutex (athena_module_construction_lock);
  for (;;)
    {
      scm_i_pthread_mutex_lock (&athena_module_registry_lock);
      if (record->state == ATHENA_MODULE_LOADED)
        {
          result = scm_is_true (record->interface)
                     ? record->interface : record->module;
          scm_i_pthread_mutex_unlock (&athena_module_registry_lock);
          scm_dynwind_end ();
          return result;
        }
      if (record->state == ATHENA_MODULE_FAILED)
        {
          failure_tag = record->failure_tag;
          failure_args = record->failure_args;
          scm_i_pthread_mutex_unlock (&athena_module_registry_lock);
          if (rethrow && scm_is_true (failure_tag))
            scm_ithrow (failure_tag, failure_args, 1);
          scm_dynwind_end ();
          return SCM_BOOL_F;
        }
      if (record->state == ATHENA_MODULE_LOADING)
        {
          if (record->owner == thread)
            {
              registered = record->module;
              scm_i_pthread_mutex_unlock (&athena_module_registry_lock);
              if (scm_is_false (registered))
                registered = scm_maybe_resolve_module (name);
              if (scm_is_true (registered))
                {
                  SCM interface = scm_module_public_interface (registered);
                  scm_dynwind_end ();
                  return scm_is_true (interface) ? interface : registered;
                }
              scm_misc_error ("module-provide",
                              "circular module load for ~S",
                              scm_list_1 (name));
            }
          else
            {
              struct athena_module_wait wait;
              int cycle;
              thread->waiting_on = record;
              cycle = athena_module_wait_cycle_p (thread, record);
              if (cycle)
                {
                  thread->waiting_on = NULL;
                  scm_i_pthread_mutex_unlock
                    (&athena_module_registry_lock);
                  scm_misc_error ("module-provide",
                                  "cross-thread circular module load for ~S",
                                  scm_list_1 (name));
                }
              wait.condition = &record->changed;
              wait.mutex = &athena_module_registry_lock;
              wait.status = 0;
              scm_without_guile (athena_module_wait_without_guile, &wait);
              thread->waiting_on = NULL;
              scm_i_pthread_mutex_unlock (&athena_module_registry_lock);
              if (wait.status != 0)
                scm_misc_error ("module-provide",
                                "could not wait for module ~S",
                                scm_list_1 (name));
              continue;
            }
        }

      record->state = ATHENA_MODULE_LOADING;
      record->owner = thread;
      scm_i_pthread_mutex_unlock (&athena_module_registry_lock);
      break;
    }

  resolution.name = name;
  resolution.relative_file = athena_module_relative_file (name);
  resolution.record = record;
  {
    SCM source_file = athena_module_source_file (name);
    resolution.private_module = scm_is_true (source_file)
      && scm_i_athena_source_path_p (source_file);
  }
  resolution.failure_tag = SCM_BOOL_F;
  resolution.failure_args = SCM_EOL;
  if (scm_is_false (resolution.relative_file))
    scm_misc_error ("module-provide", "invalid ATHENA module name ~S",
                    scm_list_1 (name));
  result = scm_c_catch (SCM_BOOL_T,
                        athena_load_module_body, &resolution,
                        athena_resolve_interface_handler, &resolution,
                        athena_resolve_interface_pre_unwind, &resolution);

  scm_i_pthread_mutex_lock (&athena_module_registry_lock);
  record->owner = NULL;
  if (scm_is_true (result))
    {
      record->interface = result;
      record->failure_tag = SCM_BOOL_F;
      record->failure_args = SCM_EOL;
      record->state = ATHENA_MODULE_LOADED;
    }
  else
    {
      record->failure_tag = resolution.failure_tag;
      record->failure_args = resolution.failure_args;
      record->state = ATHENA_MODULE_FAILED;
      failure_tag = record->failure_tag;
      failure_args = record->failure_args;
    }
  scm_i_pthread_cond_broadcast (&record->changed);
  scm_i_pthread_mutex_unlock (&athena_module_registry_lock);

  if (scm_is_true (result))
    {
      scm_dynwind_end ();
      return result;
    }
  if (rethrow)
    {
      if (scm_is_true (failure_tag))
        scm_ithrow (failure_tag, failure_args, 1);
      scm_misc_error ("module-provide",
                      "ATHENA module ~S failed without an exception",
                      scm_list_1 (name));
    }
  scm_dynwind_end ();
  return SCM_BOOL_F;
}

SCM_DEFINE (scm_athena_module_available_p, "module-available?", 1, 0, 0,
            (SCM name), "Return true if ATHENA module @var{name} is loadable.")
#define FUNC_NAME s_scm_athena_module_available_p
{
  return scm_from_bool (scm_is_true
    (athena_module_provide_internal (name, 0)));
}
#undef FUNC_NAME

SCM_DEFINE (scm_athena_module_provide, "module-provide", 1, 0, 0,
            (SCM name), "Load ATHENA module @var{name} exactly once.")
#define FUNC_NAME s_scm_athena_module_provide
{
  return athena_module_provide_internal (name, 1);
}
#undef FUNC_NAME

SCM_DEFINE (scm_athena_module_load, "module-load", 1, 0, 0,
            (SCM name), "Load ATHENA module @var{name} exactly once.")
#define FUNC_NAME s_scm_athena_module_load
{
  return athena_module_provide_internal (name, 1);
}
#undef FUNC_NAME

SCM_DEFINE (scm_athena_import_modules_x, "%athena-import-modules!",
            2, 0, 0, (SCM target, SCM names),
            "Import ATHENA module interfaces into @var{target}.")
#define FUNC_NAME s_scm_athena_import_modules_x
{
  SCM cursor = names;
  while (scm_is_pair (cursor))
    {
      SCM interface = athena_module_provide_internal (scm_car (cursor), 1);
      scm_call_2 (athena_module_use_proc, target, interface);
      cursor = scm_cdr (cursor);
    }
  if (!scm_is_null (cursor))
    scm_misc_error (FUNC_NAME, "invalid module list ~S", scm_list_1 (names));
  return SCM_UNSPECIFIED;
}
#undef FUNC_NAME

struct athena_inherit_fold
{
  SCM target;
  SCM public_interface;
};

static SCM
athena_inherit_binding (void *data, SCM name, SCM variable, SCM result)
{
  struct athena_inherit_fold *fold = data;
  SCM local = scm_module_local_variable (fold->target, name);
  SCM exported = scm_module_local_variable (fold->public_interface, name);

  if (scm_is_true (local) && !athena_variable_bound_p (local)
      && athena_variable_bound_p (variable))
    {
      /* The legacy binder interns unresolved forward references in the shared
         root module.  A later inherit must satisfy that exact variable;
         merely adding the provider to the module-use list leaves the local
         placeholder shadowing it forever.  Binding the existing variable also
         preserves references already emitted by Guile's compiler.  */
      scm_module_define (fold->target, name, scm_variable_ref (variable));
      local = scm_module_local_variable (fold->target, name);
      if (scm_is_false (exported))
        scm_call_3 (athena_module_add_proc, fold->public_interface, name,
                    local);
    }
  else if (scm_is_false (local) && scm_is_false (exported))
    scm_call_3 (athena_module_add_proc, fold->public_interface, name, variable);
  return result;
}

SCM_DEFINE (scm_athena_inherit_modules_x, "%athena-inherit-modules!",
            2, 0, 0, (SCM target, SCM names),
            "Import and non-conflictingly re-export ATHENA modules.")
#define FUNC_NAME s_scm_athena_inherit_modules_x
{
  struct athena_inherit_fold fold;
  SCM cursor = names;
  fold.target = target;
  fold.public_interface = scm_module_public_interface (target);
  if (scm_is_false (fold.public_interface))
    scm_misc_error (FUNC_NAME, "target module has no public interface", SCM_EOL);

  while (scm_is_pair (cursor))
    {
      SCM interface = athena_module_provide_internal (scm_car (cursor), 1);
      scm_call_2 (athena_module_use_proc, target, interface);
      scm_internal_hash_fold (athena_inherit_binding, &fold, SCM_UNSPECIFIED,
                              SCM_MODULE_OBARRAY (interface));
      cursor = scm_cdr (cursor);
    }
  if (!scm_is_null (cursor))
    scm_misc_error (FUNC_NAME, "invalid module list ~S", scm_list_1 (names));
  return SCM_UNSPECIFIED;
}
#undef FUNC_NAME

SCM_DEFINE (scm_athena_import_from_expand, "%athena-import-from-expand",
            1, 0, 0, (SCM names),
            "Expand ATHENA's import-from syntax using native module loading.")
#define FUNC_NAME s_scm_athena_import_from_expand
{
  if (!athena_proper_list_p (names))
    scm_misc_error (FUNC_NAME, "invalid module list ~S", scm_list_1 (names));
  return scm_list_3 (athena_symbol ("%athena-import-modules!"),
                     scm_list_1 (athena_symbol ("current-module")),
                     athena_quote (names));
}
#undef FUNC_NAME

SCM_DEFINE (scm_athena_inherit_modules_expand,
            "%athena-inherit-modules-expand", 1, 0, 0, (SCM names),
            "Expand ATHENA's inherit-modules syntax using native loading.")
#define FUNC_NAME s_scm_athena_inherit_modules_expand
{
  if (!athena_proper_list_p (names))
    scm_misc_error (FUNC_NAME, "invalid module list ~S", scm_list_1 (names));
  return scm_list_3 (athena_symbol ("%athena-inherit-modules!"),
                     scm_list_1 (athena_symbol ("current-module")),
                     athena_quote (names));
}
#undef FUNC_NAME

SCM_DEFINE (scm_athena_texmacs_module_expand,
            "%athena-texmacs-module-expand", 2, 0, 0,
            (SCM name, SCM options),
            "Expand an ATHENA module declaration in the private runtime.")
#define FUNC_NAME s_scm_athena_texmacs_module_expand
{
  SCM actions = SCM_EOL;
  SCM cursor = options;
  SCM define_module;
  SCM configure_module;
  SCM use_root;
  SCM register_module;

  if (!athena_proper_list_p (name))
    scm_misc_error (FUNC_NAME, "invalid module name ~S", scm_list_1 (name));

  while (scm_is_pair (cursor))
    {
      SCM option = scm_car (cursor);
      SCM keyword;
      SCM action = SCM_BOOL_F;
      if (!scm_is_pair (option))
        scm_misc_error (FUNC_NAME, "invalid module option ~S",
                        scm_list_1 (option));
      keyword = athena_option_key (scm_car (option));
      if (scm_is_false (keyword))
        scm_misc_error (FUNC_NAME, "invalid module option ~S",
                        scm_list_1 (option));
      if (scm_is_eq (keyword, athena_symbol ("use")))
        action = scm_list_3
          (athena_symbol ("eval-when"),
           scm_list_3 (athena_symbol ("expand"), athena_symbol ("load"),
                       athena_symbol ("eval")),
           scm_list_3 (athena_symbol ("%athena-import-modules!"),
                       scm_list_1 (athena_symbol ("current-module")),
                       athena_quote (scm_cdr (option))));
      else if (scm_is_eq (keyword, athena_symbol ("inherit")))
        action = scm_list_3
          (athena_symbol ("eval-when"),
           scm_list_3 (athena_symbol ("expand"), athena_symbol ("load"),
                       athena_symbol ("eval")),
           scm_list_3 (athena_symbol ("%athena-inherit-modules!"),
                       scm_list_1 (athena_symbol ("current-module")),
                       athena_quote (scm_cdr (option))));
      else if (scm_is_eq (keyword, athena_symbol ("export")))
        action = scm_cons
          (athena_symbol ("begin"),
           scm_list_2
             (scm_list_2
                (athena_symbol ("display"),
                 scm_from_utf8_string
                   ("Warning] The option :export is no longer supported\n")),
              scm_list_2
                (athena_symbol ("display"),
                 scm_from_utf8_string
                   ("       ] Please use tm-define instead\n"))));
      else
        scm_misc_error (FUNC_NAME, "unknown module option ~S",
                        scm_list_1 (scm_car (option)));
      actions = scm_cons (action, actions);
      cursor = scm_cdr (cursor);
    }

  define_module = scm_cons
    (athena_symbol ("define-module"),
     scm_cons (name,
       scm_list_2 (scm_from_utf8_keyword ("use-module"),
                   scm_list_2 (athena_symbol ("athena"),
                               athena_symbol ("runtime")))));
  configure_module = scm_list_3
    (athena_symbol ("eval-when"),
     scm_list_3 (athena_symbol ("expand"), athena_symbol ("load"),
                 athena_symbol ("eval")),
     scm_list_2 (athena_symbol ("%athena-configure-module!"),
                 scm_list_1 (athena_symbol ("current-module"))));
  use_root = scm_list_3
    (athena_symbol ("eval-when"),
     scm_list_3 (athena_symbol ("expand"), athena_symbol ("load"),
                 athena_symbol ("eval")),
     scm_list_3 (athena_symbol ("module-use!"),
                 scm_list_1 (athena_symbol ("current-module")),
                 scm_list_1 (athena_symbol ("%athena-root-module"))));
  register_module = scm_list_3
    (athena_symbol ("eval-when"),
     scm_list_2 (athena_symbol ("load"), athena_symbol ("eval")),
     scm_list_4 (athena_symbol ("%athena-module-register!"),
                 athena_quote (name),
                 scm_list_1 (athena_symbol ("current-module")),
                 scm_list_1 (athena_symbol ("current-filename"))));

  {
    SCM mark_sequential = scm_list_3
      (athena_symbol ("eval-when"),
       scm_list_1 (athena_symbol ("expand")),
       scm_list_2 (athena_symbol ("%athena-module-mark-sequential!"),
                   scm_list_1 (athena_symbol ("current-module"))));

    actions = scm_reverse_x (actions, SCM_EOL);
    return scm_cons (athena_symbol ("begin"),
                     scm_cons (define_module,
                       scm_cons (configure_module,
                         scm_cons (mark_sequential,
                           scm_cons (use_root,
                             scm_cons (register_module, actions))))));
  }
}
#undef FUNC_NAME

static SCM
athena_tm_define_expand_parts (SCM head, SCM body, SCM conditions,
                               SCM properties)
{
  SCM name = athena_head_name (head);
  SCM effective_body = body;
  SCM value;
  SCM former;
  SCM public_value;
  SCM warning_form;
  SCM install_form;
  SCM definition_form;
  SCM phased_definition;

  if (!athena_proper_list_p (body) || !athena_proper_list_p (conditions)
      || !athena_proper_list_p (properties))
    scm_misc_error ("tm-define", "expected proper definition lists", SCM_EOL);

  if (!scm_is_null (conditions))
    effective_body = scm_list_1
      (scm_list_4 (athena_symbol ("if"),
                   athena_condition_conjunction (conditions),
                   athena_begin_expression (body),
                   athena_apply_form (athena_symbol ("former"), head)));

  value = athena_lambda_form (head, effective_body);
  former = scm_list_2
    (athena_symbol ("former"),
     scm_list_3
       (athena_symbol ("or"),
        scm_list_2 (athena_symbol ("%athena-definition-ref"),
                    scm_list_2 (athena_symbol ("quote"), name)),
        scm_list_3 (athena_symbol ("lambda"), athena_symbol ("args"),
                    scm_list_1 (athena_symbol ("noop")))));
  if (athena_compiling_scheme_bytecode_p ())
    warning_form = scm_list_1 (athena_symbol ("noop"));
  else
    warning_form = scm_list_3
      (athena_symbol ("when"),
       scm_list_3
         (athena_symbol ("and"),
          scm_list_2
            (athena_symbol ("not"),
             scm_list_3
               (athena_symbol ("or"),
                scm_list_2 (athena_symbol ("%athena-definition-defined?"),
                            scm_list_2 (athena_symbol ("quote"), name)),
                scm_list_2 (athena_symbol ("%athena-definition-ref"),
                            scm_list_2 (athena_symbol ("quote"), name)))),
          scm_from_bool (!scm_is_null (conditions))),
       scm_cons
         (athena_symbol ("display*"),
          scm_list_4
            (scm_from_utf8_string ("warning: conditional master routine "),
             scm_list_2 (athena_symbol ("quote"), name),
             scm_from_utf8_string ("\n   "),
             scm_list_2 (athena_symbol ("quote"), value))));
  public_value = scm_cons
    (athena_symbol ("let*"),
     scm_cons (scm_list_1 (former), scm_list_1 (value)));
  {
    SCM procedure = athena_symbol ("definition-procedure");
    SCM install_public = scm_list_5
      (athena_symbol ("%athena-definition-install!"),
       scm_list_2 (athena_symbol ("quote"), name), procedure,
       scm_list_2 (athena_symbol ("quote"), value),
       scm_list_2 (athena_symbol ("module-name"),
                   scm_list_1 (athena_symbol ("current-module"))));
    install_form = scm_list_3
      (athena_symbol ("let"),
       scm_list_1 (scm_list_2 (procedure, public_value)), install_public);
  }
  definition_form = scm_cons
    (athena_symbol ("begin"), scm_list_2 (warning_form, install_form));
  /* A Guile 1.8 tm-define publishes into the shared TeXmacs environment; it
     does not replace a same-named private helper in the source module.  Run
     the registry installation during expansion as well as load/eval so that
     later macros in the same sequential source can already call it. */
  phased_definition = scm_list_3
    (athena_symbol ("eval-when"),
     scm_list_3 (athena_symbol ("expand"), athena_symbol ("load"),
                 athena_symbol ("eval")),
     definition_form);
  return scm_cons (athena_symbol ("begin"),
                   scm_cons (phased_definition, properties));
}

SCM_DEFINE (scm_athena_tm_define_expand, "%athena-tm-define-expand",
            4, 0, 0, (SCM head, SCM body, SCM conditions, SCM properties),
            "Expand ATHENA's tm-define form into Guile 3 definitions.")
#define FUNC_NAME s_scm_athena_tm_define_expand
{
  return athena_tm_define_expand_parts
    (head, body, conditions, properties);
}
#undef FUNC_NAME

struct athena_definition_options
{
  SCM body;
  SCM conditions;
  SCM property_conditions;
  SCM properties;
  SCM wrappers;
};

static SCM
athena_required_option_argument (const char *who, SCM option)
{
  SCM args = scm_cdr (option);
  if (!scm_is_pair (args))
    scm_misc_error (who, "option requires an argument: ~S",
                    scm_list_1 (scm_car (option)));
  return scm_car (args);
}

static void
athena_add_property_descriptor (struct athena_definition_options *parsed,
                                SCM property, SCM value)
{
  parsed->properties = scm_cons (scm_cons (property, value),
                                 parsed->properties);
}

static SCM
athena_definition_formals (SCM head)
{
  if (!scm_is_pair (head))
    scm_misc_error ("tm-define", "conditional definition has no arguments: ~S",
                    scm_list_1 (head));
  return scm_cdr (head);
}

static struct athena_definition_options
athena_parse_definition_options (SCM head, SCM body)
{
  struct athena_definition_options parsed;
  SCM cursor = body;
  parsed.conditions = SCM_EOL;
  parsed.property_conditions = SCM_EOL;
  parsed.properties = SCM_EOL;
  parsed.wrappers = SCM_EOL;

  while (scm_is_pair (cursor)
         && athena_option_form_p (scm_car (cursor)))
    {
      SCM option = scm_car (cursor);
      SCM args = scm_cdr (option);
      SCM key = athena_option_key (scm_car (option));

      if (scm_is_eq (key, athena_symbol ("mode")))
        {
          SCM condition = athena_required_option_argument ("tm-define", option);
          parsed.conditions = scm_cons (condition, parsed.conditions);
          parsed.property_conditions =
            scm_cons (condition, parsed.property_conditions);
        }
      else if (scm_is_eq (key, athena_symbol ("require")))
        {
          SCM condition = scm_list_3
            (athena_symbol ("lambda"), athena_definition_formals (head),
             athena_required_option_argument ("tm-define", option));
          parsed.conditions = scm_cons (condition, parsed.conditions);
        }
      else if (scm_is_eq (key, athena_symbol ("applicable")))
        {
          SCM predicate = scm_cons
            (athena_symbol ("lambda"), scm_cons (athena_symbol ("args"), args));
          athena_add_property_descriptor
            (&parsed, scm_from_utf8_keyword ("applicable"),
             scm_list_2 (athena_symbol ("list"), predicate));
        }
      else if (scm_is_eq (key, athena_symbol ("type"))
               || scm_is_eq (key, athena_symbol ("synopsis"))
               || scm_is_eq (key, athena_symbol ("returns"))
               || scm_is_eq (key, athena_symbol ("note")))
        athena_add_property_descriptor
          (&parsed, scm_symbol_to_keyword (key), athena_quote (args));
      else if (scm_is_eq (key, athena_symbol ("synopsis*")))
        {
          athena_add_property_descriptor
            (&parsed, scm_from_utf8_keyword ("synopsis"), athena_quote (args));
          athena_add_property_descriptor
            (&parsed, scm_from_utf8_keyword ("synopsis*"), athena_quote (args));
        }
      else if (scm_is_eq (key, athena_symbol ("secure"))
               || scm_is_eq (key, athena_symbol ("check-mark"))
               || scm_is_eq (key, athena_symbol ("interactive"))
               || scm_is_eq (key, athena_symbol ("balloon")))
        athena_add_property_descriptor
          (&parsed, scm_car (option),
           scm_cons (athena_symbol ("list"), args));
      else if (scm_is_eq (key, athena_symbol ("argument")))
        {
          SCM argument = athena_required_option_argument ("tm-define", option);
          SCM formals = athena_definition_formals (head);
          athena_add_property_descriptor
            (&parsed, scm_from_utf8_keyword ("arguments"),
             athena_quote (formals));
          athena_add_property_descriptor
            (&parsed,
             scm_list_2 (scm_from_utf8_keyword ("argument"), argument),
             athena_quote (scm_cdr (args)));
        }
      else if (scm_is_eq (key, athena_symbol ("default"))
               || scm_is_eq (key, athena_symbol ("proposals")))
        {
          SCM argument = athena_required_option_argument ("tm-define", option);
          SCM value = scm_cons
            (athena_symbol ("lambda"),
             scm_cons (SCM_EOL, scm_cdr (args)));
          athena_add_property_descriptor
            (&parsed, scm_list_2 (scm_car (option), argument), value);
        }
      else if (scm_is_eq (key, athena_symbol ("state"))
               || scm_is_eq (key, athena_symbol ("state-slots")))
        {
          SCM wrapper = scm_is_eq (key, athena_symbol ("state"))
            ? athena_symbol ("with-state-by-name")
            : athena_symbol ("with-state-slots-by-name");
          parsed.wrappers = scm_cons
            (scm_cons (wrapper,
                       athena_required_option_argument ("tm-define", option)),
             parsed.wrappers);
        }
      else
        scm_misc_error ("tm-define", "unknown option ~S",
                        scm_list_1 (scm_car (option)));
      cursor = scm_cdr (cursor);
    }

  if (!athena_proper_list_p (cursor))
    scm_misc_error ("tm-define", "invalid definition body ~S",
                    scm_list_1 (body));

  parsed.body = cursor;
  while (scm_is_pair (parsed.wrappers))
    {
      SCM wrapper = scm_car (parsed.wrappers);
      parsed.body = scm_list_1
        (scm_cons (scm_car (wrapper),
                   scm_cons (scm_cdr (wrapper), parsed.body)));
      parsed.wrappers = scm_cdr (parsed.wrappers);
    }
  parsed.properties = scm_reverse_x (parsed.properties, SCM_EOL);
  return parsed;
}

static SCM
athena_property_forms (SCM name, struct athena_definition_options *parsed)
{
  SCM result = SCM_EOL;
  SCM cursor = parsed->properties;
  SCM conditions = scm_cons (athena_symbol ("list"),
                             parsed->property_conditions);
  while (scm_is_pair (cursor))
    {
      SCM descriptor = scm_car (cursor);
      SCM form = scm_list_5
        (athena_symbol ("property-set!"), athena_quote (name),
         athena_quote (scm_car (descriptor)), scm_cdr (descriptor), conditions);
      result = scm_cons (form, result);
      cursor = scm_cdr (cursor);
    }
  return scm_reverse_x (result, SCM_EOL);
}

SCM_DEFINE (scm_athena_tm_define_expand_full,
            "%athena-tm-define-expand-full", 2, 0, 0,
            (SCM head, SCM body),
            "Parse and expand an ATHENA definition entirely in native code.")
#define FUNC_NAME s_scm_athena_tm_define_expand_full
{
  struct athena_definition_options parsed =
    athena_parse_definition_options (head, body);
  SCM name = athena_head_name (head);
  SCM properties = athena_property_forms (name, &parsed);
  return athena_tm_define_expand_parts
    (head, parsed.body, parsed.conditions, properties);
}
#undef FUNC_NAME

SCM_DEFINE (scm_athena_tm_property_expand, "%athena-tm-property-expand",
            2, 0, 0, (SCM head, SCM body),
            "Parse and expand ATHENA property declarations in native code.")
#define FUNC_NAME s_scm_athena_tm_property_expand
{
  struct athena_definition_options parsed =
    athena_parse_definition_options (head, body);
  SCM name = athena_head_name (head);
  if (!scm_is_null (parsed.body))
    scm_misc_error (FUNC_NAME, "unexpected tm-property body ~S",
                    scm_list_1 (parsed.body));
  return scm_cons (athena_symbol ("begin"),
                   athena_property_forms (name, &parsed));
}
#undef FUNC_NAME

static SCM
athena_macroify (SCM head)
{
  if (scm_is_pair (head))
    return scm_cons (athena_macroify (scm_car (head)), scm_cdr (head));
  if (!scm_is_symbol (head))
    scm_misc_error ("tm-define-macro", "invalid macro head ~S",
                    scm_list_1 (head));
  return scm_string_to_symbol
    (scm_string_append
      (scm_list_2 (scm_symbol_to_string (head),
                   scm_from_utf8_string ("$impl"))));
}

SCM_DEFINE (scm_athena_tm_define_macro_expand,
            "%athena-tm-define-macro-expand", 2, 0, 0,
            (SCM head, SCM body),
            "Expand ATHENA's overloaded macro definition in native code.")
#define FUNC_NAME s_scm_athena_tm_define_macro_expand
{
  SCM macro_head = athena_macroify (head);
  SCM implementation = scm_athena_tm_define_expand_full (macro_head, body);
  SCM name = athena_head_name (head);
  SCM macro_body = athena_apply_form (athena_head_name (macro_head), head);
  SCM macro_definition = scm_cons
    (athena_symbol ("define-public-macro"),
     scm_cons (head, scm_list_1 (macro_body)));
  SCM binding_install = scm_list_3
    (athena_symbol ("eval-when"),
     scm_list_3 (athena_symbol ("expand"), athena_symbol ("load"),
                 athena_symbol ("eval")),
     scm_list_3 (athena_symbol ("%athena-binding-install!"),
                 scm_list_1 (athena_symbol ("current-module")),
                 athena_quote (name)));
  return scm_list_4 (athena_symbol ("begin"), implementation,
                     macro_definition, binding_install);
}
#undef FUNC_NAME

SCM_DEFINE (scm_athena_runtime_version, "%athena-runtime-version", 0, 0, 0,
            (void),
            "Return the version of ATHENA's native Scheme runtime API.")
#define FUNC_NAME s_scm_athena_runtime_version
{
  return scm_from_utf8_string ("1");
}
#undef FUNC_NAME

SCM_DEFINE (scm_athena_root_module, "%athena-root-module", 0, 0, 0,
            (void), "Return ATHENA's root Scheme module.")
#define FUNC_NAME s_scm_athena_root_module
{
  return athena_root_module;
}
#undef FUNC_NAME

/* ATHENA inherits TeXmacs' sequential Guile 1.8 module semantics: globals
   supplied by C++ glue, tm-define, or lazy modules are shared through the root
   module.  Only return bindings which already exist there.  Creating a root
   placeholder for every unresolved identifier changes lexical identity and
   breaks syntax literals such as SRFI-26's <> and <...>.  Guile's normal
   unresolved top-level reference remains dynamic and can see a binding which
   is installed into the imported root module later.  */
SCM_DEFINE (scm_athena_global_binder, "%athena-global-binder", 3, 0, 0,
            (SCM module, SCM name, SCM define_p),
            "Resolve a legacy ATHENA global through its shared root module.")
#define FUNC_NAME s_scm_athena_global_binder
{
  SCM variable;
  SCM result;
  (void) module;
  (void) define_p;
  if (!scm_is_symbol (name))
    scm_wrong_type_arg_msg (FUNC_NAME, 2, name, "symbol");
  scm_dynwind_begin (0);
  scm_dynwind_pthread_mutex_lock (&athena_publication_lock);
  variable = scm_module_local_variable (athena_root_module, name);
  result = athena_variable_bound_p (variable) ? variable : SCM_BOOL_F;
  scm_dynwind_end ();
  return result;
}
#undef FUNC_NAME

SCM_DEFINE (scm_athena_configure_module_x, "%athena-configure-module!",
            1, 0, 0, (SCM module),
            "Apply ATHENA's Guile 1.8-compatible module lookup policy.")
#define FUNC_NAME s_scm_athena_configure_module_x
{
  SCM handlers;
  SCM_VALIDATE_MODULE (1, module);
  handlers = scm_call_1 (athena_lookup_duplicates_handlers_proc,
                         scm_list_1 (athena_symbol ("last")));
  scm_call_2 (athena_set_module_binder_proc, module,
              athena_global_binder_proc);
  scm_call_2 (athena_set_module_declarative_proc, module, SCM_BOOL_F);
  scm_call_2 (athena_set_module_duplicates_proc, module, handlers);
  return module;
}
#undef FUNC_NAME

SCM_DEFINE (scm_athena_module_register_x, "%athena-module-register!", 3, 0, 0,
            (SCM name, SCM module, SCM source_file),
            "Register @var{module} as ATHENA module @var{name}.")
#define FUNC_NAME s_scm_athena_module_register_x
{
  SCM effective_source = source_file;
  SCM interface = scm_module_public_interface (module);
  struct athena_runtime_thread *thread = athena_current_runtime_thread ();
  struct athena_module_record *record;
  int conflicting_owner = 0;

  if (scm_is_false (interface))
    interface = module;
  if (!scm_is_string (effective_source))
    {
      SCM port = scm_current_load_port ();
      if (scm_is_true (port))
        effective_source = scm_port_filename (port);
    }
  if (!scm_is_string (effective_source))
    effective_source = athena_module_source_file (name);
  if (scm_is_string (effective_source))
    {
      SCM resolved_source = scm_sys_search_load_path (effective_source);
      if (scm_is_string (resolved_source))
        effective_source = resolved_source;
    }

  record = athena_module_record (name, 1);
  scm_i_pthread_mutex_lock (&athena_module_registry_lock);
  if (record->state == ATHENA_MODULE_LOADING
      && record->owner != NULL && record->owner != thread)
    conflicting_owner = 1;
  else
    {
      record->module = module;
      record->source_file = effective_source;
      if (record->state != ATHENA_MODULE_LOADING)
        {
          /* Normal resolve-interface loads are completed by the surrounding
             state machine.  A direct primitive load has no end callback, so
             registration is its atomic publication boundary. */
          record->interface = interface;
          record->failure_tag = SCM_BOOL_F;
          record->failure_args = SCM_EOL;
          record->state = ATHENA_MODULE_LOADED;
          record->owner = NULL;
          scm_i_pthread_cond_broadcast (&record->changed);
        }
    }
  scm_i_pthread_mutex_unlock (&athena_module_registry_lock);
  if (conflicting_owner)
    scm_misc_error (FUNC_NAME, "concurrent registration of module ~S",
                    scm_list_1 (name));
  return module;
}
#undef FUNC_NAME

SCM_DEFINE (scm_athena_module_mark_sequential_x,
            "%athena-module-mark-sequential!", 1, 0, 0, (SCM module),
            "Mark a compiler module as using ATHENA sequential top-level semantics.")
#define FUNC_NAME s_scm_athena_module_mark_sequential_x
{
  scm_dynwind_begin (0);
  scm_dynwind_pthread_mutex_lock (&athena_module_registry_lock);
  scm_hashq_set_x (athena_sequential_modules, module, SCM_BOOL_T);
  scm_dynwind_end ();
  return module;
}
#undef FUNC_NAME

SCM_DEFINE (scm_athena_module_ref, "%athena-module-ref", 1, 0, 0,
            (SCM name),
            "Return the registered ATHENA module named @var{name}.")
#define FUNC_NAME s_scm_athena_module_ref
{
  struct athena_module_record *record;
  SCM module = SCM_BOOL_F;
  record = athena_module_record (name, 0);
  scm_i_pthread_mutex_lock (&athena_module_registry_lock);
  if (record != NULL)
    module = record->module;
  scm_i_pthread_mutex_unlock (&athena_module_registry_lock);
  return module;
}
#undef FUNC_NAME

SCM_DEFINE (scm_athena_sequential_top_level_p,
            "%athena-sequential-top-level?", 1, 0, 0, (SCM module),
            "Return true for modules using ATHENA's sequential definitions.")
#define FUNC_NAME s_scm_athena_sequential_top_level_p
{
  SCM name;
  struct athena_module_record *record;
  int result;
  if (scm_is_eq (module, athena_root_module))
    return SCM_BOOL_T;
  name = scm_call_1 (athena_module_name_proc, module);
  scm_dynwind_begin (0);
  scm_dynwind_pthread_mutex_lock (&athena_module_registry_lock);
  result = scm_is_true
    (scm_hashq_ref (athena_sequential_modules, module, SCM_BOOL_F));
  scm_dynwind_end ();
  record = scm_is_true (name) ? athena_module_record (name, 0) : NULL;
  scm_i_pthread_mutex_lock (&athena_module_registry_lock);
  if (record != NULL && scm_is_true (record->module))
    result = 1;
  scm_i_pthread_mutex_unlock (&athena_module_registry_lock);
  return scm_from_bool (result);
}
#undef FUNC_NAME

SCM_DEFINE (scm_athena_binding_install_x, "%athena-binding-install!",
            2, 0, 0, (SCM module, SCM name),
            "Copy @var{name} from @var{module} into ATHENA's root module.")
#define FUNC_NAME s_scm_athena_binding_install_x
{
  SCM variable = scm_module_variable (module, name);
  SCM result;
  if (!athena_variable_bound_p (variable))
    scm_misc_error (FUNC_NAME, "module has no binding named ~S",
                    scm_list_1 (name));
  scm_dynwind_begin (0);
  scm_dynwind_pthread_mutex_lock (&athena_publication_lock);
  scm_module_define (athena_root_module, name, scm_variable_ref (variable));
  scm_module_export (athena_root_module, scm_list_1 (name));
  result = scm_variable_ref (variable);
  scm_dynwind_end ();
  return result;
}
#undef FUNC_NAME

SCM_DEFINE (scm_athena_definition_defined_p, "%athena-definition-defined?",
            1, 0, 0, (SCM name),
            "Return true when @var{name} has an ATHENA definition.")
#define FUNC_NAME s_scm_athena_definition_defined_p
{
  SCM found;
  scm_dynwind_begin (0);
  scm_dynwind_pthread_mutex_lock (&athena_publication_lock);
  found = scm_hashq_ref (athena_definition_sources, name, SCM_BOOL_F);
  scm_dynwind_end ();
  return scm_from_bool (scm_is_true (found));
}
#undef FUNC_NAME

SCM_DEFINE (scm_athena_definition_ref, "%athena-definition-ref", 1, 0, 0,
            (SCM name), "Return ATHENA's root definition named @var{name}.")
#define FUNC_NAME s_scm_athena_definition_ref
{
  SCM variable;
  SCM result;
  scm_dynwind_begin (0);
  scm_dynwind_pthread_mutex_lock (&athena_publication_lock);
  variable = scm_module_variable (athena_root_module, name);
  result = athena_variable_bound_p (variable)
             ? scm_variable_ref (variable) : SCM_BOOL_F;
  scm_dynwind_end ();
  return result;
}
#undef FUNC_NAME

SCM_DEFINE (scm_athena_definition_install_x, "%athena-definition-install!",
            4, 0, 0, (SCM name, SCM procedure, SCM source, SCM module_name),
            "Install an ATHENA root definition and register its metadata.")
#define FUNC_NAME s_scm_athena_definition_install_x
{
  scm_dynwind_begin (0);
  scm_dynwind_pthread_mutex_lock (&athena_publication_lock);
  scm_module_define (athena_root_module, name, procedure);
  scm_module_export (athena_root_module, scm_list_1 (name));
  athena_table_prepend (athena_definition_sources, name, source);
  scm_hashq_set_x (athena_definition_names, procedure, name);
  athena_table_prepend (athena_definition_modules, name, module_name);
  scm_dynwind_end ();
  return procedure;
}
#undef FUNC_NAME

SCM_DEFINE (scm_athena_definition_sources_ref, "%athena-definition-sources",
            1, 0, 0, (SCM name),
            "Return source forms registered for ATHENA definition @var{name}.")
#define FUNC_NAME s_scm_athena_definition_sources_ref
{
  SCM result;
  scm_dynwind_begin (0);
  scm_dynwind_pthread_mutex_lock (&athena_publication_lock);
  result = scm_hashq_ref (athena_definition_sources, name, SCM_BOOL_F);
  scm_dynwind_end ();
  return result;
}
#undef FUNC_NAME

SCM_DEFINE (scm_athena_definition_modules_ref, "%athena-definition-modules",
            1, 0, 0, (SCM name),
            "Return module names that contributed to definition @var{name}.")
#define FUNC_NAME s_scm_athena_definition_modules_ref
{
  SCM result;
  scm_dynwind_begin (0);
  scm_dynwind_pthread_mutex_lock (&athena_publication_lock);
  result = scm_hashq_ref (athena_definition_modules, name, SCM_EOL);
  scm_dynwind_end ();
  return result;
}
#undef FUNC_NAME

SCM_DEFINE (scm_athena_procedure_name, "%athena-procedure-name", 1, 0, 0,
            (SCM procedure),
            "Return ATHENA's registered symbolic name for @var{procedure}.")
#define FUNC_NAME s_scm_athena_procedure_name
{
  SCM result;
  scm_dynwind_begin (0);
  scm_dynwind_pthread_mutex_lock (&athena_publication_lock);
  result = scm_hashq_ref (athena_definition_names, procedure, SCM_BOOL_F);
  scm_dynwind_end ();
  return result;
}
#undef FUNC_NAME

static SCM
collect_hash_key (void *closure, SCM key, SCM value, SCM result)
{
  (void) closure;
  (void) value;
  return scm_cons (key, result);
}

SCM_DEFINE (scm_athena_defined_symbols, "%athena-defined-symbols", 0, 0, 0,
            (void), "Return all symbols registered through tm-define.")
#define FUNC_NAME s_scm_athena_defined_symbols
{
  SCM result;
  scm_dynwind_begin (0);
  scm_dynwind_pthread_mutex_lock (&athena_publication_lock);
  result = scm_internal_hash_fold (collect_hash_key, NULL, SCM_EOL,
                                   athena_definition_sources);
  scm_dynwind_end ();
  return result;
}
#undef FUNC_NAME

SCM_DEFINE (scm_athena_ctx_add_condition, "ctx-add-condition", 3, 0, 0,
            (SCM context, SCM kind, SCM condition),
            "Append @var{condition} to an ATHENA dispatch context.")
#define FUNC_NAME s_scm_athena_ctx_add_condition
{
  (void) kind;
  if (!athena_proper_list_p (context))
    scm_wrong_type_arg_msg (FUNC_NAME, 1, context, "proper list");
  return scm_append (scm_list_2 (context, scm_list_1 (condition)));
}
#undef FUNC_NAME

SCM_DEFINE (scm_athena_ctx_insert, "ctx-insert", 3, 0, 0,
            (SCM context, SCM data, SCM conditions),
            "Prepend a conditional value to an ATHENA dispatch context.")
#define FUNC_NAME s_scm_athena_ctx_insert
{
  if (scm_is_false (context))
    context = SCM_EOL;
  if (!athena_proper_list_p (context) || !athena_proper_list_p (conditions))
    scm_misc_error (FUNC_NAME, "invalid context or condition list", SCM_EOL);
  return scm_cons (scm_cons (conditions, data), context);
}
#undef FUNC_NAME

SCM_DEFINE (scm_athena_ctx_find, "ctx-find", 2, 0, 0,
            (SCM context, SCM conditions),
            "Find the value with exactly matching ATHENA conditions.")
#define FUNC_NAME s_scm_athena_ctx_find
{
  while (scm_is_pair (context))
    {
      SCM entry = scm_car (context);
      if (scm_is_pair (entry)
          && scm_is_true (scm_equal_p (scm_car (entry), conditions)))
        return scm_cdr (entry);
      context = scm_cdr (context);
    }
  return SCM_BOOL_F;
}
#undef FUNC_NAME

SCM_DEFINE (scm_athena_ctx_remove, "ctx-remove", 2, 0, 0,
            (SCM context, SCM conditions),
            "Remove values with exactly matching ATHENA conditions.")
#define FUNC_NAME s_scm_athena_ctx_remove
{
  SCM result = SCM_EOL;
  while (scm_is_pair (context))
    {
      SCM entry = scm_car (context);
      if (!(scm_is_pair (entry)
            && scm_is_true (scm_equal_p (scm_car (entry), conditions))))
        result = scm_cons (entry, result);
      context = scm_cdr (context);
    }
  return scm_reverse_x (result, SCM_EOL);
}
#undef FUNC_NAME

static int
athena_conditions_match_p (SCM conditions, SCM args)
{
  if (scm_is_false (args))
    args = SCM_EOL;
  while (scm_is_pair (conditions))
    {
      if (scm_is_false (scm_apply_0 (scm_car (conditions), args)))
        return 0;
      conditions = scm_cdr (conditions);
    }
  return scm_is_null (conditions);
}

SCM_DEFINE (scm_athena_ctx_resolve, "ctx-resolve", 2, 0, 0,
            (SCM context, SCM args),
            "Resolve the first ATHENA context whose predicates hold.")
#define FUNC_NAME s_scm_athena_ctx_resolve
{
  while (scm_is_pair (context))
    {
      SCM entry = scm_car (context);
      if (scm_is_pair (entry)
          && athena_conditions_match_p (scm_car (entry), args))
        return scm_cdr (entry);
      context = scm_cdr (context);
    }
  return SCM_BOOL_F;
}
#undef FUNC_NAME

SCM_DEFINE (scm_athena_property_context, "%athena-property-context", 2, 0, 0,
            (SCM name, SCM property),
            "Return the contextual values for an ATHENA procedure property.")
#define FUNC_NAME s_scm_athena_property_context
{
  SCM key = scm_cons (name, property);
  SCM result;
  scm_dynwind_begin (0);
  scm_dynwind_pthread_mutex_lock (&athena_publication_lock);
  result = scm_hash_ref (athena_property_contexts, key, SCM_BOOL_F);
  scm_dynwind_end ();
  return result;
}
#undef FUNC_NAME

SCM_DEFINE (scm_athena_property_context_set_x,
            "%athena-property-context-set!", 3, 0, 0,
            (SCM name, SCM property, SCM context),
            "Store contextual values for an ATHENA procedure property.")
#define FUNC_NAME s_scm_athena_property_context_set_x
{
  SCM key = scm_cons (name, property);
  scm_dynwind_begin (0);
  scm_dynwind_pthread_mutex_lock (&athena_publication_lock);
  scm_hash_set_x (athena_property_contexts, key, context);
  scm_dynwind_end ();
  return context;
}
#undef FUNC_NAME

SCM_DEFINE (scm_athena_property_set_x, "property-set!", 4, 0, 0,
            (SCM name, SCM property, SCM value, SCM conditions),
            "Install a contextual ATHENA procedure property.")
#define FUNC_NAME s_scm_athena_property_set_x
{
  SCM key = scm_cons (name, property);
  SCM context;
  if (!athena_proper_list_p (conditions))
    scm_wrong_type_arg_msg (FUNC_NAME, 4, conditions, "proper list");
  scm_dynwind_begin (0);
  scm_dynwind_pthread_mutex_lock (&athena_publication_lock);
  context = scm_hash_ref (athena_property_contexts, key, SCM_EOL);
  context = scm_cons (scm_cons (conditions, value), context);
  scm_hash_set_x (athena_property_contexts, key, context);
  scm_dynwind_end ();
  return value;
}
#undef FUNC_NAME

SCM_DEFINE (scm_athena_property, "property", 2, 0, 0,
            (SCM name, SCM property),
            "Resolve a contextual ATHENA procedure property.")
#define FUNC_NAME s_scm_athena_property
{
  SCM key;
  SCM context;
  scm_dynwind_begin (0);
  scm_dynwind_pthread_mutex_lock (&athena_publication_lock);
  if (scm_is_true (scm_procedure_p (name)))
    name = scm_hashq_ref (athena_definition_names, name, SCM_BOOL_F);
  if (scm_is_false (name))
    {
      scm_dynwind_end ();
      return SCM_BOOL_F;
    }
  key = scm_cons (name, property);
  context = scm_hash_ref (athena_property_contexts, key, SCM_EOL);
  scm_dynwind_end ();
  while (scm_is_pair (context))
    {
      SCM entry = scm_car (context);
      if (scm_is_pair (entry)
          && athena_conditions_match_p (scm_car (entry), SCM_EOL))
        return scm_cdr (entry);
      context = scm_cdr (context);
    }
  return SCM_BOOL_F;
}
#undef FUNC_NAME

SCM_DEFINE (scm_athena_lazy_register_x, "%athena-lazy-register!", 2, 0, 0,
            (SCM name, SCM module),
            "Register @var{module} as a lazy provider of @var{name}.")
#define FUNC_NAME s_scm_athena_lazy_register_x
{
  scm_dynwind_begin (0);
  scm_dynwind_pthread_mutex_lock (&athena_publication_lock);
  athena_table_prepend (athena_lazy_modules, name, module);
  scm_dynwind_end ();
  return SCM_UNSPECIFIED;
}
#undef FUNC_NAME

SCM_DEFINE (scm_athena_not_define_option_p, "not-define-option?", 1, 0, 0,
            (SCM value),
            "Return true unless @var{value} starts an ATHENA definition option.")
#define FUNC_NAME s_scm_athena_not_define_option_p
{
  return scm_from_bool
    (!athena_option_form_p (value));
}
#undef FUNC_NAME

SCM_DEFINE (scm_athena_lazy_install_x, "%athena-lazy-install!", 3, 0, 0,
            (SCM name, SCM procedure, SCM module),
            "Install a lazy ATHENA root binding unless one already exists.")
#define FUNC_NAME s_scm_athena_lazy_install_x
{
  SCM variable;
  SCM result;
  scm_dynwind_begin (0);
  scm_dynwind_pthread_mutex_lock (&athena_publication_lock);
  variable = scm_module_variable (athena_root_module, name);
  if (!athena_variable_bound_p (variable))
    {
      scm_module_define (athena_root_module, name, procedure);
      scm_module_export (athena_root_module, scm_list_1 (name));
      scm_hashq_set_x (athena_definition_names, procedure, name);
    }
  athena_table_prepend (athena_lazy_modules, name, module);
  result = athena_variable_bound_p (variable)
             ? scm_variable_ref (variable) : procedure;
  scm_dynwind_end ();
  return result;
}
#undef FUNC_NAME

SCM_DEFINE (scm_athena_lazy_call, "%athena-lazy-call", 3, 0, 0,
            (SCM name, SCM module, SCM args),
            "Resolve and invoke lazy ATHENA definition @var{name}.")
#define FUNC_NAME s_scm_athena_lazy_call
{
  SCM variable;
  SCM procedure;
  SCM defined;
  athena_module_provide_internal (module, 1);
  scm_dynwind_begin (0);
  scm_dynwind_pthread_mutex_lock (&athena_publication_lock);
  defined = scm_hashq_ref (athena_definition_sources, name, SCM_BOOL_F);
  if (scm_is_false (defined))
    scm_misc_error (FUNC_NAME, "module ~S did not define ~S",
                    scm_list_2 (module, name));
  variable = scm_module_variable (athena_root_module, name);
  if (!athena_variable_bound_p (variable))
    scm_misc_error (FUNC_NAME, "could not resolve lazy definition ~S",
                    scm_list_1 (name));
  procedure = scm_variable_ref (variable);
  scm_dynwind_end ();
  return scm_apply_0 (procedure, args);
}
#undef FUNC_NAME

static int
athena_lazy_snapshot_contains_p (SCM snapshot, SCM module)
{
  while (scm_is_pair (snapshot))
    {
      if (scm_is_true (scm_equal_p (scm_car (snapshot), module)))
        return 1;
      snapshot = scm_cdr (snapshot);
    }
  return 0;
}

static SCM
athena_lazy_remove_snapshot (SCM current, SCM snapshot)
{
  SCM result = SCM_EOL;
  while (scm_is_pair (current))
    {
      SCM module = scm_car (current);
      if (!athena_lazy_snapshot_contains_p (snapshot, module))
        result = scm_cons (module, result);
      current = scm_cdr (current);
    }
  return scm_reverse_x (result, SCM_EOL);
}

SCM_DEFINE (scm_athena_lazy_force, "lazy-define-force", 1, 0, 0,
            (SCM value), "Load all providers of a lazy ATHENA definition.")
#define FUNC_NAME s_scm_athena_lazy_force
{
  SCM name = value;
  SCM modules;
  SCM snapshot;
  if (scm_is_true (scm_procedure_p (name)))
    {
      SCM registered;
      scm_dynwind_begin (0);
      scm_dynwind_pthread_mutex_lock (&athena_publication_lock);
      registered =
        scm_hashq_ref (athena_definition_names, name, SCM_BOOL_F);
      scm_dynwind_end ();
      name = scm_is_false (registered) ? scm_procedure_name (name) : registered;
      if (scm_is_false (name))
        return SCM_UNSPECIFIED;
    }
  if (!scm_is_symbol (name))
    scm_wrong_type_arg_msg (FUNC_NAME, 1, value, "procedure or symbol");
  scm_dynwind_begin (0);
  scm_dynwind_pthread_mutex_lock (&athena_publication_lock);
  modules = scm_hashq_ref (athena_lazy_modules, name, SCM_EOL);
  snapshot = modules;
  scm_dynwind_end ();
  while (scm_is_pair (modules))
    {
      athena_module_provide_internal (scm_car (modules), 1);
      modules = scm_cdr (modules);
    }
  scm_dynwind_begin (0);
  scm_dynwind_pthread_mutex_lock (&athena_publication_lock);
  modules = scm_hashq_ref (athena_lazy_modules, name, SCM_EOL);
  modules = athena_lazy_remove_snapshot (modules, snapshot);
  if (scm_is_null (modules))
    scm_hashq_remove_x (athena_lazy_modules, name);
  else
    scm_hashq_set_x (athena_lazy_modules, name, modules);
  scm_dynwind_end ();
  return SCM_UNSPECIFIED;
}
#undef FUNC_NAME

SCM_DEFINE (scm_athena_lazy_define_expand, "%athena-lazy-define-expand",
            2, 0, 0, (SCM module, SCM names),
            "Expand lazy ATHENA definitions using the native provider table.")
#define FUNC_NAME s_scm_athena_lazy_define_expand
{
  SCM forms = SCM_EOL;
  SCM options = SCM_EOL;
  SCM cursor = names;

  while (scm_is_pair (cursor)
         && athena_option_form_p (scm_car (cursor)))
    {
      options = scm_cons (scm_car (cursor), options);
      cursor = scm_cdr (cursor);
    }
  options = scm_reverse_x (options, SCM_EOL);

  while (scm_is_pair (cursor))
    {
      SCM name = scm_car (cursor);
      SCM args = athena_symbol ("args");
      SCM head;
      SCM option_body;
      SCM effective_body;
      SCM properties;
      struct athena_definition_options parsed;
      SCM existing_binding;
      SCM lazy_call;
      SCM lazy_lambda;
      SCM value;
      SCM define_form;
      SCM install_form;
      if (!scm_is_symbol (name))
        scm_misc_error (FUNC_NAME, "invalid lazy definition name ~S",
                        scm_list_1 (name));
      existing_binding = scm_list_2
        (athena_symbol ("existing"),
         scm_list_2 (athena_symbol ("%athena-definition-ref"),
                     athena_quote (name)));
      lazy_call = scm_list_4 (athena_symbol ("%athena-lazy-call"),
                              athena_quote (name), athena_quote (module), args);
      head = scm_cons (name, args);
      option_body = scm_append
        (scm_list_2 (options, scm_list_1 (lazy_call)));
      parsed = athena_parse_definition_options (head, option_body);
      effective_body = parsed.body;
      if (!scm_is_null (parsed.conditions))
        effective_body = scm_list_1
          (scm_list_4
             (athena_symbol ("if"),
              athena_condition_conjunction (parsed.conditions),
              athena_begin_expression (parsed.body),
              scm_list_3 (athena_symbol ("apply"),
                          athena_symbol ("noop"), args)));
      lazy_lambda = athena_lambda_form (head, effective_body);
      properties = athena_property_forms (name, &parsed);
      value = scm_list_3
        (athena_symbol ("let"), scm_list_1 (existing_binding),
         scm_list_4 (athena_symbol ("if"), athena_symbol ("existing"),
                     athena_symbol ("existing"), lazy_lambda));
      define_form = scm_list_3 (athena_symbol ("define"), name, value);
      install_form = scm_list_4 (athena_symbol ("%athena-lazy-install!"),
                                 athena_quote (name), name,
                                 athena_quote (module));
      forms = scm_append
        (scm_list_2 (scm_reverse_x (properties, SCM_EOL),
                     scm_cons (install_form, scm_cons (define_form, forms))));
      cursor = scm_cdr (cursor);
    }
  if (!scm_is_null (cursor))
    scm_misc_error (FUNC_NAME, "invalid lazy definition list ~S",
                    scm_list_1 (names));
  return scm_cons (athena_symbol ("begin"), scm_reverse_x (forms, SCM_EOL));
}
#undef FUNC_NAME

SCM_DEFINE (scm_athena_lazy_modules_ref, "%athena-lazy-modules", 1, 0, 0,
            (SCM name), "Return lazy provider modules for @var{name}.")
#define FUNC_NAME s_scm_athena_lazy_modules_ref
{
  SCM result;
  scm_dynwind_begin (0);
  scm_dynwind_pthread_mutex_lock (&athena_publication_lock);
  result = scm_hashq_ref (athena_lazy_modules, name, SCM_EOL);
  scm_dynwind_end ();
  return result;
}
#undef FUNC_NAME

SCM_DEFINE (scm_athena_lazy_clear_x, "%athena-lazy-clear!", 1, 0, 0,
            (SCM name), "Remove all lazy provider modules for @var{name}.")
#define FUNC_NAME s_scm_athena_lazy_clear_x
{
  scm_dynwind_begin (0);
  scm_dynwind_pthread_mutex_lock (&athena_publication_lock);
  scm_hashq_remove_x (athena_lazy_modules, name);
  scm_dynwind_end ();
  return SCM_UNSPECIFIED;
}
#undef FUNC_NAME

static SCM
athena_native_define_public_macro (SCM head, SCM body)
{
  SCM name = athena_head_name (head);
  SCM transformer;
  SCM definition;
  SCM export_form;

  if (!scm_is_pair (head))
    scm_misc_error ("define-public-macro", "invalid macro head ~S",
                    scm_list_1 (head));
  transformer = scm_cons
    (athena_symbol ("lambda"), scm_cons (scm_cdr (head), body));
  definition = scm_list_3 (athena_symbol ("define-macro"), name,
                           transformer);
  export_form = scm_list_2 (athena_symbol ("export"), name);
  return scm_list_4
    (athena_symbol ("eval-when"),
     scm_list_3 (athena_symbol ("expand"), athena_symbol ("load"),
                 athena_symbol ("eval")),
     definition, export_form);
}

static SCM
athena_native_provide_public (SCM module, SCM head, SCM body)
{
  SCM name = athena_head_name (head);
  SCM variable;

  /* Guile stores hygienic source-module information as
     (hygiene module-name ...), whereas module-variable expects the resolved
     module object.  This is the use-site module carried by the syntax object,
     not the runtime module in which this C transformer was installed. */
  if (scm_is_pair (module)
      && scm_is_eq (scm_car (module), athena_symbol ("hygiene")))
    module = scm_resolve_module (scm_cdr (module));
  if (scm_is_false (module))
    module = scm_current_module ();
  variable = scm_module_variable (module, name);

  /* Bytecode workers first bootstrap ATHENA from source.  Their live module
     therefore contains compatibility procedures supplied by earlier ATHENA
     files, even though a fresh bytecode-only process does not.  Compile those
     definitions unless the name belonged to the pristine Guile/ATHENA runtime
     visible at process initialization. */
  if (athena_compiling_scheme_bytecode_p ())
    {
      if (scm_is_true (scm_hashq_ref (athena_initial_root_bindings, name,
                                      SCM_BOOL_F)))
        return SCM_BOOL_F;
      return scm_cons (athena_symbol ("define-public"),
                       scm_cons (head, body));
    }
  if (scm_is_true (variable) && scm_is_true (scm_variable_bound_p (variable)))
    return SCM_BOOL_F;
  return scm_cons (athena_symbol ("define-public"),
                   scm_cons (head, body));
}

static SCM
athena_native_import_from (SCM modules)
{
  return scm_athena_import_from_expand (modules);
}

static SCM
athena_native_inherit_modules (SCM modules)
{
  return scm_athena_inherit_modules_expand (modules);
}

static SCM
athena_native_lazy_define (SCM module, SCM names)
{
  return scm_athena_lazy_define_expand (module, names);
}

static SCM
athena_native_tm_define (SCM head, SCM body)
{
  return scm_athena_tm_define_expand_full (head, body);
}

static SCM
athena_native_tm_property (SCM head, SCM body)
{
  return scm_athena_tm_property_expand (head, body);
}

static SCM
athena_native_tm_define_macro (SCM head, SCM body)
{
  return scm_athena_tm_define_macro_expand (head, body);
}

static SCM athena_required_syntax_argument (const char *who, SCM args);

/* Guile 1.8's memoizing defmacros expanded nested macro calls only when the
   corresponding expression was evaluated.  Guile 3 recursively expands the
   complete result while compiling a procedure.  A self-recursive legacy macro
   therefore never reaches bytecode (math-edit's concat-isolate! is the
   canonical example).  Keep ordinary defmacros on Guile 3's eager path, but
   lower a self-recursive defmacro to a finite lexical letrec expansion. */
static int
athena_contains_macro_call (SCM expression, SCM name)
{
  if (!scm_is_pair (expression))
    return 0;
  if (scm_is_eq (scm_car (expression), athena_symbol ("quote")))
    return 0;
  if (scm_is_eq (scm_car (expression), name))
    return 1;
  return athena_contains_macro_call (scm_car (expression), name)
         || athena_contains_macro_call (scm_cdr (expression), name);
}

static SCM
athena_rewrite_macro_calls (SCM expression, SCM name, SCM replacement)
{
  if (!scm_is_pair (expression))
    return expression;
  if (scm_is_eq (scm_car (expression), athena_symbol ("quote")))
    return expression;
  if (scm_is_eq (scm_car (expression), name))
    return scm_cons (replacement,
                     athena_rewrite_macro_calls (scm_cdr (expression),
                                                 name, replacement));
  return scm_cons (athena_rewrite_macro_calls (scm_car (expression),
                                               name, replacement),
                   athena_rewrite_macro_calls (scm_cdr (expression),
                                               name, replacement));
}

SCM_DEFINE (scm_athena_recursive_macro_expand,
            "%athena-recursive-macro-expand", 4, 0, 0,
            (SCM name, SCM transformer, SCM formals, SCM actuals),
            "Expand a Guile 1.8-style recursive defmacro to finite letrec code.")
#define FUNC_NAME s_scm_athena_recursive_macro_expand
{
  SCM helper;
  SCM actual_expansion;
  SCM template_expansion;
  SCM helper_lambda;
  SCM helper_binding;

  if (!scm_is_symbol (name))
    scm_wrong_type_arg_msg (FUNC_NAME, 1, name, "symbol");
  if (scm_is_false (scm_procedure_p (transformer)))
    scm_wrong_type_arg_msg (FUNC_NAME, 2, transformer, "procedure");
  if (!athena_proper_list_p (formals) || !athena_proper_list_p (actuals))
    scm_misc_error (FUNC_NAME,
                    "recursive defmacro requires fixed proper arguments",
                    scm_list_2 (formals, actuals));

  helper = scm_gensym (scm_from_utf8_string ("athena-recursive-macro-"));
  actual_expansion = scm_apply_0 (transformer, actuals);
  template_expansion = scm_apply_0 (transformer, formals);
  actual_expansion = athena_rewrite_macro_calls
    (actual_expansion, name, helper);
  template_expansion = athena_rewrite_macro_calls
    (template_expansion, name, helper);
  helper_lambda = scm_cons (athena_symbol ("lambda"),
                            scm_cons (formals,
                                      scm_list_1 (template_expansion)));
  helper_binding = scm_list_2 (helper, helper_lambda);
  return scm_list_3 (athena_symbol ("letrec"),
                     scm_list_1 (helper_binding), actual_expansion);
}
#undef FUNC_NAME

static SCM
athena_native_define_macro (SCM arguments)
{
  SCM declaration = athena_required_syntax_argument ("define-macro",
                                                      arguments);
  SCM tail = scm_cdr (arguments);
  SCM name;
  SCM formals = SCM_BOOL_F;
  SCM transformer_expression;
  SCM transformer_name = scm_gensym
    (scm_from_utf8_string ("athena-defmacro-transformer-"));
  SCM form_name = scm_gensym
    (scm_from_utf8_string ("athena-defmacro-form-"));
  int recursive = 0;

  if (scm_is_pair (declaration))
    {
      name = scm_car (declaration);
      formals = scm_cdr (declaration);
      if (!scm_is_symbol (name) || !athena_formals_p (formals)
          || !athena_proper_list_p (tail) || scm_is_null (tail))
        scm_misc_error ("define-macro", "invalid macro declaration ~S",
                        scm_list_1 (declaration));
      transformer_expression = scm_cons
        (athena_symbol ("lambda"), scm_cons (formals, tail));
      recursive = athena_proper_list_p (formals)
                  && athena_contains_macro_call (tail, name);
    }
  else
    {
      if (!scm_is_symbol (declaration) || !athena_proper_list_p (tail))
        scm_misc_error ("define-macro", "invalid macro declaration ~S",
                        scm_list_1 (declaration));
      name = declaration;
      if (scm_is_pair (tail) && scm_is_null (scm_cdr (tail)))
        transformer_expression = scm_car (tail);
      else if (scm_is_pair (tail) && scm_is_pair (scm_cdr (tail))
               && scm_is_null (scm_cddr (tail)))
        transformer_expression = scm_cadr (tail);
      else
        scm_misc_error ("define-macro", "invalid transformer for ~S",
                        scm_list_1 (name));
    }

  {
    SCM syntax_datum = scm_list_2 (athena_symbol ("syntax->datum"),
                                   form_name);
    SCM actuals = scm_list_2 (athena_symbol ("cdr"), syntax_datum);
    SCM expansion = recursive
      ? scm_list_5 (athena_symbol ("%athena-recursive-macro-expand"),
                    athena_quote (name), transformer_name,
                    athena_quote (formals), actuals)
      : scm_list_3 (athena_symbol ("apply"), transformer_name, actuals);
    SCM syntax_result = scm_list_3 (athena_symbol ("datum->syntax"),
                                    form_name, expansion);
    SCM transformer_lambda = scm_list_3 (athena_symbol ("lambda"),
                                         scm_list_1 (form_name),
                                         syntax_result);
    SCM transformer_binding = scm_list_2 (transformer_name,
                                          transformer_expression);
    SCM transformer_value = scm_list_3 (athena_symbol ("let"),
                                        scm_list_1 (transformer_binding),
                                        transformer_lambda);
    return scm_list_3 (athena_symbol ("define-syntax"), name,
                       transformer_value);
  }
}

static SCM
athena_native_texmacs_module (SCM name, SCM options)
{
  return scm_athena_texmacs_module_expand (name, options);
}

typedef SCM (*athena_native_transformer) (SCM args);

static SCM
athena_required_syntax_argument (const char *who, SCM args)
{
  if (!scm_is_pair (args))
    scm_misc_error (who, "missing required syntax argument", SCM_EOL);
  return scm_car (args);
}

static SCM
athena_native_transform_define_public_macro (SCM args)
{
  return athena_native_define_public_macro
    (athena_required_syntax_argument ("define-public-macro", args),
     scm_cdr (args));
}

static SCM
athena_native_transform_import_from (SCM args)
{
  return athena_native_import_from (args);
}

static SCM
athena_native_transform_inherit_modules (SCM args)
{
  return athena_native_inherit_modules (args);
}

static SCM
athena_native_transform_lazy_define (SCM args)
{
  return athena_native_lazy_define
    (athena_required_syntax_argument ("lazy-define", args), scm_cdr (args));
}

static SCM
athena_native_transform_tm_define (SCM args)
{
  return athena_native_tm_define
    (athena_required_syntax_argument ("tm-define", args), scm_cdr (args));
}

static SCM
athena_native_transform_tm_property (SCM args)
{
  return athena_native_tm_property
    (athena_required_syntax_argument ("tm-property", args), scm_cdr (args));
}

static SCM
athena_native_transform_tm_define_macro (SCM args)
{
  return athena_native_tm_define_macro
    (athena_required_syntax_argument ("tm-define-macro", args),
     scm_cdr (args));
}

static SCM
athena_native_transform_texmacs_module (SCM args)
{
  return athena_native_texmacs_module
    (athena_required_syntax_argument ("texmacs-module", args),
     scm_cdr (args));
}

static SCM
athena_native_syntax_expand (SCM form, athena_native_transformer transformer)
{
  SCM datum = scm_call_1 (athena_syntax_to_datum_proc, form);
  SCM expansion;

  if (!scm_is_pair (datum))
    scm_misc_error ("athena-native-syntax-expand",
                    "invalid ATHENA syntax ~S", scm_list_1 (datum));
  expansion = transformer (scm_cdr (datum));
  return scm_call_2 (athena_datum_to_syntax_proc, form, expansion);
}

#define ATHENA_NATIVE_SYNTAX_TRANSFORMER(c_name, transformer) \
  static SCM c_name (SCM form)                               \
  {                                                          \
    return athena_native_syntax_expand (form, transformer);  \
  }

ATHENA_NATIVE_SYNTAX_TRANSFORMER
  (athena_syntax_define_public_macro,
   athena_native_transform_define_public_macro)
ATHENA_NATIVE_SYNTAX_TRANSFORMER
  (athena_syntax_define_macro, athena_native_define_macro)

static SCM
athena_syntax_provide_public (SCM form)
{
  SCM datum = scm_call_1 (athena_syntax_to_datum_proc, form);
  SCM args;
  SCM expansion;

  if (!scm_is_pair (datum))
    scm_misc_error ("provide-public", "invalid ATHENA syntax ~S",
                    scm_list_1 (datum));
  args = scm_cdr (datum);
  expansion = athena_native_provide_public
    (scm_syntax_module (form),
     athena_required_syntax_argument ("provide-public", args),
     scm_cdr (args));
  return scm_call_2 (athena_datum_to_syntax_proc, form, expansion);
}

ATHENA_NATIVE_SYNTAX_TRANSFORMER
  (athena_syntax_import_from, athena_native_transform_import_from)
ATHENA_NATIVE_SYNTAX_TRANSFORMER
  (athena_syntax_inherit_modules, athena_native_transform_inherit_modules)
ATHENA_NATIVE_SYNTAX_TRANSFORMER
  (athena_syntax_lazy_define, athena_native_transform_lazy_define)
ATHENA_NATIVE_SYNTAX_TRANSFORMER
  (athena_syntax_tm_define, athena_native_transform_tm_define)
ATHENA_NATIVE_SYNTAX_TRANSFORMER
  (athena_syntax_tm_property, athena_native_transform_tm_property)
ATHENA_NATIVE_SYNTAX_TRANSFORMER
  (athena_syntax_tm_define_macro, athena_native_transform_tm_define_macro)
ATHENA_NATIVE_SYNTAX_TRANSFORMER
  (athena_syntax_texmacs_module, athena_native_transform_texmacs_module)

#undef ATHENA_NATIVE_SYNTAX_TRANSFORMER

static void
athena_install_native_syntax (SCM module, const char *name,
                              scm_t_subr function)
{
  SCM symbol = athena_symbol (name);
  SCM procedure = scm_c_make_gsubr (name, 1, 0, 0, function);
  SCM transformer = scm_make_syntax_transformer
    (symbol, athena_symbol ("macro"), procedure);
  scm_c_module_define (module, name, transformer);
}

static SCM
athena_thread_safe_module_use_x (SCM module, SCM interface)
{
#define FUNC_NAME "module-use!"
  SCM_VALIDATE_MODULE (1, module);
  SCM_VALIDATE_MODULE (2, interface);
  if (!scm_is_eq (module, interface)
      && scm_i_module_use (module, interface))
    scm_call_1 (athena_module_modified_proc, module);
#undef FUNC_NAME
  return SCM_UNSPECIFIED;
}

static SCM
athena_thread_safe_module_use_interfaces_x (SCM module, SCM interfaces)
{
  SCM cursor;
  int changed = 0;
#define FUNC_NAME "module-use-interfaces!"
  SCM_VALIDATE_MODULE (1, module);
  for (cursor = interfaces; scm_is_pair (cursor); cursor = scm_cdr (cursor))
    {
      SCM interface = scm_car (cursor);
      SCM_VALIDATE_MODULE (2, interface);
      if (!scm_is_eq (module, interface)
          && scm_i_module_use (module, interface))
        changed = 1;
    }
  if (!scm_is_null (cursor))
    scm_wrong_type_arg_msg (FUNC_NAME, 2, interfaces, "proper list");
  if (changed)
    scm_call_1 (athena_module_modified_proc, module);
#undef FUNC_NAME
  return SCM_UNSPECIFIED;
}

static void
init_athena_runtime_module (void *data)
{
  (void) data;
  athena_module_records = scm_c_make_hash_table (127);
  athena_sequential_modules = scm_c_make_hash_table (127);
  athena_definition_sources = scm_c_make_hash_table (4093);
  athena_definition_names = scm_c_make_hash_table (4093);
  athena_definition_modules = scm_c_make_hash_table (4093);
  athena_property_contexts = scm_c_make_hash_table (4093);
  athena_lazy_modules = scm_c_make_hash_table (1021);

  scm_gc_protect_object (athena_module_records);
  scm_gc_protect_object (athena_sequential_modules);
  scm_gc_protect_object (athena_definition_sources);
  scm_gc_protect_object (athena_definition_names);
  scm_gc_protect_object (athena_definition_modules);
  scm_gc_protect_object (athena_property_contexts);
  scm_gc_protect_object (athena_lazy_modules);

#include "athena-runtime.x"

  athena_install_native_syntax
    (scm_current_module (), "define-public-macro",
     (SCM_FUNC_CAST_ARBITRARY_ARGS) athena_syntax_define_public_macro);
  athena_install_native_syntax
    (scm_current_module (), "define-macro",
     (SCM_FUNC_CAST_ARBITRARY_ARGS) athena_syntax_define_macro);
  athena_install_native_syntax
    (scm_current_module (), "provide-public",
     (SCM_FUNC_CAST_ARBITRARY_ARGS) athena_syntax_provide_public);
  athena_install_native_syntax
    (scm_current_module (), "import-from",
     (SCM_FUNC_CAST_ARBITRARY_ARGS) athena_syntax_import_from);
  athena_install_native_syntax
    (scm_current_module (), "inherit-modules",
     (SCM_FUNC_CAST_ARBITRARY_ARGS) athena_syntax_inherit_modules);
  athena_install_native_syntax
    (scm_current_module (), "lazy-define",
     (SCM_FUNC_CAST_ARBITRARY_ARGS) athena_syntax_lazy_define);
  athena_install_native_syntax
    (scm_current_module (), "tm-define",
     (SCM_FUNC_CAST_ARBITRARY_ARGS) athena_syntax_tm_define);
  athena_install_native_syntax
    (scm_current_module (), "tm-property",
     (SCM_FUNC_CAST_ARBITRARY_ARGS) athena_syntax_tm_property);
  athena_install_native_syntax
    (scm_current_module (), "tm-define-macro",
     (SCM_FUNC_CAST_ARBITRARY_ARGS) athena_syntax_tm_define_macro);
  athena_install_native_syntax
    (scm_current_module (), "texmacs-module",
     (SCM_FUNC_CAST_ARBITRARY_ARGS) athena_syntax_texmacs_module);

  scm_c_module_define
    (scm_current_module (), "lazy-catch",
     scm_c_private_ref ("guile", "with-throw-handler"));
  scm_module_export (scm_current_module (),
                     scm_list_1 (athena_symbol ("lazy-catch")));

  scm_c_export ("%athena-runtime-version",
                "%athena-root-module",
                "%athena-configure-module!",
                "%athena-public-define-if-absent!",
                "%athena-root-binding-set!",
                "%athena-root-binding-ref",
                "%athena-module-export!",
                "%athena-module-register!",
                "%athena-module-mark-sequential!",
                "%athena-module-ref",
                "%athena-sequential-top-level?",
                "%athena-import-modules!",
                "%athena-inherit-modules!",
                "%athena-import-from-expand",
                "%athena-inherit-modules-expand",
                "%athena-texmacs-module-expand",
                "%athena-binding-install!",
                "%athena-definition-defined?",
                "%athena-definition-ref",
                "%athena-definition-install!",
                "%athena-definition-sources",
                "%athena-definition-modules",
                "%athena-procedure-name",
                "%athena-defined-symbols",
                "%athena-property-context",
                "%athena-property-context-set!",
                "%athena-tm-define-expand",
                "%athena-tm-define-expand-full",
                "%athena-tm-property-expand",
                "%athena-tm-define-macro-expand",
                "%athena-recursive-macro-expand",
                "ctx-add-condition",
                "ctx-insert",
                "ctx-find",
                "ctx-remove",
                "ctx-resolve",
                "property-set!",
                "property",
                "not-define-option?",
                "%athena-lazy-register!",
                "%athena-lazy-install!",
                "%athena-lazy-call",
                "%athena-lazy-define-expand",
                "%athena-lazy-modules",
                "%athena-lazy-clear!",
                "lazy-define-force",
                "module-available?",
                "module-provide",
                "module-load",
                "lazy-catch",
                NULL);
}

void
scm_init_athena_runtime (void)
{
  static const char *const compatibility_syntax[] = {
    "define-macro",
    "define-public-macro",
    "provide-public",
    "import-from",
    "inherit-modules",
    "texmacs-module",
    "lazy-define",
    "tm-define",
    "tm-property",
    "tm-define-macro"
  };
  SCM runtime;
  SCM core;
  SCM handlers;
  size_t i;

  /* Prefix keywords and source positions belong to ATHENA's private source
     language, not to Guile's own standard library.  In particular, boot-9
     must be compiled with Guile 3's native #:keyword reader.  ATHENA sets the
     source root before initializing the embedded runtime and in every
     bytecode worker. */
  if (getenv ("ATHENA_GUILE_SOURCE_ROOT"))
    {
      scm_c_eval_string ("(read-set! keywords 'prefix)");
      scm_c_eval_string ("(read-enable 'positions)");
    }

  athena_root_module = scm_current_module ();
  scm_gc_protect_object (athena_root_module);
  athena_module_construction_lock = scm_make_recursive_mutex ();
  scm_gc_protect_object (athena_module_construction_lock);
  scm_c_module_define (athena_root_module, "texmacs-user",
                       athena_root_module);
  scm_module_export (athena_root_module,
                     scm_list_1 (athena_symbol ("texmacs-user")));
  athena_resolve_interface_proc =
    scm_c_private_ref ("guile", "resolve-interface");
  athena_make_fresh_user_module_proc =
    scm_c_private_ref ("guile", "make-fresh-user-module");
  athena_module_modified_proc =
    scm_c_private_ref ("guile", "module-modified");
  athena_module_use_proc =
    scm_c_make_gsubr ("module-use!", 2, 0, 0,
                      (SCM_FUNC_CAST_ARBITRARY_ARGS)
                      athena_thread_safe_module_use_x);
  athena_module_use_interfaces_proc =
    scm_c_make_gsubr ("module-use-interfaces!", 2, 0, 0,
                      (SCM_FUNC_CAST_ARBITRARY_ARGS)
                      athena_thread_safe_module_use_interfaces_x);
  /* Compiled callers dereference this variable at run time.  Replace it
     before ATHENA starts actor threads, leaving Guile's single-threaded boot
     path unchanged while making all later module imports use one publication
     operation. */
  scm_variable_set_x (scm_c_private_lookup ("guile", "module-use!"),
                      athena_module_use_proc);
  scm_variable_set_x
    (scm_c_private_lookup ("guile", "module-use-interfaces!"),
     athena_module_use_interfaces_proc);
  athena_module_add_proc = scm_c_private_ref ("guile", "module-add!");
  athena_module_name_proc = scm_c_private_ref ("guile", "module-name");
  athena_syntax_to_datum_proc =
    scm_c_private_ref ("guile", "syntax->datum");
  athena_datum_to_syntax_proc =
    scm_c_private_ref ("guile", "datum->syntax");
  athena_set_module_binder_proc =
    scm_c_private_ref ("guile", "set-module-binder!");
  athena_set_module_declarative_proc =
    scm_c_private_ref ("guile", "set-module-declarative?!");
  athena_set_module_duplicates_proc =
    scm_c_private_ref ("guile", "set-module-duplicates-handlers!");
  athena_lookup_duplicates_handlers_proc =
    scm_c_private_ref ("guile", "lookup-duplicates-handlers");
  athena_original_module_export_proc =
    scm_c_private_ref ("guile", "module-export!");
  scm_gc_protect_object (athena_resolve_interface_proc);
  scm_gc_protect_object (athena_make_fresh_user_module_proc);
  scm_gc_protect_object (athena_module_use_proc);
  scm_gc_protect_object (athena_module_use_interfaces_proc);
  scm_gc_protect_object (athena_module_modified_proc);
  scm_gc_protect_object (athena_module_add_proc);
  scm_gc_protect_object (athena_module_name_proc);
  scm_gc_protect_object (athena_syntax_to_datum_proc);
  scm_gc_protect_object (athena_datum_to_syntax_proc);
  scm_gc_protect_object (athena_set_module_binder_proc);
  scm_gc_protect_object (athena_set_module_declarative_proc);
  scm_gc_protect_object (athena_set_module_duplicates_proc);
  scm_gc_protect_object (athena_lookup_duplicates_handlers_proc);
  scm_gc_protect_object (athena_original_module_export_proc);
  /* The legacy root intentionally imports ATHENA definitions which replace
     Guile's generic helpers (for example display, when, and select).  Give
     that module the same deterministic last-import-wins policy as ATHENA
     modules, without installing the legacy global binder on Guile's root. */
  handlers = scm_call_1 (athena_lookup_duplicates_handlers_proc,
                         scm_list_1 (athena_symbol ("last")));
  scm_call_2 (athena_set_module_duplicates_proc, athena_root_module,
              handlers);
  scm_call_2 (athena_set_module_declarative_proc, athena_root_module,
              SCM_BOOL_F);
  scm_c_define_module ("athena runtime", init_athena_runtime_module, NULL);
  runtime = scm_c_resolve_module ("athena runtime");
  athena_global_binder_proc =
    scm_c_private_ref ("athena runtime", "%athena-global-binder");
  scm_gc_protect_object (athena_global_binder_proc);
  core = scm_c_resolve_module ("guile");
  for (i = 0; i < sizeof (compatibility_syntax) /
                        sizeof (compatibility_syntax[0]); ++i)
    athena_install_core_syntax (runtime, core, compatibility_syntax[i]);
  scm_c_module_define
    (core, "module-export!",
     scm_c_private_ref ("athena runtime", "%athena-module-export!"));
  scm_c_use_module ("athena runtime");
  athena_snapshot_initial_root_bindings ();
}

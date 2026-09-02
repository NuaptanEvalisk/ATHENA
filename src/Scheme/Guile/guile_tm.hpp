/******************************************************************************
 * MODULE     : guile_tm.hpp
 * DESCRIPTION: Everything which depends on the version of Guile
 *              should be move to this file
 * COPYRIGHT  : (C) 1999-2011  Joris van der Hoeven and Massimiliano Gubinelli
 *******************************************************************************
 * This software falls under the GNU general public license version 3 or later.
 * It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
 * in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
 ******************************************************************************/

#ifndef GUILE_TM_H
#define GUILE_TM_H

#include "tm_configure.hpp"
#include "actor_transport.hpp"
#include "blackbox.hpp"
#include "array.hpp"

#include <type_traits>


#ifdef OS_MINGW
// we redefine some symbols to avoid name clashes with Windows headers (included by Guile)
#define GROUP WIN_GROUP
#ifdef IN
#define MY_IN IN
#undef IN
#endif
#ifdef OUT
#define MY_OUT OUT
#undef OUT
#endif
#ifdef MENU_EVENT
#define MY_MENU_EVENT MENU_EVENT
#undef MENU_EVENT
#endif
#endif // OS_MINGW

#include <libguile.h>

#ifdef OS_MINGW
#undef GROUP
#undef IN
#undef OUT
#undef MENU_EVENT
#ifdef MY_MENU_EVENT
#define MENU_EVENT MY_MENU_EVENT
#undef MY_MENU_EVENT
#endif
#ifdef MY_IN
#define IN MY_IN
#undef MY_IN
#endif
#ifdef MY_OUT
#define OUT MY_OUT
#undef MY_OUT
#endif
#endif // OS_MINGW

#define SCM_NULL scm_list_n (SCM_UNDEFINED)
#define scm_bool2scm scm_from_bool
#define scm_is_list(x) scm_is_true(scm_list_p(x))
#define scm_scm2bool scm_is_true
#define scm_is_int scm_is_integer
#define scm_is_double scm_is_real
#define scm_new_procedure(name,r,a,b,c) scm_c_define_gsubr(name,a,b,c,(scm_t_subr)r)
#define scm_lookup_string(name) scm_variable_ref(scm_c_lookup(name))
#define scm_long2scm scm_from_long
#define scm_scm2long scm_to_long
#define scm_double2scm scm_from_double
#define scm_scm2double scm_to_double
#define scm_internal_lazy_catch(tag,body,body_data,handler,handler_data) \
  scm_internal_catch(tag,body,body_data,handler,handler_data)

#define SCM_ARG8 8
#define SCM_ARG9 9
#define SCM_ARG10 10
#define SCM_ARG11 11

typedef SCM (*FN)();

typedef SCM tmscm;

struct tmscm_root_handle;
class SchemeExecutionContext;

using scheme_execution_callback= tmscm (*) (void*);

tmscm_root_handle* tmscm_root_acquire (tmscm obj);
tmscm tmscm_root_value (const tmscm_root_handle* handle);
void tmscm_root_release (tmscm_root_handle* handle) noexcept;
athena_scheme_handle_id scheme_command_handle_acquire (tmscm command);
tmscm scheme_command_handle_value (athena_scheme_handle_id id);
void scheme_command_handle_release (athena_scheme_handle_id id) noexcept;
void scheme_runtime_safe_point ();
void scheme_runtime_drain_all ();
bool scheme_runtime_is_initialized () noexcept;
tmscm scheme_with_execution_context (
  const SchemeExecutionContext& context,
  scheme_execution_callback callback, void* data);

bool tmscm_is_blackbox (tmscm obj);
tmscm blackbox_to_tmscm (blackbox b);
blackbox tmscm_to_blackbox (tmscm obj);

inline tmscm tmscm_null () { return SCM_NULL; }
inline tmscm tmscm_true () { return SCM_BOOL_T; }
inline tmscm tmscm_false () { return SCM_BOOL_F; }
inline void tmscm_set_car (tmscm a, tmscm b) { SCM_SETCAR(a,b); }
inline void tmscm_set_cdr (tmscm a, tmscm b) { SCM_SETCDR(a,b); }
	
	
inline bool tmscm_is_equal (tmscm o1, tmscm o2) { return SCM_NFALSEP ( scm_equal_p(o1, o2)); }



inline bool tmscm_is_null (tmscm obj) { return scm_is_null (obj); }
inline bool tmscm_is_pair (tmscm obj) { return scm_is_pair (obj); }
inline bool tmscm_is_list (tmscm obj) { return scm_is_list (obj); }
inline bool tmscm_is_bool (tmscm obj) { return scm_is_bool (obj); }
inline bool tmscm_is_int (tmscm obj) { return scm_is_int (obj); }
inline bool tmscm_is_double (tmscm obj) { return scm_is_double (obj); }
inline bool tmscm_is_string (tmscm obj) { return scm_is_string (obj); }
inline bool tmscm_is_symbol (tmscm obj) { return scm_is_symbol (obj); }

inline tmscm tmscm_cons (tmscm obj1, tmscm obj2) { return scm_cons (obj1, obj2); }
inline tmscm tmscm_car (tmscm obj) { return SCM_CAR (obj); }
inline tmscm tmscm_cdr (tmscm obj) { return SCM_CDR (obj); }
inline tmscm tmscm_caar (tmscm obj) { return SCM_CAAR (obj); }
inline tmscm tmscm_cadr (tmscm obj) { return SCM_CADR (obj); }
inline tmscm tmscm_cdar (tmscm obj) { return SCM_CDAR (obj); }
inline tmscm tmscm_cddr (tmscm obj) { return SCM_CDDR (obj); }
inline tmscm tmscm_caddr (tmscm obj) { return SCM_CADDR (obj); }
inline tmscm tmscm_cadddr (tmscm obj) { return SCM_CADDDR (obj); }



SCM bool_to_scm (bool b);
SCM int_to_scm (int i);
SCM long_to_scm (long l);
SCM double_to_scm (double i);



inline tmscm bool_to_tmscm (bool b) { return bool_to_scm (b); }
inline tmscm int_to_tmscm (int i) { return int_to_scm (i); }
inline tmscm long_to_tmscm (long l) { return long_to_scm (l); }
inline tmscm double_to_tmscm (double i) { return double_to_scm (i); }
tmscm string_to_tmscm (string s);
tmscm symbol_to_tmscm (string s);

inline bool tmscm_to_bool (tmscm obj) { return scm_to_bool (obj); }
inline int tmscm_to_int (tmscm obj) { return scm_to_int (obj); }
inline int tmscm_to_uint (tmscm obj) { return scm_to_uint (obj); }
inline double tmscm_to_double (tmscm obj) { return scm_to_double (obj); }
string tmscm_to_string (tmscm obj);
string tmscm_to_symbol (tmscm obj);




tmscm eval_scheme_file (string name);
tmscm eval_scheme (string s);
tmscm call_scheme (tmscm fun);
tmscm call_scheme (tmscm fun, tmscm a1);
tmscm call_scheme (tmscm fun, tmscm a1, tmscm a2);
tmscm call_scheme (tmscm fun, tmscm a1, tmscm a2, tmscm a3);
tmscm call_scheme (tmscm fun, tmscm a1, tmscm a2, tmscm a3, tmscm a4);
tmscm call_scheme (tmscm fun, array<tmscm> a);


template<typename Return, typename... Args>
inline void
tmscm_install_procedure_checked (const char* name, Return (*func) (Args...),
                                 int args, int p0, int p1) {
  static_assert (std::is_same<Return, tmscm>::value,
                 "Scheme primitives must return tmscm");
  scm_new_procedure (name, reinterpret_cast<FN> (func), args, p0, p1);
}

#define tmscm_install_procedure(name, func, args, p0, p1) \
  tmscm_install_procedure_checked (name, func, args, p0, p1)

#define TMSCM_ASSERT(_cond, _arg, _pos, _subr) \
 SCM_ASSERT(_cond, _arg, _pos, _subr)

#define TMSCM_ARG1 SCM_ARG1
#define TMSCM_ARG2 SCM_ARG2
#define TMSCM_ARG3 SCM_ARG3
#define TMSCM_ARG4 SCM_ARG4
#define TMSCM_ARG5 SCM_ARG5
#define TMSCM_ARG6 SCM_ARG6
#define TMSCM_ARG7 SCM_ARG7
#define TMSCM_ARG8 SCM_ARG8
#define TMSCM_ARG9 SCM_ARG9
#define TMSCM_ARG10 SCM_ARG10

#define TMSCM_UNSPECIFIED SCM_UNSPECIFIED


string scheme_dialect ();


#endif // defined GUILE_TM_H

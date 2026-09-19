/******************************************************************************
* MODULE     : document_commands.cpp
* DESCRIPTION: Actor-owned generic document editing commands
* COPYRIGHT  : (C) 2001 Joris van der Hoeven, 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
*******************************************************************************/

#include "document_commands.hpp"
#include "editor.hpp"
#include "new_view.hpp"

bool
document_test_default (object variables) {
  if (!is_list (variables)) return false;
  editor ed= get_current_editor ();
  array<object> items= as_array_object (variables);
  for (int i= 0; i < N (items); ++i) {
    if (!is_string (items[i])) return false;
    if (ed->defined_in_init (as_string (items[i]))) return false;
  }
  return true;
}

void
document_init_default (object variables) {
  if (!is_list (variables)) return;
  array<object> items= as_array_object (variables);
  for (int i= 0; i < N (items); ++i)
    if (is_string (items[i])) init_default_current_view (as_string (items[i]));
}

object
document_get_init_env (string variable) {
  tree value= get_current_editor ()->get_init_value (variable);
  if (is_atomic (value)) return object (as_string (value));
  if (is_compound (value, "macro") && N (value) == 1 && is_atomic (value[0]))
    return object (as_string (value[0]));
  return object (false);
}

bool
document_test_init (string variable, string value) {
  return get_current_editor ()->get_init_value (variable) == tree (value);
}

void
document_set_init_env (string variable, tree value) {
  editor ed= get_current_editor ();
  tree old= ed->get_init_value (variable);
  if (is_compound (old, "macro") && N (old) == 1 &&
      !is_compound (value, "macro"))
    ed->init_env (variable, compound ("macro", value));
  else
    ed->init_env (variable, value);
}

bool
document_test_init_true (string variable) {
  return document_test_init (variable, "true");
}

void
document_init_multi (object values) {
  if (!is_list (values)) return;
  array<object> items= as_array_object (values);
  object default_keyword= keyword_object ("default");
  for (int i= 0; i + 1 < N (items); i += 2) {
    if (!is_string (items[i])) continue;
    string variable= as_string (items[i]);
    object value= items[i + 1];
    if (variable == "font" && value == default_keyword) {
      call ("remove-font-packages");
      init_default_current_view ("font");
    }
    else if (variable == "font")
      call ("init-font", value);
    else if (value == default_keyword)
      init_default_current_view (variable);
    else if (is_string (value))
      get_current_editor ()->init_env (variable, tree (as_string (value)));
  }
}

/******************************************************************************
* MODULE     : document_style_commands.cpp
* DESCRIPTION: Actor-owned generic document style commands
* COPYRIGHT  : (C) 2001 Joris van der Hoeven, 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
*******************************************************************************/

#include "document_style_commands.hpp"
#include "editor.hpp"

namespace {

array<string> current_style_strings () {
  tree style= get_current_editor ()->get_style ();
  array<string> result;
  if (is_atomic (style)) {
    result << as_string (style);
    return result;
  }
  if (!is_compound (style, "tuple")) return result;
  for (int i= 0; i < N (style); ++i)
    if (is_atomic (style[i])) result << as_string (style[i]);
  return result;
}

object style_list_object (array<string> styles) {
  array<object> objects;
  for (int i= 0; i < N (styles); ++i) objects << object (styles[i]);
  return as_list_object (objects);
}

void set_style_strings (array<string> styles) {
  (void) call ("set-style-list", style_list_object (styles));
}

bool contains_style (array<string> styles, string value) {
  for (int i= 0; i < N (styles); ++i)
    if (styles[i] == value) return true;
  return false;
}

} // namespace

object
document_style_category (string style) {
  return object (style);
}

bool
document_style_category_overrides (object left, object right) {
  return left == right;
}

bool
document_style_category_precedes (object, object) {
  return false;
}

bool
document_style_includes (string, string) {
  return false;
}

bool
document_style_overrides (string left, string right) {
  object left_category= call ("style-category", object (left));
  object right_category= call ("style-category", object (right));
  return as_bool (call ("style-category-overrides?", left_category, right_category));
}

bool
document_style_precedes (string left, string right) {
  object left_category= call ("style-category", object (left));
  object right_category= call ("style-category", object (right));
  return as_bool (call ("style-category-precedes?", left_category, right_category));
}

object
document_get_style_list () {
  return style_list_object (current_style_strings ());
}

object
document_embedded_style_list (object extra_packages) {
  array<string> result= current_style_strings ();
  if (is_list (extra_packages)) {
    array<object> extras= as_array_object (extra_packages);
    for (int i= 0; i < N (extras); ++i) {
      if (!is_string (extras[i])) continue;
      string package= as_string (extras[i]);
      if (!contains_style (result, package)) result << package;
    }
  }
  return style_list_object (result);
}

bool
document_has_no_style () {
  return N (current_style_strings ()) == 0;
}

void
document_set_no_style () {
  set_style_strings (array<string> ());
}

bool
document_has_main_style (string style) {
  array<string> styles= current_style_strings ();
  return N (styles) > 0 && styles[0] == style;
}

void
document_notify_new_style (string) {
}

bool
document_has_style_package (string package) {
  array<string> styles= current_style_strings ();
  if (contains_style (styles, package)) return true;

  bool included= false;
  bool overridden= false;
  for (int i= 0; i < N (styles); ++i) {
    if (as_bool (call ("style-includes?", object (styles[i]), object (package))))
      included= true;
    if (as_bool (call ("style-overrides?", object (styles[i]), object (package))))
      overridden= true;
  }
  return included && !overridden;
}

bool
document_not_has_style_package (string package) {
  return !document_has_style_package (package);
}

void
document_add_style_package (string package) {
  array<string> styles= current_style_strings ();
  styles << package;
  set_style_strings (styles);
}

void
document_remove_style_package (string package) {
  array<string> styles= current_style_strings ();
  array<string> filtered;
  for (int i= 0; i < N (styles); ++i)
    if (styles[i] != package) filtered << styles[i];
  set_style_strings (filtered);
}

void
document_remove_style_package_star (string package) {
  document_remove_style_package (package);
}

void
document_toggle_style_package (string package) {
  if (document_has_style_package (package))
    document_remove_style_package (package);
  else
    document_add_style_package (package);
}

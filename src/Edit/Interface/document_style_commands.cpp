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

bool contains_style (array<string> styles, string value);

bool style_arrays_equal (array<string> left, array<string> right) {
  if (N (left) != N (right)) return false;
  for (int i= 0; i < N (left); ++i)
    if (left[i] != right[i]) return false;
  return true;
}

array<string> style_tail (array<string> styles) {
  array<string> result;
  for (int i= 1; i < N (styles); ++i) result << styles[i];
  return result;
}

bool style_relation_any (array<string> styles, string relation, string value) {
  for (int i= 0; i < N (styles); ++i)
    if (as_bool (call (relation, object (styles[i]), object (value)))) return true;
  return false;
}

array<string> normalize_style_list_star (array<string> styles) {
  if (N (styles) == 0) return styles;
  string first= styles[0];
  array<string> tail= style_tail (styles);

  if (style_relation_any (tail, "style-overrides?", first))
    return normalize_style_list_star (tail);

  if (style_relation_any (tail, "style-precedes?", first)) {
    // Legacy Scheme computes (list-delete tail predicate). Style entries are
    // strings and the deleted value is a procedure, so the tail is unchanged.
    array<string> normalized= normalize_style_list_star (tail);
    array<string> result;
    result << normalized[0];
    array<string> reordered;
    reordered << first;
    for (int i= 1; i < N (normalized); ++i) reordered << normalized[i];
    array<string> remainder= normalize_style_list_star (reordered);
    for (int i= 0; i < N (remainder); ++i) result << remainder[i];
    return result;
  }

  array<string> result;
  result << first;
  array<string> remainder= normalize_style_list_star (tail);
  for (int i= 0; i < N (remainder); ++i) result << remainder[i];
  return result;
}

array<string> normalize_style_list_starstar (array<string> styles,
                                             array<string> before) {
  if (N (styles) == 0) return styles;
  string first= styles[0];
  array<string> next_before= before;
  next_before << first;
  array<string> remainder=
    normalize_style_list_starstar (style_tail (styles), next_before);
  if (style_relation_any (before, "style-includes?", first)) return remainder;

  array<string> result;
  result << first;
  for (int i= 0; i < N (remainder); ++i) result << remainder[i];
  return result;
}

array<string> normalize_style_list (array<string> styles) {
  array<string> unique;
  for (int i= 0; i < N (styles); ++i)
    if (!contains_style (unique, styles[i])) unique << styles[i];
  if (N (unique) == 0) return unique;

  array<string> before;
  before << unique[0];
  array<string> normalized=
    normalize_style_list_starstar (normalize_style_list_star (style_tail (unique)),
                                   before);
  array<string> result;
  result << unique[0];
  for (int i= 0; i < N (normalized); ++i) result << normalized[i];
  return result;
}

bool object_to_style_strings (object value, array<string>& styles) {
  if (!is_list (value)) return false;
  array<object> items= as_array_object (value);
  for (int i= 0; i < N (items); ++i) {
    if (!is_string (items[i])) return false;
    styles << as_string (items[i]);
  }
  return true;
}

void set_style_strings (array<string> styles) {
  array<string> normalized= normalize_style_list (styles);
  if (style_arrays_equal (normalized, current_style_strings ())) return;
  array<tree> children;
  for (int i= 0; i < N (normalized); ++i) children << tree (normalized[i]);
  get_current_editor ()->change_style (tree (TUPLE, children));
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

void
document_set_style_list (object value) {
  array<string> styles;
  if (!object_to_style_strings (value, styles)) return;
  set_style_strings (styles);
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

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

#include <initializer_list>

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

namespace {

void notify_page_change () { (void) call ("notify-page-change"); }

bool defaults_absent (std::initializer_list<const char*> variables) {
  editor ed= get_current_editor ();
  for (const char* variable: variables)
    if (ed->defined_in_init (string (variable))) return false;
  return true;
}

void reset_defaults (std::initializer_list<const char*> variables) {
  for (const char* variable: variables)
    init_default_current_view (string (variable));
}

} // namespace

bool document_test_default_page_medium () {
  return defaults_absent ({"page-medium"});
}

void document_init_default_page_medium () {
  reset_defaults ({"page-medium"});
  notify_page_change ();
}

bool document_test_page_medium (string value) {
  return get_current_editor ()->get_init_string ("page-medium") == value;
}

void document_init_page_medium (string value) {
  get_current_editor ()->init_env ("page-medium", tree (value));
  notify_page_change ();
}

bool document_test_default_page_type () {
  return defaults_absent ({"page-type", "page-width", "page-height"});
}

void document_default_page_type () {
  reset_defaults ({"page-type", "page-width", "page-height"});
  notify_page_change ();
}

bool document_test_page_type (string value) {
  return get_current_editor ()->get_init_string ("page-type") == value;
}

void document_init_page_type (string value) {
  editor ed= get_current_editor ();
  ed->init_env ("page-type", tree (value));
  ed->init_env ("page-width", tree ("auto"));
  ed->init_env ("page-height", tree ("auto"));
  notify_page_change ();
}

void document_init_page_size (string width, string height) {
  editor ed= get_current_editor ();
  ed->init_env ("page-type", tree ("user"));
  ed->init_env ("page-width", tree (width));
  ed->init_env ("page-height", tree (height));
  notify_page_change ();
}

bool document_test_default_page_orientation () {
  return defaults_absent ({"page-orientation"});
}

void document_init_default_page_orientation () {
  reset_defaults ({"page-orientation"});
  notify_page_change ();
}

bool document_test_page_orientation (string value) {
  return get_current_editor ()->get_env_string ("page-orientation") == value;
}

void document_init_page_orientation (string value) {
  get_current_editor ()->init_env ("page-orientation", tree (value));
  notify_page_change ();
}

bool document_visible_header_and_footer () {
  return get_current_editor ()->get_env_string ("page-show-hf") == "true";
}

void document_toggle_visible_header_and_footer () {
  editor ed= get_current_editor ();
  string next= ed->get_env_string ("page-show-hf") == "true" ? "false" : "true";
  ed->init_env ("page-show-hf", tree (next));
}

bool document_page_width_margin () {
  return get_current_editor ()->get_env_string ("page-width-margin") == "true";
}

void document_toggle_page_width_margin () {
  editor ed= get_current_editor ();
  string next= ed->get_env_string ("page-width-margin") == "true" ? "false" : "true";
  ed->init_env ("page-width-margin", tree (next));
}

bool document_not_page_screen_margin () {
  return get_current_editor ()->get_env_string ("page-screen-margin") == "false";
}

void document_toggle_page_screen_margin () {
  editor ed= get_current_editor ();
  string next= ed->get_env_string ("page-screen-margin") == "false" ? "true" : "false";
  ed->init_env ("page-screen-margin", tree (next));
}

namespace {

bool has_style_package (string name) {
  return as_bool (call ("has-style-package?", object (name)));
}

void add_style_package (string name) {
  (void) call ("add-style-package", object (name));
}

void remove_style_package (string name) {
  (void) call ("remove-style-package", object (name));
}

} // namespace

bool document_reduced_margins () {
  return document_test_init ("page-odd", "1cm");
}

void document_toggle_reduced_margins () {
  if (has_style_package ("reduced-margins"))
    remove_style_package ("reduced-margins");
  else if (has_style_package ("normal-margins"))
    remove_style_package ("normal-margins");
  else if (document_reduced_margins ())
    add_style_package ("normal-margins");
  else
    add_style_package ("reduced-margins");
}

bool document_indent_paragraphs () {
  object value= document_get_init_env ("par-first");
  if (!is_string (value)) return true;
  string s= as_string (value);
  return !(s == "0fn" || s == "0em" || s == "0tab" ||
           s == "0cm" || s == "0mm" || s == "0in");
}

void document_toggle_indent_paragraphs () {
  if (has_style_package ("indent-paragraphs"))
    remove_style_package ("indent-paragraphs");
  else if (has_style_package ("padded-paragraphs"))
    remove_style_package ("padded-paragraphs");
  else if (document_indent_paragraphs ())
    add_style_package ("padded-paragraphs");
  else
    add_style_package ("indent-paragraphs");
}

bool document_no_page_numbers () {
  return document_test_init ("no-page-numbers", "true");
}

void document_toggle_no_page_numbers () {
  if (has_style_package ("page-numbers"))
    remove_style_package ("page-numbers");
  else if (has_style_package ("no-page-numbers"))
    remove_style_package ("no-page-numbers");
  else if (document_no_page_numbers ())
    add_style_package ("page-numbers");
  else
    add_style_package ("no-page-numbers");
}

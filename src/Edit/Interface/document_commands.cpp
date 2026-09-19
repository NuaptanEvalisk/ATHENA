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
#include "font.hpp"
#include "analyze.hpp"

#include <cmath>
#include <initializer_list>

bool document_in_source_mode () {
  return get_current_editor ()->get_env_string ("preamble") == "true";
}

void document_toggle_source_mode () {
  editor ed= get_current_editor ();
  string next= ed->get_env_string ("preamble") == "true" ? "false" : "true";
  if (next == "true") {
    ed->init_env ("src-style", tree (as_string (call ("get-preference", object ("source tree style")))));
    ed->init_env ("src-special", tree (as_string (call ("get-preference", object ("source tree special rendering")))));
    ed->init_env ("src-compact", tree (as_string (call ("get-preference", object ("source tree compactification")))));
    ed->init_env ("src-close", tree (as_string (call ("get-preference", object ("source tree closing style")))));
  }
  ed->init_env ("preamble", tree (next));
}

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

bool tex_gyre_document_font (string value) {
  return value == "Bonum" || value == "bonum" ||
         value == "Pagella" || value == "pagella" ||
         value == "Schola" || value == "schola" ||
         value == "Termes" || value == "termes" ||
         starts (value, "TeX Gyre Bonum") ||
         starts (value, "TeX Gyre Pagella") ||
         starts (value, "TeX Gyre Schola") ||
         starts (value, "TeX Gyre Termes");
}

string tex_gyre_document_profile (string value) {
  if (value == "Bonum" || value == "bonum" || starts (value, "TeX Gyre Bonum"))
    return "TeX Gyre Bonum";
  if (value == "Pagella" || value == "pagella" || starts (value, "TeX Gyre Pagella"))
    return "TeX Gyre Pagella";
  if (value == "Schola" || value == "schola" || starts (value, "TeX Gyre Schola"))
    return "TeX Gyre Schola";
  if (value == "Termes" || value == "termes" || starts (value, "TeX Gyre Termes"))
    return "TeX Gyre Termes";
  return value;
}

string font_package_name (string value) {
  if (value == "Fira") return "fira-font";
  if (value == "Linux Biolinum") return "biolinum-font";
  if (value == "Linux Libertine") return "libertine-font";
  return value * "-font";
}

} // namespace

string
document_font_display_name (string value) {
  string family= main_family (value);
  if (family == "bonum" || starts (family, "TeX Gyre Bonum")) return "Bonum";
  if (family == "pagella" || starts (family, "TeX Gyre Pagella")) return "Pagella";
  if (family == "schola" || starts (family, "TeX Gyre Schola")) return "Schola";
  if (family == "termes" || starts (family, "TeX Gyre Termes")) return "Termes";
  return upcase_first (family);
}

bool
document_test_init_font (string value, object) {
  string current= get_current_editor ()->get_init_string ("font");
  return document_font_display_name (current) == document_font_display_name (value);
}

void
document_remove_font_packages () {
  object styles= call ("get-style-list");
  if (!is_list (styles)) return;
  list<string> current= as_list_string (styles);
  list<string> filtered;
  for (list<string> it= current; !is_nil (it); it= it->next)
    if (!ends (it->item, "-font")) filtered << it->item;
  call ("set-style-list", object (filtered));
}

void
document_init_font (string value, object options) {
  editor ed= get_current_editor ();
  array<object> opts= is_list (options) ? as_array_object (options) : array<object> ();

  if (value == "TeXmacs Computer Modern") {
    call ("init-font", object ("roman"), object ("roman"));
    return;
  }
  if (value == "roman" &&
      !(N (opts) == 1 && is_string (opts[0]) && as_string (opts[0]) == "roman")) {
    call ("init-font", object ("roman"), object ("roman"));
    return;
  }
  if (tex_gyre_document_font (value)) {
    ed->init_env ("font", tree (tex_gyre_document_profile (value)));
    init_default_current_view ("math-font");
    ed->init_env ("font-family", tree ("rm"));
    call ("remove-font-packages");
    return;
  }
  if (starts (value, "Stix")) {
    call ("init-font", object ("stix"), object ("math-stix"));
    return;
  }

  ed->init_env ("font", tree (value));
  if (N (opts) > 0 && is_string (opts[0]))
    ed->init_env ("math-font", tree (as_string (opts[0])));
  ed->init_env ("font-family", tree ("rm"));
  call ("remove-font-packages");

  string package= font_package_name (value);
  object file= call ("url-append", object ("$ATHENA_PATH/packages/customize/fonts"),
                     object (package * ".ts"));
  if (as_bool (call ("url-exists?", file))) {
    init_default_current_view ("font");
    init_default_current_view ("font-family");
    call ("add-style-package", object (package));
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

bool document_test_default_page_rendering () {
  return defaults_absent ({"page-medium"});
}

void document_init_default_page_rendering () {
  reset_defaults ({"page-medium", "page-border", "page-packet", "page-offset"});
  notify_page_change ();
}

int document_panorama_packets () {
  editor ed= get_current_editor ();
  int nr= ed->nr_pages ();
  SI ww= ed->get_window_width ();
  SI wh= ed->get_window_height ();
  SI pw= ed->get_page_width (false);
  SI ph= ed->get_page_height (false);
  int best_n= 0;
  double best_f= 0.0;
  for (int n= 1; n <= nr; ++n) {
    int rows= (nr + n - 1) / n;
    double tw= ((double) n) * ((double) pw);
    double th= ((double) rows) * ((double) ph);
    double aw= ((double) ww) - ((double) n) * 5120.0;
    double ah= ((double) wh) - ((double) rows) * 5120.0;
    double fw= aw / tw;
    double fh= ah / th;
    double factor= std::min (fw, fh);
    if (n == 1 || factor > best_f) {
      best_n= n;
      best_f= factor;
    }
  }
  if (best_n > 0) return best_n;
  if (nr > 10) return 10;
  return (int) std::ceil (std::sqrt ((double) nr));
}

string document_get_init_page_rendering () {
  editor ed= get_current_editor ();
  if (ed->get_init_string ("page-border") == "attached") return "book";
  if (ed->get_init_string ("page-packet") != "1") return "panorama";
  object slideshow= call ("tree-innermost", symbol_object ("slideshow"));
  bool inside_slideshow= !(is_bool (slideshow) && !as_bool (slideshow));
  if (ed->get_init_string ("page-medium") == "paper" && !inside_slideshow)
    return "slideshow";
  return ed->get_init_string ("page-medium");
}

bool document_test_page_rendering (string value) {
  return document_get_init_page_rendering () == value;
}

void document_apply_page_rendering_state (string value) {
  editor ed= get_current_editor ();
  if (value == "paper" || value == "papyrus")
    call ("set-preference", object ("page medium"), object (value));
  call ("save-zoom", object (document_get_init_page_rendering ()));

  if (value == "book") {
    ed->init_env ("page-medium", tree ("paper"));
    ed->init_env ("page-border", tree ("attached"));
    ed->init_env ("page-packet", tree ("2"));
    ed->init_env ("page-offset", tree ("1"));
  }
  else if (value == "panorama") {
    ed->init_env ("page-medium", tree ("paper"));
    ed->init_env ("page-packet", tree (as_string (document_panorama_packets ())));
    reset_defaults ({"page-border", "page-offset"});
  }
  else if (value == "slideshow") {
    ed->init_env ("page-medium", tree ("paper"));
    reset_defaults ({"page-packet", "page-border", "page-offset"});
  }
  else {
    ed->init_env ("page-medium", tree (value));
    reset_defaults ({"page-border", "page-packet", "page-offset"});
  }
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

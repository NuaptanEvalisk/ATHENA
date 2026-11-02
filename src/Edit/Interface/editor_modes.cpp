// Copyright (C) 2001 Joris van der Hoeven, 2026 ATHENA contributors.
// GPL-3.0-or-later.
#include "editor_modes.hpp"
#include "editor.hpp"
#include "file.hpp"

namespace {
bool content_mode (string name) {
  editor ed= get_current_editor ();
  return ed->get_env_string ("mode") == name && !ed->inside_graphics ();
}
}
bool editor_mode_source () { return get_current_editor ()->get_env_string ("mode") == "src"; }
bool editor_mode_text () { return content_mode ("text"); }
bool editor_mode_math () { return content_mode ("math"); }
bool editor_mode_program () { return content_mode ("prog"); }
bool editor_mode_table () {
  editor ed= get_current_editor ();
  return ed->inside ("table") && !ed->inside_graphics ();
}
bool editor_mode_screens () { return get_current_editor ()->inside ("screens"); }
bool editor_mode_standard () { return get_current_editor ()->defined_at_init ("std-dtd"); }
bool editor_mode_standard_text () { return editor_mode_text () && editor_mode_standard (); }
bool editor_mode_smart_reference () { return get_current_editor ()->defined_at_init ("smart-ref-package"); }
bool editor_mode_verbatim () {
  editor ed= get_current_editor ();
  return editor_mode_text () &&
    (ed->inside ("verbatim") || ed->inside ("verbatim-code") || ed->inside ("code"));
}
bool editor_mode_variants_disabled () {
  editor ed= get_current_editor ();
  path p= path_up (ed->the_path ());
  if (is_nil (p)) return false;
  tree t= ed->the_subtree (path_up (p));
  return is_func (t, HLINK) || is_func (t, REFERENCE) || is_func (t, PAGEREF) || is_func (t, LABEL);
}
bool editor_mode_emacs () {
  string profile= get_preference ("look and feel");
  if (profile == "default") profile= default_look_and_feel ();
  return profile == "emacs";
}

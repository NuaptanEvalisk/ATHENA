// Copyright (C) 2001 Joris van der Hoeven, 2026 ATHENA contributors.
// GPL-3.0-or-later.
#include "Editor/Commands/editor_modes.hpp"
#include "boot.hpp"
#include "Editor/editor.hpp"
#include "file.hpp"
#include <stdexcept>

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

editor_mode_predicate editor_mode_resolve (const std::string& name) {
  if (name == "in-text?") return editor_mode_text;
  if (name == "in-math?") return editor_mode_math;
  if (name == "in-prog?") return editor_mode_program;
  if (name == "in-source?") return editor_mode_source;
  if (name == "in-table?") return editor_mode_table;
  if (name == "in-std?") return editor_mode_standard;
  if (name == "in-std-text?") return editor_mode_standard_text;
  if (name == "in-smart-ref?") return editor_mode_smart_reference;
  if (name == "in-verbatim?") return editor_mode_verbatim;
  if (name == "in-variants-disabled?") return editor_mode_variants_disabled;
  if (name == "in-hybrid?") return [] { return get_current_editor ()->hybrid_name_context (); };
  if (name == "in-graphics?") return [] { return get_current_editor ()->inside_graphics (); };
  throw std::runtime_error ("Unknown native editor mode: " + name);
}

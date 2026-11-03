/******************************************************************************
* MODULE     : totex_compat.cpp
* DESCRIPTION: Legacy Scheme-facing LaTeX API compatibility
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "convert.hpp"

void
latex_compat_set_style (string style) {
  latex_export_set_latex_style (style);
  latex_export_recompute_dependencies ();
}

void
latex_compat_add_extra (string package) {
  if (latex_export_add_latex_extra_package (package))
    latex_export_recompute_dependencies ();
}

bool
latex_compat_has_style (string style) {
  return style == latex_export_latex_style ();
}

bool
latex_compat_has_texmacs_style (string style) {
  return style == latex_export_latex_texmacs_style ();
}

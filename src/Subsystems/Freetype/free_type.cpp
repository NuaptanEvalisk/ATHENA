/******************************************************************************
* MODULE     : free_type.cpp
* DESCRIPTION: Interface with Free Type II
* COPYRIGHT  : (C) 2003  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "free_type.hpp"
#include "font_domain.hpp"

namespace {
struct freetype_state {
  FT_Library library= nullptr;
  bool initialized= false;
  bool error= true;
  ~freetype_state () {
    if (library != nullptr) FT_Done_FreeType (library);
  }
};
}

bool
ft_initialize () {
  auto& state= font_domain_local<freetype_state> ();
  if (!state.initialized) {
    state.error= FT_Init_FreeType (&state.library) != 0;
    state.initialized= true;
  }
  return state.error;
}

FT_Library
current_ft_library () {
  (void) ft_initialize ();
  return font_domain_local<freetype_state> ().library;
}

bool
ft_present () {
  return !ft_initialize ();
}

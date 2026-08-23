
/******************************************************************************
* MODULE     : tex_init.cpp
* DESCRIPTION: initializations for using Metafont
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "boot.hpp"
#include "file.hpp"
#include "path.hpp"
#include "sys_utils.hpp"
#include "convert.hpp"
#include "tex_files.hpp"

/******************************************************************************
* Determine installed programs
******************************************************************************/

static void
init_helper_binaries () {
  if (exists_in_path ("kpsepath")) {
    debug_boot << "kpsepath works with your TeX distribution\n";
    set_setting ("KPSEPATH", "true");
  }
  else set_setting ("KPSEPATH", "false");

  if (exists_in_path ("kpsewhich")) {
    debug_boot << "kpsewhich works with your TeX distribution\n";
    set_setting ("KPSEWHICH", "true");
  }
  else set_setting ("KPSEWHICH", "false");

  if (exists_in_path ("mktextfm")) { 	 
   debug_boot << "mktextfm works with your TeX distribution\n"; 	 
   set_setting ("MAKETFM", "mktextfm"); 	 
  } 	 
  else if (exists_in_path ("MakeTeXTFM")) { 	 
    debug_boot << "MakeTeXTFM works with your TeX distribution\n"; 	 
    set_setting ("MAKETFM", "MakeTeXTFM"); 	 
  } 	 
  else if (exists_in_path ("maketfm")){ 	 
    debug_boot << "maketfm works with your TeX distribution\n"; 	 
    set_setting ("MAKETFM", "maketfm"); 	 
  } 	 
  else set_setting ("MAKETFM", "false");
  
  if (exists_in_path ("mktexpk")) { 	 
    debug_boot << "mktexpk works with your TeX distribution\n"; 	 
    set_setting ("MAKEPK", "mktexpk"); 	 
  } 	 
  else if (exists_in_path ("MakeTeXPK")) { 	 
    debug_boot << "MakeTeXPK works with your TeX distribution\n"; 	 
    set_setting ("MAKEPK", "MakeTeXPK"); 	 
  } 	 
  else if (exists_in_path ("makepk")){ 	 
    debug_boot << "makepk works with your TeX distribution\n"; 	 
    set_setting ("MAKEPK", "makepk"); 	 
  } 	 
  else set_setting ("MAKEPK", "false");

  set_setting ("DPI", "600");
}

/******************************************************************************
* Setting up and initializing TeX fonts
******************************************************************************/

void
setup_tex () {
  remove ("$ATHENA_HOME_PATH/fonts/font-index.scm");
  init_helper_binaries ();
}

void
init_tex () {
  // TFM, PK, and Type1 directory expansion is only needed by the legacy TeX
  // font backend.  The resolvers initialize their respective paths on first
  // use so ordinary TrueType/OpenType startup does not scan unused trees.
}

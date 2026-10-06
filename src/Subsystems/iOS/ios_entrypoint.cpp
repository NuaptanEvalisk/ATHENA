/******************************************************************************
* MODULE     : ios_entrypoint.cpp
* DESCRIPTION: Qt-managed UIKit entry point without desktop path discovery
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "athena_ios.hpp"
#include "sys_utils.hpp"
#include <QCoreApplication>
#include <cstdio>

extern int texmacs_entrypoint (int, char**);
extern void texmacs_init_guile_hooks ();

int main (int argc, char** argv) {
  std::string error;
  if (!athena_ios_initialize_paths (error)) {
    std::fprintf (stderr, "ATHENA startup: %s\n", error.c_str ());
    return 1;
  }
  athena_ios_install_application_bridge ();
  texmacs_init_guile_hooks ();
  return texmacs_entrypoint (argc, argv);
}

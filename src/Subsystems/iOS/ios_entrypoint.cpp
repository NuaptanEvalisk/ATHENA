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
#include <cstdlib>

extern int texmacs_entrypoint (int, char**);
extern void texmacs_init_guile_hooks ();

int main (int argc, char** argv) {
  std::string error;
  if (!athena_ios_initialize_paths (error)) {
    std::fprintf (stderr, "ATHENA startup: %s\n", error.c_str ());
    return 1;
  }
  // Temporary device diagnostics, available through the application container.
  const std::string log= std::string (std::getenv ("ATHENA_HOME_PATH")) +
    "/system/ipados-startup.log";
  if (std::freopen (log.c_str (), "w", stderr)) {
    std::setvbuf (stderr, nullptr, _IONBF, 0);
    setenv ("ATHENA_GUILE_LOAD_TRACE", "1", 1);
  }
  athena_ios_install_application_bridge ();
  texmacs_init_guile_hooks ();
  return texmacs_entrypoint (argc, argv);
}

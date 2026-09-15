/******************************************************************************
* MODULE     : headless.cpp
* DESCRIPTION: shared ATHENA headless runtime state
* COPYRIGHT  : (C) 2026  Nuaptan
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "boot.hpp"

#if defined(ATHENA_TEST_BODY) && (defined(__GNUC__) || defined(__clang__))
#define ATHENA_TEST_WEAK __attribute__((weak))
#else
#define ATHENA_TEST_WEAK
#endif

ATHENA_TEST_WEAK bool headless_mode= false;

ATHENA_TEST_WEAK bool
is_headless () {
  return headless_mode;
}

#undef ATHENA_TEST_WEAK

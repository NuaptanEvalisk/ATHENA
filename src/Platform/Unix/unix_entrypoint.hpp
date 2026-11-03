/******************************************************************************
* MODULE     : unix_entrypoint.hpp
* DESCRIPTION: Unix entry point for TeXmacs
* COPYRIGHT  : (C) 2024 Liza Belos
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef ATHENA_UNIX_ENTRYPOINT_HPP
#define ATHENA_UNIX_ENTRYPOINT_HPP

// Native application entry point, with UTF-8 arguments.
int texmacs_entrypoint(int argc, char** argv);

#endif
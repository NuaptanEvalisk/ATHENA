/******************************************************************************
* MODULE     : document_commands.hpp
* DESCRIPTION: Native generic document editing commands
* COPYRIGHT  : (C) 2001 Joris van der Hoeven, 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
*******************************************************************************/

#ifndef ATHENA_DOCUMENT_COMMANDS_HPP
#define ATHENA_DOCUMENT_COMMANDS_HPP

#include "scheme.hpp"
#include "tree.hpp"

bool document_test_default (object variables);
void document_init_default (object variables);
object document_get_init_env (string variable);
bool document_test_init (string variable, string value);
void document_set_init_env (string variable, tree value);
bool document_test_init_true (string variable);
void document_init_multi (object values);

#endif

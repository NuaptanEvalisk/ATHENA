/******************************************************************************
* MODULE     : native_dynamic_commands.hpp
* DESCRIPTION: Actor-owned operations on folding and switching document trees
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef ATHENA_NATIVE_DYNAMIC_COMMANDS_HPP
#define ATHENA_NATIVE_DYNAMIC_COMMANDS_HPP

#include "editor.hpp"

// Only the operations exposed through the native Dynamic menu belong here.
// They mutate the current actor's attached source tree through observer-aware
// native tree transactions; no detached copy or Scheme intermediary is used.
void native_dynamic_buffer_command (editor ed, string operation, string mode);

#endif // ATHENA_NATIVE_DYNAMIC_COMMANDS_HPP

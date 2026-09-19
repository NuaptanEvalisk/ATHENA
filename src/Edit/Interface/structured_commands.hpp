/******************************************************************************
* MODULE     : structured_commands.hpp
* DESCRIPTION: Native generic structured tree editing commands
* COPYRIGHT  : (C) 2001 Joris van der Hoeven, 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
*******************************************************************************/

#ifndef ATHENA_STRUCTURED_COMMANDS_HPP
#define ATHENA_STRUCTURED_COMMANDS_HPP

#include "tree.hpp"
#include "scheme.hpp"

bool generic_context (tree t);
bool generic_complex_context (tree t);
bool generic_simple_context (tree t);
bool generic_document_context (tree t);
bool generic_table_markup_context (tree t);
bool generic_structured_horizontal_context (tree t);
bool generic_structured_vertical_context (tree t);

void generic_structured_insert_horizontal (tree t, bool forwards);
void generic_structured_insert_vertical (tree t, bool downwards);
void generic_structured_remove_horizontal (tree t, bool forwards);
void generic_structured_remove_vertical (tree t, bool downwards);
void generic_structured_insert_extremal (tree t, bool forwards);
void generic_structured_insert_incremental (tree t, bool downwards);
void generic_structured_horizontal (tree t, bool forwards);
void generic_structured_vertical (tree t, bool downwards);
void generic_structured_inner_extremal (tree t, bool forwards);
void generic_structured_extremal (tree t, bool forwards);
void generic_structured_incremental (tree t, bool downwards);
void generic_structured_exit (tree t, bool forwards);
void generic_special_navigate (tree t, object direction);
void generic_special_horizontal (tree t, bool forwards);
void generic_special_vertical (tree t, bool downwards);
void generic_special_extremal (tree t, bool forwards);
void generic_special_incremental (tree t, bool downwards);

#endif

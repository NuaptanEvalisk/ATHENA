/******************************************************************************
* MODULE     : QTMESCSymbolPicker.hpp
* DESCRIPTION: Mathematica-style ESC symbol picker for ATHENA
* COPYRIGHT  : (C) 2026 Nuaptan
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMESC_SYMBOL_PICKER_HPP
#define QTMESC_SYMBOL_PICKER_HPP

#include "string.hpp"

void initialize_escape_symbol_picker_data ();
// Actor callers return empty immediately and receive insertion asynchronously.
string escape_symbol_picker_dialog ();
void escape_symbol_configurator_show ();

#endif // QTMESC_SYMBOL_PICKER_HPP

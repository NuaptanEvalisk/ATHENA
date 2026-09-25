/******************************************************************************
* MODULE     : native_math_keyboard.hpp
* DESCRIPTION: Native JSON math shortcut registry
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************/
#ifndef ATHENA_NATIVE_MATH_KEYBOARD_HPP
#define ATHENA_NATIVE_MATH_KEYBOARD_HPP

#include "command.hpp"
#include "string.hpp"

// Returns true when the native math registry owns this exact/prefix sequence.
// Output status follows server::get_keycomb: 1 = command, 2 = shorthand text.
bool native_math_keyboard_get_keycomb (
  string& combination, int& status, command& cmd, string& shorthand, string& help);

int native_math_keyboard_binding_count ();
bool native_math_keyboard_has_registered_key (string combination);
bool native_math_keyboard_context_active ();
bool native_math_disable_pre_edit (string key);
string native_math_downgrade_pre_edit (string key);

#endif

/******************************************************************************
* MODULE     : native_keyboard_loader.hpp
* DESCRIPTION: Native demand loading and idle prewarming of keyboard modules
* COPYRIGHT  : (C) 2026 The ATHENA developers
******************************************************************************/

#ifndef ATHENA_NATIVE_KEYBOARD_LOADER_HPP
#define ATHENA_NATIVE_KEYBOARD_LOADER_HPP

#include "scheme.hpp"

void native_keyboard_initialize ();
void native_keyboard_force (bool all);
void native_keyboard_start_warmup ();
object native_keyboard_warmup_step ();

#endif

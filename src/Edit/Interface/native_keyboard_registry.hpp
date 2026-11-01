/******************************************************************************
* MODULE     : native_keyboard_registry.hpp
* DESCRIPTION: Native keyboard registration and context-sensitive lookup
* COPYRIGHT  : (C) 2026 The ATHENA developers
******************************************************************************/

#ifndef ATHENA_NATIVE_KEYBOARD_REGISTRY_HPP
#define ATHENA_NATIVE_KEYBOARD_REGISTRY_HPP

#include "scheme.hpp"

void native_keyboard_bind (object conditions, string key, object action, string help);
void native_keyboard_unbind (object conditions, string key);
void native_keyboard_bind_source (string key, object source);
object native_keyboard_find (string key);
string native_keyboard_inverse (object source);
string native_keyboard_cached_inverse (object source);
object native_keyboard_system_inverse (object source);
object native_keyboard_reverse (object source);
object native_keyboard_catalog ();
void native_keyboard_list_shortcuts ();

#endif

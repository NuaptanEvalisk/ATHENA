/******************************************************************************
* MODULE     : native_keyboard_registry.hpp
* DESCRIPTION: Native keyboard registration and context-sensitive lookup
* COPYRIGHT  : (C) 2026 The ATHENA developers
******************************************************************************/

#ifndef ATHENA_NATIVE_KEYBOARD_REGISTRY_HPP
#define ATHENA_NATIVE_KEYBOARD_REGISTRY_HPP

#include "string.hpp"
#include <functional>
#include <optional>
#include <string>

struct native_keyboard_context {
  std::string identity;
  std::function<bool()> matches;
};

struct native_key_binding {
  std::function<void()> action;
  std::string text, help;
};

void native_keyboard_bind (native_keyboard_context context, string key,
                           native_key_binding action, string source);
void native_keyboard_unbind (const std::string& context, string key);
std::optional<native_key_binding> native_keyboard_find (string key);
bool native_keyboard_has_binding (string key);
string native_keyboard_inverse (string source);
string native_keyboard_cached_inverse (string source);
std::optional<string> native_keyboard_system_inverse (string source);
void native_keyboard_list_shortcuts ();

#endif

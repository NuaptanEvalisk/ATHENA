/******************************************************************************
* MODULE     : native_input_events.hpp
* DESCRIPTION: Native keyboard dispatch and input policy
* COPYRIGHT  : (C) 2026 The ATHENA developers
******************************************************************************/

#ifndef ATHENA_NATIVE_INPUT_EVENTS_HPP
#define ATHENA_NATIVE_INPUT_EVENTS_HPP

#include "array.hpp"
#include "tree.hpp"

void native_keyboard_press (string key, double time);
void native_mouse_event (string kind, int x, int y, int modifiers,
                         double time, array<double> data);
void native_delayed_mouse_event (string kind, int x, int y, int modifiers,
                                 double time, array<double> data);
void native_keyboard_insert (tree value);
void native_deferred_tree_set (tree source, tree value);

#endif

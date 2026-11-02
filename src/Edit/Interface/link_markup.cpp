// Copyright (C) 2006 Joris van der Hoeven, 2026 ATHENA contributors.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "link_markup.hpp"

object native_link_flatten_child (tree t) {
  if (is_compound (t, "script")) {
    object args= null_object ();
    // Script arguments retain their attached tree identity, unlike the code.
    for (int i= N(t)-1; i >= 1; --i) args= cons (object (t[i]), args);
    return cons (symbol_object ("script"), cons (tree_to_stree (t[0]), args));
  }
  if (is_compound (t, "attr")) {
    object attributes= null_object ();
    for (int i= (N(t)/2)*2-2; i >= 0; i-= 2)
      attributes= cons (cons (tree_to_stree (t[i]), tree_to_stree (t[i+1])),
                        attributes);
    return attributes;
  }
  return tree_to_stree (t);
}

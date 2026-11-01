// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ATHENA_REFERENCE_MARKUP_HPP
#define ATHENA_REFERENCE_MARKUP_HPP

#include "tree.hpp"
#include "object.hpp"

tree reference_typed_markup (tree name, tree wrapper, tree arguments);
tree reference_typed_markup (tree name, tree arguments);
tree reference_smart_markup (tree arguments);
object reference_preview (string id);
void reference_show_preview (tree body, tree label);
object reference_infer_label (tree t);
bool reference_inactive_at_cursor ();
void reference_resolve_at_cursor ();
bool reference_navigate (tree t, bool forwards, bool extremal);

#endif

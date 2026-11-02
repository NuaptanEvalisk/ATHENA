// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ATHENA_REFERENCE_MARKUP_HPP
#define ATHENA_REFERENCE_MARKUP_HPP

#include "tree.hpp"
#include <optional>

tree reference_typed_markup (tree name, tree wrapper, tree arguments);
tree reference_typed_markup (tree name, tree arguments);
tree reference_smart_markup (tree arguments);
std::optional<tree> reference_preview (string id);
void reference_show_preview (tree body, tree label);
std::optional<string> reference_infer_label (tree t);
bool reference_inactive_at_cursor ();
void reference_resolve_at_cursor ();
bool reference_navigate (tree t, bool forwards, bool extremal);
void reference_insert_wikilink ();
void reference_insert_transclusion ();
void reference_before_transclusion ();
void reference_after_transclusion ();
void reference_select_transclusion ();
void reference_flatten_document ();

#endif

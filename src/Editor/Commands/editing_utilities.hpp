// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once
#include "tree.hpp"

bool editing_unnumbered_symbol (tree_label tag);
bool editing_variant_context (tree t);
void editing_variant_set (tree t, tree_label tag);
void editing_alternate_toggle (tree t);
void editing_mouse_fold (tree t);
void editing_mouse_unfold (tree t);
void editing_select_enlarge ();


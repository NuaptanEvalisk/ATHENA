// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once
#include "scheme.hpp"
path insertion_path (tree content, object selectors);
void insertion_insert (tree content, object selectors);
void insertion_make (tree_label tag, object arity);
void insertion_make (tree_label tag, int arity= -1);
void insertion_make_with (string variable, object value);

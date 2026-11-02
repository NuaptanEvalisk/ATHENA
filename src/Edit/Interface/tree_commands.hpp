// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once
#include "scheme.hpp"
object tree_command_ref (object source, object selectors);
object tree_command_path (object source, object selectors);
tree tree_command_set (tree source, object arguments);
tree tree_command_insert (tree source, int index, object value);
bool tree_command_is (object source, object selectors);
bool tree_command_in (object source, object selectors);
void tree_command_go_to (object source, object selectors);
void tree_command_select (object source, object selectors);
void tree_command_focus (object source, object selectors);
object tree_command_innermost (object predicate, object options);

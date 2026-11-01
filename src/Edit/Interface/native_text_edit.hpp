// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once
#include "tree.hpp"

bool text_node_in_group (tree t, const char* group);
string text_algorithm_root (string tag);
bool text_algorithm_numbered (tree t);
bool text_algorithm_named (tree t);
bool text_algorithm_specified (tree t);
void text_algorithm_toggle_number (tree t);
void text_algorithm_toggle_name (tree t);
void text_algorithm_toggle_specification (tree t);

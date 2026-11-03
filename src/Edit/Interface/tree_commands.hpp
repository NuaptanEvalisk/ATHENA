// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once
#include "tree.hpp"
#include <optional>

std::optional<tree> tree_command_outer (tree source);
std::optional<tree> tree_command_down (tree source);
bool tree_command_cursor_inside (tree source);

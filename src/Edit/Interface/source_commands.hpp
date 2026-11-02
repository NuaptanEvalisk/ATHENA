// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once
#include "array.hpp"
#include "tree.hpp"
#include <optional>
#include <vector>
void source_make_latex ();
void source_activate_compound ();
void source_inactive_toggle (tree);
void source_remove_unary_document ();
bool source_enter (tree);
bool source_variant (tree, bool);
void source_extract_style (bool);
std::vector<string> source_search_options (string);
array<string> source_search_parameters (string);
std::optional<array<string>> source_theme_members (string);
std::optional<string> source_member_theme (string);
array<string> source_search_themes (string);
bool source_has_macro (string);
void source_edit_macro (string);
void source_edit_focus_macro ();
array<string> source_personal_macros ();
tree source_render_edit_macro (tree);
void source_edit_package (string);
void source_edit_style ();

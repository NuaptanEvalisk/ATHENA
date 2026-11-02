// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once
#include "scheme.hpp"
#include <optional>
void source_make_latex ();
void source_activate_compound ();
void source_inactive_toggle (tree);
void source_remove_unary_document ();
bool source_enter (tree);
bool source_variant (tree, bool);
void source_extract_style (bool);
object source_search_options (object);
object source_search_tag_options (tree);
array<string> source_search_parameters (string);
object source_search_tag_parameters (tree);
object source_assigned_parameters (object);
std::optional<array<string>> source_theme_members (string);
std::optional<string> source_member_theme (string);
array<string> source_search_themes (string);
bool source_has_macro (object);
void source_edit_macro (object);
void source_edit_focus_macro ();
array<string> source_personal_macros ();
tree source_get_definition (object);
tree source_render_edit_macro (tree);
void source_edit_package (string);
void source_edit_style ();

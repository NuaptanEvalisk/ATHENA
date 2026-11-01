// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once
#include "tree.hpp"

bool text_node_in_group (tree t, const char* group, bool numbered_variants= false);
bool text_titled_context (tree t);
bool text_doc_title_context (tree t);
bool text_doc_author_context (tree t);
void text_make_doc_data ();
void text_make_abstract_data ();
void text_make_doc_data_element (tree_label tag);
void text_make_author_data_element (tree_label tag);
void text_make_abstract_data_element (tree_label tag);
void text_note_toggle_custom (tree t);
void text_titled_toggle_name (tree t);
void text_frame_toggle_title (tree t);
bool text_float_context (tree t);
bool text_footnote_context (tree t);
bool text_rich_float_context (tree t);
bool text_floatable_context (tree t);
bool text_float_wide (tree t);
bool text_floatable_wide (tree t);
void text_float_toggle_wide (tree t);
void text_floatable_toggle_wide (tree t);
void text_turn_floating (tree t);
void text_turn_non_floating (tree t);
void text_toggle_anchor ();
void text_make_header (string tag);
void text_section_switch_to (int index);
void text_make_section (tree_label tag);
void text_make_unnamed_section (tree_label tag);
string text_algorithm_root (string tag);
bool text_algorithm_numbered (tree t);
bool text_algorithm_named (tree t);
bool text_algorithm_specified (tree t);
void text_algorithm_toggle_number (tree t);
void text_algorithm_toggle_name (tree t);
void text_algorithm_toggle_specification (tree t);

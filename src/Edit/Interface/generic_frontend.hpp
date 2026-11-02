// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once
#include "scheme.hpp"
void generic_insert_dialog (string kind);
void generic_insert_include_dialog ();
void generic_insert_link_image_dialog ();
void generic_insert_inline_image_dialog ();
void generic_insert_thumbnails_dialog ();
void generic_insert_figure (string kind);
void generic_make_animation (tree_label);
void generic_update_document (string);
void generic_open_global_search ();
void generic_search_open_result (url, path);
void generic_search_open_occurrence (url, path, path);
void generic_open_paragraph_format ();
void generic_open_page_format ();
void generic_install_style (url);
void generic_install_style_dialog ();
void generic_make_with_package (tree_label, string);
object generic_preferred_fonts ();
array<string> generic_preferred_font_names ();
void generic_set_preferred_fonts (array<string> fonts);
tree generic_render_cardlink (tree, tree);
void generic_display_balloon (tree, tree, tree, tree, tree);
void generic_clear_style_cache ();

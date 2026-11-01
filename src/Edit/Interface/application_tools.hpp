// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#ifndef ATHENA_APPLICATION_TOOLS_HPP
#define ATHENA_APPLICATION_TOOLS_HPP
#include "scheme.hpp"
int tools_count_characters (object document);
int tools_count_words (object document);
int tools_count_lines (object document);
tree tools_selection_or_document ();
void tools_show_character_count ();
void tools_show_word_count ();
void tools_show_line_count ();
void tools_toggle_save_aux ();
void tools_toggle_show_keyboard ();
void tools_clear_font_cache ();
void tools_scan_fonts ();
void tools_clean_cache ();
void tools_picture_gc ();
#endif

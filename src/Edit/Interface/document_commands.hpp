/******************************************************************************
* MODULE     : document_commands.hpp
* DESCRIPTION: Native generic document editing commands
* COPYRIGHT  : (C) 2001 Joris van der Hoeven, 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
*******************************************************************************/

#ifndef ATHENA_DOCUMENT_COMMANDS_HPP
#define ATHENA_DOCUMENT_COMMANDS_HPP

#include "scheme.hpp"
#include "tree.hpp"

bool document_test_default (object variables);
bool document_in_source_mode ();
void document_toggle_source_mode ();
void document_init_default (object variables);
object document_get_init_env (string variable);
bool document_test_init (string variable, string value);
void document_set_init_env (string variable, tree value);
bool document_test_init_true (string variable);
void document_init_multi (object values);
string document_font_display_name (string value);
bool document_test_init_font (string value, object options);
void document_remove_font_packages ();
void document_init_font (string value, object options);
bool document_test_default_page_medium ();
void document_init_default_page_medium ();
bool document_test_page_medium (string value);
void document_init_page_medium (string value);
bool document_test_default_page_type ();
void document_default_page_type ();
bool document_test_page_type (string value);
void document_init_page_type (string value);
void document_init_page_size (string width, string height);
bool document_test_default_page_orientation ();
void document_init_default_page_orientation ();
bool document_test_page_orientation (string value);
void document_init_page_orientation (string value);
bool document_visible_header_and_footer ();
void document_toggle_visible_header_and_footer ();
bool document_page_width_margin ();
void document_toggle_page_width_margin ();
bool document_not_page_screen_margin ();
void document_toggle_page_screen_margin ();
bool document_reduced_margins ();
void document_toggle_reduced_margins ();
bool document_indent_paragraphs ();
void document_toggle_indent_paragraphs ();
bool document_no_page_numbers ();
void document_toggle_no_page_numbers ();

#endif

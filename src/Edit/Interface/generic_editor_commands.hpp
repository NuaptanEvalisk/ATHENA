/******************************************************************************
* MODULE     : generic_editor_commands.hpp
* DESCRIPTION: Native generic editor commands
* COPYRIGHT  : (C) 2001 Joris van der Hoeven, 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
*******************************************************************************/

#ifndef ATHENA_GENERIC_EDITOR_COMMANDS_HPP
#define ATHENA_GENERIC_EDITOR_COMMANDS_HPP

#include "scheme.hpp"

void generic_go_to_line (int line, object optional_from);
void generic_go_to_column (int column, object optional_from);
object generic_select_word (string word, tree t, int column);
object generic_search_parameters (object label);
void generic_label_insert (tree t);
void generic_recenter_window ();
void generic_make_label ();
void generic_make_inline_image (object values);
void generic_make_link_image (object values);
void generic_make_thumbnails_sub (object files, int columns);
void generic_notify_activated (tree t);
void generic_notify_disactivated (tree t);
object generic_focus_label (tree t);
object generic_focus_get_label (tree t);
object generic_focus_set_label (tree t, string value);
object generic_focus_list_search_label (object children);
object generic_focus_search_label (tree t);
void generic_search_next ();
void generic_search_previous ();
void generic_focus_open_search_tool (tree t);
bool generic_mini_flow_context (tree t);
bool generic_in_main_flow ();
bool generic_balloon_context (tree t);
bool generic_image_context (tree t);
bool generic_embedded_image_context (tree t);
bool generic_linked_image_context (tree t);
object generic_embedded_suffix (tree t);
object generic_embedded_propose (tree t, int number);
void generic_save_embedded_image (tree t, url name);
void generic_link_embedded_image (tree t, url name);
void generic_link_embedded_image_copies (tree t, url name);
void generic_embedded_saver (url name);
void generic_embedded_linker (url name);
void generic_embedded_linker_copies (url name);
void generic_save_all_embedded_images ();
void generic_link_all_embedded_images ();
void generic_embed_image (tree t);
void generic_embed_images (tree t);
void generic_embed_this_image ();
void generic_embed_all_images ();
object generic_spell_live_current_selection ();
object generic_spell_live_current_word ();
object generic_spell_live_current_language ();
object generic_spell_live_current_suggestions ();
bool generic_test_balloon_halign (string value);
void generic_set_balloon_halign (string value);
bool generic_test_balloon_valign (string value);
void generic_set_balloon_valign (string value);
string generic_cardlink_native_type (tree destination);
tree generic_cardlink_native_render (tree body, string icon, string type);
string generic_cardlink_default_link_body (tree destination);
tree generic_display_link_as_card (tree t);
tree generic_display_card_as_link (tree t);
void generic_make_specific (string format);
void generic_make_include (url target);
void generic_make_experimental_build_warning ();
void generic_make_note_ref ();
void generic_make_note_inline ();
void generic_make_note_wide ();
void generic_make_note_footnote ();
void generic_make_marginal_note ();
bool generic_test_marginal_note_hpos (string position);
void generic_set_marginal_note_hpos (string position);
bool generic_test_marginal_note_valign (string alignment);
void generic_set_marginal_note_valign (string alignment);
void generic_make_insertion (string type);
void generic_insertion_positioning (string position, bool allowed);
bool generic_test_insertion_positioning (string position);
bool generic_not_test_insertion_positioning (string position);
void generic_toggle_insertion_positioning (string position);
void generic_toggle_insertion_positioning_not (string position);
bool generic_string_variable_name (tree t, int i);
bool generic_hidden_child (tree t, int i);
string generic_tree_child_name_star (tree t, int i);
string generic_tree_child_long_name_star (tree t, int i);
string generic_type_to_format (string type);
string generic_type_to_width (string type);
bool generic_inputter_active (tree t, string type);
string generic_inputter_decode (tree t, string type);
scheme_tree generic_inputter_encode (string value, string type);
bool generic_parameter_test (string name, object value, object mode);
void generic_parameter_set (string name, object value, object mode);
object generic_parameter_get (string name, object mode);
string generic_parameter_get_string (string name, object mode);
bool generic_parameter_default (string name, object mode);
void generic_parameter_reset (string name, object mode);
bool generic_parameter_enabled (string name, object mode);
object generic_focus_doc_arg_names (tree t, int start, object previous_names);
string generic_parameter_name (string name);
bool generic_parameter_show_in_menu (string name);
bool generic_parameter_value (object value);
object generic_focus_variants_of (tree t);
string generic_focus_tag_name (object label);
object generic_child_proposals (tree t, int i);
object generic_focus_parameters_list (tree t, object mode);
object generic_focus_parameters_list_memo (tree t, object mode);
void generic_focus_parameters_cache_clear ();

#endif

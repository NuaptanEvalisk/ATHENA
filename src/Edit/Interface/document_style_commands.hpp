/******************************************************************************
* MODULE     : document_style_commands.hpp
* DESCRIPTION: Native generic document style commands
* COPYRIGHT  : (C) 2001 Joris van der Hoeven, 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
*******************************************************************************/

#ifndef ATHENA_DOCUMENT_STYLE_COMMANDS_HPP
#define ATHENA_DOCUMENT_STYLE_COMMANDS_HPP

#include "scheme.hpp"

object document_style_category (string style);
bool document_style_category_overrides (object left, object right);
bool document_style_category_precedes (object left, object right);
bool document_style_includes (string style, string included);
bool document_style_overrides (string left, string right);
bool document_style_precedes (string left, string right);
object document_style_get_documentation (object style);
string document_style_get_menu_name (string style);
string document_custom_style_file_name (url name);
url document_url_resolve_package (string name);
bool document_install_custom_style (url source);

object document_get_style_list ();
void document_set_style_list (object styles);
object document_embedded_style_list (object extra_packages);
bool document_has_no_style ();
void document_set_no_style ();
bool document_has_main_style (string style);
void document_notify_new_style (string style);

bool document_has_style_package (string package);
bool document_not_has_style_package (string package);
void document_add_style_package (string package);
void document_remove_style_package (string package);
void document_remove_style_package_star (string package);
void document_toggle_style_package (string package);

#endif


/******************************************************************************
* MODULE     : boot.hpp
* DESCRIPTION: manipulation of TeX font files
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef BOOT_H
#define BOOT_H
#include "url.hpp"

extern tree athena_settings;
extern int  install_status;
extern bool use_which;
extern bool use_locate;
extern bool headless_mode;

bool   is_headless ();
string get_setting (string var, string def= "");
void   set_setting (string var, string val);
void   get_old_settings (string s);
void   init_upgrade ();
void   init_athena ();
void   init_plugins ();
void   setup_athena ();
void   release_boot_lock ();
bool   test_athena_path (url path, bool set_environment = true);

scheme_tree plugin_list ();

bool   has_user_preference (string var);
void   register_user_preference (string var, string def, bool string_def);
void   register_user_preference_callback (string var, string callback);
bool   user_preference_default_is_string (string var);
string get_user_preference (string var, string def= "");
string get_user_preference_callback (string var);
array<string> get_user_preference_names ();
array<string> get_user_preference_callback_names ();
void   set_user_preference (string var, string val);
void   reset_user_preference (string var);
void   load_user_preferences ();
void   load_user_preferences (url prefs_file);
void   dump_user_preferences (url prefs_file);
void   save_user_preferences ();

#endif // defined BOOT_H

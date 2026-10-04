
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

#include <vector>

extern tree athena_settings;
extern int  install_status;
extern bool use_which;
extern bool use_locate;
extern bool headless_mode;

bool   is_headless ();
string get_setting (string var, string def= "");
void   set_setting (string var, string val);
void   init_athena ();
void   init_athena_resource_paths ();
void   init_system_state ();
void   setup_athena ();
void   release_boot_lock ();
bool   test_athena_path (url path, bool set_environment = true);


bool   has_user_preference (string var);
void   register_user_preference (string var, string def, bool string_def);
void   register_user_preference_callback (string var, string callback);
bool   user_preference_default_is_string (string var);
bool   user_preference_is_sensitive (string var);

struct user_preference_choice_definition {
  string value;
  string label;
};

struct user_preference_ui_definition {
  string key;
  string type;
  string scope;
  string category;
  string tab;
  string section;
  string label;
  string control;
  string provider;
  string help;
  string unit;
  int category_order= 0;
  int tab_order= 0;
  int section_order= 0;
  int order= 0;
  bool restart= false;
  std::vector<user_preference_choice_definition> choices;
};

bool get_user_preference_ui_definition (
  string var, user_preference_ui_definition& definition);
std::vector<user_preference_ui_definition> get_user_preference_ui_definitions ();
string get_user_preference (string var, string def= "");
string get_user_preference_callback (string var);
array<string> get_user_preference_names ();
array<string> get_user_preference_callback_names ();
void   set_user_preference (string var, string val);
void   reset_user_preference (string var);
array<string> color_picker_recent_colors ();
array<string> color_picker_saved_colors ();
void color_picker_remember_color (string color);
void color_picker_set_saved_colors (array<string> colors);
void   load_user_preferences ();
void   load_user_preferences (url prefs_file);
void   dump_user_preferences (url prefs_file);
void   save_user_preferences ();

#endif // defined BOOT_H

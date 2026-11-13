
/******************************************************************************
* MODULE     : init_athena.cpp
* DESCRIPTION: Initialization of TeXmacs
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "App/Boot/boot.hpp"
#include "athena_platform.hpp"
#include "file.hpp"
#include "sys_utils.hpp"
#include "analyze.hpp"
#include "convert.hpp"
#include "merge_sort.hpp"
#include "Document/NodeContracts/execution_state.hpp"
#include "language.hpp"
#include "App/Frontend/desktop_data_notifications.hpp"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QByteArray>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#ifdef OS_MINGW
#include <time.h>
#include <direct.h>
#endif

tree athena_settings = tuple ();
int  install_status   = 0;
bool use_which        = false;
bool use_locate       = false;

/******************************************************************************
* Subroutines for paths
******************************************************************************/

static url
get_env_path (string which) {
  return url ("$" * which);
}

static void
set_env_path (string which, url val) {
  //cout << which << " := " << val << "\n";
  if ((!is_none (val)) && (val->t != ""))
    set_env (which, as_string (val));
}

static url
get_env_path (string which, url def) {
  url val= get_env_path (which);
  if (is_none (val) || (val->t == "")) {
    set_env_path (which, def);
    return def;
  }
  return val;
}

/******************************************************************************
* Initialize main paths
******************************************************************************/

static void
init_main_paths () {
#ifdef OS_MINGW
  if (is_none (get_env_path ("ATHENA_HOME_PATH", get_env ("APPDATA") * "/ATHENA"))) {
#else
  if (is_none (get_env_path ("ATHENA_HOME_PATH", "~/.ATHENA"))) {
#endif
    boot_error << "\n";
    boot_error << "Installation problem: please send a bug report.\n";
    boot_error << "'ATHENA_HOME_PATH' could not be set to '~/.ATHENA'.\n";
    boot_error << "You may try to set this environment variable manually\n";
    boot_error << "\n";
    FAILED ("installation problem");
    exit (1);
  }
}

/******************************************************************************
* Directory for temporary files
******************************************************************************/

static string main_tmp_dir= "$ATHENA_HOME_PATH/system/tmp";
static const string home_tmp_dir= "$ATHENA_HOME_PATH/system/tmp";

static void
make_dir (url which) {
  if (is_none(which))
    return ;
  if (!is_directory (which)) {
    make_dir (head (which));
    mkdir (which);
  }
}

static bool
make_writable_dir (url which) {
  if (is_none (which)) return false;
  make_dir (which);
  if (!is_directory (which)) return false;
  string name= concretize (which);
  return access (as_charp (name), W_OK | X_OK) == 0;
}

static bool
set_main_tmp_dir (string dir) {
  url u= url_system (dir);
  if (!make_writable_dir (u)) return false;
  main_tmp_dir= dir;
  set_env ("ATHENA_TMP_PATH", concretize (u));
  return true;
}

static void
init_main_tmp_dir () {
  string env_tmp= get_env ("ATHENA_TMP_PATH");
  if (env_tmp != "" && set_main_tmp_dir (env_tmp)) return;

#if !defined (OS_MINGW)
  if (make_writable_dir (url_system ("/dev/shm"))) {
    string shm_tmp=
      "/dev/shm/ATHENA-" * as_string ((int) getuid ()) * "/system/tmp";
    if (set_main_tmp_dir (shm_tmp)) return;
  }
#endif

  (void) set_main_tmp_dir (home_tmp_dir);
}

static url
url_temp_dir_sub () {
#ifdef OS_MINGW
  static url tmp_dir=
    url_system (main_tmp_dir) * url_system (as_string (time (NULL)));
#else
  static url tmp_dir=
    url_system (main_tmp_dir) * url_system (as_string ((int) getpid ()));
#endif
  return (tmp_dir);
}

url
url_temp_dir () {
  static url u;
  if (u == url_none()) {
    u= url_temp_dir_sub ();
    make_dir (u);
  }
  return u;
}

bool
process_running (int pid) {
#ifndef OS_MINGW
  return pid > 0 && (kill (pid, 0) == 0 || errno == EPERM);
#else
  string cmd= "ps -p " * as_string (pid);
  string ret= eval_system (cmd);
  return occurs (as_string (pid), ret);
#endif
}

static void
clean_temp_dirs () {
  bool err= false;
  array<string> a= read_directory (main_tmp_dir, err);
#ifndef OS_MINGW
  for (int i=0; i<N(a); i++)
    if (is_int (a[i]))
      if (!process_running (as_int (a[i])))
        if (a[i] != as_string ((int) getpid ()))
          system ("rm -rf", url (main_tmp_dir) * url (a[i]));
#else
  /* delete the directories after 7 days */
  time_t ts = as_int (basename (url_temp_dir_sub ())) - (3600 * 24 * 7 );
  for (int i=0; i<N(a); i++)     
    if (is_int (a[i])) {
      time_t td= as_int (a[i]);
      if (td < ts) {
        url cur = url (main_tmp_dir) * url (a[i]);
        array<string> f= read_directory (cur, err);
        for (int j=0; j<N(f); j++) remove (cur * url (f[j]));
        rmdir (as_charp (as_string (cur)));
      }
    }
#endif
}

/******************************************************************************
* Make user directories
******************************************************************************/

static void
init_user_dirs () {
  make_dir ("$ATHENA_HOME_PATH");
  make_dir ("$ATHENA_HOME_PATH/bin");
  make_dir ("$ATHENA_HOME_PATH/doc");
  make_dir ("$ATHENA_HOME_PATH/doc/about");
  make_dir ("$ATHENA_HOME_PATH/doc/about/changes");
  make_dir ("$ATHENA_HOME_PATH/fonts");
  make_dir ("$ATHENA_HOME_PATH/fonts/enc");
  make_dir ("$ATHENA_HOME_PATH/fonts/error");
  make_dir ("$ATHENA_HOME_PATH/fonts/truetype");
  make_dir ("$ATHENA_HOME_PATH/fonts/unpacked");
  make_dir ("$ATHENA_HOME_PATH/fonts/virtual");
  make_dir ("$ATHENA_HOME_PATH/langs");
  make_dir ("$ATHENA_HOME_PATH/langs/mathematical");
  make_dir ("$ATHENA_HOME_PATH/langs/mathematical/syntax");
  make_dir ("$ATHENA_HOME_PATH/langs/natural");
  make_dir ("$ATHENA_HOME_PATH/langs/natural/hyphen");
  make_dir ("$ATHENA_HOME_PATH/langs/programming");
  make_dir ("$ATHENA_HOME_PATH/misc");
  make_dir ("$ATHENA_HOME_PATH/misc/patterns");
  make_dir ("$ATHENA_HOME_PATH/misc/pixmaps");
  make_dir ("$ATHENA_HOME_PATH/misc/themes");
  make_dir ("$ATHENA_HOME_PATH/packages");
  make_dir ("$ATHENA_HOME_PATH/styles");
  make_dir ("$ATHENA_HOME_PATH/system");
  make_dir ("$ATHENA_HOME_PATH/system/bib");
  make_dir ("$ATHENA_HOME_PATH/system/cache");
  make_dir ("$ATHENA_HOME_PATH/system/certificates");
  make_dir ("$ATHENA_HOME_PATH/system/database");
  make_dir ("$ATHENA_HOME_PATH/system/database/bib");
  make_dir ("$ATHENA_HOME_PATH/system/make");
  make_dir ("$ATHENA_HOME_PATH/system/tmp");
  init_main_tmp_dir ();
  make_dir ("$ATHENA_HOME_PATH/texts");
  make_dir ("$ATHENA_HOME_PATH/users");
  change_mode ("$ATHENA_HOME_PATH/system", 7 << 6);
  change_mode ("$ATHENA_HOME_PATH/users", 7 << 6);
  clean_temp_dirs ();
}

/******************************************************************************
* Boot locks
******************************************************************************/

static void
acquire_boot_lock () {
  //cout << "Acquire lock\n";
  url lock_file= "$ATHENA_HOME_PATH/system/boot_lock";
  if (exists (lock_file)) {
    remove (url ("$ATHENA_HOME_PATH/system/setup.scm"));
    remove (url ("$ATHENA_HOME_PATH/system/cache") * url_wildcard ("*"));
    remove (url ("$ATHENA_HOME_PATH/fonts/error") * url_wildcard ("*"));    
  }
  else save_string (lock_file, "", false);
}

void
release_boot_lock () {
  //cout << "Release lock\n";
  url lock_file= "$ATHENA_HOME_PATH/system/boot_lock";
  remove (lock_file);
}


/******************************************************************************
* Set additional environment variables
******************************************************************************/

void
init_athena_pixmap_path () {
  (void) get_env_path ("ATHENA_PIXMAP_PATH",
                       url ("$ATHENA_PATH/misc/pixmaps") |
                       url ("$ATHENA_HOME_PATH/misc/pixmaps") |
                       url ("$ATHENA_PATH/misc/pixmaps/modern/32x32/settings") |
                       url ("$ATHENA_PATH/misc/pixmaps/modern/32x32/table") |
                       url ("$ATHENA_PATH/misc/pixmaps/modern/24x24/main") |
                       url ("$ATHENA_PATH/misc/pixmaps/modern/20x20/mode") |
                       url ("$ATHENA_PATH/misc/pixmaps/modern/16x16/focus"));
}

void
init_athena_resource_paths () {
  // Set the application binary and resource paths
  url bin_path= get_env_path ("PATH");
#if defined (OS_MINGW) || defined (OS_MACOS)
  bin_path= bin_path | url ("$ATHENA_PATH/bin");
#endif
  set_env_path ("PATH", bin_path);
  // Get TeXmacs style and package paths
  url style_root=
    get_env_path ("ATHENA_STYLE_ROOT",
                  "$ATHENA_HOME_PATH/styles:$ATHENA_PATH/styles");
  url package_root=
    get_env_path ("ATHENA_PACKAGE_ROOT",
                  "$ATHENA_HOME_PATH/packages:$ATHENA_PATH/packages");
  url all_root= style_root | package_root;
  url style_path=
    get_env_path ("ATHENA_STYLE_PATH",
                  search_sub_dirs (all_root));
  url text_root=
    get_env_path ("ATHENA_TEXT_ROOT",
                  "$ATHENA_HOME_PATH/texts:$ATHENA_PATH/texts");
  url text_path=
    get_env_path ("ATHENA_TEXT_PATH",
                  search_sub_dirs (text_root));

  // Get other data paths
  (void) get_env_path ("ATHENA_FILE_PATH",text_path | style_path);
  (void) set_env_path ("ATHENA_SECURE_PATH",
                       get_env_path ("ATHENA_SECURE_PATH") |
                       "$ATHENA_PATH:$ATHENA_HOME_PATH");
  (void) get_env_path ("ATHENA_PATTERN_PATH",
                       "$ATHENA_HOME_PATH/misc/patterns" |
                       url ("$ATHENA_PATH/misc/patterns") |
                       url ("$ATHENA_PATH/misc/pictures"));
  init_athena_pixmap_path ();
  (void) get_env_path ("ATHENA_DOCUMENT_LOCALE_PATH",
                       url ("$ATHENA_PATH/langs/document"));
  (void) get_env_path ("ATHENA_THEME_PATH",
                       url ("$ATHENA_PATH/misc/themes") |
                       url ("$ATHENA_HOME_PATH/misc/themes"));
#ifdef OS_WIN32
  set_env ("ATHENA_SOURCE_PATH", "");
#else
  set_env ("ATHENA_SOURCE_PATH", ATHENA_SOURCES);
#endif
}

/******************************************************************************
* Miscellaneous initializations
******************************************************************************/

static void
init_misc () {
  // Test whether 'which' works
#ifdef OS_MINGW
  use_which = false;
#else
  use_which = (var_eval_system ("which texmacs 2> /dev/null") != "");
#endif
  //string loc= var_eval_system ("locate bin/locate 2> /dev/null");
  //use_locate= (search_forwards ("bin/locate", loc) > 0);

  // Set extra environment variables for Cygwin
#ifdef OS_CYGWIN
  set_env ("CYGWIN", "check_case:strict");
  set_env ("COMSPEC", "");
  set_env ("ComSpec", "");
#endif

}

/******************************************************************************
* Persistent machine capability state
******************************************************************************/

string
get_setting (string var, string def) {
  int i, n= N (athena_settings);
  for (i=0; i<n; i++)
    if (is_tuple (athena_settings[i], var, 1)) {
      return scm_unquote (as_string (athena_settings[i][1]));
    }
  return def;
}

void
set_setting (string var, string val) {
  int i, n= N (athena_settings);
  for (i=0; i<n; i++)
    if (is_tuple (athena_settings[i], var, 1)) {
      athena_settings[i][1]= scm_quote (val);
      return;
    }
  athena_settings << tuple (var, scm_quote (val));
}

static url
system_state_file () {
  return "$ATHENA_HOME_PATH/system/sys_state.json";
}

static bool
save_system_state_json () {
  QJsonObject root;
  root.insert ("format", "athena-system-state");
  root.insert ("version", 3);
  QByteArray bytes= QJsonDocument (root).toJson (QJsonDocument::Indented);
  return !save_string (system_state_file (), string (bytes.constData ()));
}

static bool
load_system_state_json () {
  string s;
  url state_file= system_state_file ();
  if (load_string (state_file, s, false)) return false;

  QJsonParseError error;
  QJsonDocument doc= QJsonDocument::fromJson (QByteArray (as_charp (s), N(s)),
                                              &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject ()) {
    std_error << "Invalid system state JSON in " << state_file << LF;
    return false;
  }

  QJsonObject root= doc.object ();
  int version= root.value ("version").toInt ();
  if (root.value ("format").toString () != "athena-system-state" ||
      (version != 1 && version != 2 && version != 3)) {
    std_error << "Unsupported system state JSON in " << state_file << LF;
    return false;
  }

  athena_settings= tuple ();
  // Older state files recorded a TeXmacs compatibility promise, not an ATHENA
  // data version. Discard it without running historical TeXmacs upgrade hooks.
  if (version < 3 && !save_system_state_json ())
    std_warning << "Could not update system state JSON in " << state_file << LF;
  return true;
}

/******************************************************************************
* First installation
******************************************************************************/

void
setup_athena () {
  debug_boot << "Welcome to ATHENA " ATHENA_APP_VERSION "\n";
  debug_boot << HRULE;

  if (!save_system_state_json ()) {
    failed_error << HRULE;
    failed_error << "I could not save or reload the file\n\n";
    failed_error << "\t" << system_state_file () << "\n\n";
    failed_error << "Please give me full access control over this file and\n";
    failed_error << "rerun 'ATHENA'.\n";
    failed_error << HRULE;    FAILED ("unable to write settings");
  }

  debug_boot << HRULE;
  debug_boot << "ATHENA setup okay, booting now.\n";
  debug_boot << HRULE;
}

/******************************************************************************
* Initialization of TeXmacs
******************************************************************************/

void
init_athena () {
  athena_install_desktop_data_notifications ();
  //cout << "Initialize -- Main paths\n";
  init_main_paths ();
  //cout << "Initialize -- User dirs\n";
  init_user_dirs ();
  //cout << "Initialize -- Boot lock\n";
  acquire_boot_lock ();
  //cout << "Initialize -- Succession status table\n";
  init_succession_status_table ();
  //cout << "Initialize -- Succession standard DRD\n";
  athena::contracts::initialize_builtins ();
  //cout << "Initialize -- User preferences\n";
  load_user_preferences ();
  //cout << "Initialize -- Environment variables\n";
  init_athena_resource_paths ();
  //cout << "Initialize -- Miscellaneous\n";
  init_misc ();
}

/******************************************************************************
* Initialization of persistent system state
******************************************************************************/

void
init_system_state () {
  install_status= 0;
  if (!load_system_state_json ()) {
    setup_athena ();
    install_status= 1;
  }

}

bool
test_athena_path (url path, bool set_environment) {
  if (!exists (path)) return false;
  if (!exists (path * "fonts")) return false;
  if (!exists (path * "misc/command-properties.json")) return false;
  if (!exists (path * "styles")) return false;
  if (set_environment) set_env_path ("ATHENA_PATH", path);
  return true;
}


/******************************************************************************
* MODULE     : texmacs.cpp
* DESCRIPTION: main program
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#define _GNU_SOURCE
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <locale.h> // for setlocale
#include <signal.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <random>
#include <sstream>
#include <vector>
#ifndef OS_MINGW
#include <dirent.h>
#endif
#ifdef STACK_SIZE
// #include <sys/resource.h>
#endif

#include "tm_ostream.hpp"
#include "font.hpp"
#include "boot.hpp"
#include "file.hpp"
#include "server.hpp"
#include "tm_timer.hpp"
#include "data_cache.hpp"
#include "tm_window.hpp"
#include "client_server.hpp"
#include "scheme.hpp"
#include "convert.hpp"
#include "Freetype/tt_file.hpp"
#include "ATHENA/Data/vault_maintenance.hpp"
#include "MCP/mcp_rag_server.hpp"

#ifdef QTTEXMACS
#include "Qt/QTMApplication.hpp"
#include "QTMGoogleTasksPane.hpp"
#include "Qt/qt_utilities.hpp"
#include <QApplication>
#include <QDir>
#endif

#ifdef MACOSX_EXTENSIONS
#include "MacOS/mac_utilities.h"
#endif

extern bool   char_clip;

extern url    tm_init_file;
extern url    tm_init_buffer_file;
extern string my_init_cmds;
extern string original_path;

extern int geometry_w, geometry_h;
extern int geometry_x, geometry_y;

extern tree the_et;
extern bool texmacs_started;

extern void aofm_debug_dump(const std::string& file_path);
extern bool aofm_import_vault(string source_dir, string destination_dir,
                              bool ignore_nonempty, int parallelism,
                              string model_vault);

bool disable_error_recovery= false;
bool start_server_flag= false;
bool headless_mode= false;
bool no_splash_screen= false;
std::string aofm_convert_file;
string aofm_convert_vault_source;
string aofm_convert_vault_destination;
string aofm_convert_vault_model_vault;
string vault_maintenance_dir;
string rag_server_dir;
string rag_embedding_model;
string rag_embedding_device= "auto";
int    rag_server_port = 8765;
bool   rag_server_port_set = false;
bool   rag_server_reindex = false;
int    rag_index_jobs = 0;
bool   aofm_ignore_nonempty_dest = false;
int    aofm_convert_vault_parallelism = 0;
string extra_init_cmd;
bool exec_exit= true;
void server_start ();
static std::string athena_to_std_string (const string& s);

static bool
std_ends_with (const std::string& s, const std::string& suffix) {
  return s.size () >= suffix.size () &&
         s.compare (s.size () - suffix.size (), suffix.size (), suffix) == 0;
}

static std::vector<std::string>
parse_vaultfile_strings_for_boot (const std::string& text) {
  std::vector<std::string> values;
  for (size_t i=0; i<text.size (); i++) {
    if (text[i] != '"') continue;
    i++;
    std::string value;
    while (i < text.size ()) {
      char c= text[i++];
      if (c == '\\' && i < text.size ()) {
        value.push_back (text[i++]);
        continue;
      }
      if (c == '"') break;
      value.push_back (c);
    }
    values.push_back (value);
  }
  return values;
}

static std::string
scheme_quote_for_boot (const std::string& text) {
  std::string out= "\"";
  for (char c: text) {
    if (c == '\\' || c == '"') out.push_back ('\\');
    out.push_back (c);
  }
  out.push_back ('"');
  return out;
}

static bool
valid_vault_relative_path_for_boot (const std::string& rel) {
  if (rel.empty ()) return true;
  std::filesystem::path p (rel);
  if (p.is_absolute ()) return false;
  for (const std::filesystem::path& part: p)
    if (part == "..") return false;
  return true;
}

static std::string
vault_preferences_json_path_for_boot (const std::string& rel) {
  if (rel.empty ()) return "vprefs.json";
  if (std_ends_with (rel, ".json")) return rel;
  if (std_ends_with (rel, ".scm"))
    return rel.substr (0, rel.size () - 4) + ".json";
  return rel + ".json";
}

static bool
write_vault_preferences_path_for_boot (
  const std::filesystem::path& vault_file,
  const std::vector<std::string>& fields,
  const std::string& prefs_rel)
{
  if (fields.size () < 2) return false;
  std::string map_rel= fields.size () >= 2 && !fields[1].empty ()
                       ? fields[1] : "map.tmdb";
  std::string ns_rel= fields.size () >= 4 && !fields[3].empty ()
                      ? fields[3] : "ns.sqlite";
  std::string startup_page= fields.size () >= 5 ? fields[4] : "";
  std::string one_time_startup_page= fields.size () >= 6 ? fields[5] : "";
  std::string summary_dir= fields.size () >= 7 ? fields[6] : "";
  std::string rag_index= fields.size () >= 8 && !fields[7].empty ()
                         ? fields[7] : "rag.sqlite";
  std::string text= "(" + scheme_quote_for_boot (fields[0]) +
                    " " + scheme_quote_for_boot (map_rel) +
                    " " + scheme_quote_for_boot (prefs_rel) +
                    " " + scheme_quote_for_boot (ns_rel) +
                    " " + scheme_quote_for_boot (startup_page) +
                    " " + scheme_quote_for_boot (one_time_startup_page) +
                    " " + scheme_quote_for_boot (summary_dir) +
                    " " + scheme_quote_for_boot (rag_index) + ")\n";
  std::ofstream file (vault_file, std::ios::binary | std::ios::trunc);
  if (!file) return false;
  file << text;
  return true;
}

static void
load_vault_preferences_for_rag_if_enabled (
  const std::filesystem::path& vault_root)
{
  if (get_preference ("vault take preferences with vault", "off") != "on")
    return;

  std::filesystem::path vault_file= vault_root / "Vaultfile";
  std::ifstream file (vault_file, std::ios::binary);
  if (!file) {
    std_warning << "Continuous RAG: vault preferences enabled, but Vaultfile "
                << "cannot be read; using system preferences" << LF;
    return;
  }
  std::ostringstream buffer;
  buffer << file.rdbuf ();
  std::vector<std::string> fields=
    parse_vaultfile_strings_for_boot (buffer.str ());
  if (fields.size () < 2) {
    std_warning << "Continuous RAG: invalid Vaultfile; using system preferences"
                << LF;
    return;
  }

  std::string prefs_rel= fields.size () >= 3 ? fields[2] : "";
  std::string json_rel= vault_preferences_json_path_for_boot (prefs_rel);
  if (!valid_vault_relative_path_for_boot (json_rel)) {
    std_warning << "Continuous RAG: Vaultfile preferences path is not "
                << "vault-relative; using system preferences" << LF;
    return;
  }

  if (prefs_rel != json_rel &&
      !write_vault_preferences_path_for_boot (vault_file, fields, json_rel))
    std_warning << "Continuous RAG: failed to normalize Vaultfile preferences "
                << "path" << LF;

  std::filesystem::path prefs_path= vault_root / json_rel;
  std::filesystem::path legacy_path= vault_root /
    (prefs_rel.empty () ? std::string ("vprefs.scm") : prefs_rel);

  if (!std::filesystem::exists (prefs_path) &&
      std::filesystem::exists (legacy_path)) {
    load_user_preferences (url (legacy_path.string ().c_str ()));
    return;
  }
  if (!std::filesystem::exists (prefs_path)) {
    std_warning << "Continuous RAG: vault preferences enabled, but "
                << prefs_path.string ().c_str ()
                << " does not exist; using system preferences" << LF;
    return;
  }
  load_user_preferences (url (prefs_path.string ().c_str ()));
}

static bool
is_positive_integer_arg (string s) {
  if (N(s) == 0) return false;
  for (int i=0; i<N(s); i++)
    if (!is_numeric (s[i])) return false;
  return as_int (s) > 0;
}

static string
qt_platform_name (string value) {
  int end= N(value);
  for (int i=0; i<N(value); i++)
    if (value[i] == ':' || value[i] == ';' || value[i] == ',') {
      end= i;
      break;
    }
  return value (0, end);
}

static bool
unsupported_qt_platform (string value) {
  string platform= qt_platform_name (locase_all (value));
  return platform == "eglfs" ||
         platform == "linuxfb" ||
         platform == "minimal" ||
         platform == "minimalegl" ||
         platform == "vnc";
}

static bool
supported_qt_platform (string value) {
  string platform= qt_platform_name (locase_all (value));
  return platform == "offscreen" ||
         platform == "wayland-egl" ||
         platform == "wayland" ||
         platform == "wayland-xcomposite-egl" ||
         platform == "wayland-xcomposite-glx" ||
         platform == "xcb"
#if defined (Q_OS_MAC)
         || platform == "cocoa"
#endif
#if defined (Q_OS_WIN)
         || platform == "windows"
#endif
         ;
}

static void
print_available_qt_platforms () {
  cerr << "ATHENA] Available platform plugins are: ";
#if defined (Q_OS_MAC)
  cerr << "cocoa, ";
#endif
  cerr << "offscreen, wayland-egl, wayland, wayland-xcomposite-egl, "
       << "wayland-xcomposite-glx, xcb";
#if defined (Q_OS_WIN)
  cerr << ", windows";
#endif
  cerr << ".\n";
}

static void
reject_qt_platform (string source, string value) {
  if (value == "help" || value == "--help" || value == "-help") {
    print_available_qt_platforms ();
    exit (0);
  }
  if (unsupported_qt_platform (value) || !supported_qt_platform (value)) {
    cerr << "ATHENA] unsupported Qt platform plugin in " << source << ": "
         << value << "\n";
    print_available_qt_platforms ();
    exit (1);
  }
}

static void
reject_unsupported_qt_platforms (int argc, char** argv) {
  string env_platform= get_env ("QT_QPA_PLATFORM");
  if (!is_empty (env_platform))
    reject_qt_platform ("QT_QPA_PLATFORM", env_platform);

  for (int i=1; i<argc; i++) {
    string arg= argv[i];
    if (starts (arg, "--platform=") || starts (arg, "-platform=")) {
      int start= starts (arg, "--platform=")? 11: 10;
      string value= arg (start, N(arg));
      reject_qt_platform ("--platform", value);
    }
    else if ((arg == "--platform" || arg == "-platform") && i+1 < argc) {
      string value= argv[i+1];
      reject_qt_platform ("--platform", value);
      i++;
    }
  }
}

static int
as_positive_integer_arg (string s) {
  return is_positive_integer_arg (s) ? as_int (s) : 0;
}

static std::string
random_hex_token (int bytes) {
  static const char* hex= "0123456789abcdef";
  std::random_device rd;
  std::mt19937 gen (rd ());
  std::uniform_int_distribution<int> dist (0, 255);
  std::string out;
  out.reserve (size_t (bytes) * 2);
  for (int i=0; i<bytes; i++) {
    int value= dist (gen);
    out.push_back (hex[(value >> 4) & 15]);
    out.push_back (hex[value & 15]);
  }
  return out;
}

#ifdef QTTEXMACS
// Qt application infrastructure
static QTMApplication* qtmapp= NULL;
static QTMCoreApplication* qtmcoreapp= NULL;

static void
startup_progress (int progress, string message) {
  if (!headless_mode && qtmapp != NULL && !no_splash_screen)
    qtmapp->set_splash_progress (progress, message);
}
#else
static void
startup_progress (int progress, string message) {
  (void) progress; (void) message;
}
#endif

#include <mimalloc-override.h>
#include <mimalloc-new-delete.h>

bool
is_headless () {
  return headless_mode;
}

/******************************************************************************
* For testing
******************************************************************************/

//#define ENABLE_TESTS
#ifdef ENABLE_TESTS
void
test_routines () {
  extern void test_math ();
  test_math ();
}
#endif

/******************************************************************************
* Clean exit on segmentation faults
******************************************************************************/

void 
clean_exit_on_segfault (int sig_num) {
  (void) sig_num;
  athena_enable_emergency_logging ();
  FAILED ("segmentation fault");
}

void
clean_exit_on_sigterm (int sig_num) {
  (void) sig_num;
#ifdef ADVANCED_DEVELOPER_MODE
  exit (0);
#else
  _exit (0);
#endif
}

/******************************************************************************
* Texmacs paths
******************************************************************************/

static void
ATHENA_collect_font_menu_probes (url u, array<string>& names) {
  string s;
  if (load_string (u, s, false)) return;

  string needle= "(font-exists-in-tt?";
  int pos= 0;
  while (pos < N(s)) {
    int start= search_forwards (needle, pos, s);
    if (start < 0) break;

    int quote= start + N(needle);
    while (quote < N(s) && s[quote] != '"') quote++;
    if (quote >= N(s)) break;

    string name;
    int end= quote + 1;
    while (end < N(s)) {
      if (s[end] == '"' && (end == quote + 1 || s[end - 1] != '\\')) break;
      name << s[end];
      end++;
    }
    if (end < N(s) && N(name) != 0) names << name;
    pos= end + 1;
  }
}

static bool
ATHENA_contains_string (array<string> names, string name) {
  for (int i=0; i<N(names); i++)
    if (names[i] == name) return true;
  return false;
}

static int
ATHENA_warm_font_menu_probe_cache () {
  array<string> raw_names;
  ATHENA_collect_font_menu_probes (
    "$ATHENA_PATH/progs/generic/document-menu.scm", raw_names);
  ATHENA_collect_font_menu_probes (
    "$ATHENA_PATH/progs/fonts/font-old-menu.scm", raw_names);

  array<string> names;
  for (int i=0; i<N(raw_names); i++)
    if (!ATHENA_contains_string (names, raw_names[i]))
      names << raw_names[i];

  for (int i=0; i<N(names); i++)
    (void) tt_font_exists (names[i]);

  if (N(names) != 0)
    cout << "ATHENA] font cache: warmed font menu existence probes"
         << ", names=" << N(names) << LF;
  return N(names);
}

void ATHENA_init_font() {
  tt_font_cache_warmup ();
  startup_progress (67, "Preparing font menus");
  ATHENA_warm_font_menu_probe_cache ();
  font_database_load ();
#if defined(QTTEXMACS) && defined(qt_no_fontconfig)
  string default_font_dir = get_env ("ATHENA_PATH") * "/fonts/truetype/stix";
  string current_qt_qpa_fontdir = get_env ("QT_QPA_FONTDIR");
  if (is_empty(current_qt_qpa_fontdir))
    set_env("QT_QPA_FONTDIR", default_font_dir);
#endif
}

void
ATHENA_init_paths (int& argc, char** argv) {
  (void) argc; (void) argv;
  url exedir = texmacs_get_application_directory();

  string current_athena_path = get_env ("ATHENA_PATH");

#ifdef Q_OS_MAC 
  // the following line can inibith external plugin loading
  // QCoreApplication::setLibraryPaths(QStringList());
  // ideally we would like to control the external plugins
  // and add the most useful (gif, jpeg, svg converters)
  // to the bundle package. I still do not have a reliable solution
  // so just allow everything that is reachable.
        
  // plugins need to be installed in TeXmacs.app/Contents/Plugins        
  string plugins_path = concretize (exedir * "../Plugins");
  QCoreApplication::addLibraryPath(QString::fromUtf8(&plugins_path[0], N(plugins_path)));
  // cout << from_qstring ( QCoreApplication::libraryPaths () .join("\n") ) << LF;
  {
    // ensure that private versions of the Qt frameworks have priority on
    // other instances.
    // in the event that we load qt plugins which could possibly link to
    // other instances of the Qt libraries
    string buf;
    buf = as_string(exedir * "../Frameworks");
    if (get_env("DYLD_FRAMEWORK_PATH") != "") buf = buf * ":" * get_env("DYLD_FRAMEWORK_PATH");    
    set_env ("DYLD_FRAMEWORK_PATH", buf);    
    buf = as_string(exedir * "../Resources/lib");
    if (get_env("DYLD_LIBRARY_PATH") != "") buf = buf * ":" * get_env("DYLD_LIBRARY_PATH");    
    if (get_env("TMLD_LIBRARY_PATH") != "") buf = buf * ":" * get_env("TMLD_LIBRARY_PATH");    
    set_env ("DYLD_LIBRARY_PATH", buf);    
  }
#endif

#ifdef Q_OS_MAC
  // Mac bundle environment initialization
  // We set some environment variables when the executable
  // is in a .app bundle on MacOSX
  if (is_empty (current_athena_path))
    set_env ("ATHENA_PATH", as_string(exedir * "../Resources/share/ATHENA"));
  //cout << get_env("PATH") * ":" * as_string(url("$PWD") * argv[0]
  // * "../../Resources/share/ATHENA/bin") << LF;
  if (exists("/bin/bash")) {
    string shell_env = var_eval_system ("PATH='' /bin/bash -l -c 'echo $PATH'");
    set_env ("PATH", get_env("PATH") * ":" * shell_env * ":" *
             as_string (exedir * "../Resources/share/ATHENA/bin"));
  } else {
    set_env ("PATH", get_env("PATH") * ":" *
             as_string (exedir * "../Resources/share/ATHENA/bin"));
  }
  // system("set");
#endif

#ifdef OS_MINGW
  // Win bundle environment initialization
  // ATHENA_PATH is set by assuming that the executable is in ATHENA/bin/
  // HOME is set to USERPROFILE
  // PWD is set to HOME
  // if PWD is lacking, then the path resolution machinery may not work
  
  if (is_empty (current_athena_path))
    set_env ("ATHENA_PATH", as_string (exedir * ".."));
  // if (get_env ("HOME") == "") //now set in immediate_options otherwise --setup option fails
  //   set_env ("HOME", get_env("USERPROFILE"));
  // HACK
  // In WINE the variable PWD is already in the outer Unix environment 
  // so we need to override it to have a correct behaviour
  if ((get_env ("PWD") == "") || (get_env ("PWD")[0] == '/'))  {
    set_env ("PWD", as_string (exedir));
    // set_env ("PWD", get_env("HOME"));
  }
  // system("set");
#endif

#ifdef OS_HAIKU
  // Initialization inside the Haiku package management environment
  // ATHENA_PATH is set relative to the executable which is in $prefix/app
  // to $prefix/data/ATHENA

  if (is_empty (current_athena_path))
    set_env ("ATHENA_PATH", as_string (exedir * "../data/ATHENA"));

  set_env ("PATH", get_env("PATH") * ":" *
           as_string (exedir * "/system/lib/ATHENA/bin"));
#endif

  // check on the latest $ATHENA_PATH
  current_athena_path = get_env ("ATHENA_PATH");
  if (is_empty (current_athena_path) ||
      !exists (url_system (current_athena_path))) {
    cout << "The required ATHENA_PATH("
         << current_athena_path
         << ") does not exists" << LF;
    exit(1);
  }
}

/******************************************************************************
* Parse command line options and set other global variables via preferences
******************************************************************************/

string the_default_font;
string where= "";

void 
set_global_options  (int argc, char** argv)  {

  // parse command line options
  bool flag= true;

  for (int i=1; i<argc; i++) {
    if (argv[i][0] == '\0') argc= i;
    else if ((argv[i][0] == '-') || (argv[i][0] == '+'))
    {
      if (argv[i][1] == '\0') continue;
      // process -x, +x or --xxx option (with optional arguments)
      string s= argv[i];
      if ((N(s)>=2) && (s(0,2)=="--")) s= s (1, N(s));
      if ((s == "-s") || (s == "-silent")) flag= false;
      else if ((s == "-V") || (s == "-verbose")) 
        debug (DEBUG_FLAG_VERBOSE, true);
      else if ((s == "-d") || (s == "-debug")) debug (DEBUG_FLAG_STD, true);
      else if (s == "-debug-events") debug (DEBUG_FLAG_EVENTS, true);
      else if (s == "-debug-io") debug (DEBUG_FLAG_IO, true);
      else if (s == "-debug-sockets") debug (DEBUG_FLAG_SOCKETS, true);
      else if (s == "-debug-gnutls") debug (DEBUG_FLAG_GNUTLS, true);
      else if (s == "-debug-bench") debug (DEBUG_FLAG_BENCH, true);
      else if (s == "-debug-history") debug (DEBUG_FLAG_HISTORY, true);
      else if (s == "-debug-qt") debug (DEBUG_FLAG_QT, true);
      else if (s == "-debug-qt-widgets") debug (DEBUG_FLAG_QT_WIDGETS, true);
      else if (s == "-debug-keyboard") debug (DEBUG_FLAG_KEYBOARD, true);
      else if (s == "-debug-packrat") debug (DEBUG_FLAG_PACKRAT, true);
      else if (s == "-debug-flatten") debug (DEBUG_FLAG_FLATTEN, true);
      else if (s == "-debug-parser") debug (DEBUG_FLAG_PARSER, true);
      else if (s == "-debug-correct") debug (DEBUG_FLAG_CORRECT, true);
      else if (s == "-debug-convert") debug (DEBUG_FLAG_CONVERT, true);
      else if (s == "-debug-remote") debug (DEBUG_FLAG_REMOTE, true);
      else if (s == "-debug-live") debug (DEBUG_FLAG_LIVE, true);
      else if (s == "-debug-all") {
        debug (DEBUG_FLAG_EVENTS, true);
        debug (DEBUG_FLAG_STD, true);
        debug (DEBUG_FLAG_IO, true);
        debug (DEBUG_FLAG_HISTORY, true);
        debug (DEBUG_FLAG_BENCH, true);
        debug (DEBUG_FLAG_QT, true);
        debug (DEBUG_FLAG_QT_WIDGETS, true);
      }
      else if (s == "-disable-error-recovery") disable_error_recovery= true;
      else if ((s == "-fn") || (s == "-font")) {
        i++;
        if (i<argc) the_default_font= argv[i];
      }
      else if ((s == "-g") || (s == "-geometry")) {
        i++;
        if (i<argc) {
          string g= argv[i];
          int j=0, j1, j2, j3;
          for (j=0; j<N(g); j++)
            if (g[j] == 'x') break;
          j1=j; if (j<N(g)) j++;
          for (; j<N(g); j++)
            if ((g[j] == '+') || (g[j] == '-')) break;
          j2=j; if (j<N(g)) j++;
          for (; j<N(g); j++)
            if ((g[j] == '+') || (g[j] == '-')) break;
          j3=j;
          if (j1<N(g)) {
            geometry_w= max (as_int (g (0, j1)), 320);
            geometry_h= max (as_int (g (j1+1, j2)), 200);
          }
          if (j3<N(g)) {
            if (g[j2] == '-') geometry_x= as_int (g (j2, j3)) - 1;
            else geometry_x= as_int (g (j2+1, j3));
            if (g[j3] == '-') geometry_y= as_int (g (j3, N(g))) - 1;
            else geometry_y= as_int (g (j3+1, N(g)));
          }
        }
      }
      else if ((s == "-b") || (s == "-initialize-buffer")) {
        i++;
        if (i<argc) tm_init_buffer_file= url_system (argv[i]);
      }
      else if (s == "-aofm-convert-file") {
        i++;
      }
      else if (s == "-aofm-convert-vault") {
        i += 2;
        if (i+1 < argc && is_positive_integer_arg (string (argv[i+1]))) i++;
      }
      else if (s == "-model-vault") {
        i++;
      }
      else if (s == "-vault-maintenance") {
        i++;
      }
      else if (s == "-rag-server") {
        i++;
      }
      else if (s == "-rag-port") {
        i++;
      }
      else if (s == "-rag-embedding-model") {
        i++;
      }
      else if (s == "-rag-embedding-device") {
        i++;
      }
      else if (s == "-rag-index-jobs") {
        i++;
      }
      else if (s == "-rag-reindex") {
        // Handled in texmacs_entrypoint
      }
      else if (s == "-ignore-nonempty-dest") {
        // Handled in texmacs_entrypoint
      }
      else if (s == "-insert-build-warning") {
        // Handled in texmacs_entrypoint
      }
      else if (s == "-no-splash-screen") {
        no_splash_screen= true;
      }
      else if ((s == "-i") || (s == "-initialize")) {
        i++;
        if (i<argc) tm_init_file= url_system (argv[i]);
      }
      else if ((s == "-v") || (s == "-version")) {
        cout << "\n";
        cout << "ATHENA (Advanced Typesetting and Hypertext Environment for Notes and Archives)\n";
        cout << "Version " << ATHENA_APP_VERSION << "\n";
        cout << ATHENA_COPYRIGHT << "\n";
        cout << "\n";
        exit (0);
      }
      else if ((s == "-p") || (s == "-path")) {
        cout << get_env ("ATHENA_PATH") << "\n";
        exit (0);
      }
      else if ((s == "-hp") || (s == "-homepath")) {
        cout << get_env ("ATHENA_HOME_PATH") << "\n";
        exit (0);
      }
      else if ((s == "-bp") || (s == "-binpath")) {
        cout << get_env ("ATHENA_BIN_PATH") << "\n";
        exit (0);
      }
      else if ((s == "-q") || (s == "-quit"))
        my_init_cmds= my_init_cmds * " (quit-TeXmacs)";
      else if ((s == "-r") || (s == "-reverse"))
        set_reverse_colors (true);
#if QT_VERSION < 0x060000
      else if (s == "-no-retina") {
        retina_manual= true;
        retina_factor= 1;
        retina_zoom  = 1;
        retina_icons = 1;
        retina_scale = 1.0;
      }
      else if ((s == "-R") || (s == "-retina")) {
        retina_manual= true;
#  ifdef MACOSX_EXTENSIONS
        retina_factor= 2;
        retina_zoom  = 1;
        retina_scale = 1.4;
#  else
        retina_factor= 1;
        retina_zoom  = 2;
        retina_scale = (tm_style_sheet == ""? 1.0: 1.6666);
#  endif
        retina_icons = 2;
      }
      else if (s == "-no-retina-icons") {
        retina_iman  = true;
        retina_icons = 1;
      }
      else if (s == "-retina-icons") {
        retina_iman  = true;
        retina_icons = 2;
      }
#endif
      else if ((s == "-c") || (s == "-convert") || (s == "-C")) {
        i+=2;
        if (i<argc) {
          url in  ("$PWD", argv[i-1]);
          url out ("$PWD", argv[ i ]);
          my_init_cmds= my_init_cmds * " " *
            "(load-buffer " * scm_quote (as_string (in)) * " :strict) " *
            "(export-buffer " * scm_quote (as_string (out)) * ")";
        }
      }
      else if ((s == "-x") || (s == "-execute")) {
        i++;
        if (i<argc) my_init_cmds= (my_init_cmds * " ") * argv[i];
      }
      else if ((s == "-X")) {
        exec_exit= false;
      }
      else if (s == "-server") set_server ();
      else if (s == "-port") {
        i++;
        if (i<argc) {
          string port_str = argv[i];
          set_server_port (as_int (port_str));
        }
      }
      else if (s == "-reset-server-preferences") {
        set_reset_preferences (true);
      }
      else if (s == "-reset-admin-password") {
        set_reset_admin_password (true);
      }
      else if (s == "-W" || s == "-build-website" ||
	       s == "-U" || s == "-update-website") {
        i+=2;
        if (i<argc) {
	  string cmd= "tmweb-convert-dir";
	  if (s == "-U" || s == "-update-website") cmd = "tmweb-update-dir";
          url in  ("$PWD", argv[i-1]);
          url out ("$PWD", argv[ i ]);
          my_init_cmds= my_init_cmds * " " *
            "(" * cmd * " " * scm_quote (as_string (in)) *
            " " * scm_quote (as_string (out)) * ")";
        }
      }
      else if (s == "-log-file") i++;
      else if ((s == "-Oc") || (s == "-no-char-clipping")) char_clip= false;
      else if ((s == "+Oc") || (s == "-char-clipping")) char_clip= true;
      else if ((s == "-S") || (s == "-setup") ||
               (s == "-delete-cache") || (s == "-delete-font-cache") ||
               (s == "-delete-style-cache") || (s == "-delete-file-cache") ||
               (s == "-delete-doc-cache") || (s == "-delete-plugin-cache") ||
               (s == "-delete-server-data") || (s == "-delete-databases") ||
	       (s == "-headless") || (s == "-H"));
      else if (s == "-build-manual") {
        if ((++i)<argc)
          extra_init_cmd << "(build-manual "
                         << scm_quote (argv[i]) << " delayed-quit)";
      }
      else if (s == "-reference-suite") {
        if ((++i)<argc)
          extra_init_cmd << "(build-ref-suite "
                         << scm_quote (argv[i]) << " delayed-quit)";
      }
      else if (s == "-test-suite") {
        if ((++i)<argc)
          extra_init_cmd << "(run-test-suite "
                         << scm_quote (argv[i]) << "delayed-quit)";
      }
      else if (starts (s, "-psn"));
      else {
        cout << "\n";
        cout << "Options for ATHENA:\n\n";
        cout << "  -b [file]  Specify scheme buffers initialization file\n";
        cout << "  -C [i] [o] Convert file 'i' into file 'o'\n";
        cout << "  -d         For debugging purposes\n";
        cout << "  -fn [font] Set the default TeX font\n";
        cout << "  -g [geom]  Set geometry of window in pixels\n";
        cout << "  -h         Display this help message\n";
        cout << "  -H         Run ATHENA in headless mode\n";
        cout << "  -i [file]  Specify scheme initialization file\n";
        cout << "  -p         Get the ATHENA path\n";
        cout << "  -q         Shortcut for -x \"(quit-TeXmacs)\"\n";
        cout << "  -r         Reverse video mode\n";
        cout << "  -s         Suppress information messages\n";
        cout << "  -S         Rerun ATHENA setup program before starting\n";
        cout << "  -v         Display current ATHENA version\n";
        cout << "  -V         Show some informative messages\n";
        cout << "  --no-splash-screen       Start without showing the splash screen\n";
        cout << "  --vault-maintenance [dir]  Maintain an ATHENA vault headlessly\n";
        cout << "  --aofm-convert-file [file]  Convert one AOFM Markdown file headlessly\n";
        cout << "  --aofm-convert-vault [src] [dest] [jobs]  Convert an AOFM vault headlessly\n";
        cout << "  --rag-server [dir]          Start a Continuous RAG MCP server\n";
        cout << "  --rag-embedding-device [auto|cpu]  Select RAG embedding device mode\n";
        cout << "  --rag-index-jobs [n]        Parallelize initial RAG indexing with n processes\n";
        cout << "  --insert-build-warning     Insert ATHENA experimental build warnings during AOFM conversion\n";
        cout << "  --model-vault [dir]        Reuse a model vault for AOFM namespace/style conversion\n";
        cout << "  -W [i] [o] Recursively convert directory into website\n";
        cout << "  -x [cmd]   Execute scheme command\n";
        cout << "  -Oc        TeX characters bitmap clipping off\n";
        cout << "  +Oc        TeX characters bitmap clipping on (default)\n";
        cout << "\nPlease report bugs to <nuaptan@outlook.com>\n";
        cout << "\n";
        exit (0);
      }
    } else {
      string s=argv[i];
      if (DEBUG_STD) debug_boot << "Loading " << s << "...\n";
      url u= url_system (s);
      if (!is_rooted (u)) u= resolve (url_pwd (), "") * u;
      string b= scm_quote (as_string (u));
      string cmd= "(load-buffer " * b * " " * where * ")";
      where= " :new-window";
      extra_init_cmd << cmd;
    }
  } // for (int i...)
  if (flag) debug (DEBUG_FLAG_AUTO, true);
  // End parse command line options

  // in headless mode quit after processing of the command line
  if (headless_mode && exec_exit && !is_server ()) my_init_cmds= my_init_cmds * " (quit-TeXmacs)";

  // Further options via environment variables
#if QT_VERSION < 0x060000
  if (get_env ("ATHENA_RETINA") == "off") {
    retina_manual= true;
    retina_factor= 1;
    retina_icons = 1;
    retina_scale = 1.0;
  }
  if (get_env ("ATHENA_RETINA") == "on") {
    retina_manual= true;
#ifdef MACOSX_EXTENSIONS
    retina_factor= 2;
    retina_zoom  = 1;
    retina_scale = 1.4;
#else
    retina_factor= 1;
    retina_zoom  = 2;
    retina_scale = (tm_style_sheet == ""? 1.0: 1.6666);
#endif
    retina_icons = 2;
  }
  if (get_env ("ATHENA_RETINA_ICONS") == "off") {
    retina_iman  = true;
    retina_icons = 1;
  }
  if (get_env ("ATHENA_RETINA_ICONS") == "on") {
    retina_iman  = true;
    retina_icons = 2;
  }
#endif
  // End options via environment variables

  // Further user preferences
  string native= "off";
  string unify = "off";
  string mini  = (os_macos ()? string ("off"): string ("on"));
  if (tm_style_sheet != "") mini= "off";
#if (defined(OS_MACOS) && QT_VERSION < 0x060000) || defined(qt_no_fontconfig)
  use_native_menubar = get_preference ("use native menubar", native) == "force";
#else
  use_native_menubar = get_preference ("use native menubar", native) == "on" || get_preference ("use native menubar", native) == "force";
#endif
  use_unified_toolbar= get_preference ("use unified toolbar", unify) == "on";
  use_mini_bars      = get_preference ("use minibars",         mini) == "on";
  if (!use_native_menubar) use_unified_toolbar= false;
  // End user preferences
}
 
/******************************************************************************
* Real main program for encaptulation of guile
******************************************************************************/

void
TeXmacs_main (int argc, char** argv) {

  startup_progress (82, "Configuring session");
  set_global_options (argc, argv);

  if (DEBUG_STD) debug_boot << "Installing internal plug-ins...\n";
  startup_progress (84, "Loading plug-ins");
  bench_start ("initialize plugins");
  init_plugins ();
  bench_cumul ("initialize plugins");
  if (DEBUG_STD) debug_boot << "Opening display...\n";
  
  startup_progress (86, "Opening display");
  gui_open (argc, argv);
  startup_progress (88, "Display ready");
  set_default_font (the_default_font);
  
  { // opening scope for server sv
    if (DEBUG_STD) debug_boot << "Starting server...\n";
    startup_progress (90, "Starting server");
    server sv;
    startup_progress (92, "Server ready");
  
    // append commands to open standard welcome messages if needed
    if (install_status == 1) {
      if (DEBUG_STD) debug_boot << "Loading welcome message...\n";
      string cmd= "(load-help-article \"about/welcome/new-welcome\")";
      // FIXME: force to load welcome message into new window
      extra_init_cmd << cmd;
    }
    else if (install_status == 2) {
      if (DEBUG_STD) debug_boot << "Loading upgrade message...\n";
      url u= "tmfs://help/plain/tm/doc/about/changes/changes-recent.en.tm";
      string b= scm_quote (as_string (u));
      string cmd= "(load-buffer " * b * " " * where * ")";
      where= " :new-window";
      extra_init_cmd << cmd;
    }

    if (rag_server_dir != "") {
      athena::mcp::RagServerOptions options;
      options.vault_root= std::filesystem::path (
        athena_to_std_string (rag_server_dir));
      load_vault_preferences_for_rag_if_enabled (options.vault_root);
      if (!rag_server_port_set) {
        string port_pref= get_user_preference ("rag mcp port", "8765");
        if (is_positive_integer_arg (port_pref))
          rag_server_port= as_positive_integer_arg (port_pref);
      }
      options.port= rag_server_port;
      if (rag_embedding_model != "")
        options.embedding_model= std::filesystem::path (
          athena_to_std_string (rag_embedding_model));
      else {
        string pref_model= get_user_preference ("rag embedding model", "");
        if (pref_model != "")
          options.embedding_model= std::filesystem::path (
            athena_to_std_string (pref_model));
      }
      string pref_device= get_user_preference ("rag embedding device", "auto");
      if (rag_embedding_device == "auto" && pref_device != "")
        rag_embedding_device= pref_device;
      if (rag_embedding_device != "cpu") rag_embedding_device= "auto";
      options.embedding_device= athena_to_std_string (rag_embedding_device);
      options.index_jobs= rag_index_jobs;
      options.force_reindex= rag_server_reindex;
      string token_pref= get_user_preference ("rag mcp bearer token", "");
      if (token_pref == "") {
        std::string token= random_hex_token (32);
        set_user_preference ("rag mcp bearer token", string (token.c_str ()));
        save_user_preferences ();
        options.bearer_token= token;
      }
      else options.bearer_token= athena_to_std_string (token_pref);

      bool ok= athena::mcp::start_rag_server (options);
      if (!ok) exit (1);
      texmacs_started= true;
      if (!disable_error_recovery) signal (SIGSEGV, clean_exit_on_segfault);
      signal (SIGTERM, clean_exit_on_sigterm);
      release_boot_lock ();
      io_info << "rag mcp: bearer token "
              << options.bearer_token.c_str () << "\n";
#ifdef QTTEXMACS
      QApplication::exec ();
#endif
      exit (0);
    }
  
    if (number_buffers () == 0) {
      if (DEBUG_STD) debug_boot << "Creating 'no name' buffer...\n";
      startup_progress (94, "Opening first document");
      open_window ();
      startup_progress (96, "First document ready");
      if (DEBUG_STD) debug_boot << "Queueing vault startup initialization...\n";
      extra_init_cmd << "(vault-startup-open-initial-buffer)";
    }
    extra_init_cmd << "(delayed (:idle 300) (ads-restore-visible-panes))";

    if (!aofm_convert_file.empty ()) {
      eval ("(lazy-initialize-force)");
      aofm_enable_converter_mode (true);
      aofm_cache_preferences ();
      aofm_debug_dump (aofm_convert_file);
      exit (0);
    }
    if (aofm_convert_vault_source != "" &&
        aofm_convert_vault_destination != "") {
      eval ("(lazy-initialize-force)");
      aofm_enable_converter_mode (true);
      aofm_cache_preferences ();
      bool ok= aofm_import_vault (aofm_convert_vault_source,
                                  aofm_convert_vault_destination,
                                  aofm_ignore_nonempty_dest,
                                  aofm_convert_vault_parallelism,
                                  aofm_convert_vault_model_vault);
      exit (ok ? 0 : 1);
    }
    if (vault_maintenance_dir != "") {
      eval ("(lazy-initialize-force)");
      bool ok= vault_maintenance_run (vault_maintenance_dir);
      exit (ok ? 0 : 1);
    }

    bench_print ();
    bench_reset ("initialize texmacs");
    bench_reset ("initialize plugins");
    bench_reset ("initialize scheme");
  
    if (DEBUG_STD) debug_boot << "Starting event loop...\n";
    texmacs_started= true;
    if (!disable_error_recovery) signal (SIGSEGV, clean_exit_on_segfault);

    // allow docker stop to work
    signal (SIGTERM, clean_exit_on_sigterm);
    if (is_server () && server_can_start ()) {
      server_start ();
    }
    release_boot_lock ();
    
    // inject scheme commands 
    if (N(extra_init_cmd) > 0) exec_delayed (scheme_cmd (extra_init_cmd));
    if (N(extra_init_cmd) > 0)
      startup_progress (97, "Scheduling startup tasks");
    startup_progress (98, "Preparing editor");
#ifdef QTTEXMACS
    google_tasks_schedule_background_refresh ();
#endif
    gui_start_loop ();
  
    if (DEBUG_STD) debug_boot << "Stopping server...\n";
  } // ending scope for server sv
  
  if (DEBUG_STD) debug_boot << "Closing display...\n";
  gui_close ();
      
  if (DEBUG_STD) debug_boot << "Good bye...\n";
}  
  
/******************************************************************************
* Main program
******************************************************************************/

#ifdef OS_MACOS
#include <sys/resource.h>
#endif

void
boot_hacks () {
#ifdef OS_MACOS
// NOTE: under MACOS, there is a limited number of open file descriptors,
// by default 256.  Any open file descriptor can actually count several times
// whenever the files is stored in various chunks on disk.  Hence, the limit
// is easily exceeded, although this situation cannot easily be debugged.
// Our current hack is to allow for at least 4096 open file descriptors.
  rlimit lims;
  getrlimit (RLIMIT_NOFILE, &lims);
  lims.rlim_cur= max ((int) lims.rlim_cur, (int) 4096);
  setrlimit (RLIMIT_NOFILE, &lims);
  //getrlimit (RLIMIT_NOFILE, &lims);
  //printf ("cur: %i\n", lims.rlim_cur);
  //printf ("max: %i\n", lims.rlim_max);
#ifdef MACOSX_EXTENSIONS
  mac_fix_yosemite_bug();
#endif

#endif
}

/******************************************************************************
* Main program
******************************************************************************/

void
immediate_options (int argc, char** argv) {
  if (get_env ("ATHENA_HOME_PATH") == "")
#ifdef OS_MINGW
  {
    if (get_env ("HOME") == "")
        set_env ("HOME", get_env("USERPROFILE"));
    set_env ("ATHENA_HOME_PATH", get_env ("APPDATA") * "\\ATHENA");
	}
#elif defined(OS_HAIKU)
    set_env ("ATHENA_HOME_PATH", get_env ("HOME") * "/config/settings/ATHENA");
#else
    set_env ("ATHENA_HOME_PATH", get_env ("HOME") * "/.ATHENA");
#endif
  if (get_env ("ATHENA_HOME_PATH") == "") return;
  for (int i=1; i<argc; i++) {
    string s= argv[i];
    if ((N(s)>=2) && (s(0,2)=="--")) s= s (1, N(s));
    if ((s == "-S") || (s == "-setup")) {
      remove (url ("$ATHENA_HOME_PATH/system/settings.scm"));
      remove (url ("$ATHENA_HOME_PATH/system/setup.scm"));
      remove (url ("$ATHENA_HOME_PATH/system/cache") * url_wildcard ("*"));
      remove (url ("$ATHENA_HOME_PATH/fonts/font-database.scm"));
      remove (url ("$ATHENA_HOME_PATH/fonts/font-features.scm"));
      remove (url ("$ATHENA_HOME_PATH/fonts/font-characteristics.scm"));
      remove (url ("$ATHENA_HOME_PATH/fonts/error") * url_wildcard ("*"));
    }
    else if (s == "-delete-cache")
      remove (url ("$ATHENA_HOME_PATH/system/cache") * url_wildcard ("*"));
    else if (s == "-delete-style-cache")
      remove (url ("$ATHENA_HOME_PATH/system/cache") * url_wildcard ("__*"));
    else if (s == "-delete-font-cache") {
      remove (url ("$ATHENA_HOME_PATH/system/cache/font_cache.scm"));
      remove (url ("$ATHENA_HOME_PATH/system/cache/font_path_cache.scm"));
      remove (url ("$ATHENA_HOME_PATH/system/cache/font_file_index.scm"));
      remove (url ("$ATHENA_HOME_PATH/fonts/font-database.scm"));
      remove (url ("$ATHENA_HOME_PATH/fonts/font-features.scm"));
      remove (url ("$ATHENA_HOME_PATH/fonts/font-characteristics.scm"));
      remove (url ("$ATHENA_HOME_PATH/fonts/error") * url_wildcard ("*"));
    }
    else if (s == "-delete-doc-cache") {
      remove (url ("$ATHENA_HOME_PATH/system/cache/doc_cache"));
      remove (url ("$ATHENA_HOME_PATH/system/cache/dir_cache.scm"));
      remove (url ("$ATHENA_HOME_PATH/system/cache/stat_cache.scm"));
    }
    else if (s == "-delete-file-cache") {
      remove (url ("$ATHENA_HOME_PATH/system/cache/doc_cache"));
      remove (url ("$ATHENA_HOME_PATH/system/cache/file_cache"));
      remove (url ("$ATHENA_HOME_PATH/system/cache/dir_cache.scm"));
      remove (url ("$ATHENA_HOME_PATH/system/cache/stat_cache.scm"));
    }
    else if (s == "-delete-plugin-cache")
      remove (url ("$ATHENA_HOME_PATH/system/cache/plugin_cache.scm"));
    else if (s == "-delete-server-data")
      system ("rm -rf", url ("$ATHENA_HOME_PATH/server"));
    else if (s == "-delete-databases") {
      system ("rm -rf", url ("$ATHENA_HOME_PATH/system/database"));
      system ("rm -rf", url ("$ATHENA_HOME_PATH/users"));
    }
#ifdef QTTEXMACS
    else if (s == "-headless" || s == "-H" || s == "-C" ||
	     s == "-build-website" || s == "-W" ||
	     s == "-update-website" || s == "-U")
      headless_mode= true;
#endif
    else if (s == "-log-file" && i + 1 < argc) {
      i++;
      char* log_file = argv[i];
      tm_ostream logf (log_file);
      if (!logf->is_writable ())
        cerr << "ATHENA] Error: could not open " << log_file << "\n";
      cout.redirect (logf);
      cerr.redirect (logf);
    }
  }
}

static const char* source_fingerprint_cache_name=
  "source_fingerprint.txt";

static std::string
athena_to_std_string (const string& s) {
  char* c= as_charp (s);
  std::string r (c, N(s));
  tm_delete_array (c);
  return r;
}

static void
athena_fingerprint_feed (uint64_t& hash, const std::string& text) {
  for (size_t i=0; i<text.size (); i++) {
    hash ^= (unsigned char) text[i];
    hash *= 1099511628211ULL;
  }
  hash ^= '\n';
  hash *= 1099511628211ULL;
}

static long long
athena_stat_mtime_nsec (const struct stat& st) {
#if defined(__APPLE__)
  return (long long) st.st_mtimespec.tv_nsec;
#elif defined(__linux__) || defined(__FreeBSD__)
  return (long long) st.st_mtim.tv_nsec;
#else
  (void) st;
  return 0;
#endif
}

static void
athena_fingerprint_feed_stat (uint64_t& hash, const std::string& rel,
                              const struct stat& st, char kind) {
  athena_fingerprint_feed (hash, std::string (1, kind));
  athena_fingerprint_feed (hash, rel);
  athena_fingerprint_feed (hash, std::to_string ((long long) st.st_mode));
  athena_fingerprint_feed (hash, std::to_string ((long long) st.st_size));
  athena_fingerprint_feed (hash, std::to_string ((long long) st.st_mtime));
  athena_fingerprint_feed (hash,
    std::to_string (athena_stat_mtime_nsec (st)));
}

#ifndef OS_MINGW
static void
athena_fingerprint_walk (const std::string& root, const std::string& rel,
                         uint64_t& hash, size_t& entries) {
  std::string dir_name= rel.empty ()? root: root + "/" + rel;
  DIR* dir= opendir (dir_name.c_str ());
  if (dir == NULL) return;

  std::vector<std::string> names;
  while (true) {
    struct dirent* ent= readdir (dir);
    if (ent == NULL) break;
    std::string name= ent->d_name;
    if (name == "." || name == "..") continue;
    names.push_back (name);
  }
  closedir (dir);
  std::sort (names.begin (), names.end ());

  for (size_t i=0; i<names.size (); i++) {
    std::string child_rel= rel.empty ()? names[i]: rel + "/" + names[i];
    std::string child_abs= root + "/" + child_rel;
    struct stat st;
    if (lstat (child_abs.c_str (), &st) != 0) continue;

    char kind= S_ISDIR (st.st_mode)? 'd':
      (S_ISLNK (st.st_mode)? 'l':
       (S_ISREG (st.st_mode)? 'f': 'o'));
    athena_fingerprint_feed_stat (hash, child_rel, st, kind);
    entries++;

    if (S_ISLNK (st.st_mode)) {
      char target[4096];
      ssize_t n= readlink (child_abs.c_str (), target, sizeof (target) - 1);
      if (n >= 0) {
        target[n]= '\0';
        athena_fingerprint_feed (hash, target);
      }
    }
    else if (S_ISDIR (st.st_mode))
      athena_fingerprint_walk (root, child_rel, hash, entries);
  }
}
#endif

static void
athena_source_fingerprint_for_subdir (uint64_t& hash, size_t& entries,
                                      const std::string& root,
                                      const char* subdir) {
#ifndef OS_MINGW
  std::string dir= root + "/" + subdir;
  struct stat st;
  if (stat (dir.c_str (), &st) != 0 || !S_ISDIR (st.st_mode))
    return;
  athena_fingerprint_feed (hash, subdir);
  athena_fingerprint_walk (dir, "", hash, entries);
#else
  (void) hash; (void) entries; (void) root; (void) subdir;
#endif
}

static std::string
athena_source_fingerprint_for_root (const char* label, const string& root_s) {
#ifdef OS_MINGW
  (void) label; (void) root_s;
  return "";
#else
  if (root_s == "") return "";
  std::string root= athena_to_std_string (root_s);

  uint64_t hash= 1469598103934665603ULL;
  size_t entries= 0;
  athena_fingerprint_feed (hash, "athena-source-fingerprint-v2");
  athena_fingerprint_feed (hash, label);
  athena_fingerprint_feed (hash, root);
  athena_source_fingerprint_for_subdir (hash, entries, root, "progs");
  athena_source_fingerprint_for_subdir (hash, entries, root, "packages");
  if (entries == 0) return "";

  char digest[64];
  snprintf (digest, sizeof (digest), "%016llx",
            (unsigned long long) hash);
  return std::string (label) + "\n" + root + "\n" +
    std::to_string (entries) + "\n" + digest + "\n";
#endif
}

static std::string
athena_current_source_fingerprint () {
  std::string fp= "athena-source-fingerprint-v2\n";
  fp += athena_source_fingerprint_for_root ("install", get_env ("ATHENA_PATH"));
  fp += athena_source_fingerprint_for_root ("home", get_env ("ATHENA_HOME_PATH"));
  return fp;
}

static bool
athena_cache_has_payload (url cache_dir) {
  bool err= false;
  array<string> entries= read_directory (cache_dir, err);
  if (err) return false;
  for (int i=0; i<N(entries); i++)
    if (entries[i] != "." && entries[i] != ".." &&
        entries[i] != source_fingerprint_cache_name)
      return true;
  return false;
}

static bool
athena_write_source_fingerprint (url fingerprint_file,
                                 const std::string& fingerprint) {
  return !save_string (fingerprint_file,
                       string (fingerprint.data (), (int) fingerprint.size ()));
}

static void
athena_restart_after_cache_refresh (int argc, char** argv) {
#ifdef OS_MINGW
  (void) argc; (void) argv;
#else
  cout << "ATHENA] cache refresh: restarting after cache cleanup" << LF;
  char** exec_argv= tm_new_array<char*> (argc + 1);
  for (int i=0; i<argc; i++) exec_argv[i]= argv[i];
  exec_argv[argc]= NULL;
  execvp (argv[0], exec_argv);
  tm_delete_array (exec_argv);
  cerr << "ATHENA] cache refresh: restart failed; continuing current process"
       << LF;
#endif
}

static void
athena_refresh_cache_if_sources_changed (int argc, char** argv) {
  url home_dir= url_system (get_env ("ATHENA_HOME_PATH"));
  if (!is_directory (home_dir)) mkdir (home_dir);
  if (!is_directory (home_dir * url ("progs")))
    mkdir (home_dir * url ("progs"));

  url cache_dir= home_dir * url ("system/cache");
  if (!is_directory (cache_dir)) mkdir (cache_dir);

  url fingerprint_file= cache_dir * url (source_fingerprint_cache_name);
  std::string current= athena_current_source_fingerprint ();
  if (current == "athena-source-fingerprint-v2\n") return;

  string stored_s;
  bool has_stored= !load_string (fingerprint_file, stored_s, false);
  std::string stored;
  if (has_stored) stored= athena_to_std_string (stored_s);

  bool stale= has_stored? stored != current:
    athena_cache_has_payload (cache_dir);
  if (!stale) {
    if (!has_stored)
      (void) athena_write_source_fingerprint (fingerprint_file, current);
    return;
  }

  cout << "ATHENA] cache refresh: source fingerprint changed; clearing cache"
       << LF;
  remove (cache_dir * url_wildcard ("*"));
  if (!is_directory (cache_dir)) mkdir (cache_dir);
  if (!athena_write_source_fingerprint (fingerprint_file, current)) {
    cerr << "ATHENA] cache refresh: could not store source fingerprint; "
         << "continuing without restart" << LF;
    return;
  }
  athena_restart_after_cache_refresh (argc, argv);
}

int
texmacs_entrypoint (int argc, char** argv) {
  for (int i=1; i<argc; i++) {
    string s= argv[i];
    if ((N(s)>=2) && (s(0,2)=="--")) s= s (1, N(s));
    if (s == "-aofm-convert-file") {
      i++;
      if (i < argc) {
        aofm_convert_file= argv[i];
        headless_mode= true;
      }
    }
    if (s == "-aofm-convert-vault") {
      if (i + 2 < argc) {
        i++;
        aofm_convert_vault_source= argv[i];
        i++;
        aofm_convert_vault_destination= argv[i];
        if (i + 1 < argc && is_positive_integer_arg (string (argv[i+1]))) {
          i++;
          aofm_convert_vault_parallelism=
            as_positive_integer_arg (string (argv[i]));
        }
        headless_mode= true;
      }
    }
    if (s == "-model-vault") {
      i++;
      if (i < argc) aofm_convert_vault_model_vault= argv[i];
    }
    if (s == "-vault-maintenance") {
      i++;
      if (i < argc) {
        vault_maintenance_dir= argv[i];
        headless_mode= true;
      }
    }
    if (s == "-rag-server") {
      i++;
      if (i < argc) {
        rag_server_dir= argv[i];
        headless_mode= true;
        exec_exit= false;
      }
    }
    if (s == "-rag-port") {
      i++;
      if (i < argc && is_positive_integer_arg (string (argv[i]))) {
        rag_server_port= as_positive_integer_arg (string (argv[i]));
        rag_server_port_set= true;
      }
    }
    if (s == "-rag-embedding-model") {
      i++;
      if (i < argc) rag_embedding_model= argv[i];
    }
    if (s == "-rag-embedding-device") {
      i++;
      if (i < argc) {
        string device= argv[i];
        rag_embedding_device= device == "cpu"? "cpu": "auto";
      }
    }
    if (s == "-rag-index-jobs") {
      i++;
      if (i < argc && is_positive_integer_arg (string (argv[i])))
        rag_index_jobs= as_positive_integer_arg (string (argv[i]));
    }
    if (s == "-rag-reindex") {
      rag_server_reindex= true;
    }
    if (s == "-ignore-nonempty-dest") {
      aofm_ignore_nonempty_dest = true;
    }
    if (s == "-insert-build-warning") {
      aofm_insert_build_warning = true;
    }
    if (s == "-no-splash-screen") {
      no_splash_screen= true;
    }
    if (s == "-headless" || s == "-H" || s == "-C" ||
	     s == "-build-website" || s == "-W" ||
	     s == "-update-website" || s == "-U")
      headless_mode= true;
  }
  ATHENA_init_paths (argc, argv);
#ifdef QTTEXMACS
  reject_unsupported_qt_platforms (argc, argv);
  bool rag_server_mode= rag_server_dir != "";
  if (!headless_mode || rag_server_mode) {
#if QT_VERSION >= 0x060000
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy
      (Qt::HighDpiScaleFactorRoundingPolicy::Round);
#if defined(OS_GNULINUX) || defined(OS_FREEBSD)
    QApplication::setStyle("fusion");
#endif
#elif QT_VERSION >= 0x050600
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
    qtmapp= new QTMApplication (argc, argv);
    if (!headless_mode && !no_splash_screen) tmapp()->show_splash ();
    startup_progress (5, "Application created");
  }
#endif
  startup_progress (8, "Reading startup options");
  immediate_options (argc, argv);
  startup_progress (10, "Startup options loaded");
  startup_progress (12, "Checking caches");
  athena_refresh_cache_if_sources_changed (argc, argv);
  startup_progress (20, "Caches ready");
#ifdef STACK_SIZE
  struct rlimit limit;

  if (getrlimit(RLIMIT_STACK, &limit) == 0) {
    if (limit.rlim_max < STACK_SIZE) {
      cerr << "Max stack allowed value : " << limit.rlim_max << "\n";
      limit.rlim_cur= limit.rlim_max;
    } else limit.rlim_cur= STACK_SIZE;
    if(setrlimit(RLIMIT_STACK, &limit)) cerr << "Cannot set stack value\n";
  } else cerr << "Cannot get stack value\n";
#endif

  original_path= get_env ("PATH");
  startup_progress (25, "Booting core");
  boot_hacks ();
  startup_progress (30, "Core booted");
  windows_delayed_refresh (1000000000);
  startup_progress (34, "Loading preferences");
  load_user_preferences ();
  startup_progress (38, "Preferences loaded");
#ifndef OS_MINGW
  set_env ("LC_NUMERIC", "POSIX");
#endif
#ifdef MACOSX_EXTENSIONS
  // Reset TeXmacs if Alt is pressed during startup
  if (mac_alternate_startup()) {
    cout << "ATHENA] Performing setup (Alt on startup)" << LF; 
    remove (url ("$ATHENA_HOME_PATH/system/settings.scm"));
    remove (url ("$ATHENA_HOME_PATH/system/setup.scm"));
    remove (url ("$ATHENA_HOME_PATH/system/cache") * url_wildcard ("*"));
    remove (url ("$ATHENA_HOME_PATH/fonts/error") * url_wildcard ("*"));    
  }
#endif 

#ifdef QTTEXMACS
  // initialize the Qt application infrastructure
  if (headless_mode)
    qtmcoreapp= new QTMCoreApplication (argc, argv);
  else {
    startup_progress (42, "Initializing interface");
    ((QTMApplication*)qtmapp)->load();
    startup_progress (50, "Interface initialized");
  }
#endif

  startup_progress (56, "Initializing caches");
  cache_initialize ();
  startup_progress (60, "Caches initialized");
  startup_progress (65, "Loading fonts");
  ATHENA_init_font  ();
  startup_progress (70, "Fonts ready");
#ifdef QTTEXMACS
  if (!headless_mode) {
#  if QT_VERSION >= 0x060000
#    ifndef OS_MACOS
    tmapp()->set_window_icon("/misc/images/ATHENA.svg");
#    endif
#  else
    tmapp()->set_window_icon("/misc/images/ATHENA-512.png");
#  endif
#endif
  }
  startup_progress (74, "Window icon ready");
  //cout << "Bench  ] Started TeXmacs\n";
  the_et     = tuple ();
  the_et->obs= ip_observer (path ());
  startup_progress (78, "Initializing editor");
  bench_start ("initialize texmacs");
  init_athena ();
  bench_cumul ("initialize texmacs");
  startup_progress (82, "Editor initialized");
#ifdef ENABLE_TESTS
  test_routines ();
#endif
//#ifdef EXPERIMENTAL
//  test_environments ();
//#endif
  startup_progress (83, "Starting Scheme");
  start_scheme (argc, argv, TeXmacs_main);
#ifdef QTTEXMACS
  if (headless_mode)
    delete qtmcoreapp;
  else
    delete qtmapp;
#endif
  return 0;
}

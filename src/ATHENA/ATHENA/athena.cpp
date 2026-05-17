
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

#ifdef AQUATEXMACS
void mac_fix_paths ();
#endif

#ifdef QTTEXMACS
#include "Qt/QTMApplication.hpp"
#include "Qt/qt_utilities.hpp"
#include <QDir>
#endif

#ifdef MACOSX_EXTENSIONS
#include "MacOS/mac_utilities.h"
#endif

#if defined(X11TEXMACS) && defined(MACOSX_EXTENSIONS)
#include "MacOS/mac_app.h"
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
                              bool ignore_nonempty, int parallelism);

bool disable_error_recovery= false;
bool start_server_flag= false;
bool headless_mode= false;
std::string aofm_debug_convert_file;
string aofm_debug_vault_convert_source;
string aofm_debug_vault_convert_destination;
string vault_maintenance_dir;
bool   aofm_ignore_nonempty_dest = false;
int    aofm_debug_vault_convert_parallelism = 0;
string extra_init_cmd;
bool exec_exit= true;
void server_start ();

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

#ifdef QTTEXMACS
// Qt application infrastructure
static QTMApplication* qtmapp= NULL;
static QTMCoreApplication* qtmcoreapp= NULL;
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

void ATHENA_init_font() {
  tt_font_cache_warmup ();
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
#if defined(QTTEXMACS) && QT_VERSION < 0x050000
  url exedir = url_system (qt_application_directory ());
#else
  url exedir = texmacs_get_application_directory();
#endif

  string current_athena_path = get_env ("ATHENA_PATH");

#ifdef Q_OS_MAC 
  // the following line can inibith external plugin loading
  // QCoreApplication::setLibraryPaths(QStringList());
  // ideally we would like to control the external plugins
  // and add the most useful (gif, jpeg, svg converters)
  // to the bundle package. I still do not have a reliable solution
  // so just allow everything that is reachable.
        
  // plugins need to be installed in TeXmacs.app/Contents/Plugins        
#if QT_VERSION < 0x050000
  QCoreApplication::addLibraryPath( QDir::cleanPath(QCoreApplication::applicationDirPath().append("/../Plugins")) );
#else
  string plugins_path = concretize (exedir * "../Plugins");
  QCoreApplication::addLibraryPath(QString::fromUtf8(&plugins_path[0], N(plugins_path)));
#endif
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

#if defined(AQUATEXMACS) || defined(Q_OS_MAC) || (defined(X11TEXMACS) && defined (MACOSX_EXTENSIONS))
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
      else if (s == "-debug-aofm-convert") {
        i++;
      }
      else if (s == "-debug-aofm-vault-convert") {
        i += 2;
        if (i+1 < argc && is_positive_integer_arg (string (argv[i+1]))) i++;
      }
      else if (s == "-vault-maintenance") {
        i++;
      }
      else if (s == "-ignore-nonempty-dest") {
        // Handled in texmacs_entrypoint
      }
      else if (s == "-insert-build-warning") {
        // Handled in texmacs_entrypoint
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
        cout << "  --vault-maintenance [dir]  Maintain an ATHENA vault headlessly\n";
        cout << "  --insert-build-warning     Insert ATHENA experimental build warnings during AOFM conversion\n";
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
  string native= (gui_version () == "qt4"? string ("on"): string ("off"));
  string unify = (gui_version () == "qt4"? string ("on"): string ("off"));
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

  set_global_options (argc, argv);

  if (DEBUG_STD) debug_boot << "Installing internal plug-ins...\n";
  bench_start ("initialize plugins");
  init_plugins ();
  bench_cumul ("initialize plugins");
  if (DEBUG_STD) debug_boot << "Opening display...\n";
  
  gui_open (argc, argv);
  set_default_font (the_default_font);
  
  { // opening scope for server sv
    if (DEBUG_STD) debug_boot << "Starting server...\n";
    server sv;
  
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
  
    if (number_buffers () == 0) {
      if (DEBUG_STD) debug_boot << "Creating 'no name' buffer...\n";
      open_window ();
      if (DEBUG_STD) debug_boot << "Queueing vault startup initialization...\n";
      extra_init_cmd << "(vault-startup-open-initial-buffer)";
    }
    extra_init_cmd << "(delayed (:idle 300) (ads-restore-visible-panes))";

    if (!aofm_debug_convert_file.empty ()) {
      eval ("(lazy-initialize-force)");
      aofm_enable_converter_mode (true);
      aofm_cache_preferences ();
      aofm_debug_dump (aofm_debug_convert_file);
      exit (0);
    }
    if (aofm_debug_vault_convert_source != "" &&
        aofm_debug_vault_convert_destination != "") {
      eval ("(lazy-initialize-force)");
      aofm_enable_converter_mode (true);
      aofm_cache_preferences ();
      bool ok= aofm_import_vault (aofm_debug_vault_convert_source,
                                  aofm_debug_vault_convert_destination,
                                  aofm_ignore_nonempty_dest,
                                  aofm_debug_vault_convert_parallelism);
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

#ifdef QTTEXMACS
#if defined(MAC_OS_X_VERSION_10_9) || defined(MAC_OS_X_VERSION_10_10)
#if QT_VERSION <= QT_VERSION_CHECK(4,8,5)
  // Work around Qt bug: https://bugreports.qt-project.org/browse/QTBUG-32789
  QFont::insertSubstitution (".Lucida Grande UI", "Lucida Grande");
#endif
#endif
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

static const char* progs_fingerprint_cache_name=
  "progs_fingerprint.txt";

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

static std::string
athena_progs_fingerprint_for_root (const char* label, const string& root_s) {
#ifdef OS_MINGW
  (void) label; (void) root_s;
  return "";
#else
  if (root_s == "") return "";
  std::string root= athena_to_std_string (root_s);
  struct stat st;
  if (stat ((root + "/progs").c_str (), &st) != 0 || !S_ISDIR (st.st_mode))
    return "";

  uint64_t hash= 1469598103934665603ULL;
  size_t entries= 0;
  athena_fingerprint_feed (hash, "athena-progs-fingerprint-v1");
  athena_fingerprint_feed (hash, label);
  athena_fingerprint_feed (hash, root);
  athena_fingerprint_walk (root + "/progs", "", hash, entries);

  char digest[64];
  snprintf (digest, sizeof (digest), "%016llx",
            (unsigned long long) hash);
  return std::string (label) + "\n" + root + "\n" +
    std::to_string (entries) + "\n" + digest + "\n";
#endif
}

static std::string
athena_current_progs_fingerprint () {
  std::string fp= "athena-progs-fingerprint-v1\n";
  fp += athena_progs_fingerprint_for_root ("install", get_env ("ATHENA_PATH"));
  fp += athena_progs_fingerprint_for_root ("home", get_env ("ATHENA_HOME_PATH"));
  return fp;
}

static bool
athena_cache_has_payload (url cache_dir) {
  bool err= false;
  array<string> entries= read_directory (cache_dir, err);
  if (err) return false;
  for (int i=0; i<N(entries); i++)
    if (entries[i] != "." && entries[i] != ".." &&
        entries[i] != progs_fingerprint_cache_name)
      return true;
  return false;
}

static bool
athena_write_progs_fingerprint (url fingerprint_file,
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
athena_refresh_cache_if_progs_changed (int argc, char** argv) {
  url home_dir= url_system (get_env ("ATHENA_HOME_PATH"));
  if (!is_directory (home_dir)) mkdir (home_dir);
  if (!is_directory (home_dir * url ("progs")))
    mkdir (home_dir * url ("progs"));

  url cache_dir= home_dir * url ("system/cache");
  if (!is_directory (cache_dir)) mkdir (cache_dir);

  url fingerprint_file= cache_dir * url (progs_fingerprint_cache_name);
  std::string current= athena_current_progs_fingerprint ();
  if (current == "athena-progs-fingerprint-v1\n") return;

  string stored_s;
  bool has_stored= !load_string (fingerprint_file, stored_s, false);
  std::string stored;
  if (has_stored) stored= athena_to_std_string (stored_s);

  bool stale= has_stored? stored != current:
    athena_cache_has_payload (cache_dir);
  if (!stale) {
    if (!has_stored)
      (void) athena_write_progs_fingerprint (fingerprint_file, current);
    return;
  }

  cout << "ATHENA] cache refresh: progs fingerprint changed; clearing cache"
       << LF;
  remove (cache_dir * url_wildcard ("*"));
  if (!is_directory (cache_dir)) mkdir (cache_dir);
  if (!athena_write_progs_fingerprint (fingerprint_file, current)) {
    cerr << "ATHENA] cache refresh: could not store progs fingerprint; "
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
    if (s == "-debug-aofm-convert") {
      i++;
      if (i < argc) {
        aofm_debug_convert_file= argv[i];
        headless_mode= true;
      }
    }
    if (s == "-debug-aofm-vault-convert") {
      if (i + 2 < argc) {
        i++;
        aofm_debug_vault_convert_source= argv[i];
        i++;
        aofm_debug_vault_convert_destination= argv[i];
        if (i + 1 < argc && is_positive_integer_arg (string (argv[i+1]))) {
          i++;
          aofm_debug_vault_convert_parallelism=
            as_positive_integer_arg (string (argv[i]));
        }
        headless_mode= true;
      }
    }
    if (s == "-vault-maintenance") {
      i++;
      if (i < argc) {
        vault_maintenance_dir= argv[i];
        headless_mode= true;
      }
    }
    if (s == "-ignore-nonempty-dest") {
      aofm_ignore_nonempty_dest = true;
    }
    if (s == "-insert-build-warning") {
      aofm_insert_build_warning = true;
    }
    if (s == "-headless" || s == "-H" || s == "-C" ||
	     s == "-build-website" || s == "-W" ||
	     s == "-update-website" || s == "-U")
      headless_mode= true;
  }
  ATHENA_init_paths (argc, argv);
#ifdef QTTEXMACS
  reject_unsupported_qt_platforms (argc, argv);
  if (!headless_mode) {
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
    tmapp()->show_splash ();
  }
#endif
  immediate_options (argc, argv);
  athena_refresh_cache_if_progs_changed (argc, argv);
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
  boot_hacks ();
  windows_delayed_refresh (1000000000);
  load_user_preferences ();
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
  else
    ((QTMApplication*)qtmapp)->load();
#endif

  cache_initialize ();
  ATHENA_init_font  ();
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
  //cout << "Bench  ] Started TeXmacs\n";
  the_et     = tuple ();
  the_et->obs= ip_observer (path ());
  bench_start ("initialize texmacs");
  init_athena ();
  bench_cumul ("initialize texmacs");
#ifdef ENABLE_TESTS
  test_routines ();
#endif
//#ifdef EXPERIMENTAL
//  test_environments ();
//#endif
  start_scheme (argc, argv, TeXmacs_main);
#ifdef QTTEXMACS
  if (headless_mode)
    delete qtmcoreapp;
  else
    delete qtmapp;
#endif
  return 0;
}

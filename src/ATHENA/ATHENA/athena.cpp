
/******************************************************************************
* MODULE     : texmacs.cpp
* DESCRIPTION: main program
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <locale.h> // for setlocale
#include <signal.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>
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
#include "new_buffer.hpp"
#include "new_view.hpp"
#include "new_document.hpp"
#include "tm_window.hpp"
#include "Interface/edit_interface.hpp"
#include "scheme.hpp"
#include "convert.hpp"
#include "Freetype/tt_file.hpp"
#include "ATHENA/Data/vault_maintenance.hpp"
#include "ATHENA/Data/artifacts.hpp"
#include "ATHENA/Data/artifact_range_llm.hpp"
#include "ATHENA/Data/vaultfile_json.hpp"
#include "ATHENA/Data/websites.hpp"
#include "MCP/mcp_rag_server.hpp"
#include "rag_delegation_crypto.hpp"

#ifdef QTTEXMACS
#include "Qt/QTMApplication.hpp"
#include "Qt/qt_gui.hpp"
#include "Qt/qt_font.hpp"
#include "QTMGoogleTasksPane.hpp"
#include "Qt/qt_utilities.hpp"
#include <QApplication>
#include <QDir>
#include <QTimer>
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

extern bool texmacs_started;

extern void aofm_debug_dump(const std::string& file_path);
extern bool aofm_import_vault(string source_dir, string destination_dir,
                              bool ignore_nonempty, int parallelism,
                              string model_vault);

bool disable_error_recovery= false;
bool start_server_flag= false;
bool headless_mode= false;
bool no_splash_screen= false;
bool skip_fonts_cache= false;
std::string aofm_convert_file;
string aofm_convert_vault_source;
string aofm_convert_vault_destination;
string aofm_convert_vault_model_vault;
string vault_maintenance_dir;
bool   vault_maintenance_check_only = false;
string rag_delegated_embedding_dir;
string vault_maintenance_toc_worker_file;
string vault_maintenance_toc_worker_marker;
string artifact_extract_worker_manifest;
string artifact_extract_worker_output;
string rag_server_dir;
string rag_embedding_model;
string rag_embedding_device= "auto";
int    rag_server_port = 8765;
bool   rag_server_port_set = false;
bool   rag_server_reindex = false;
int    rag_index_jobs = 0;
string rag_listen_address = "127.0.0.1";
string delegation_key_dir;
string delegation_accepted_clients;
bool   rag_generate_server_keypair = false;
string artifact_range_model;
int    artifact_range_batch_size = 16;
int    artifact_queue_limit = 4096;
int    artifact_queue_bytes = 32 * 1024 * 1024;
string website_generate_dir;
string website_generate_id;
string website_post_command_dir;
string website_post_command_id;
string scheme_bytecode_output_dir;
string scheme_bytecode_source_file;
string scheme_bytecode_compiled_file;
string scheme_bytecode_manifest_file;
bool   aofm_ignore_nonempty_dest = false;
int    aofm_convert_vault_parallelism = 0;
string extra_init_cmd;
bool exec_exit= true;
static std::string athena_to_std_string (const string& s);

static bool
athena_compile_scheme_file (const std::filesystem::path& source,
                            const std::filesystem::path& output) {
  namespace fs= std::filesystem;
  std::error_code ec;
  fs::create_directories (output.parent_path (), ec);
  if (ec) {
    std::cerr << "ATHENA Scheme bytecode: could not create "
              << output.parent_path () << ": " << ec.message () << '\n';
    return false;
  }

  eval ("(use-modules (system base compile))");
  eval ("(use-modules (utils edit variants) (database db-base))");
  eval ("(when (and (not (defined? 'athena-time)) "
        "                 (defined? 'texmacs-time)) "
        "  (module-define! (current-module) 'athena-time texmacs-time))");
  eval ("(let* ((module (resolve-module '(database db-base))) "
        "       (database (module-variable module 'current-database)) "
        "       (provider (module-ref module 'global-database))) "
        "  (variable-set! database (provider)))");
  string optimization= get_env ("ATHENA_SCHEME_OPTIMIZATION_LEVEL");
  if (optimization != "0" && optimization != "1" &&
      optimization != "2" && optimization != "3")
    optimization= "1";
  string command= "(with-database (global-database) (compile-file " *
    scm_quote (string (source.generic_string ().c_str ())) *
    " #:output-file " *
    scm_quote (string (output.generic_string ().c_str ())) *
    " #:env (current-module) #:optimization-level " * optimization *
    " #:warning-level 0))";
  object result= eval (command);
  if (is_list (result) || !fs::exists (output)) {
    std::cerr << "ATHENA Scheme bytecode: compilation failed for "
              << source << '\n';
    if (is_list (result))
      std::cerr << "ATHENA Scheme bytecode: Guile exception: "
                << athena_to_std_string (object_to_string (result)) << '\n';
    return false;
  }
  return true;
}

static void
athena_enable_scheme_dependency_cache () {
  string cache= get_env ("ATHENA_SCHEME_DEPENDENCY_CACHE_PATH");
  if (cache == "") cache= scheme_bytecode_output_dir;
  eval ("(let ((cache " * scm_quote (cache) * ")) "
        "  (unless (member cache %load-compiled-path) "
        "    (set! %load-compiled-path "
        "          (cons cache %load-compiled-path))))");
}

static bool
athena_compile_scheme_bytecode () {
  namespace fs= std::filesystem;
  const fs::path source_root (athena_to_std_string (get_env ("ATHENA_PATH")) +
                              "/progs");
  const fs::path output_root (athena_to_std_string (
    scheme_bytecode_output_dir));
  std::vector<fs::path> sources;
  std::error_code ec;

  for (fs::recursive_directory_iterator it (source_root, ec), end;
       !ec && it != end; it.increment (ec))
    if (it->is_regular_file (ec) && it->path ().extension () == ".scm")
      sources.push_back (it->path ());
  if (ec) {
    std::cerr << "ATHENA Scheme bytecode: could not enumerate "
              << source_root << ": " << ec.message () << '\n';
    return false;
  }
  std::sort (sources.begin (), sources.end ());
  fs::create_directories (output_root, ec);
  if (ec) {
    std::cerr << "ATHENA Scheme bytecode: could not create " << output_root
              << ": " << ec.message () << '\n';
    return false;
  }

  size_t completed= 0;
  for (const fs::path& source: sources) {
    fs::path relative= fs::relative (source, source_root, ec);
    if (ec) return false;
    fs::path output= output_root / relative;
    output.replace_extension (".go");
    if (!athena_compile_scheme_file (source, output)) return false;
    completed++;
    std::cout << "ATHENA Scheme bytecode: " << completed << "/"
              << sources.size () << " " << relative.generic_string () << '\n';
  }

  std::ofstream stamp (output_root / ".complete", std::ios::binary);
  stamp << ATHENA_GUILE_RUNTIME_ID << '\n' << completed << '\n';
  return stamp.good ();
}

static bool
athena_compile_scheme_manifest () {
  namespace fs= std::filesystem;
  std::ifstream input (athena_to_std_string (scheme_bytecode_manifest_file),
                       std::ios::binary);
  if (!input) {
    std::cerr << "ATHENA Scheme bytecode: could not open worker manifest "
              << athena_to_std_string (scheme_bytecode_manifest_file) << '\n';
    return false;
  }

  std::string source;
  std::string output;
  size_t completed= 0;
  while (std::getline (input, source, '\0')) {
    if (!std::getline (input, output, '\0')) {
      std::cerr << "ATHENA Scheme bytecode: truncated worker manifest "
                << athena_to_std_string (scheme_bytecode_manifest_file) << '\n';
      return false;
    }
    if (source.empty () || output.empty ()) {
      std::cerr << "ATHENA Scheme bytecode: empty path in worker manifest "
                << athena_to_std_string (scheme_bytecode_manifest_file) << '\n';
      return false;
    }
    if (!athena_compile_scheme_file (fs::path (source), fs::path (output)))
      return false;
    completed++;
    std::cout << "ATHENA Scheme bytecode: " << source << '\n';
  }
  return completed != 0;
}

#ifdef OS_MINGW
#ifndef CP_UTF8
#define CP_UTF8 65001
#endif
extern "C" __declspec(dllimport) unsigned long __stdcall
GetModuleFileNameW (void* hModule, wchar_t* lpFilename, unsigned long nSize);
extern "C" __declspec(dllimport) int __stdcall
WideCharToMultiByte (unsigned int CodePage, unsigned long dwFlags,
                     const wchar_t* lpWideCharStr, int cchWideChar,
                     char* lpMultiByteStr, int cbMultiByte,
                     const char* lpDefaultChar, int* lpUsedDefaultChar);

static string
athena_windows_module_directory (bool strip_bin) {
  std::vector<wchar_t> wide_path (32768);
  unsigned long n=
    GetModuleFileNameW (nullptr, wide_path.data (),
                        (unsigned long) wide_path.size ());
  if (n == 0 || n >= wide_path.size ()) return "";

  std::wstring path (wide_path.data (), n);
  for (wchar_t& c : path)
    if (c == L'\\') c= L'/';

  size_t slash= path.find_last_of (L'/');
  if (slash == std::wstring::npos) return "";
  path.resize (slash);

  if (strip_bin) {
    size_t parent= path.find_last_of (L'/');
    if (parent != std::wstring::npos) {
      std::wstring leaf= path.substr (parent + 1);
      if (leaf == L"bin" || leaf == L"Bin" || leaf == L"BIN")
        path.resize (parent);
    }
  }

  int bytes= WideCharToMultiByte (CP_UTF8, 0, path.c_str (),
                                  (int) path.size (), nullptr, 0,
                                  nullptr, nullptr);
  if (bytes <= 0) return "";
  std::string utf8 ((size_t) bytes, '\0');
  WideCharToMultiByte (CP_UTF8, 0, path.c_str (), (int) path.size (),
                       utf8.data (), bytes, nullptr, nullptr);
  return string (utf8.data (), utf8.size ());
}
#endif

static bool
std_ends_with (const std::string& s, const std::string& suffix) {
  return s.size () >= suffix.size () &&
         s.compare (s.size () - suffix.size (), suffix.size (), suffix) == 0;
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

static void
load_vault_preferences_if_enabled (const std::filesystem::path& vault_root,
                                   const char* context)
{
  if (get_preference ("vault take preferences with vault", "off") != "on")
    return;

  std::string vaultfile_error;
  AthenaVaultfileInfo info;
  if (!athena_vaultfile_read (vault_root, info, vaultfile_error)) {
    std_warning << context << ": vault preferences enabled, but "
                << vaultfile_error.c_str ()
                << "; using system preferences" << LF;
    return;
  }

  std::string prefs_rel= info.preferences_path;
  std::string json_rel= vault_preferences_json_path_for_boot (prefs_rel);
  if (!valid_vault_relative_path_for_boot (json_rel)) {
    std_warning << context << ": Vaultfile.json preferences path is not "
                << "vault-relative; using system preferences" << LF;
    return;
  }

  if (prefs_rel != json_rel) {
    info.preferences_path= json_rel;
    if (!athena_vaultfile_write (vault_root, info, vaultfile_error))
      std_warning << context << ": failed to normalize Vaultfile.json "
                  << "preferences path: " << vaultfile_error.c_str () << LF;
  }

  std::filesystem::path prefs_path= vault_root / json_rel;
  std::filesystem::path legacy_path= vault_root /
    (prefs_rel.empty () ? std::string ("vprefs.scm") : prefs_rel);

  if (!std::filesystem::exists (prefs_path) &&
      std::filesystem::exists (legacy_path)) {
    load_user_preferences (url (legacy_path.string ().c_str ()));
    return;
  }
  if (!std::filesystem::exists (prefs_path)) {
    std_warning << context << ": vault preferences enabled, but "
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

#ifdef QTTEXMACS
static string
qt_platform_from_arguments (int argc, char** argv) {
  for (int i=1; i<argc; i++) {
    string arg= argv[i];
    if (starts (arg, "--platform=") || starts (arg, "-platform=")) {
      int start= starts (arg, "--platform=")? 11: 10;
      return arg (start, N(arg));
    }
    if ((arg == "--platform" || arg == "-platform") && i+1 < argc)
      return argv[i+1];
  }
  return "";
}

static bool
qt_platform_is_wayland (string value) {
  string platform= qt_platform_name (locase_all (value));
  return starts (platform, "wayland");
}

static bool
requested_wayland_qt_platform (int argc, char** argv) {
  string arg_platform= qt_platform_from_arguments (argc, argv);
  if (!is_empty (arg_platform)) return qt_platform_is_wayland (arg_platform);

  string env_platform= get_env ("QT_QPA_PLATFORM");
  if (!is_empty (env_platform)) return qt_platform_is_wayland (env_platform);

  string session_type= locase_all (get_env ("XDG_SESSION_TYPE"));
  return session_type == "wayland" || get_env ("WAYLAND_DISPLAY") != "";
}

static void
normalize_wayland_qt_scaling (int argc, char** argv) {
  if (!requested_wayland_qt_platform (argc, argv)) return;

  string value= get_env ("QT_AUTO_SCREEN_SCALE_FACTOR");
  if (value != "" && value != "0")
    set_env ("QT_AUTO_SCREEN_SCALE_FACTOR", "0");

  // QtWayland already receives fractional scaling through wp_fractional_scale.
  // A process-level QT_SCALE_FACTOR multiplies that again, leaving top-level
  // widgets and xdg-popup positioners in different effective coordinate spaces.
  if (get_env ("QT_SCALE_FACTOR") != "")
    unsetenv ("QT_SCALE_FACTOR");
  if (get_env ("QT_SCREEN_SCALE_FACTORS") != "")
    unsetenv ("QT_SCREEN_SCALE_FACTORS");
  if (get_env ("QT_FONT_DPI") != "")
    unsetenv ("QT_FONT_DPI");
  if (get_env ("QT_SCALE_FACTOR_ROUNDING_POLICY") != "")
    unsetenv ("QT_SCALE_FACTOR_ROUNDING_POLICY");
}
#endif

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

static void
startup_process_events () {
  if (!headless_mode && qtmapp != NULL && !no_splash_screen)
    qtmapp->processEvents (QEventLoop::ExcludeUserInputEvents, 25);
}

static int startup_scheme_compile_depth= 0;

static string
startup_scheme_module_name (string source) {
  string install_root= get_env ("ATHENA_PATH") * "/";
  string source_root= install_root * "progs/";
  if (starts (source, source_root)) return source (N(source_root), N(source));
  if (starts (source, install_root))
    return source (N(install_root), N(source));
  return source;
}

static void
startup_scheme_compile (bool compiling, string source) {
  if (headless_mode || qtmapp == NULL || no_splash_screen) return;
  if (compiling) {
    startup_scheme_compile_depth++;
    qtmapp->set_splash_busy (
      "Compiling Scheme module: " * startup_scheme_module_name (source));
  }
  else {
    startup_scheme_compile_depth= std::max (0, startup_scheme_compile_depth - 1);
    if (startup_scheme_compile_depth == 0)
      qtmapp->set_splash_progress (83, "Loading Scheme modules");
  }
}
#else
static void
startup_progress (int progress, string message) {
  (void) progress; (void) message;
}

static void
startup_process_events () {}

static void
startup_scheme_compile (bool compiling, string source) {
  (void) compiling; (void) source;
}
#endif

#ifndef ATHENA_TSAN_BUILD
#  include <mimalloc-override.h>
#  include <mimalloc-new-delete.h>
#endif

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
  // Headless services can receive SIGTERM while Qt, SQLite, or an embedding
  // runtime is active.  Running C++ global destructors from a signal handler
  // is not async-signal-safe; let the operating system reclaim the process.
  if (headless_mode) _exit (0);
#ifdef ADVANCED_DEVELOPER_MODE
  exit (0);
#else
  _exit (0);
#endif
}

void ATHENA_init_font() {
  tt_font_cache_set_warmup_disabled (skip_fonts_cache);
  startup_progress (67, "Loading font database");
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
        
  // Plugins need to be installed in ATHENA.app/Contents/Plugins.
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
  
  if (is_empty (current_athena_path)) {
    string module_root= athena_windows_module_directory (true);
    if (!is_empty (module_root)) set_env ("ATHENA_PATH", module_root);
    else set_env ("ATHENA_PATH", as_string (exedir * ".."));
  }
  // if (get_env ("HOME") == "") //now set in immediate_options otherwise --setup option fails
  //   set_env ("HOME", get_env("USERPROFILE"));
  // HACK
  // In WINE the variable PWD is already in the outer Unix environment 
  // so we need to override it to have a correct behaviour
  if ((get_env ("PWD") == "") || (get_env ("PWD")[0] == '/'))  {
    string module_bin= athena_windows_module_directory (false);
    if (!is_empty (module_bin)) set_env ("PWD", module_bin);
    else set_env ("PWD", as_string (exedir));
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
      else if (s == "-rag-delegated-embedding") {
        i++;
      }
      else if (s == "-vault-maintenance-toc-worker") {
        i += 2;
      }
      else if (s == "-generate-website") {
        i += 2;
      }
      else if (s == "-run-website-post-command") {
        i += 2;
      }
      else if (s == "-compile-scheme-bytecode") {
        i++;
      }
      else if (s == "-compile-scheme-bytecode-worker") {
        i += 3;
      }
      else if (s == "-compile-scheme-bytecode-worker-list") {
        i += 2;
      }
      else if (s == "-check-only") {
        // Handled in texmacs_entrypoint
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
      else if (s == "-rag-listen-address") {
        i++;
      }
      else if (s == "-delegation-key-dir") {
        i++;
      }
      else if (s == "-delegation-accepted-clients") {
        i++;
      }
      else if (s == "-artifact-range-model" ||
               s == "-artifact-range-batch-size" ||
               s == "-artifact-queue-limit" ||
               s == "-artifact-queue-bytes") i++;
      else if (s == "-generate-server-keypair") {
        // Handled in texmacs_entrypoint
      }
      else if (s == "-skip-fonts-cache") {
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
      else if (s == "-log-file") i++;
      else if ((s == "-Oc") || (s == "-no-char-clipping")) char_clip= false;
      else if ((s == "+Oc") || (s == "-char-clipping")) char_clip= true;
      else if ((s == "-S") || (s == "-setup") ||
               (s == "-delete-cache") || (s == "-delete-font-cache") ||
               (s == "-delete-style-cache") || (s == "-delete-file-cache") ||
               (s == "-delete-doc-cache") || (s == "-delete-plugin-cache") ||
               (s == "-delete-databases") ||
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
        cout << "  --no-splash-screen       Start without the startup progress window\n";
        cout << "  --vault-maintenance [dir]  Maintain an ATHENA vault headlessly\n";
        cout << "  --rag-delegated-embedding [dir]  Run only delegated incremental embedding\n";
        cout << "  --vault-maintenance-toc-worker [file] [marker]  Internal ToC maintenance worker\n";
        cout << "  --generate-website [dir] [id]  Generate a vault website headlessly\n";
        cout << "  --run-website-post-command [dir] [id]  Run only a website post-generation command\n";
        cout << "  --check-only               With --vault-maintenance, run only the document health check\n";
        cout << "  --aofm-convert-file [file]  Convert one AOFM Markdown file headlessly\n";
        cout << "  --aofm-convert-vault [src] [dest] [jobs]  Convert an AOFM vault headlessly\n";
        cout << "  --rag-server [dir]          Start a Continuous RAG MCP server\n";
        cout << "  --rag-embedding-device [auto|cpu]  Select RAG embedding device mode\n";
        cout << "  --rag-index-jobs [n]        Parallelize initial RAG indexing with n processes\n";
        cout << "  --rag-listen-address [addr] Listen address for RAG server endpoints\n";
        cout << "  --delegation-key-dir [dir]  ATHENA delegation server key directory\n";
        cout << "  --delegation-accepted-clients [json]  Accepted delegation client keys\n";
        cout << "  --artifact-range-model [gguf]  Artifact definition-span model\n";
        cout << "  --artifact-range-batch-size [n]  Artifact model microbatch size\n";
        cout << "  --artifact-queue-limit [n]  Maximum queued artifact requests\n";
        cout << "  --artifact-queue-bytes [n]  Maximum artifact queue plaintext bytes\n";
        cout << "  --generate-server-keypair   Generate an ATHENA delegation server keypair\n";
        cout << "  --skip-fonts-cache         Skip the private font file cache\n";
        cout << "  --insert-build-warning     Insert ATHENA experimental build warnings during AOFM conversion\n";
        cout << "  --model-vault [dir]        Reuse a model vault for AOFM namespace/style conversion\n";
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
  if (headless_mode && exec_exit) my_init_cmds= my_init_cmds * " (quit-TeXmacs)";

  // Further options via environment variables
  // End options via environment variables

  // Further user preferences
  string native= "off";
  string unify = "off";
  string mini  = (os_macos ()? string ("off"): string ("on"));
  if (tm_style_sheet != "") mini= "off";
#if defined(qt_no_fontconfig)
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

  if (scheme_bytecode_output_dir != "") {
    init_plugins ();
    gui_open (argc, argv);
    server sv;
    // Bootstrap against source so legacy shared-root bindings are installed
    // in their historical order. Only then expose completed dependency
    // levels to compile-file's module resolver.
    athena_enable_scheme_dependency_cache ();
    bool ok= scheme_bytecode_manifest_file != ""
      ? athena_compile_scheme_manifest ()
      : (scheme_bytecode_source_file == ""
           ? athena_compile_scheme_bytecode ()
           : athena_compile_scheme_file (
               std::filesystem::path (athena_to_std_string (
                 scheme_bytecode_source_file)),
               std::filesystem::path (athena_to_std_string (
                 scheme_bytecode_compiled_file))));
    release_boot_lock ();
    exit (ok ? 0 : 1);
  }

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
    bench_start ("start server");
    server sv;
    bench_cumul ("start server");
    startup_progress (92, "Server ready");

    // GUI startup reaches the document file layer through its deferred window
    // and menu initialization.  Headless document workloads have no such
    // transition, so establish that capability explicitly before any C++
    // workload evaluates load/save/export commands in the shared Scheme root.
    // Pure service modes deliberately keep their smaller module footprint.
    if (headless_mode && rag_server_dir == "" &&
        rag_delegated_embedding_dir == "")
      eval ("(module-provide '(athena athena tm-files))");
  
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
      load_vault_preferences_if_enabled (options.vault_root,
                                         "Continuous RAG");
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
      options.listen_address= athena_to_std_string (rag_listen_address);
      if (delegation_key_dir != "")
        options.delegation_key_dir= std::filesystem::path (
          athena_to_std_string (delegation_key_dir));
      if (delegation_accepted_clients != "")
        options.delegation_accepted_clients= std::filesystem::path (
          athena_to_std_string (delegation_accepted_clients));
      options.artifact_range_model= artifact_range_model == "" ?
        std::filesystem::path (athena_artifact_range_model_path ()):
        std::filesystem::path (athena_to_std_string (artifact_range_model));
      options.artifact_range_batch_size= artifact_range_batch_size;
      options.artifact_queue_limit= artifact_queue_limit;
      options.artifact_queue_bytes= artifact_queue_bytes;
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
              << "configured" << "\n";
#ifdef QTTEXMACS
      QCoreApplication::exec ();
#endif
      exit (0);
    }

    if (rag_delegated_embedding_dir != "") {
      release_boot_lock ();
      bool ok= vault_rag_delegation_run (rag_delegated_embedding_dir);
      exit (ok ? 0 : 1);
    }
  
    if (number_buffers () == 0) {
      if (DEBUG_STD) debug_boot << "Creating 'no name' buffer...\n";
      startup_progress (94, "Building editor window");
      bench_start ("build editor window");
      qt_wait_for_font_fallback_warmup ();
      defer_next_editor_chrome_build ();
      defer_next_view_initialization ();
      open_window ();
      bench_cumul ("build editor window");
      bench_cumul ("startup to editor shell");
      startup_progress (96, "Editor window ready");
      schedule_deferred_view_initialization ();
      QTimer::singleShot (1000, [] () {
        cache_validate_font_directories ();
      });
      if (DEBUG_STD) debug_boot << "Queueing vault startup initialization...\n";
      extra_init_cmd << "(delayed (:idle 1) "
                        "(begin "
                        "(import-from (utils plugins plugin-convert)) "
                        "(update-menus)))";
      extra_init_cmd << "(delayed (:idle 100) "
                        "(import-from (fonts fonts-truetype)))";
      extra_init_cmd << "(delayed (:idle 0) "
                        "(vault-startup-open-initial-buffer))";
      extra_init_cmd << "(kbd-start-inverse-warmup)";
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
      release_boot_lock ();
      bool ok= vault_maintenance_run (vault_maintenance_dir,
                                      vault_maintenance_check_only);
      exit (ok ? 0 : 1);
    }
    if (vault_maintenance_toc_worker_file != "" &&
        vault_maintenance_toc_worker_marker != "") {
      eval ("(lazy-initialize-force)");
      string cmd= "(load-buffer (system->url " *
                  scm_quote (vault_maintenance_toc_worker_file) * ") :strict)";
      eval (cmd);
      bool failed= true;
      try {
        get_current_editor ()->generate_aux ("table-of-contents");
        url document= url_system (vault_maintenance_toc_worker_file);
        failed= buffer_save (document);
      }
      catch (...) {
        failed= true;
      }
      std::ofstream marker (athena_to_std_string (
        vault_maintenance_toc_worker_marker), std::ios::binary);
      if (marker) marker << (failed ? "save-failed" : "ok");
      marker.close ();
      exit (failed ? 1 : 0);
    }
    if (website_generate_dir != "" && website_generate_id != "") {
      eval ("(lazy-initialize-force)");
      load_vault_preferences_if_enabled (
        std::filesystem::path (athena_to_std_string (website_generate_dir)),
        "Website generation");
      std::string error;
      bool ok= athena_generate_website (
        athena_to_std_string (website_generate_dir),
        athena_to_std_string (website_generate_id), error);
      if (!ok) std_error << "website generation failed: "
                         << error.c_str () << LF;
      exit (ok ? 0 : 1);
    }
    if (website_post_command_dir != "" && website_post_command_id != "") {
      eval ("(lazy-initialize-force)");
      std::string error;
      bool ok= athena_run_website_post_command (
        athena_to_std_string (website_post_command_dir),
        athena_to_std_string (website_post_command_id), error);
      if (!ok) std_error << "website post-generation command failed: "
                         << error.c_str () << LF;
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
      remove (url ("$ATHENA_HOME_PATH/system/sys_state.json"));
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
      remove (url ("$ATHENA_HOME_PATH/system/cache/font_path_cache_v2.scm"));
      remove (url ("$ATHENA_HOME_PATH/system/cache/font_file_index_v2.scm"));
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
    else if (s == "-delete-databases") {
      system ("rm -rf", url ("$ATHENA_HOME_PATH/system/database"));
      system ("rm -rf", url ("$ATHENA_HOME_PATH/users"));
    }
#ifdef QTTEXMACS
    else if (s == "-headless" || s == "-H" || s == "-C")
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

static std::filesystem::path
athena_default_delegation_key_dir () {
  const char* xdg= getenv ("XDG_CONFIG_HOME");
  const char* home= getenv ("HOME");
  std::filesystem::path base=
    xdg != nullptr && xdg[0] != '\0' ? std::filesystem::path (xdg):
    (home == nullptr || home[0] == '\0' ? std::filesystem::path ("."):
      std::filesystem::path (home) / ".config");
  std::filesystem::path current= base / "ATHENA" / "delegation";
  std::filesystem::path legacy= base / "ATHENA" / "rag-delegation";
  std::error_code ec;
  if (!std::filesystem::exists (current) &&
      std::filesystem::exists (legacy))
    std::filesystem::rename (legacy, current, ec);
  return current;
}

static void
handle_rag_server_keypair_generation () {
  if (!rag_generate_server_keypair) return;
  std::filesystem::path key_dir=
    delegation_key_dir == "" ?
      athena_default_delegation_key_dir ():
      std::filesystem::path (athena_to_std_string (delegation_key_dir));
  athena::rag::delegation::KeyPair keypair;
  bool generated= false;
  std::string error;
  if (!athena::rag::delegation::ensure_keypair (
        key_dir, "server", keypair, &generated, error)) {
    std::fprintf (stderr,
                  "ATHENA delegation: failed to create server keypair: %s\n",
                  error.c_str ());
    exit (1);
  }
  std::printf ("ATHENA delegation server keypair %s in %s\n",
               generated ? "generated": "already exists",
               key_dir.generic_string ().c_str ());
  std::printf ("Public key: %s\n",
               athena::rag::delegation::base64_encode (
                 keypair.public_key).c_str ());
  std::printf ("Fingerprint: %s\n",
               athena::rag::delegation::fingerprint_for_public_key (
                 keypair.public_key).c_str ());
  std::printf ("Accepted clients file: %s\n",
               (key_dir / "accepted-clients.json").generic_string ().c_str ());
  if (rag_server_dir == "") exit (0);
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
athena_restart_after_startup_refresh (int argc, char** argv,
                                      const char* reason) {
#ifdef OS_MINGW
  (void) argc; (void) argv; (void) reason;
#else
  cout << "ATHENA] startup refresh: restarting after " << reason << LF;
  char** exec_argv= tm_new_array<char*> (argc + 1);
  for (int i=0; i<argc; i++) exec_argv[i]= argv[i];
  exec_argv[argc]= NULL;
  execvp (argv[0], exec_argv);
  tm_delete_array (exec_argv);
  cerr << "ATHENA] startup refresh: restart failed; continuing current process"
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
  athena_restart_after_startup_refresh (argc, argv, "cache cleanup");
}

static bool
athena_refresh_stale_scheme_bytecode (int argc, char** argv) {
#ifdef OS_MINGW
  (void) argc; (void) argv;
  return false;
#else
  namespace fs= std::filesystem;
  std::string athena_root= athena_to_std_string (get_env ("ATHENA_PATH"));
  std::string home_root= athena_to_std_string (get_env ("ATHENA_HOME_PATH"));
  if (athena_root.empty () || home_root.empty ()) return false;

  fs::path source_root= fs::path (athena_root) / "progs";
  fs::path output_root;
  std::string configured_cache= athena_to_std_string (
    get_env ("ATHENA_GUILE_CACHE_PATH"));
  if (!configured_cache.empty ()) output_root= configured_cache;
  else output_root= fs::path (athena_root) / "lib" / "athena-scheme" /
                    ATHENA_GUILE_RUNTIME_ID;

  std::ifstream stamp (output_root / ".complete", std::ios::binary);
  std::string runtime_id;
  if (!std::getline (stamp, runtime_id) ||
      runtime_id != ATHENA_GUILE_RUNTIME_ID)
    return false;

  std::error_code ec;
  size_t stale_count= 0;
  for (fs::recursive_directory_iterator it (source_root, ec), end;
       !ec && it != end; it.increment (ec)) {
    if (!it->is_regular_file (ec) || it->path ().extension () != ".scm")
      continue;
    fs::path relative= fs::relative (it->path (), source_root, ec);
    if (ec) break;
    fs::path compiled= output_root / relative;
    compiled.replace_extension (".go");
    if (!fs::exists (compiled, ec) ||
        fs::last_write_time (it->path (), ec) >
          fs::last_write_time (compiled, ec))
      stale_count++;
    if (ec) break;
  }
  if (ec || stale_count == 0) return false;

  if (access (output_root.c_str (), W_OK) != 0) {
    cerr << "ATHENA Scheme bytecode: stale compiled modules detected, but "
         << output_root.string ().c_str () << " is not writable" << LF;
    return false;
  }

  fs::path script= fs::path (athena_root).parent_path () / "tools" /
                   "compile-athena-scheme-bytecode.sh";
  if (!fs::is_regular_file (script, ec)) {
    cerr << "ATHENA Scheme bytecode: runtime compiler not found: "
         << script.string ().c_str () << LF;
    return false;
  }

  fs::path binary= fs::read_symlink ("/proc/self/exe", ec);
  if (ec) {
    ec.clear ();
    binary= fs::absolute (argv[0], ec);
  }
  if (ec) return false;

  fs::path compile_home= fs::path (home_root) / "system" /
                         "scheme-bytecode-runtime-refresh";
  fs::path runtime_root= fs::path (athena_root) / "lib" / "athena-guile";
  fs::path library_root= fs::path (athena_root) / "lib";
  std::string jobs= athena_to_std_string (
    get_env ("ATHENA_SCHEME_COMPILE_JOBS"));
  if (jobs.empty ()) jobs= "20";

  std::vector<std::string> arguments= {
    "/bin/bash", script.string (), binary.string (), output_root.string (),
    athena_root, compile_home.string (), runtime_root.string (),
    source_root.string (), library_root.string (), library_root.string (),
    library_root.string (), jobs, ATHENA_GUILE_RUNTIME_ID};
  std::vector<char*> exec_arguments;
  exec_arguments.reserve (arguments.size () + 1);
  for (std::string& argument: arguments)
    exec_arguments.push_back (argument.data ());
  exec_arguments.push_back (nullptr);

  cout << "ATHENA Scheme bytecode: detected " << stale_count
       << " stale module" << (stale_count == 1 ? "" : "s") << LF;
  startup_progress (0, "Compiling Scheme bytecode: preparing module plan");
  const char* previous_refresh= getenv ("ATHENA_SCHEME_RUNTIME_REFRESH");
  std::string previous_refresh_value=
    previous_refresh == nullptr ? "" : previous_refresh;
  setenv ("ATHENA_SCHEME_RUNTIME_REFRESH", "1", 1);
  int compiler_output[2]= {-1, -1};
  bool capture_output= pipe (compiler_output) == 0;
  if (capture_output) {
    (void) fcntl (compiler_output[0], F_SETFD, FD_CLOEXEC);
    (void) fcntl (compiler_output[1], F_SETFD, FD_CLOEXEC);
  }

  pid_t child= fork ();
  if (child == 0) {
    if (capture_output) {
      close (compiler_output[0]);
      if (dup2 (compiler_output[1], STDOUT_FILENO) < 0) _exit (127);
      close (compiler_output[1]);
    }
    execv ("/bin/bash", exec_arguments.data ());
    _exit (127);
  }
  if (previous_refresh == nullptr)
    unsetenv ("ATHENA_SCHEME_RUNTIME_REFRESH");
  else
    setenv ("ATHENA_SCHEME_RUNTIME_REFRESH",
            previous_refresh_value.c_str (), 1);
  if (child < 0) {
    if (capture_output) {
      close (compiler_output[0]);
      close (compiler_output[1]);
    }
    cerr << "ATHENA Scheme bytecode: could not start runtime compiler" << LF;
    return false;
  }

  if (capture_output) {
    close (compiler_output[1]);
    int flags= fcntl (compiler_output[0], F_GETFL, 0);
    if (flags >= 0)
      (void) fcntl (compiler_output[0], F_SETFL, flags | O_NONBLOCK);
  }

  size_t compile_total= 0;
  size_t compile_completed= 0;
  std::string pending_output;
  auto consume_line= [&] (const std::string& line) {
    static const std::string plan_prefix=
      "ATHENA Scheme bytecode: recompiling ";
    static const std::string module_prefix= "ATHENA Scheme bytecode: /";
    if (line.compare (0, plan_prefix.size (), plan_prefix) == 0) {
      size_t affected= 0;
      size_t available= 0;
      if (std::sscanf (
            line.c_str (),
            "ATHENA Scheme bytecode: recompiling %zu of %zu modules",
            &affected, &available) == 2) {
        compile_total= affected;
        startup_progress (
          0, "Compiling Scheme bytecode: 0/" *
               as_string (static_cast<long int> (compile_total)) * " modules");
      }
      return;
    }
    if (compile_total == 0 ||
        line.compare (0, module_prefix.size (), module_prefix) != 0 ||
        line.size () < 4 || line.substr (line.size () - 4) != ".scm")
      return;

    compile_completed= std::min (compile_completed + 1, compile_total);
    std::string source= line.substr (
      std::string ("ATHENA Scheme bytecode: ").size ());
    std::string source_prefix= source_root.string () + "/";
    if (source.compare (0, source_prefix.size (), source_prefix) == 0)
      source.erase (0, source_prefix.size ());
    int progress= static_cast<int> (
      (100 * compile_completed) / std::max<size_t> (compile_total, 1));
    startup_progress (
      progress,
      "Compiling Scheme bytecode: " *
        as_string (static_cast<long int> (compile_completed)) * "/" *
        as_string (static_cast<long int> (compile_total)) * "\n" *
        string (source.c_str ()));
  };

  int status= 0;
  pid_t waited= 0;
  bool output_open= capture_output;
  while (waited == 0 || output_open) {
    if (output_open) {
      char buffer[4096];
      ssize_t count;
      do {
        count= read (compiler_output[0], buffer, sizeof (buffer));
        if (count > 0) {
          std::cout.write (buffer, count);
          std::cout.flush ();
          pending_output.append (buffer, static_cast<size_t> (count));
          size_t newline;
          while ((newline= pending_output.find ('\n')) != std::string::npos) {
            consume_line (pending_output.substr (0, newline));
            pending_output.erase (0, newline + 1);
          }
        }
        else if (count == 0) {
          close (compiler_output[0]);
          output_open= false;
        }
      } while (count > 0);
    }

    if (waited == 0) {
      do waited= waitpid (child, &status, WNOHANG);
      while (waited < 0 && errno == EINTR);
    }
    startup_process_events ();
    if (waited == 0 || output_open) usleep (20000);
  }
  if (!pending_output.empty ()) consume_line (pending_output);
  if (waited == child && WIFEXITED (status) && WEXITSTATUS (status) == 0)
    startup_progress (100, "Scheme bytecode compiled; restarting ATHENA");
  if (waited != child || !WIFEXITED (status) || WEXITSTATUS (status) != 0) {
    cerr << "ATHENA Scheme bytecode: runtime refresh failed" << LF;
    return false;
  }
  return true;
#endif
}

int
texmacs_entrypoint (int argc, char** argv) {
  bench_start ("startup to editor shell");
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
        // This mode owns its synchronous lifetime and exits after all passes.
        // A queued Scheme quit would be consumed by nested Qt event loops,
        // such as the RAG delegation network request, and end maintenance
        // before the request completes.
        exec_exit= false;
      }
    }
    if (s == "-rag-delegated-embedding") {
      i++;
      if (i < argc) {
        rag_delegated_embedding_dir= argv[i];
        headless_mode= true;
        exec_exit= false;
      }
    }
    if (s == "-vault-maintenance-toc-worker") {
      if (i + 2 < argc) {
        i++;
        vault_maintenance_toc_worker_file= argv[i];
        i++;
        vault_maintenance_toc_worker_marker= argv[i];
        headless_mode= true;
      }
    }
    if (s == "-artifact-extract-worker") {
      if (i + 2 < argc) {
        i++;
        artifact_extract_worker_manifest= argv[i];
        i++;
        artifact_extract_worker_output= argv[i];
        headless_mode= true;
      }
    }
    if (s == "-generate-website") {
      i++;
      if (i < argc) website_generate_dir= argv[i];
      i++;
      if (i < argc) {
        website_generate_id= argv[i];
        headless_mode= true;
      }
    }
    if (s == "-run-website-post-command") {
      i++;
      if (i < argc) website_post_command_dir= argv[i];
      i++;
      if (i < argc) {
        website_post_command_id= argv[i];
        headless_mode= true;
      }
    }
    if (s == "-compile-scheme-bytecode") {
      i++;
      if (i < argc) {
        scheme_bytecode_output_dir= argv[i];
        headless_mode= true;
        no_splash_screen= true;
        skip_fonts_cache= true;
        exec_exit= false;
      }
    }
    if (s == "-compile-scheme-bytecode-worker") {
      if (i + 3 < argc) {
        scheme_bytecode_output_dir= argv[++i];
        scheme_bytecode_source_file= argv[++i];
        scheme_bytecode_compiled_file= argv[++i];
        headless_mode= true;
        no_splash_screen= true;
        skip_fonts_cache= true;
        exec_exit= false;
      }
    }
    if (s == "-compile-scheme-bytecode-worker-list") {
      if (i + 2 < argc) {
        scheme_bytecode_output_dir= argv[++i];
        scheme_bytecode_manifest_file= argv[++i];
        headless_mode= true;
        no_splash_screen= true;
        skip_fonts_cache= true;
        exec_exit= false;
      }
    }
    if (s == "-check-only") {
      vault_maintenance_check_only= true;
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
    if (s == "-rag-listen-address") {
      i++;
      if (i < argc) rag_listen_address= argv[i];
    }
    if (s == "-delegation-key-dir") {
      i++;
      if (i < argc) delegation_key_dir= argv[i];
    }
    if (s == "-delegation-accepted-clients") {
      i++;
      if (i < argc) delegation_accepted_clients= argv[i];
    }
    if (s == "-artifact-range-model") {
      i++;
      if (i < argc) artifact_range_model= argv[i];
    }
    if (s == "-artifact-range-batch-size") {
      i++;
      if (i < argc && is_positive_integer_arg (string (argv[i])))
        artifact_range_batch_size= std::clamp (
          as_positive_integer_arg (string (argv[i])), 1, 16);
    }
    if (s == "-artifact-queue-limit") {
      i++;
      if (i < argc && is_positive_integer_arg (string (argv[i])))
        artifact_queue_limit= as_positive_integer_arg (string (argv[i]));
    }
    if (s == "-artifact-queue-bytes") {
      i++;
      if (i < argc && is_positive_integer_arg (string (argv[i])))
        artifact_queue_bytes= as_positive_integer_arg (string (argv[i]));
    }
    if (s == "-generate-server-keypair") {
      rag_generate_server_keypair= true;
      headless_mode= true;
    }
    if (s == "-skip-fonts-cache") {
      skip_fonts_cache= true;
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
    if (s == "-headless" || s == "-H" || s == "-C")
      headless_mode= true;
  }
  ATHENA_init_paths (argc, argv);
  if (scheme_bytecode_output_dir != "") {
    set_env ("ATHENA_GUILE_SOURCE_ROOT", "$ATHENA_PATH/progs");
    set_env ("GUILE_AUTO_COMPILE", "0");
  }
  if (artifact_extract_worker_manifest != "" &&
      artifact_extract_worker_output != "") {
    std::string error;
    bool ok= athena_artifacts_run_extract_worker (
      std::filesystem::path (athena_to_std_string (
        artifact_extract_worker_manifest)),
      std::filesystem::path (athena_to_std_string (
        artifact_extract_worker_output)), error);
    if (!ok) std::cerr << "artifact extraction worker: " << error << '\n';
    return ok ? 0 : 1;
  }
  handle_rag_server_keypair_generation ();
#ifdef QTTEXMACS
  bench_start ("create qt application");
  reject_unsupported_qt_platforms (argc, argv);
  normalize_wayland_qt_scaling (argc, argv);
  bool rag_server_mode= rag_server_dir != "";
  bool website_generation_mode= website_generate_dir != "";
  if (website_generation_mode &&
      is_empty (qt_platform_from_arguments (argc, argv)) &&
      get_env ("QT_QPA_PLATFORM") == "" &&
      get_env ("WAYLAND_DISPLAY") == "" && get_env ("DISPLAY") == "")
    qputenv ("QT_QPA_PLATFORM", "offscreen");
  if (headless_mode && rag_server_mode) {
    qtmcoreapp= new QTMCoreApplication (argc, argv);
  }
  else if (!headless_mode || website_generation_mode) {
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy
      (requested_wayland_qt_platform (argc, argv) ?
       Qt::HighDpiScaleFactorRoundingPolicy::PassThrough :
       Qt::HighDpiScaleFactorRoundingPolicy::Round);
#if defined(OS_GNULINUX) || defined(OS_FREEBSD)
    if (!requested_wayland_qt_platform (argc, argv))
      QApplication::setStyle("fusion");
#endif
    qtmapp= new QTMApplication (argc, argv);
    if (!headless_mode && !no_splash_screen) tmapp()->show_splash ();
    startup_progress (5, "Application created");
  }
  bench_cumul ("create qt application");
#endif
  startup_progress (8, "Reading startup options");
  immediate_options (argc, argv);
  startup_progress (10, "Startup options loaded");
  startup_progress (12, "Checking caches");
  bench_start ("check startup caches");
  bool refreshed_scheme_bytecode= false;
  if (scheme_bytecode_output_dir == "") {
    refreshed_scheme_bytecode=
      athena_refresh_stale_scheme_bytecode (argc, argv);
    athena_refresh_cache_if_sources_changed (argc, argv);
    if (refreshed_scheme_bytecode)
      athena_restart_after_startup_refresh (
        argc, argv, "Scheme bytecode compilation");
  }
  bench_cumul ("check startup caches");
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
  bench_start ("load startup preferences");
  load_user_preferences ();
  bench_cumul ("load startup preferences");
  startup_progress (38, "Preferences loaded");
#ifndef OS_MINGW
  set_env ("LC_NUMERIC", "POSIX");
#endif
#ifdef MACOSX_EXTENSIONS
  // Reset TeXmacs if Alt is pressed during startup
  if (mac_alternate_startup()) {
    cout << "ATHENA] Performing setup (Alt on startup)" << LF; 
    remove (url ("$ATHENA_HOME_PATH/system/sys_state.json"));
    remove (url ("$ATHENA_HOME_PATH/system/settings.scm"));
    remove (url ("$ATHENA_HOME_PATH/system/setup.scm"));
    remove (url ("$ATHENA_HOME_PATH/system/cache") * url_wildcard ("*"));
    remove (url ("$ATHENA_HOME_PATH/fonts/error") * url_wildcard ("*"));    
  }
#endif

#ifdef QTTEXMACS
  // initialize the Qt application infrastructure
  if (headless_mode) {
    if (qtmapp == NULL && qtmcoreapp == NULL)
      qtmcoreapp= new QTMCoreApplication (argc, argv);
  }
  else {
    startup_progress (42, "Initializing interface");
    bench_start ("initialize qt interface");
    athena_initialize_wayland_ui_scale ();
    ((QTMApplication*)qtmapp)->load();
    bench_cumul ("initialize qt interface");
    startup_progress (50, "Interface initialized");
  }
#endif

  startup_progress (56, "Initializing caches");
  bench_start ("initialize data caches");
  cache_initialize ();
  bench_cumul ("initialize data caches");
  startup_progress (60, "Caches initialized");
  if (scheme_bytecode_output_dir == "") {
    startup_progress (65, "Loading fonts");
    bench_start ("initialize fonts");
    ATHENA_init_font ();
    bench_cumul ("initialize fonts");
    startup_progress (70, "Fonts ready");
  }
#ifdef QTTEXMACS
  if (!headless_mode) qt_start_font_fallback_warmup ();
#endif
#ifdef QTTEXMACS
  if (!headless_mode) {
#    ifndef OS_MACOS
    tmapp()->set_window_icon("/misc/images/ATHENA.svg");
#endif
#endif
  }
  startup_progress (74, "Window icon ready");
  //cout << "Bench  ] Started TeXmacs\n";
  reset_document_tree (current_document_tree ());
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
  startup_progress (83, "Loading Scheme modules");
  start_scheme (argc, argv, TeXmacs_main, startup_scheme_compile);
#ifdef QTTEXMACS
  if (headless_mode) {
    if (qtmapp != NULL) delete qtmapp;
    else delete qtmcoreapp;
  }
  else
    delete qtmapp;
#endif
  return 0;
}

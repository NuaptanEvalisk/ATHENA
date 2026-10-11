
/******************************************************************************
* MODULE     : basic.cpp
* DESCRIPTION: fast global new and delete
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "basic.hpp"
#include "string.hpp"
#include "analyze.hpp"
#include "scheme.hpp"
#include "url.hpp"
#include "file.hpp"
#include "Freetype/tt_file.hpp"
#include "sys_utils.hpp"
#include "convert.hpp"
#include <mutex>

/******************************************************************************
* debugging
******************************************************************************/


/******************************************************************************
* debugging messages
******************************************************************************/

tree debug_messages (TUPLE);
bool debug_lf_flag= false;
extern bool texmacs_started;

static void
debug_message_sub (string channel, string msg, bool flush_at_end) {
  if (occurs ("\n", msg)) {
    int pos= search_forwards ("\n", 0, msg);
    debug_message_sub (channel, msg (0, pos), false);
    debug_lf_flag= true;
    cout << "\n";
    if (pos+1 < N(msg))
      debug_message_sub (channel, msg (pos+1, N(msg)), false);
  }
  else {
    int n= N(debug_messages);
    if (!debug_lf_flag && n>0 && is_tuple (debug_messages[n-1], channel)) {
      tree *t= &(debug_messages[n-1][1]);
      *t= (*t)->label * msg;
      cout << msg;
    }
    else {
      debug_messages << tuple (channel, msg, "");
      debug_lf_flag= false;
      if (channel != "debug-boot") {
        cout << "ATHENA] ";
        if (channel != "debug-automatic" &&
            channel != "boot-error")
          cout << channel << ", ";
      }
      cout << msg;
    }
  }
  if (flush_at_end) cout.flush ();
}

void
debug_message_sub (string channel, string msg) {
  debug_message_sub (channel, msg, true);
}

void
debug_message (string channel, string msg) {
  debug_message_sub (channel, msg);
  if (texmacs_started && channel != "debug-widgets")
    call ("notify-debug-message", object (channel));
}

void
debug_formatted (string channel, tree msg) {
  int n= N(debug_messages);
  if (n>0 && is_tuple (debug_messages[n-1], channel)) {
    debug_messages[n-1][2]= msg;
    if (texmacs_started && channel != "debug-widgets")
      call ("notify-debug-message", object (channel));
  }
}

tree
get_debug_messages (string kind, int max_number) {
  tree m (TUPLE);
  for (int i=N(debug_messages)-1; i>=0; i--) {
    tree t= debug_messages[i];
    if (!is_func (t, TUPLE, 3) || !is_atomic (t[0])) continue;
    string s= t[0]->label;
    if (kind == "Debugging console" ||
        ends (s, "-error") ||
        ends (s, "-warning"))
      m << t;
    if (N(m) >= max_number) break;
  }
  tree r (TUPLE);
  for (int i=N(m)-1; i>=0; i--) r << m[i];
  return r;
}

void
clear_debug_messages (string channel) {
  tree r= tree (TUPLE);
  for (int i=0; i<N(debug_messages); i++)
    if (is_func (debug_messages[i], TUPLE, 3) &&
        debug_messages[i][0] != channel)
      r << debug_messages[i];
  debug_messages= r;
  debug_lf_flag = false;
}

void
clear_debug_messages () {
  debug_messages= tree (TUPLE);
  debug_lf_flag = false;
}

#ifdef USE_EXCEPTIONS
static thread_local string the_report;
string get_crash_report (const char* msg);

void
qarma_crash_report (string report) {
  // Save full crash report to a file
  url dir ("$ATHENA_HOME_PATH/system/crash");
  url err;
  {
    // Ordinary exceptions can arrive from several actors at once. Allocate
    // and save the numbered file together; fatal signals never enter here.
    static std::mutex report_files;
    std::lock_guard<std::mutex> lock (report_files);
    err= url_numbered (dir, "crash_report_", "");
    save_string (err, report);
  }

  // Extract first 10 lines for the dialog
  string summary = "";
  int lines = 0;
  for (int i=0; i<N(report); i++) {
    if (report[i] == '\n') lines++;
    summary << report[i];
    if (lines >= 10) break;
  }

  // Display crash report using qarma
  string qarma_text = "ATHENA has encountered a fatal error and needs to close.\n";
  qarma_text << "A full crash report has been saved to: " << as_string (err) << "\n\n";
  qarma_text << "Summary:\n" << summary;
  if (lines >= 10) qarma_text << "...\n";
  
  string escaped_report = "";
  for (int i=0; i<N(qarma_text); i++) {
    if (qarma_text[i] == '\"') escaped_report << "\\\"";
    else if (qarma_text[i] == '`') escaped_report << "\\`";
    else if (qarma_text[i] == '$') escaped_report << "\\$";
    else if (qarma_text[i] == '\\') escaped_report << "\\\\";
    else if (qarma_text[i] == '\n') escaped_report << "<br/>";
    else escaped_report << qarma_text[i];
  }

  string qarma_cmd = "qarma --error --text \"";
  qarma_cmd << escaped_report << "\" ";
  qarma_cmd << "--selectable-labels --name=\"ATHENA Error Reporting\" ";
  qarma_cmd << "--title=\"ATHENA Error Reporting\" --modal &";
  
  system (qarma_cmd);
}

static void
report_desktop_exception (const char* msg) {
  the_report   = get_crash_report (msg);
  cout << "Throwing " << msg << LF;
  cout << "-------------------------------------------------\n";
  cout << the_report << LF;
  cout << "-------------------------------------------------\n";
  qarma_crash_report (the_report);
}

namespace {
struct desktop_exception_reporting {
  desktop_exception_reporting () {
    set_tm_exception_reporter (report_desktop_exception);
  }
} desktop_exception_reporting_instance;
}

void
handle_exceptions () {
  if (N (the_exception) != 0) {
    formatted arg (verbatim_to_tree (the_report, false, "utf-8"));
    failed_error << "Exception, " << the_exception << arg << LF;
    the_exception= "";
    the_report   = "";
  }
}
#endif

/******************************************************************************
* miscellaneous routines
******************************************************************************/


/******************************************************************************
* Various TeXmacs blends
******************************************************************************/

bool
gui_is_x () {
#ifdef QTTEXMACS
  return false;
#else
  return true;
#endif
}

bool
gui_is_qt () {
#ifdef QTTEXMACS
  return true;
#else
  return false;
#endif
}

bool
os_win32 () {
#if defined (OS_WIN32)
  return true;
#else
  return false;
#endif
}

bool
os_mingw () {
#ifdef OS_MINGW
  return true;
#else
  return false;
#endif
}

bool
os_mingw64 () {
#ifdef OS_MINGW64
  return true;
#else
  return false;
#endif
}

bool
os_macos () {
#if defined (OS_MACOS)
  return true;
#else
  return false;
#endif
}

bool
use_macos_fonts () {
#ifdef OS_MACOS
  if (gui_is_qt ()) return true;
  string s= get_preference ("look and feel");
  if (s != "default" && s != "macos") return false;
  return tt_font_exists ("LucidaGrande");
#else
  return false;
#endif
}

static const char*
default_look_and_feel_impl () {
  if (os_mingw () || os_win32 ()) return "windows";
  if (os_macos ()) return "macos";
  string session= get_env ("DESKTOP_SESSION");
  if (session == "") session= get_env ("XDG_CURRENT_DESKTOP");
  session= locase_all (session);
  if (occurs ("gnome", session))    return "gnome";
  if (occurs ("cinnamon", session)) return "gnome";
  if (occurs ("mate", session))     return "gnome";
  //if (occurs ("ubuntu", session))   return "unity";
  if (occurs ("kde", session))      return "kde";
  //if (occurs ("xfce", session))     return "xfce";
  //if (occurs ("lxde", session))     return "lxde";
  //return "emacs";
  return "gnome"; // default UI (much more "standard" than emacs) 
}

const char*
default_look_and_feel () {
  static const char* ret= default_look_and_feel_impl ();
  return ret;
}

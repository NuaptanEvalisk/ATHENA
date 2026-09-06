
/******************************************************************************
* MODULE     : tm_debug.cpp
* DESCRIPTION: Debugging facilities
* COPYRIGHT  : (C) 2011  Joris van der Hoeven
*              (C) 2008  Timo Bingmann from http://idlebox.net
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "tm_server.hpp"
#include "file.hpp"
#include "tm_link.hpp"
#include "sys_utils.hpp"
#include "new_document.hpp"
#include "scheme_execution_context.hpp"
#include "System/Misc/crash_report.hpp"

#include <ctime>

std::atomic<bool> rescue_mode {false};

/******************************************************************************
* Status reports
******************************************************************************/

string get_system_date () {
  std::time_t now= std::time (nullptr);
  std::tm local_time;
#ifdef _WIN32
  if (localtime_s (&local_time, &now) != 0) return "Unknown date";
#else
  if (localtime_r (&now, &local_time) == nullptr) return "Unknown date";
#endif
  char buffer[1024];
  size_t len = std::strftime(buffer, sizeof(buffer), "%a %b %d %H:%M:%S %Z %Y", &local_time);
  if (len > 0) {
    return string(buffer, len);
  } else {
    return "Unknown date";
  }
}

string
get_system_information () {
  string r;
  r << "System information:\n";
  r << "  TeXmacs version  : "
    << ATHENA_VERSION << "\n";
  r << "  Built by         : "
    << BUILD_USER << "\n";
  r << "  Building date    : "
    << BUILD_DATE << "\n";
  r << "  Operating system : "
    << HOST_OS << "\n";
  r << "  Vendor           : "
    << HOST_VENDOR << "\n";
  r << "  Processor        : "
    << HOST_CPU << "\n";
  r << "  Crash date       : "
    << get_system_date() << "\n";
  return r;
}

string
path_as_string (path p) {
  if (is_nil (p)) return "[]";
  string r= "[ ";
  r << as_string (p->item);
  p= p->next;
  while (!is_nil (p)) {
    r << ", " << as_string (p->item);
    p= p->next;
  }
  r << " ]";
  return r;
}

static string
get_execution_status_report () {
  std::string report= athena_crash_execution_report ();
  return "Execution status:\n  " * string (report.data (), report.size ());
}

void
tree_report (string& s, tree t, path p, int indent) {
  for (int i=0; i<indent; i++) s << " ";
  if (is_atomic (t)) {
    s << raw_quote (t->label);
    s << " -- " << path_as_string (p) << "\n";
  }
  else {
    s << as_string (L(t));
    s << " -- " << path_as_string (p) << "\n";
    for (int i=0; i<N(t); i++)
      tree_report (s, t[i], p * i, indent+2);
  }
}

string
tree_report (tree t, path p) {
  string s;
  tree_report (s, t, p, 0);
  return s;
}

/******************************************************************************
* Crash management
******************************************************************************/

string
get_crash_report (const char* msg) {
  string r;
  r << "Error message:\n  " << msg << "\n"
    << "\n" << get_system_information ()
    << "\n" << get_execution_status_report ()
    << "\n" << get_stacktrace ();
  return r;
}

void
tm_failure (const char* msg) {
  rescue_mode.store (true);
  // A damaged process cannot safely run Scheme, autosave other actors or
  // destroy shared services. The fatal-signal reporter leaves those untouched.
  athena_crash_abort (msg);
}

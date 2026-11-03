
/******************************************************************************
* MODULE     : server_log.cpp
* DESCRIPTION: server log facilities
* COPYRIGHT  : (C) 2022  Gregoire lecerf
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#if !defined(OS_MACOS) && !defined(OS_MINGW)
#include "server_log.hpp"
#include "language.hpp"
#include "locale.hpp"
#include <unistd.h>
#include <syslog.h>

static bool server_log_active= false;

static void
server_log_ensure_initialized () {
  if (!server_log_active) {
    openlog ("texmacs", LOG_PID | LOG_NDELAY | LOG_CONS, LOG_LOCAL0);
    server_log_active= true;
  }
}

void
server_log_start () {
  server_log_ensure_initialized ();
}

void
server_log_stop () {
  if (server_log_active) {
    closelog ();
    server_log_active= false; }
}

struct syslog_handler {
  inline syslog_handler () {}
  inline ~syslog_handler () { server_log_stop (); }
};

void
server_log_write (int level, string m) {
  static const string pid= as_string ((int) getpid ());
  static const string uid= as_string ((int) getuid ());
  static const string prefix= "pid=" * pid * ", uid=" * uid * ", ";
  string msg= prefix * m;
  string date= get_date (get_locale_language (), "yyyy-MM-dd, HH:mm:ss");
  server_get_stream (level) << date << ", " << msg << "\n";
}
#endif

#ifdef OS_MACOS
#include "server_log.hpp"
#include "language.hpp"
#include "locale.hpp"
#include <unistd.h>
#include <os/log.h>

static bool server_log_active= false;
static os_log_t _os_log;

void
server_log_start () {
  _os_log= os_log_create ("org.texmacs.logging", "server");
  if (os_log_type_enabled (_os_log, OS_LOG_TYPE_DEFAULT) &&
      os_log_type_enabled (_os_log, OS_LOG_TYPE_ERROR))
    server_log_active= true;
}

void
server_log_stop () {
  server_log_active= false;
}

void
server_log_write (int level, string m) {
  static const string pid= as_string ((int) getpid ());
  static const string uid= as_string ((int) getuid ());
  static const string prefix= "pid=" * pid * ", uid=" * uid * ", ";
  string msg= prefix * m;
  string date= get_date (get_locale_language (), "yyyy-MM-dd, HH:mm:ss");
  server_get_stream (level) << date << ", " << msg << "\n";
}
#endif

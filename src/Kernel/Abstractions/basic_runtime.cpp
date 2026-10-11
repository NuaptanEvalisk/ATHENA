/* Runtime primitives extracted from basic.cpp.
 * Copyright (C) 1999 Joris van der Hoeven. GPL-3.0-or-later.
 */
#include "basic.hpp"
#include "string.hpp"
#include <atomic>

static std::atomic<long int> debug_status {0};

bool
debug (int which, bool write_flag) {
  if (write_flag) {
    debug_status.fetch_or (((long int) 1) << which, std::memory_order_relaxed);
    return 0;
  }
  else return (debug_status.load (std::memory_order_relaxed) &
               (((long int) 1) << which)) > 0;
}

int
debug_off () {
  return debug_status.exchange (0, std::memory_order_relaxed);
}

void
debug_on (int status) {
  debug_status.store (status, std::memory_order_relaxed);
}

static void
debug_set (int which, bool on) {
  if (on) debug_status.fetch_or (((long int) 1) << which,
                                std::memory_order_relaxed);
  else debug_status.fetch_and (~(((long int) 1) << which),
                               std::memory_order_relaxed);
}

void
debug_set (string s, bool on) {
  if (s == "auto") debug_set (DEBUG_FLAG_AUTO, on);
  else if (s == "verbose") debug_set (DEBUG_FLAG_VERBOSE, on);
  else if (s == "events") debug_set (DEBUG_FLAG_EVENTS, on);
  else if (s == "std") debug_set (DEBUG_FLAG_STD, on);
  else if (s == "io") debug_set (DEBUG_FLAG_IO, on);
  else if (s == "gnutls") debug_set (DEBUG_FLAG_GNUTLS, on);
  else if (s == "bench") debug_set (DEBUG_FLAG_BENCH, on);
  else if (s == "history") debug_set (DEBUG_FLAG_HISTORY, on);
  else if (s == "qt") debug_set (DEBUG_FLAG_QT, on);
  else if (s == "qt-widgets") debug_set (DEBUG_FLAG_QT_WIDGETS, on);
  else if (s == "keyboard") debug_set (DEBUG_FLAG_KEYBOARD, on);
  else if (s == "packrat") debug_set (DEBUG_FLAG_PACKRAT, on);
  else if (s == "flatten") debug_set (DEBUG_FLAG_FLATTEN, on);
  else if (s == "convert") debug_set (DEBUG_FLAG_CONVERT, on);
}

static bool
debug_get (int which) {
  return (debug_status.load (std::memory_order_relaxed) &
          (((long int) 1) << which)) != 0;
}

bool
debug_get (string s) {
  if (s == "auto") return debug_get (DEBUG_FLAG_AUTO);
  else if (s == "verbose") return debug_get (DEBUG_FLAG_VERBOSE);
  else if (s == "events") return debug_get (DEBUG_FLAG_EVENTS);
  else if (s == "std") return debug_get (DEBUG_FLAG_STD);
  else if (s == "io") return debug_get (DEBUG_FLAG_IO);
  else if (s == "gnutls") return debug_get (DEBUG_FLAG_GNUTLS);
  else if (s == "bench") return debug_get (DEBUG_FLAG_BENCH);
  else if (s == "history") return debug_get (DEBUG_FLAG_HISTORY);
  else if (s == "qt") return debug_get (DEBUG_FLAG_QT);
  else if (s == "qt-widgets") return debug_get (DEBUG_FLAG_QT_WIDGETS);
  else if (s == "keyboard") return debug_get (DEBUG_FLAG_KEYBOARD);
  else if (s == "packrat") return debug_get (DEBUG_FLAG_PACKRAT);
  else if (s == "flatten") return debug_get (DEBUG_FLAG_FLATTEN);
  else if (s == "convert") return debug_get (DEBUG_FLAG_CONVERT);
  else return false;
}

int
new_type_identifier () {
  static int id= 0;
  id--;
  return id;
}

static int current_indent= 0;

tm_ostream&
operator << (tm_ostream& out, display_control ctrl) {
  int i;
  switch (ctrl) {
  case INDENT:
    out << "  ";
    current_indent += 2;
    break;
  case UNINDENT:
    out << "\b\b";
    current_indent -= 2;
    break;
  case HRULE:
    for (i=current_indent; i<78; i++) out << "-";
  case LF:
    out << "\n";
    for (i=0; i<current_indent; i++) out << " ";
    break;    
  }
  return out;
}

namespace {
std::atomic<tm_exception_reporter> exception_reporter {nullptr};
}

thread_local string the_exception;

void
set_tm_exception_reporter (tm_exception_reporter reporter) {
  exception_reporter.store (reporter, std::memory_order_release);
}

void
tm_throw (const char* msg) {
  the_exception= msg;
  auto reporter= exception_reporter.load (std::memory_order_acquire);
  if (reporter) reporter (msg);
  else {
    cerr << "Throwing " << msg << LF;
    cerr.flush ();
  }
  throw string (msg);
}

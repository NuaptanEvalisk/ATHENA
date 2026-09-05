
/******************************************************************************
* MODULE     : tm_ostream.cpp
* DESCRIPTION: Output stream class
* COPYRIGHT  : (C) 2009-2013  David MICHEL, Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "tm_ostream.hpp"
#include "analyze.hpp"
#include "tree.hpp"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <csignal>
#include <string>
#include <vector>
#include <unordered_map>
#include <spdlog/logger.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#if defined (OS_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

#if defined (OS_MINGW64)
#include "Windows64/windows64_system.hpp"
#elif defined (OS_MINGW)
#include "Windows/windows32_system.hpp"
#else
#include "Unix/unix_system.hpp"
#endif

#ifndef HAVE_SNPRINTF
#warning "security issue: snprintf not available, potential buffer overflows"
#endif

namespace {

std::mutex athena_log_mutex;
std::shared_ptr<spdlog::sinks::sink> athena_log_file_sink;
std::shared_ptr<spdlog::sinks::sink> athena_stdout_sink;
std::shared_ptr<spdlog::sinks::sink> athena_stderr_sink;
int athena_logger_serial= 0;
volatile std::sig_atomic_t athena_emergency_logging= 0;

const char*
athena_log_pattern () {
  return "[%Y-%m-%d %H:%M:%S.%e] [%l] %v";
}

const char*
athena_console_log_pattern () {
  return "%^[%Y-%m-%d %H:%M:%S.%e] [%l] %v%$";
}

std::string
athena_normalize_log_line (const std::string& line) {
  const std::string athena_prefix= "ATHENA] ";
  if (line.compare (0, athena_prefix.size (), athena_prefix) == 0)
    return line.substr (athena_prefix.size ());
  if (line == "ATHENA]") return "";
  return line;
}

bool
athena_log_line_starts (const std::string& line, const std::string& prefix) {
  return line.compare (0, prefix.size (), prefix) == 0;
}

spdlog::level::level_enum
athena_level_for_log_line (spdlog::level::level_enum base,
                           const std::string& line) {
  if (athena_log_line_starts (line, "debug-"))
    return spdlog::level::debug;

  if (athena_log_line_starts (line, "WARNING:") ||
      athena_log_line_starts (line, "warning:") ||
      athena_log_line_starts (line, "std-warning,") ||
      athena_log_line_starts (line, "convert-warning,") ||
      athena_log_line_starts (line, "typeset-warning,") ||
      athena_log_line_starts (line, "io-warning,") ||
      athena_log_line_starts (line, "widkit-warning,") ||
      athena_log_line_starts (line, "bibtex-warning,"))
    return spdlog::level::warn;

  if (athena_log_line_starts (line, "ERROR:") ||
      athena_log_line_starts (line, "Error:") ||
      athena_log_line_starts (line, "std-error,") ||
      athena_log_line_starts (line, "failed-error,") ||
      athena_log_line_starts (line, "boot-error,") ||
      athena_log_line_starts (line, "qt-error,") ||
      athena_log_line_starts (line, "widkit-error,") ||
      athena_log_line_starts (line, "aqua-error,") ||
      athena_log_line_starts (line, "font-error,") ||
      athena_log_line_starts (line, "convert-error,") ||
      athena_log_line_starts (line, "bibtex-error,") ||
      athena_log_line_starts (line, "io-error,"))
    return spdlog::level::err;

  return base;
}

void
athena_log_line (const std::shared_ptr<spdlog::logger>& logger,
                 spdlog::level::level_enum base, const std::string& line) {
  std::string normalized= athena_normalize_log_line (line);
  logger->log (athena_level_for_log_line (base, normalized), "{}",
               normalized);
}

std::filesystem::path
athena_default_log_file () {
  const char* home_path= std::getenv ("ATHENA_HOME_PATH");
  if (home_path != nullptr && home_path[0] != '\0')
    return std::filesystem::path (home_path) / "system" / "log" / "ATHENA.log";

  const char* user_home= std::getenv ("HOME");
  if (user_home != nullptr && user_home[0] != '\0')
    return std::filesystem::path (user_home) / ".ATHENA" / "system" / "log" /
           "ATHENA.log";

  return std::filesystem::path ("ATHENA.log");
}

std::shared_ptr<spdlog::sinks::sink>
athena_get_default_file_sink () {
  std::lock_guard<std::mutex> lock (athena_log_mutex);
  if (athena_log_file_sink) return athena_log_file_sink;

  std::filesystem::path log_file= athena_default_log_file ();
  std::filesystem::path log_dir= log_file.parent_path ();
  if (!log_dir.empty ()) std::filesystem::create_directories (log_dir);

  athena_log_file_sink=
    std::make_shared<spdlog::sinks::basic_file_sink_mt> (log_file.string (),
                                                         false);
  athena_log_file_sink->set_pattern (athena_log_pattern ());
  return athena_log_file_sink;
}

std::shared_ptr<spdlog::sinks::sink>
athena_get_console_sink (bool error_stream) {
  std::lock_guard<std::mutex> lock (athena_log_mutex);
  std::shared_ptr<spdlog::sinks::sink>& sink=
    error_stream ? athena_stderr_sink : athena_stdout_sink;
  if (!sink) {
    if (error_stream)
      sink= std::make_shared<spdlog::sinks::stderr_color_sink_mt> ();
    else
      sink= std::make_shared<spdlog::sinks::stdout_color_sink_mt> ();
    sink->set_pattern (athena_console_log_pattern ());
  }
  return sink;
}

std::shared_ptr<spdlog::logger>
athena_make_logger (bool error_stream, bool console_sink,
                    const char* file_override) {
  try {
    std::vector<std::shared_ptr<spdlog::sinks::sink> > sinks;
    if (console_sink) sinks.push_back (athena_get_console_sink (error_stream));
    if (file_override != nullptr) {
      std::filesystem::path log_file (file_override);
      std::filesystem::path log_dir= log_file.parent_path ();
      if (!log_dir.empty ()) std::filesystem::create_directories (log_dir);
      auto sink=
        std::make_shared<spdlog::sinks::basic_file_sink_mt> (file_override,
                                                             true);
      sink->set_pattern (athena_log_pattern ());
      sinks.push_back (sink);
    }
    else {
      try {
        sinks.push_back (athena_get_default_file_sink ());
      }
      catch (const std::exception&) {
        if (!console_sink) return nullptr;
      }
    }

    std::lock_guard<std::mutex> lock (athena_log_mutex);
    std::string name= std::string ("athena-stream-") +
                      std::to_string (++athena_logger_serial);
    auto logger= std::make_shared<spdlog::logger> (name, sinks.begin (),
                                                   sinks.end ());
    logger->set_level (spdlog::level::trace);
    logger->flush_on (spdlog::level::trace);
    return logger;
  }
  catch (const std::exception&) {
    return nullptr;
  }
}

std::shared_ptr<spdlog::logger>
athena_get_worker_logger () {
  static std::shared_ptr<spdlog::logger> logger=
    athena_make_logger (false, true, nullptr);
  return logger;
}

} // namespace

void
athena_spdlog_info (const std::string& message) {
  std::shared_ptr<spdlog::logger> logger= athena_get_worker_logger ();
  if (logger) athena_log_line (logger, spdlog::level::info, message);
}

void
athena_spdlog_warning (const std::string& message) {
  std::shared_ptr<spdlog::logger> logger= athena_get_worker_logger ();
  if (logger) athena_log_line (logger, spdlog::level::warn, message);
}

void
athena_spdlog_error (const std::string& message) {
  std::shared_ptr<spdlog::logger> logger= athena_get_worker_logger ();
  if (logger) athena_log_line (logger, spdlog::level::err, message);
}

void
athena_enable_emergency_logging () {
  athena_emergency_logging= 1;
}

/******************************************************************************
* Routines for abstract base class
******************************************************************************/

tm_ostream_rep::tm_ostream_rep (): ref_count (0) {}
tm_ostream_rep::~tm_ostream_rep () {}
void tm_ostream_rep::flush () {}
void tm_ostream_rep::clear () {}
bool tm_ostream_rep::is_writable () const { return false; }
void tm_ostream_rep::write (const char*, size_t n) { (void) n; }
void tm_ostream_rep::write (tree t) { (void) t; }

/******************************************************************************
* Standard streams
******************************************************************************/

struct thread_log_line {
  std::shared_ptr<spdlog::logger> logger;
  spdlog::level::level_enum level;
  std::string text;
  ~thread_log_line () {
    if (!text.empty ()) athena_log_line (logger, level, text);
  }
};

static std::string&
thread_line (const std::shared_ptr<spdlog::logger>& logger,
             spdlog::level::level_enum level) {
  static thread_local std::unordered_map<spdlog::logger*, thread_log_line> lines;
  auto& entry= lines[logger.get ()];
  if (!entry.logger) {
    entry.logger= logger;
    entry.level= level;
  }
  return entry.text;
}

class std_ostream_rep: public tm_ostream_rep {
  FILE *file;
  bool is_w;
  bool is_mine;
  bool raw_file;
  bool error_stream;
  std::shared_ptr<spdlog::logger> logger;
  spdlog::level::level_enum level;

public:
  std_ostream_rep ();
  std_ostream_rep (char*);
  std_ostream_rep (FILE*);
  ~std_ostream_rep ();

  bool is_writable () const;
  void write (const char* s, size_t n);
  void flush ();
};

std_ostream_rep::std_ostream_rep ():
  file (0), is_w (false), is_mine (false), raw_file (false),
  error_stream (false),
  logger (), level (spdlog::level::info)
{
  logger= athena_make_logger (false, true, nullptr);
  is_w= logger != nullptr;
}

std_ostream_rep::std_ostream_rep (char* fn):
  file (0), is_w (false), is_mine (false), raw_file (false),
  error_stream (false),
  logger (), level (spdlog::level::info)
{
  logger= athena_make_logger (false, false, fn);
  is_w= logger != nullptr;
}

std_ostream_rep::std_ostream_rep (FILE* f) :
  file (0), is_w (false), is_mine (false), raw_file (false),
  error_stream (f == stderr),
  logger (), level (spdlog::level::info)
{
  if (f == stdout || f == stderr) {
    level= (f == stderr) ? spdlog::level::err : spdlog::level::info;
    logger= athena_make_logger (f == stderr, true, nullptr);
    is_w= logger != nullptr;
  }
  else {
    file= f;
    raw_file= true;
    is_w= file != nullptr;
  }
}

std_ostream_rep::~std_ostream_rep () {
  // Standard streams can outlive the main thread's TLS buffers.
  if (raw_file && file) fflush (file);
  else if (logger) logger->flush ();
  if (raw_file && file && is_mine) fclose (file);
}

bool
std_ostream_rep::is_writable () const {
  return is_w;
}

void
std_ostream_rep::write (const char* s, size_t n) {
  if (!is_w) {
    return;
  }
  if (n == 0) {
    return;
  }
  if (athena_emergency_logging && !raw_file) {
#if defined (OS_WIN32)
    int fd= error_stream ? 2 : 1;
    (void) _write (fd, s, (unsigned int) n);
#else
    int fd= error_stream ? STDERR_FILENO : STDOUT_FILENO;
    (void) ::write (fd, s, n);
#endif
    return;
  }
  if (raw_file) {
    if (!file) return;
    ssize_t written= texmacs_fwrite(s, n, file);
    if (written < 0 || ((size_t) written) != n) {
      is_w = false;
      return;
    }
    const char* c= s;
    while (*c != 0 && *c != '\n') ++c;
    if (*c == '\n') flush ();
    return;
  }
  if (!logger) return;

  std::string& line= thread_line (logger, level);
  size_t start= 0;
  for (size_t i=0; i<n; ++i) {
    if (s[i] == '\n') {
      line.append (s + start, i - start);
      athena_log_line (logger, level, line);
      line.clear ();
      start= i + 1;
    }
  }
  if (start < n) line.append (s + start, n - start);
}

void
std_ostream_rep::flush () {
  if (!is_w) return;
  if (athena_emergency_logging && !raw_file) return;
  if (raw_file) {
    if (file) fflush (file);
    return;
  }
  if (!logger) return;
  std::string& line= thread_line (logger, level);
  if (!line.empty ()) {
    athena_log_line (logger, level, line);
    line.clear ();
  }
  logger->flush ();
}

/******************************************************************************
* String streams
******************************************************************************/

class string_ostream_rep: public tm_ostream_rep {
public:
  string* buf;

public:
  string_ostream_rep (string* buf);
  ~string_ostream_rep ();

  bool is_writable () const;
  void write (const char* s, size_t n);
};

string_ostream_rep::string_ostream_rep (string* buf2): buf (buf2) {}
string_ostream_rep::~string_ostream_rep () {}
bool string_ostream_rep::is_writable () const { return true; }
void string_ostream_rep::write (const char* s, size_t n) { (*buf) << string (s,n); }


tm_ostream
string_ostream (string& buf) {
  return (tm_ostream_rep*) tm_new<string_ostream_rep> (&buf);
}

/******************************************************************************
* Buffered streams
******************************************************************************/

class buffered_ostream_rep: public tm_ostream_rep {
public:
  tm_ostream_rep* master;
  string buf;

public:
  buffered_ostream_rep (tm_ostream_rep* master);
  ~buffered_ostream_rep ();

  bool is_writable () const;
  void write (const char* s, size_t n);
};

buffered_ostream_rep::buffered_ostream_rep (tm_ostream_rep* master2):
  master (master2) {}

buffered_ostream_rep::~buffered_ostream_rep () {}

bool
buffered_ostream_rep::is_writable () const {
  return true;
}

void
buffered_ostream_rep::write (const char* s, size_t n) {
  buf << string (s, n);
}

/******************************************************************************
* Streams for debugging purposes
******************************************************************************/

class debug_ostream_rep: public tm_ostream_rep {
public:
  string channel;
  string pending;

public:
  debug_ostream_rep (string channel);
  ~debug_ostream_rep ();

  bool is_writable () const;
  void write (const char* s, size_t n);
  void write (tree t);
  void flush ();
  void clear ();
};

debug_ostream_rep::debug_ostream_rep (string channel2):
  channel (channel2), pending ("") {}
debug_ostream_rep::~debug_ostream_rep () { flush (); }

bool
debug_ostream_rep::is_writable () const {
  return true;
}

void
debug_ostream_rep::clear () {
  pending= "";
  clear_debug_messages (channel);
}

void
debug_ostream_rep::write (const char* s, size_t n) {
  pending << string (s, n);
  while (occurs ("\n", pending)) {
    int pos= search_forwards ("\n", 0, pending);
    debug_message (channel, pending (0, pos+1));
    pending= pending (pos+1, N(pending));
  }
}

void
debug_ostream_rep::write (tree t) {
  flush ();
  debug_formatted (channel, t);
}

void
debug_ostream_rep::flush () {
  if (pending == "") return;
  debug_message (channel, pending);
  pending= "";
}

tm_ostream
debug_ostream (string channel) {
  return (tm_ostream_rep*) tm_new<debug_ostream_rep> (channel);
}

/******************************************************************************
* Abstract user interface
******************************************************************************/

tm_ostream::tm_ostream ():
  rep (tm_new<std_ostream_rep> ()) { INC_COUNT (this->rep); }
tm_ostream::tm_ostream (char* s):
  rep (tm_new<std_ostream_rep> (s)) { INC_COUNT (this->rep); }
tm_ostream::tm_ostream (FILE* f):
  rep (tm_new<std_ostream_rep> (f)) { INC_COUNT (this->rep); }
tm_ostream::tm_ostream (const tm_ostream& x):
  rep(x.rep) { INC_COUNT (this->rep); }
tm_ostream::tm_ostream (tm_ostream_rep* rep2): rep(rep2) {
  INC_COUNT (this->rep); }
tm_ostream::~tm_ostream () {
  DEC_COUNT (this->rep); }
tm_ostream_rep* tm_ostream::operator -> () {
  return rep; }
tm_ostream& tm_ostream::operator = (const tm_ostream& x) {
  if (this->rep != x.rep) {
    INC_COUNT (x.rep); DEC_COUNT (this->rep);
    this->rep=x.rep;
  }
  return *this; }
bool tm_ostream::operator == (tm_ostream& out) {
  return (&out == this); }

void
tm_ostream::clear () {
  rep->clear ();
}

void
tm_ostream::flush () {
  rep->flush ();
}

void
tm_ostream::buffer () {
  rep= tm_new<buffered_ostream_rep> (rep);
}

string
tm_ostream::unbuffer () {
  buffered_ostream_rep* ptr= (buffered_ostream_rep*) rep;
  rep= ptr->master;
  string r= ptr->buf;
  tm_delete<buffered_ostream_rep> (ptr);
  return r;
}

void
tm_ostream::redirect (tm_ostream x) {
  INC_COUNT (x.rep);
  DEC_COUNT (this->rep);
  this->rep= x.rep;
}

/******************************************************************************
* Print methods for standard types
******************************************************************************/

tm_ostream&
tm_ostream::operator << (bool b) {
  if (b) rep->write ("true");
  else rep->write ("false");
  return *this;
}

tm_ostream&
tm_ostream::operator << (char c) {
  static char _buf[8];
#ifdef HAVE_SNPRINTF
  int n = snprintf (_buf, 8, "%c", c);
#else
  int n = sprintf (_buf, "%c", c);
#endif
  rep->write (_buf, n);
  return *this;
}

tm_ostream&
tm_ostream::operator << (short sh) {
  static char _buf[32];
#ifdef HAVE_SNPRINTF
  int n = snprintf (_buf, 32, "%hd", sh);
#else
  int n = sprintf (_buf, "%hd", sh);
#endif
  rep->write (_buf, n);
  return *this;
}

tm_ostream&
tm_ostream::operator << (unsigned short ush) {
  static char _buf[32];
#ifdef HAVE_SNPRINTF
  int n = snprintf (_buf, 32, "%hu", ush);
#else
  int n = sprintf (_buf, "%hu", ush);
#endif
  rep->write (_buf, n);
  return *this;
}

tm_ostream&
tm_ostream::operator << (int i) {
  static char _buf[64];
#ifdef HAVE_SNPRINTF
  int n = snprintf (_buf, 64, "%d", i);
#else
  int n = sprintf (_buf, "%d", i);
#endif
  rep->write (_buf, n);
  return *this;
}

tm_ostream&
tm_ostream::operator << (unsigned int ui) {
  static char _buf[64];
#ifdef HAVE_SNPRINTF
  int n = snprintf (_buf, 64, "%u", ui);
#else
  int n = sprintf (_buf, "%u", ui);
#endif
  rep->write (_buf, n);
  return *this;
}

tm_ostream&
tm_ostream::operator << (long l) {
  static char _buf[64];
#ifdef HAVE_SNPRINTF
  int n = snprintf (_buf, 64, "%ld", l);
#else
  int n = sprintf (_buf, "%ld", l);
#endif
  rep->write (_buf, n);
  return *this;
}

tm_ostream&
tm_ostream::operator << (unsigned long ul) {
  static char _buf[64];
#ifdef HAVE_SNPRINTF
  int n = snprintf (_buf, 64, "%lu", ul);
#else
  int n = sprintf (_buf, "%lu", ul);
#endif
  rep->write (_buf, n);
  return *this;
}

tm_ostream&
tm_ostream::operator << (long long ll) {
  static char _buf[64];
#ifdef HAVE_SNPRINTF
  int n = snprintf (_buf, 64, "%lld", ll);
#else
  int n = sprintf (_buf, "%lld", ll);
#endif
  rep->write (_buf, n);
  return *this;
}

tm_ostream&
tm_ostream::operator << (unsigned long long ull) {
  static char _buf[64];
#ifdef HAVE_SNPRINTF
  int n = snprintf (_buf, 64, "%llu", ull);
#else
  int n = sprintf (_buf, "%llu", ull);
#endif
  rep->write (_buf, n);
  return *this;
}

tm_ostream&
tm_ostream::operator << (float f) {
  static char _buf[32];
#ifdef HAVE_SNPRINTF
  int n = snprintf (_buf, 32, "%g", f);
#else
  int n = sprintf (_buf, "%g", f);
#endif
  rep->write (_buf, n);
  return *this;
}

tm_ostream&
tm_ostream::operator << (double d) {
  static char _buf[64];
#ifdef HAVE_SNPRINTF
  int n = snprintf (_buf, 64, "%g", d);
#else
  int n = sprintf (_buf, "%g", d);
#endif
  rep->write (_buf, n);
  return *this;
}

tm_ostream&
tm_ostream::operator << (long double ld) {
  static char _buf[128];
#ifdef HAVE_SNPRINTF
  int n = snprintf (_buf, 128, "%Lg", ld);
#else
  int n = sprintf (_buf, "%Lg", ld);
#endif
  rep->write (_buf, n);
  return *this;
}

tm_ostream&
tm_ostream::operator << (formatted f) {
  rep->write (f.rep);
  return *this;
}

/******************************************************************************
* Standard output streams
******************************************************************************/

tm_ostream  tm_ostream::private_cout (stdout);
tm_ostream  tm_ostream::private_cerr (stderr);

tm_ostream& cout= tm_ostream::private_cout;
tm_ostream& cerr= tm_ostream::private_cerr;

tm_ostream std_error       = debug_ostream ("std-error");
tm_ostream failed_error    = debug_ostream ("failed-error");
tm_ostream boot_error      = debug_ostream ("boot-error");
tm_ostream qt_error        = debug_ostream ("qt-error");
tm_ostream widkit_error    = debug_ostream ("widkit-error");
tm_ostream aqua_error      = debug_ostream ("aqua-error");
tm_ostream font_error      = debug_ostream ("font-error");
tm_ostream convert_error   = debug_ostream ("convert-error");
tm_ostream bibtex_error    = debug_ostream ("bibtex-error");
tm_ostream io_error        = debug_ostream ("io-error");

tm_ostream std_warning     = debug_ostream ("std-warning");
tm_ostream convert_warning = debug_ostream ("convert-warning");
tm_ostream typeset_warning = debug_ostream ("typeset-warning");
tm_ostream io_warning      = debug_ostream ("io-warning");
tm_ostream widkit_warning  = debug_ostream ("widkit-warning");
tm_ostream bibtex_warning  = debug_ostream ("bibtex-warning");

tm_ostream io_emergency     = debug_ostream ("io-emergency");
tm_ostream io_alert         = debug_ostream ("io-alert");
tm_ostream io_critical      = debug_ostream ("io-critical");
tm_ostream io_notice        = debug_ostream ("io-notice");
tm_ostream io_info          = debug_ostream ("io-info");

tm_ostream debug_std       = debug_ostream ("debug-std");
tm_ostream debug_qt        = debug_ostream ("debug-qt");
tm_ostream debug_aqua      = debug_ostream ("debug-aqua");
tm_ostream debug_widgets   = debug_ostream ("debug-widgets");
tm_ostream debug_fonts     = debug_ostream ("debug-fonts");
tm_ostream debug_convert   = debug_ostream ("debug-convert");
tm_ostream debug_typeset   = debug_ostream ("debug-typeset");
tm_ostream debug_edit      = debug_ostream ("debug-edit");
tm_ostream debug_packrat   = debug_ostream ("debug-packrat");
tm_ostream debug_history   = debug_ostream ("debug-history");
tm_ostream debug_keyboard  = debug_ostream ("debug-keyboard");
tm_ostream debug_automatic = debug_ostream ("debug-automatic");
tm_ostream debug_boot      = debug_ostream ("debug-boot");
tm_ostream debug_events    = debug_ostream ("debug-events");
tm_ostream debug_shell     = debug_ostream ("debug-shell");
tm_ostream debug_io        = debug_ostream ("debug-io");
tm_ostream debug_spell     = debug_ostream ("debug-spell");
tm_ostream debug_updater   = debug_ostream ("debug-updater");

tm_ostream std_bench       = debug_ostream ("std-bench");

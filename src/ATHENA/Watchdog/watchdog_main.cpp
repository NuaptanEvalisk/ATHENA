#include "watchdog_protocol.hpp"

#if !defined(__linux__)
#include <cstdio>
int main () {
  std::fputs ("ATHENA-Watchdog is currently supported on Linux only.\n", stderr);
  return 2;
}
#else

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <poll.h>
#include <spawn.h>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <vector>

extern char** environ;
namespace fs= std::filesystem;
using Clock= std::chrono::steady_clock;
using Milliseconds= std::chrono::milliseconds;

namespace {

struct Options {
  int heartbeat_timeout_ms= 3000;
  int gdb_timeout_ms= 15000;
  std::vector<int> capture_schedule_ms {0, 3000, 12000};
  bool use_gdb= true;
  std::string gdb_path= "gdb";
  fs::path output_dir;
  std::vector<std::string> child;
};

struct Incident {
  fs::path directory;
  Clock::time_point detected_at;
  std::uint64_t last_sequence= 0;
  std::size_t next_capture= 0;
  std::uint64_t number= 0;
};

struct DrainResult {
  bool received= false;
  bool ready= false;
  bool heartbeat= false;
  bool shutdown= false;
  bool restart= false;
  bool peer_closed= false;
  athena_watchdog::Packet latest;
};

volatile sig_atomic_t forwarded_signal= 0;

void signal_handler (int signal_number) {
  forwarded_signal= signal_number;
}

std::string json_escape (std::string_view value) {
  std::ostringstream out;
  for (unsigned char c: value) {
    switch (c) {
      case '\\': out << "\\\\"; break;
      case '"': out << "\\\""; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default:
        if (c < 0x20) {
          out << "\\u" << std::hex << std::setw (4) << std::setfill ('0')
              << static_cast<int> (c) << std::dec;
        }
        else out << static_cast<char> (c);
    }
  }
  return out.str ();
}

std::uint64_t monotonic_ns () {
  struct timespec now {};
  if (clock_gettime (CLOCK_MONOTONIC, &now) != 0) return 0;
  return static_cast<std::uint64_t> (now.tv_sec) * 1000000000ULL +
         static_cast<std::uint64_t> (now.tv_nsec);
}

std::string wall_timestamp () {
  std::time_t now= std::time (nullptr);
  struct tm tm {};
  localtime_r (&now, &tm);
  char buffer[32];
  std::strftime (buffer, sizeof (buffer), "%Y%m%d-%H%M%S", &tm);
  return buffer;
}

bool parse_positive_int (std::string_view text, int& result) {
  if (text.empty ()) return false;
  std::string storage (text);
  char* end= nullptr;
  errno= 0;
  long value= std::strtol (storage.c_str (), &end, 10);
  if (errno != 0 || end == storage.c_str () || *end != '\0' || value <= 0 ||
      value > 3600000)
    return false;
  result= static_cast<int> (value);
  return true;
}

std::vector<int> parse_schedule (std::string_view text) {
  std::vector<int> schedule;
  std::size_t start= 0;
  while (start <= text.size ()) {
    std::size_t comma= text.find (',', start);
    std::string part (text.substr (start, comma == std::string_view::npos
                                           ? text.size () - start
                                           : comma - start));
    if (part.empty ()) return {};
    char* end= nullptr;
    errno= 0;
    long value= std::strtol (part.c_str (), &end, 10);
    if (errno != 0 || end == part.c_str () || *end != '\0' || value < 0 ||
        value > 3600000)
      return {};
    schedule.push_back (static_cast<int> (value));
    if (comma == std::string_view::npos) break;
    start= comma + 1;
  }
  std::sort (schedule.begin (), schedule.end ());
  schedule.erase (std::unique (schedule.begin (), schedule.end ()), schedule.end ());
  return schedule;
}

fs::path default_output_dir () {
  const char* configured= std::getenv ("ATHENA_HOME_PATH");
  if (configured != nullptr && *configured != '\0')
    return fs::path (configured) / "system" / "hangs";
  const char* home= std::getenv ("HOME");
  return fs::path (home != nullptr && *home != '\0' ? home : "/tmp") /
         ".ATHENA" / "system" / "hangs";
}

void usage () {
  std::cerr
    << "usage: ATHENA-Watchdog [options] -- ATHENA.bin [arguments...]\n"
    << "  --heartbeat-timeout-ms=N  runtime stall threshold (default 3000)\n"
    << "  --capture-schedule-ms=L   offsets after detection (default 0,3000,12000)\n"
    << "  --gdb-timeout-ms=N        per-capture GDB limit (default 15000)\n"
    << "  --gdb-path=PATH            debugger executable (default gdb)\n"
    << "  --output-dir=PATH          incident root directory\n"
    << "  --no-gdb                   capture /proc only\n";
}

std::optional<Options> parse_options (int argc, char** argv) {
  Options options;
  options.output_dir= default_output_dir ();
  int i= 1;
  for (; i < argc; ++i) {
    std::string arg= argv[i];
    if (arg == "--") { ++i; break; }
    if (arg == "--no-gdb") { options.use_gdb= false; continue; }
    auto read_value= [&] (std::string_view prefix) -> std::optional<std::string> {
      if (arg.rfind (prefix, 0) != 0) return std::nullopt;
      return arg.substr (prefix.size ());
    };
    if (auto value= read_value ("--heartbeat-timeout-ms=")) {
      if (!parse_positive_int (*value, options.heartbeat_timeout_ms)) return std::nullopt;
      continue;
    }
    if (auto value= read_value ("--gdb-timeout-ms=")) {
      if (!parse_positive_int (*value, options.gdb_timeout_ms)) return std::nullopt;
      continue;
    }
    if (auto value= read_value ("--capture-schedule-ms=")) {
      options.capture_schedule_ms= parse_schedule (*value);
      if (options.capture_schedule_ms.empty ()) return std::nullopt;
      continue;
    }
    if (auto value= read_value ("--gdb-path=")) {
      if (value->empty ()) return std::nullopt;
      options.gdb_path= *value;
      continue;
    }
    if (auto value= read_value ("--output-dir=")) {
      if (value->empty ()) return std::nullopt;
      options.output_dir= *value;
      continue;
    }
    return std::nullopt;
  }
  for (; i < argc; ++i) options.child.emplace_back (argv[i]);
  if (options.child.empty ()) return std::nullopt;
  return options;
}

bool set_nonblocking (int fd) {
  int flags= fcntl (fd, F_GETFL, 0);
  return flags >= 0 && fcntl (fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

int duplicate_high (int fd) {
  return fcntl (fd, F_DUPFD_CLOEXEC, 10);
}

pid_t spawn_child (const Options& options, int child_socket, int supervisor_socket) {
  posix_spawn_file_actions_t actions;
  if (posix_spawn_file_actions_init (&actions) != 0) return -1;
  bool actions_ok=
    posix_spawn_file_actions_adddup2 (&actions, child_socket,
                                      athena_watchdog::child_fd) == 0 &&
    posix_spawn_file_actions_addclose (&actions, child_socket) == 0 &&
    posix_spawn_file_actions_addclose (&actions, supervisor_socket) == 0;
  if (!actions_ok) {
    posix_spawn_file_actions_destroy (&actions);
    return -1;
  }

  std::vector<std::string> arguments;
  arguments.reserve (options.child.size () + 1);
  arguments.push_back (options.child.front ());
  arguments.push_back ("--watchdog-fd=" +
                       std::to_string (athena_watchdog::child_fd));
  arguments.insert (arguments.end (), options.child.begin () + 1,
                    options.child.end ());
  std::vector<char*> argv;
  argv.reserve (arguments.size () + 1);
  for (std::string& argument: arguments) argv.push_back (argument.data ());
  argv.push_back (nullptr);

  pid_t pid= -1;
  int error= posix_spawnp (&pid, argv[0], &actions, nullptr, argv.data (), environ);
  posix_spawn_file_actions_destroy (&actions);
  if (error != 0) {
    errno= error;
    return -1;
  }
  return pid;
}

int open_pidfd (pid_t pid) {
#ifdef SYS_pidfd_open
  return static_cast<int> (syscall (SYS_pidfd_open, pid, 0));
#else
  (void) pid;
  return -1;
#endif
}

bool read_file (const fs::path& path, std::ostream& out) {
  std::ifstream input (path, std::ios::binary);
  if (!input) return false;
  out << input.rdbuf () << '\n';
  return true;
}

void snapshot_one (std::ostream& out, const fs::path& path,
                   std::string_view title) {
  out << "===== " << title << " =====\n";
  if (!read_file (path, out)) out << "<unavailable>\n";
}

void capture_proc (pid_t pid, const fs::path& destination) {
  std::ofstream out (destination);
  if (!out) return;
  fs::path root= fs::path ("/proc") / std::to_string (pid);
  snapshot_one (out, root / "status", "/proc/PID/status");
  snapshot_one (out, root / "cmdline", "/proc/PID/cmdline");
  snapshot_one (out, root / "maps", "/proc/PID/maps");

  std::error_code ec;
  fs::path tasks= root / "task";
  std::vector<fs::path> tids;
  for (fs::directory_iterator it (tasks, ec), end; !ec && it != end;
       it.increment (ec))
    if (it->is_directory (ec)) tids.push_back (it->path ());
  std::sort (tids.begin (), tids.end ());
  for (const fs::path& tid: tids) {
    out << "\n######## TID " << tid.filename ().string () << " ########\n";
    snapshot_one (out, tid / "status", "status");
    snapshot_one (out, tid / "stat", "stat");
    snapshot_one (out, tid / "wchan", "wchan");
    snapshot_one (out, tid / "syscall", "syscall");
  }
}

int tracer_pid (pid_t pid) {
  std::ifstream input (fs::path ("/proc") / std::to_string (pid) / "status");
  std::string line;
  while (std::getline (input, line)) {
    if (line.rfind ("TracerPid:", 0) != 0) continue;
    return std::atoi (line.c_str () + std::strlen ("TracerPid:"));
  }
  return 0;
}

void wait_gdb_with_timeout (pid_t pid, int timeout_ms) {
  Clock::time_point deadline= Clock::now () + Milliseconds (timeout_ms);
  for (;;) {
    int status= 0;
    pid_t result= waitpid (pid, &status, WNOHANG);
    if (result == pid || (result < 0 && errno != EINTR)) return;
    if (Clock::now () >= deadline) {
      kill (-pid, SIGKILL);
      kill (pid, SIGKILL);
      while (waitpid (pid, &status, 0) < 0 && errno == EINTR) {}
      return;
    }
    std::this_thread::sleep_for (Milliseconds (20));
  }
}

void run_gdb (const Options& options, pid_t child_pid,
              std::size_t capture_index, const fs::path& destination) {
  int existing_tracer= tracer_pid (child_pid);
  if (existing_tracer != 0) {
    std::ofstream note (destination);
    note << "Skipped GDB: TracerPid=" << existing_tracer << '\n';
    return;
  }

  pid_t pid= fork ();
  if (pid < 0) {
    std::ofstream note (destination);
    note << "Could not fork GDB: " << std::strerror (errno) << '\n';
    return;
  }
  if (pid == 0) {
    setpgid (0, 0);
    int fd= open (destination.c_str (), O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC,
                  0600);
    if (fd >= 0) {
      dup2 (fd, STDOUT_FILENO);
      dup2 (fd, STDERR_FILENO);
      if (fd > STDERR_FILENO) close (fd);
    }
    std::string pid_text= std::to_string (child_pid);
    const char* trace_command= capture_index == 0
      ? "thread apply all bt 80"
      : "thread apply all bt full 40";
    execlp (options.gdb_path.c_str (), options.gdb_path.c_str (),
            "-q", "-nx", "-batch",
            "-iex", "set debuginfod enabled off",
            "-ex", "set pagination off",
            "-ex", "set confirm off",
            "-ex", "set print elements 200",
            "-ex", "set print repeats 20",
            "-ex", "handle SIGPWR nostop noprint pass",
            "-ex", "handle SIGXCPU nostop noprint pass",
            "-ex", "handle SIGPIPE nostop noprint pass",
            "-ex", "info threads",
            "-ex", trace_command,
            "-p", pid_text.c_str (), static_cast<char*> (nullptr));
    dprintf (STDERR_FILENO, "exec %s failed: %s\n",
             options.gdb_path.c_str (), std::strerror (errno));
    _exit (127);
  }
  wait_gdb_with_timeout (pid, options.gdb_timeout_ms);
}

void write_meta (const Options& options, pid_t pid, const Incident& incident,
                 std::uint64_t watchdog_gap_ms) {
  std::ofstream out (incident.directory / "meta.json");
  if (!out) return;
  out << "{\n"
      << "  \"format\": \"athena-hang-report\",\n"
      << "  \"version\": 1,\n"
      << "  \"pid\": " << pid << ",\n"
      << "  \"detected_monotonic_ns\": " << monotonic_ns () << ",\n"
      << "  \"last_heartbeat_sequence\": " << incident.last_sequence << ",\n"
      << "  \"heartbeat_timeout_ms\": " << options.heartbeat_timeout_ms << ",\n"
      << "  \"watchdog_scheduling_gap_ms\": " << watchdog_gap_ms << ",\n"
      << "  \"executable\": \"" << json_escape (options.child.front ()) << "\"\n"
      << "}\n";
}

fs::path make_incident_directory (const Options& options, pid_t pid,
                                  std::uint64_t number) {
  std::error_code ec;
  fs::create_directories (options.output_dir, ec);
  fs::permissions (options.output_dir, fs::perms::owner_all,
                   fs::perm_options::replace, ec);
  fs::path directory= options.output_dir /
    (wall_timestamp () + "-pid-" + std::to_string (pid) + "-" +
     std::to_string (number));
  fs::create_directories (directory, ec);
  fs::permissions (directory, fs::perms::owner_all,
                   fs::perm_options::replace, ec);
  return directory;
}

void capture_incident (const Options& options, pid_t pid, Incident& incident) {
  std::size_t index= incident.next_capture;
  char suffix[16];
  std::snprintf (suffix, sizeof (suffix), "%03zu", index);
  capture_proc (pid, incident.directory / (std::string ("proc-") + suffix + ".txt"));
  if (options.use_gdb)
    run_gdb (options, pid, index,
             incident.directory / (std::string ("gdb-") + suffix + ".txt"));
  ++incident.next_capture;
}

void write_recovery (const Incident& incident, Clock::time_point now,
                     std::uint64_t sequence) {
  std::ofstream out (incident.directory / "recovered.json");
  auto duration= std::chrono::duration_cast<Milliseconds> (now - incident.detected_at);
  out << "{\n  \"recovered_after_detection_ms\": " << duration.count ()
      << ",\n  \"heartbeat_sequence\": " << sequence << "\n}\n";
}

void write_termination (const Incident& incident, int status) {
  std::ofstream out (incident.directory / "terminated.json");
  out << "{\n  \"wait_status\": " << status << "\n}\n";
}

bool valid_packet (const athena_watchdog::Packet& packet) {
  return packet.magic == athena_watchdog::packet_magic &&
         packet.version == athena_watchdog::packet_version;
}

DrainResult drain_packets (int fd) {
  DrainResult result;
  for (;;) {
    athena_watchdog::Packet packet;
    ssize_t count= recv (fd, &packet, sizeof (packet), MSG_DONTWAIT);
    if (count == 0) {
      result.peer_closed= true;
      return result;
    }
    if (count < 0) {
      if (errno == EINTR) continue;
      if (errno != EAGAIN && errno != EWOULDBLOCK)
        result.peer_closed= true;
      return result;
    }
    if (count != static_cast<ssize_t> (sizeof (packet)) || !valid_packet (packet))
      continue;
    result.received= true;
    result.latest= packet;
    switch (static_cast<athena_watchdog::PacketType> (packet.type)) {
      case athena_watchdog::PacketType::Ready: result.ready= true; break;
      case athena_watchdog::PacketType::Heartbeat: result.heartbeat= true; break;
      case athena_watchdog::PacketType::Shutdown: result.shutdown= true; break;
      case athena_watchdog::PacketType::Restart: result.restart= true; break;
    }
  }
}

int child_exit_code (int status) {
  if (WIFEXITED (status)) return WEXITSTATUS (status);
  if (WIFSIGNALED (status)) return 128 + WTERMSIG (status);
  return 1;
}

} // namespace

static int supervise_child (const Options& options, bool& restart_requested) {
  int pair[2]= {-1, -1};
  if (socketpair (AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) != 0) {
    std::cerr << "ATHENA-Watchdog: socketpair failed: " << std::strerror (errno) << '\n';
    return 2;
  }
  int supervisor_fd= duplicate_high (pair[0]);
  int child_socket= duplicate_high (pair[1]);
  close (pair[0]); close (pair[1]);
  if (supervisor_fd < 0 || child_socket < 0 || !set_nonblocking (supervisor_fd)) {
    std::cerr << "ATHENA-Watchdog: could not prepare IPC channel\n";
    if (supervisor_fd >= 0) close (supervisor_fd);
    if (child_socket >= 0) close (child_socket);
    return 2;
  }

  pid_t child_pid= spawn_child (options, child_socket, supervisor_fd);
  close (child_socket);
  if (child_pid < 0) {
    std::cerr << "ATHENA-Watchdog: could not start " << options.child.front ()
              << ": " << std::strerror (errno) << '\n';
    close (supervisor_fd);
    return 2;
  }
  int pidfd= open_pidfd (child_pid);

  bool ready= false;
  bool shutdown_seen= false;
  bool stopping= false;
  std::uint64_t last_sequence= 0;
  Clock::time_point last_seen= Clock::now ();
  Clock::time_point last_loop= last_seen;
  std::uint64_t incident_number= 0;
  std::optional<Incident> incident;
  int child_status= 0;
  bool child_exited= false;

  while (!child_exited) {
    if (forwarded_signal != 0) {
      int signal_number= forwarded_signal;
      forwarded_signal= 0;
      stopping= true;
      restart_requested= false;
      kill (child_pid, signal_number);
    }

    struct pollfd descriptors[2] {};
    int descriptor_count= 0;
    if (supervisor_fd >= 0) {
      descriptors[descriptor_count].fd= supervisor_fd;
      descriptors[descriptor_count].events= POLLIN | POLLHUP | POLLERR;
      ++descriptor_count;
    }
    if (pidfd >= 0) {
      descriptors[descriptor_count].fd= pidfd;
      descriptors[descriptor_count].events= POLLIN | POLLHUP | POLLERR;
      ++descriptor_count;
    }
    int poll_result= poll (descriptor_count ? descriptors : nullptr,
                           descriptor_count, 100);
    if (poll_result < 0 && errno != EINTR)
      std::cerr << "ATHENA-Watchdog: poll failed: " << std::strerror (errno) << '\n';

    Clock::time_point now= Clock::now ();
    auto watchdog_gap= std::chrono::duration_cast<Milliseconds> (now - last_loop);
    last_loop= now;

    DrainResult drained;
    if (supervisor_fd >= 0) drained= drain_packets (supervisor_fd);
    if (drained.received) {
      last_sequence= drained.latest.sequence;
      last_seen= now;
      ready= ready || drained.ready;
      restart_requested= !stopping && (restart_requested || drained.restart);
      shutdown_seen= shutdown_seen || drained.shutdown || drained.restart;
      if (incident && (drained.heartbeat || drained.ready)) {
        std::cerr << "ATHENA-Watchdog: ATHENA recovered after stall; report: "
                  << incident->directory << '\n';
        write_recovery (*incident, now, last_sequence);
        incident.reset ();
      }
    }
    if (drained.peer_closed && supervisor_fd >= 0) {
      close (supervisor_fd);
      supervisor_fd= -1;
    }

    pid_t wait_result= waitpid (child_pid, &child_status, WNOHANG);
    if (wait_result == child_pid) child_exited= true;
    else if (wait_result < 0 && errno == ECHILD) child_exited= true;
    if (child_exited) {
      // The child may send Restart and exit between the drain and waitpid.
      if (supervisor_fd >= 0 && !stopping)
        restart_requested= restart_requested || drain_packets (supervisor_fd).restart;
      break;
    }

    if (ready && !shutdown_seen && !incident &&
        now - last_seen >= Milliseconds (options.heartbeat_timeout_ms)) {
      Incident next;
      next.detected_at= now;
      next.last_sequence= last_sequence;
      next.number= ++incident_number;
      next.directory= make_incident_directory (options, child_pid, next.number);
      write_meta (options, child_pid, next,
                  static_cast<std::uint64_t> (std::max<std::int64_t> (
                    0, static_cast<std::int64_t> (watchdog_gap.count ()))));
      std::cerr << "ATHENA-Watchdog: no UI heartbeat for "
                << options.heartbeat_timeout_ms << " ms; report: "
                << next.directory << '\n';
      incident= std::move (next);
    }

    if (incident && incident->next_capture < options.capture_schedule_ms.size ()) {
      int offset= options.capture_schedule_ms[incident->next_capture];
      if (now - incident->detected_at >= Milliseconds (offset))
        capture_incident (options, child_pid, *incident);
    }
  }

  if (incident) write_termination (*incident, child_status);
  if (pidfd >= 0) close (pidfd);
  if (supervisor_fd >= 0) close (supervisor_fd);
  return child_exit_code (child_status);
}

int main (int argc, char** argv) {
  umask (077);
  auto parsed= parse_options (argc, argv);
  if (!parsed) { usage (); return 2; }
  Options options= std::move (*parsed);

  struct sigaction action {};
  action.sa_handler= signal_handler;
  sigemptyset (&action.sa_mask);
  for (int signal_number: {SIGTERM, SIGINT, SIGHUP, SIGQUIT})
    sigaction (signal_number, &action, nullptr);

  for (;;) {
    bool restart_requested= false;
    int status= supervise_child (options, restart_requested);
    if (!restart_requested || status != 0 || forwarded_signal != 0) return status;
    std::cerr << "ATHENA-Watchdog: restarting ATHENA\n";
  }
}

#endif

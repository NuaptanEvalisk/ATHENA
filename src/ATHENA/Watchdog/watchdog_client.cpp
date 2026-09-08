#include "watchdog_client.hpp"
#include "watchdog_protocol.hpp"

#if defined(__linux__)

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <QCoreApplication>
#include <QTimer>

namespace {

int watchdog_fd= -1;
bool watchdog_configured= false;
std::uint64_t watchdog_sequence= 0;
QTimer* heartbeat_timer= nullptr;

std::uint64_t
monotonic_ns () noexcept {
  struct timespec now {};
  if (clock_gettime (CLOCK_MONOTONIC, &now) != 0) return 0;
  return static_cast<std::uint64_t> (now.tv_sec) * 1000000000ULL +
         static_cast<std::uint64_t> (now.tv_nsec);
}

bool
send_packet (athena_watchdog::PacketType type,
             athena_watchdog::Phase phase) noexcept {
  if (watchdog_fd < 0) return false;
  athena_watchdog::Packet packet;
  packet.type= static_cast<std::uint16_t> (type);
  packet.phase= static_cast<std::uint32_t> (phase);
  packet.sequence= ++watchdog_sequence;
  packet.monotonic_ns= monotonic_ns ();
  ssize_t written= send (watchdog_fd, &packet, sizeof (packet),
                          MSG_DONTWAIT | MSG_NOSIGNAL);
  if (written == static_cast<ssize_t> (sizeof (packet))) return true;
  if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK ||
                      errno == EINTR))
    return false;
  close (watchdog_fd);
  watchdog_fd= -1;
  return false;
}

bool
parse_fd_option (const char* value, int& fd) noexcept {
  static constexpr const char prefix[]= "--watchdog-fd=";
  if (value == nullptr || std::strncmp (value, prefix, sizeof (prefix) - 1) != 0)
    return false;
  const char* number= value + sizeof (prefix) - 1;
  if (*number == '\0') return false;
  char* end= nullptr;
  errno= 0;
  long parsed= std::strtol (number, &end, 10);
  if (errno != 0 || end == number || *end != '\0' || parsed < 0 ||
      parsed > (1 << 20))
    return false;
  fd= static_cast<int> (parsed);
  return true;
}

} // namespace

void
athena_watchdog_configure_from_argv (int& argc, char** argv) noexcept {
  int configured_fd= -1;
  int out= 1;
  for (int i= 1; i < argc; ++i) {
    int candidate= -1;
    if (parse_fd_option (argv[i], candidate)) {
      if (configured_fd < 0) configured_fd= candidate;
      continue;
    }
    argv[out++]= argv[i];
  }
  argc= out;
  argv[argc]= nullptr;
  if (configured_fd < 0) return;
  watchdog_configured= true;

  int flags= fcntl (configured_fd, F_GETFL, 0);
  if (flags < 0 || fcntl (configured_fd, F_SETFL, flags | O_NONBLOCK) != 0) {
    close (configured_fd);
    return;
  }
  int descriptor_flags= fcntl (configured_fd, F_GETFD, 0);
  if (descriptor_flags < 0 ||
      fcntl (configured_fd, F_SETFD, descriptor_flags | FD_CLOEXEC) != 0) {
    close (configured_fd);
    return;
  }
  watchdog_fd= configured_fd;

  (void) prctl (PR_SET_PTRACER, static_cast<unsigned long> (getppid ()),
                0UL, 0UL, 0UL);
}

AthenaWatchdogRestartResult
athena_watchdog_request_restart () noexcept {
  if (!watchdog_configured) return AthenaWatchdogRestartResult::NotSupervised;
  return send_packet (athena_watchdog::PacketType::Restart,
                      athena_watchdog::Phase::Shutdown)
    ? AthenaWatchdogRestartResult::Requested
    : AthenaWatchdogRestartResult::Failed;
}

void
athena_watchdog_start_qt_heartbeat () noexcept {
  if (watchdog_fd < 0 || heartbeat_timer != nullptr || qApp == nullptr) return;
  send_packet (athena_watchdog::PacketType::Ready,
               athena_watchdog::Phase::UiLoop);
  heartbeat_timer= new QTimer (qApp);
  heartbeat_timer->setTimerType (Qt::PreciseTimer);
  heartbeat_timer->setInterval (250);
  QObject::connect (heartbeat_timer, &QTimer::timeout, qApp, [] () {
    send_packet (athena_watchdog::PacketType::Heartbeat,
                 athena_watchdog::Phase::UiLoop);
  });
  QObject::connect (qApp, &QCoreApplication::aboutToQuit, qApp, [] () {
    send_packet (athena_watchdog::PacketType::Shutdown,
                 athena_watchdog::Phase::Shutdown);
  });
  heartbeat_timer->start ();
}

#else

void athena_watchdog_configure_from_argv (int&, char**) noexcept {}
void athena_watchdog_start_qt_heartbeat () noexcept {}
AthenaWatchdogRestartResult athena_watchdog_request_restart () noexcept {
  return AthenaWatchdogRestartResult::NotSupervised;
}

#endif

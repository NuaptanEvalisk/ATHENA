#include "ATHENA/Watchdog/watchdog_protocol.hpp"
#include "ATHENA/Watchdog/watchdog_client.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <time.h>
#include <unistd.h>

namespace {

std::uint64_t sequence= 0;

std::uint64_t monotonic_ns () {
  struct timespec now {};
  clock_gettime (CLOCK_MONOTONIC, &now);
  return static_cast<std::uint64_t> (now.tv_sec) * 1000000000ULL + now.tv_nsec;
}

void send_packet (int fd, athena_watchdog::PacketType type) {
  athena_watchdog::Packet packet;
  packet.type= static_cast<std::uint16_t> (type);
  packet.phase= static_cast<std::uint32_t> (athena_watchdog::Phase::UiLoop);
  packet.sequence= ++sequence;
  packet.monotonic_ns= monotonic_ns ();
  (void) send (fd, &packet, sizeof (packet), MSG_NOSIGNAL);
}

int parse_fd (int argc, char** argv) {
  for (int i= 1; i < argc; ++i) {
    constexpr const char prefix[]= "--watchdog-fd=";
    if (std::strncmp (argv[i], prefix, sizeof (prefix) - 1) == 0)
      return std::atoi (argv[i] + sizeof (prefix) - 1);
  }
  return -1;
}

std::string mode (int argc, char** argv) {
  for (int i= 1; i < argc; ++i)
    if (std::strncmp (argv[i], "--watchdog-fd=", 14) != 0) return argv[i];
  return {};
}

void heartbeats (int fd, int duration_ms) {
  auto end= std::chrono::steady_clock::now () + std::chrono::milliseconds (duration_ms);
  while (std::chrono::steady_clock::now () < end) {
    send_packet (fd, athena_watchdog::PacketType::Heartbeat);
    std::this_thread::sleep_for (std::chrono::milliseconds (40));
  }
}

} // namespace

int main (int argc, char** argv) {
  int fd= parse_fd (argc, argv);
  std::string selected= mode (argc, argv);
  if (fd < 0 || selected.empty ()) return 2;
  if (athena_watchdog_request_restart () !=
      AthenaWatchdogRestartResult::NotSupervised) return 4;
  if (selected.rfind ("restart-", 0) == 0) {
    if (argc < 4) return 5;
    std::string record= argv[argc - 1];
    bool first= !std::ifstream (record).good ();
    {
      std::ofstream out (record, std::ios::app);
      out << getpid () << ' ' << getppid () << '\n';
    }
    if (first) {
      if (selected != "restart-startup")
        send_packet (fd, athena_watchdog::PacketType::Ready);
      athena_watchdog_configure_from_argv (argc, argv);
      if (athena_watchdog_request_restart () !=
          AthenaWatchdogRestartResult::Requested) return 6;
      return selected == "restart-failure" ? 7 : 0;
    }
    // Prove that the replacement receives a fresh channel and is monitored
    // with the original supervisor's timeout and diagnostic directory.
    send_packet (fd, athena_watchdog::PacketType::Ready);
    std::this_thread::sleep_for (std::chrono::milliseconds (700));
    send_packet (fd, athena_watchdog::PacketType::Shutdown);
    return 0;
  }
  send_packet (fd, athena_watchdog::PacketType::Ready);
  if (selected == "healthy") {
    heartbeats (fd, 450);
  }
  else if (selected == "recover") {
    heartbeats (fd, 100);
    std::this_thread::sleep_for (std::chrono::milliseconds (450));
    heartbeats (fd, 300);
  }
  else if (selected == "stall") {
    std::this_thread::sleep_for (std::chrono::milliseconds (700));
  }
  else if (selected == "close-channel") {
    close (fd);
    std::this_thread::sleep_for (std::chrono::milliseconds (700));
    return 0;
  }
  else return 3;
  send_packet (fd, athena_watchdog::PacketType::Shutdown);
  return 0;
}

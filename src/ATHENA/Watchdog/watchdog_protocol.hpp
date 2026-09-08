#pragma once

#include <cstdint>

namespace athena_watchdog {

constexpr std::uint32_t packet_magic= 0x41545744U; // "ATWD"
constexpr std::uint16_t packet_version= 1;
constexpr int child_fd= 3;

enum class PacketType: std::uint16_t {
  Ready= 1,
  Heartbeat= 2,
  Shutdown= 3,
};

enum class Phase: std::uint32_t {
  Startup= 1,
  UiLoop= 2,
  Shutdown= 3,
};

struct Packet {
  std::uint32_t magic= packet_magic;
  std::uint16_t version= packet_version;
  std::uint16_t type= 0;
  std::uint32_t phase= 0;
  std::uint32_t flags= 0;
  std::uint64_t sequence= 0;
  std::uint64_t monotonic_ns= 0;
};

static_assert (sizeof (Packet) == 32, "Watchdog protocol must stay fixed-size");

} // namespace athena_watchdog

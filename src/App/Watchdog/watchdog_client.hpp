#pragma once
#include "athena_platform.hpp"

enum class AthenaWatchdogRestartResult { NotSupervised, Requested, Failed };
#if ATHENA_ENABLE_WATCHDOG
void athena_watchdog_configure_from_argv (int& argc, char** argv) noexcept;
void athena_watchdog_start_qt_heartbeat () noexcept;

AthenaWatchdogRestartResult athena_watchdog_request_restart () noexcept;
#else
inline void athena_watchdog_configure_from_argv (int&, char**) noexcept {}
inline void athena_watchdog_start_qt_heartbeat () noexcept {}
inline AthenaWatchdogRestartResult athena_watchdog_request_restart () noexcept {
  return AthenaWatchdogRestartResult::NotSupervised;
}
#endif

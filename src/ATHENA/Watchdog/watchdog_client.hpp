#pragma once

void athena_watchdog_configure_from_argv (int& argc, char** argv) noexcept;
void athena_watchdog_start_qt_heartbeat () noexcept;

enum class AthenaWatchdogRestartResult { NotSupervised, Requested, Failed };
AthenaWatchdogRestartResult athena_watchdog_request_restart () noexcept;

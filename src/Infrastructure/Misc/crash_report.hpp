/******************************************************************************
* MODULE     : crash_report.hpp
* DESCRIPTION: Signal-safe crash metadata, independent of editor ownership
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#ifndef ATHENA_CRASH_REPORT_HPP
#define ATHENA_CRASH_REPORT_HPP

#include <cstdint>
#include <string>

enum class AthenaCrashThreadRole: unsigned {
  Unknown, Main, BufferActor, RenderService
};

// Called on the owning thread, never from a signal handler.
void athena_crash_register_thread (AthenaCrashThreadRole role,
                                   std::uint64_t owner_actor= 0) noexcept;
void athena_crash_set_execution (std::uint64_t actor, std::uint64_t view,
                                std::uint64_t command) noexcept;
std::string athena_crash_execution_report ();
[[noreturn]] void athena_crash_abort (const char* reason) noexcept;

// Install once, after runtime signal setup. Reports use a private, pre-opened
// file in directory and stderr. Fatal signals are re-raised with default action.
bool athena_install_crash_handlers (const char* directory) noexcept;

#endif

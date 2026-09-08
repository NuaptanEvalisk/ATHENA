/******************************************************************************
* MODULE     : stacktrace_symbolize.hpp
* DESCRIPTION: Best-effort source locations for ordinary native stack reports
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#ifndef ATHENA_STACKTRACE_SYMBOLIZE_HPP
#define ATHENA_STACKTRACE_SYMBOLIZE_HPP

#include <cstddef>
#include <string>
#include <vector>

// Never call from a signal handler. Unknown frames have empty descriptions.
std::vector<std::string> athena_symbolize_stack (
  void* const* frames, std::size_t count);

#endif

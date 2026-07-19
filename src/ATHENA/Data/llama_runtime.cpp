/******************************************************************************
* MODULE     : llama_runtime.cpp
* DESCRIPTION: Process-wide llama.cpp runtime ownership for ATHENA
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "ATHENA/Data/llama_runtime.hpp"

#include "tm_ostream.hpp"

#include <ggml-backend.h>
#include <llama.h>

#include <cctype>
#include <mutex>
#include <string>

namespace {

void
athena_llama_log (enum ggml_log_level level, const char* message, void*) {
  if (message == nullptr || message[0] == '\0' ||
      level < GGML_LOG_LEVEL_WARN) return;
  bool progress_only= true;
  for (const unsigned char* p= (const unsigned char*) message; *p; p++)
    if (*p != '.' && std::isspace (*p) == 0) {
      progress_only= false;
      break;
  }
  if (progress_only) return;
  std::string text (message);
  while (!text.empty () && (text.back () == '\n' || text.back () == '\r'))
    text.pop_back ();
  athena_spdlog_warning ("llama.cpp: " + text);
}

} // namespace

void
athena_llama_runtime_initialize () {
  static std::once_flag initialized;
  std::call_once (initialized, [] {
    llama_log_set (athena_llama_log, nullptr);
    ggml_backend_load_all ();
    llama_backend_init ();
  });
}

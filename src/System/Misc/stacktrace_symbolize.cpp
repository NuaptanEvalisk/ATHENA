/******************************************************************************
* MODULE     : stacktrace_symbolize.cpp
* DESCRIPTION: Bounded, shell-free addr2line lookup with ELF load-bias handling
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include "stacktrace_symbolize.hpp"

#ifdef __linux__
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <link.h>
#include <map>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace {
using Clock= std::chrono::steady_clock;
struct Module {
  std::uintptr_t address;
  std::string file;
  std::uintptr_t bias= 0;
};

int locate_module (dl_phdr_info* info, std::size_t, void* data) {
  auto& module= *static_cast<Module*> (data);
  for (unsigned i= 0; i < info->dlpi_phnum; ++i) {
    const auto& segment= info->dlpi_phdr[i];
    auto start= info->dlpi_addr + segment.p_vaddr;
    if (segment.p_type != PT_LOAD || module.address < start ||
        module.address - start >= segment.p_memsz) continue;
    module.file= *info->dlpi_name ? info->dlpi_name : "/proc/self/exe";
    // /proc/self in the spawned process would refer to addr2line itself.
    if (module.file == "/proc/self/exe")
      module.file= "/proc/" + std::to_string (getpid ()) + "/exe";
    module.bias= info->dlpi_addr; // zero for ET_EXEC, ASLR bias for ET_DYN
    return 1;
  }
  return 0;
}

std::string run_addr2line (std::vector<std::string>& arguments,
                          Clock::time_point deadline) {
  int pipefd[2];
  if (pipe2 (pipefd, O_CLOEXEC) != 0) return {};
  // Keep pipe descriptors distinct from stdin/stdout/stderr even when closed.
  for (int& fd: pipefd) {
    if (fd > STDERR_FILENO) continue;
    int copy= fcntl (fd, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
    close (fd);
    fd= copy;
  }
  if (pipefd[0] < 0 || pipefd[1] < 0) {
    for (int fd: pipefd) if (fd >= 0) close (fd);
    return {};
  }
  posix_spawn_file_actions_t actions;
  if (posix_spawn_file_actions_init (&actions) != 0) {
    close (pipefd[0]); close (pipefd[1]);
    return {};
  }
  bool ready=
    posix_spawn_file_actions_adddup2 (&actions, pipefd[1], STDOUT_FILENO) == 0 &&
    posix_spawn_file_actions_addclose (&actions, pipefd[0]) == 0 &&
    posix_spawn_file_actions_addclose (&actions, pipefd[1]) == 0 &&
    posix_spawn_file_actions_addopen (&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0) == 0 &&
    posix_spawn_file_actions_addopen (&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0) == 0;
  std::vector<char*> argv;
  for (auto& argument: arguments) argv.push_back (argument.data ());
  argv.push_back (nullptr);
  pid_t child= -1;
  int error= ready ? posix_spawnp (&child, argv[0], &actions, nullptr,
                                  argv.data (), environ) : EINVAL;
  posix_spawn_file_actions_destroy (&actions);
  close (pipefd[1]);
  if (error != 0) { close (pipefd[0]); return {}; }
  fcntl (pipefd[0], F_SETFL, O_NONBLOCK);
  std::string output;
  bool exited= false, eof= false;
  int status= 0;
  while (Clock::now () < deadline && output.size () < 256 * 1024) {
    char bytes[4096];
    ssize_t size= read (pipefd[0], bytes, sizeof (bytes));
    if (size > 0) output.append (bytes, size);
    else if (size == 0) eof= true;
    else if (errno != EAGAIN && errno != EINTR) break;
    if (!exited) {
      pid_t result= waitpid (child, &status, WNOHANG);
      if (result == child) exited= true;
      else if (result < 0 && errno != EINTR) break;
    }
    if (exited && eof) break;
    if (size > 0) continue;
    pollfd descriptor {pipefd[0], POLLIN, 0};
    poll (&descriptor, 1, 10);
  }
  close (pipefd[0]);
  if (!exited) {
    kill (child, SIGKILL);
    while (waitpid (child, &status, 0) < 0 && errno == EINTR) {}
    return {};
  }
  if (!eof || !WIFEXITED (status) || WEXITSTATUS (status) != 0) return {};
  return output;
}
} // namespace
#endif

std::vector<std::string>
athena_symbolize_stack (void* const* frames, std::size_t count) {
  std::vector<std::string> result (count);
#ifdef __linux__
  struct Frame { std::size_t index; std::uintptr_t relative; };
  std::map<std::string, std::vector<Frame>> modules;
  for (std::size_t i= 0; i < count; ++i) {
    auto address= reinterpret_cast<std::uintptr_t> (frames[i]);
    if (!address) continue;
    // backtrace supplies return PCs; locate the preceding call instruction.
    Module module {address - 1, {}};
    dl_iterate_phdr (locate_module, &module);
    if (!module.file.empty ())
      modules[module.file].push_back ({i, module.address - module.bias});
  }
  const auto deadline= Clock::now () + std::chrono::seconds (2);
  for (const auto& entry: modules) {
    if (Clock::now () >= deadline) break;
    std::vector<std::string> args {"addr2line", "-f", "-C", "-p", "-e", entry.first};
    for (const auto& frame: entry.second) {
      char address[2 + sizeof (std::uintptr_t) * 2 + 1];
      snprintf (address, sizeof (address), "0x%llx",
                static_cast<unsigned long long> (frame.relative));
      args.emplace_back (address);
    }
    std::istringstream lines (run_addr2line (args, deadline));
    for (const auto& frame: entry.second) {
      std::string line;
      if (!std::getline (lines, line)) break;
      if (!line.empty () && line.find ("??") == std::string::npos)
        result[frame.index]= std::move (line);
    }
  }
#else
  (void) frames;
#endif
  return result;
}

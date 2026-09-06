/******************************************************************************
* MODULE     : crash_report.cpp
* DESCRIPTION: Bounded fatal-signal records and OS-owned core dumps
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include "crash_report.hpp"
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>

#ifndef _WIN32
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#include <ucontext.h>
#ifdef __linux__
#include <sys/syscall.h>
#endif
#endif

namespace {

static_assert (std::atomic<std::uint64_t>::is_always_lock_free,
               "Signal metadata must not acquire a runtime lock");
static_assert (std::atomic<unsigned>::is_always_lock_free,
               "Signal metadata must not acquire a runtime lock");

thread_local std::atomic<unsigned> updating {0};
thread_local std::atomic<unsigned> thread_role {0};
thread_local std::atomic<std::uint64_t> thread_actor {0};
thread_local std::atomic<std::uint64_t> actor_id {0}, view_id {0}, command_id {0};

struct Report {
  char bytes[1024];
  std::size_t size= 0;
  void text (const char* s) noexcept {
    while (*s && size < sizeof (bytes)) bytes[size++]= *s++;
  }
  void number (std::uint64_t n, unsigned base= 10) noexcept {
    char digits[32];
    unsigned count= 0;
    do {
      digits[count++]= "0123456789abcdef"[n % base];
      n /= base;
    } while (n);
    while (count && size < sizeof (bytes)) bytes[size++]= digits[--count];
  }
};

const char* role_name (unsigned role) noexcept {
  switch (static_cast<AthenaCrashThreadRole> (role)) {
    case AthenaCrashThreadRole::Main: return "Main";
    case AthenaCrashThreadRole::BufferActor: return "BufferActor";
    case AthenaCrashThreadRole::RenderService: return "RenderService";
    default: return "Unregistered";
  }
}

void append_context (Report& report) noexcept {
#ifdef __linux__
  // Linux's raw gettid syscall performs no allocation or userspace locking.
  report.text ("tid="); report.number (syscall (SYS_gettid));
#endif
  report.text (" role=");
  report.text (role_name (thread_role.load (std::memory_order_relaxed)));
  report.text (" owner-actor=");
  report.number (thread_actor.load (std::memory_order_relaxed));
  if (updating.load (std::memory_order_relaxed)) {
    report.text (" execution-context=transition\n");
    return;
  }
  std::atomic_signal_fence (std::memory_order_seq_cst);
  report.text (" actor="); report.number (actor_id.load (std::memory_order_relaxed));
  report.text (" view="); report.number (view_id.load (std::memory_order_relaxed));
  report.text (" command="); report.number (command_id.load (std::memory_order_relaxed));
  report.text ("\n");
}

#ifndef _WIN32
static_assert (std::atomic<int>::is_always_lock_free,
               "Signal descriptors must be lock-free");
std::atomic<int> report_fd {-1}, diagnostic_fd {-1};
alignas (16) thread_local unsigned char alternate_stack[64 * 1024];

void write_report (int fd, const Report& report) noexcept {
  if (fd < 0) return;
  // One bounded record; do not retry indefinitely on a broken logging sink.
  ssize_t ignored= write (fd, report.bytes, report.size);
  (void) ignored;
}

void fatal_signal (int signal, siginfo_t* info, void* raw_context) noexcept {
  Report report;
  report.text ("ATHENA fatal signal="); report.number (signal);
  report.text (" pid="); report.number (getpid ());
  report.text (" code=");
  int code= info ? info->si_code : 0;
  if (code < 0) report.text ("-");
  report.number (code < 0 ? -static_cast<std::int64_t> (code) : code);
  report.text (" address=0x");
  report.number (info && code > 0 ? reinterpret_cast<std::uintptr_t> (info->si_addr) : 0, 16);
  report.text (" pc=0x");
  std::uintptr_t pc= 0;
#if defined(__linux__) && defined(__x86_64__)
  if (raw_context) pc= static_cast<ucontext_t*> (raw_context)->uc_mcontext.gregs[REG_RIP];
#elif defined(__linux__) && defined(__aarch64__)
  if (raw_context) pc= static_cast<ucontext_t*> (raw_context)->uc_mcontext.pc;
#else
  (void) raw_context;
#endif
  report.number (pc, 16); report.text ("\n");
  append_context (report);
  report.text ("Editor state not inspected. Use the OS core dump for all-thread backtraces.\n");
  write_report (report_fd.load (std::memory_order_relaxed), report);
  write_report (diagnostic_fd.load (std::memory_order_relaxed), report);

  struct sigaction action {};
  action.sa_handler= SIG_DFL;
  sigemptyset (&action.sa_mask);
  sigaction (signal, &action, nullptr);
  // raise targets this thread. Only unblock the fatal signal, not any SIGPIPE
  // produced by a closed stderr pipe, and never unwind a damaged native stack.
  raise (signal);
  sigset_t unblocked;
  sigemptyset (&unblocked);
  sigaddset (&unblocked, signal);
  pthread_sigmask (SIG_UNBLOCK, &unblocked, nullptr);
  _exit (128 + signal);
}

void uncaught_exception () noexcept {
  athena_crash_abort ("C++ termination");
}
#endif

} // namespace

void athena_crash_register_thread (AthenaCrashThreadRole role,
                                   std::uint64_t owner_actor) noexcept {
  thread_role.store (static_cast<unsigned> (role), std::memory_order_relaxed);
  thread_actor.store (owner_actor, std::memory_order_relaxed);
#ifndef _WIN32
  stack_t previous {};
  if (sigaltstack (nullptr, &previous) == 0 && (previous.ss_flags & SS_DISABLE)) {
    stack_t stack {};
    stack.ss_sp= alternate_stack;
    stack.ss_size= sizeof (alternate_stack);
    sigaltstack (&stack, nullptr);
  }
#endif
}

void athena_crash_set_execution (std::uint64_t actor, std::uint64_t view,
                                std::uint64_t command) noexcept {
  updating.store (1, std::memory_order_relaxed);
  std::atomic_signal_fence (std::memory_order_seq_cst);
  actor_id.store (actor, std::memory_order_relaxed);
  view_id.store (view, std::memory_order_relaxed);
  command_id.store (command, std::memory_order_relaxed);
  std::atomic_signal_fence (std::memory_order_seq_cst);
  updating.store (0, std::memory_order_relaxed);
}

std::string athena_crash_execution_report () {
  Report report;
  append_context (report);
  return std::string (report.bytes, report.size);
}

[[noreturn]] void athena_crash_abort (const char* reason) noexcept {
#ifndef _WIN32
  Report report;
  report.text ("ATHENA fatal error: ");
  report.text (reason ? reason : "unknown");
  report.text ("\n");
  write_report (report_fd.load (std::memory_order_relaxed), report);
  write_report (diagnostic_fd.load (std::memory_order_relaxed), report);
#else
  (void) reason;
#endif
  std::abort ();
}

bool athena_install_crash_handlers (const char* directory) noexcept {
#ifndef _WIN32
#ifdef __linux__
  // Open before the report file, which could otherwise reuse a closed fd 2.
  // A separate file description does not change the application's stderr flags.
  if (diagnostic_fd < 0)
    diagnostic_fd= open ("/proc/self/fd/2", O_WRONLY | O_APPEND | O_NONBLOCK | O_CLOEXEC);
#endif
  bool saved= report_fd >= 0;
  if (!saved && directory && *directory) {
    try {
      std::filesystem::path root (directory);
      std::filesystem::create_directories (root);
      auto path= root / ("fatal-" + std::to_string (getpid ()) + ".log");
      report_fd= open (path.c_str (), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
      saved= report_fd >= 0;
    }
    catch (...) { saved= false; }
  }
  struct sigaction action {};
  action.sa_sigaction= fatal_signal;
  sigfillset (&action.sa_mask);
  action.sa_flags= SA_SIGINFO | SA_ONSTACK;
  bool installed= true;
  for (int signal: {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT})
    installed= sigaction (signal, &action, nullptr) == 0 && installed;
  std::set_terminate (uncaught_exception);
  return saved && installed;
#else
  (void) directory;
  return false;
#endif
}

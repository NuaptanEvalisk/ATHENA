#include "System/Misc/crash_report.hpp"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <fcntl.h>
#include <new>
#include <stdexcept>
#include <thread>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>

static std::atomic<bool> forbid_allocation {false};
void* operator new (std::size_t size) {
  if (forbid_allocation.load ()) _exit (90);
  if (void* p= std::malloc (size)) return p;
  throw std::bad_alloc ();
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

static volatile bool stop_recursion= false;
__attribute__((noinline)) static int overflow_stack (unsigned n) {
  volatile char padding[4096];
  padding[0]= n;
  padding[4095]= n+1;
  return padding[0] + (stop_recursion ? 0 : overflow_stack (n+1)) + padding[4095];
}

static void fault (void* address) {
  *static_cast<volatile char*> (address)= 1;
  _exit (91);
}

int main (int argc, char** argv) {
  if (argc != 3) return 2;
  struct rlimit limit {0, 0};
  setrlimit (RLIMIT_CORE, &limit);
  const char* mode= argv[2];
  int reader= -1;
  if (!std::strcmp (mode, "closed-stderr")) close (2);
  if (!std::strcmp (mode, "full-stderr")) {
    int pipefd[2];
    if (pipe2 (pipefd, O_NONBLOCK | O_CLOEXEC)) return 3;
    char fill[4096] {};
    while (write (pipefd[1], fill, sizeof (fill)) > 0) {}
    fcntl (pipefd[1], F_SETFL, 0);
    dup2 (pipefd[1], 2);
    close (pipefd[1]);
    reader= pipefd[0];
  }
  athena_crash_register_thread (AthenaCrashThreadRole::Main);
  if (!athena_install_crash_handlers (argv[1])) return 4;
  if (reader >= 0 && (fcntl (2, F_GETFL) & O_NONBLOCK)) return 5;
  void* address= mmap (nullptr, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (address == MAP_FAILED) return 6;
  auto worker= [&] {
    if (!std::strcmp (mode, "actor")) {
      athena_crash_register_thread (AthenaCrashThreadRole::BufferActor, 71);
      athena_crash_set_execution (71, 91, 111);
    }
    else if (!std::strcmp (mode, "render"))
      athena_crash_register_thread (AthenaCrashThreadRole::RenderService);
    else if (!std::strcmp (mode, "stack")) {
      athena_crash_register_thread (AthenaCrashThreadRole::BufferActor, 81);
      overflow_stack (0);
    }
    fault (address);
  };
  if (!std::strcmp (mode, "actor") || !std::strcmp (mode, "render") ||
      !std::strcmp (mode, "unregistered") || !std::strcmp (mode, "stack")) {
    std::thread thread (worker);
    thread.join ();
    return 7;
  }
  if (!std::strcmp (mode, "uncaught")) throw std::runtime_error ("probe");
  if (!std::strcmp (mode, "fpe")) raise (SIGFPE);
  if (!std::strcmp (mode, "ill")) raise (SIGILL);
  if (!std::strcmp (mode, "bus")) raise (SIGBUS);
  if (!std::strcmp (mode, "allocator")) forbid_allocation.store (true);
  fault (address);
}

/******************************************************************************
* MODULE     : native_context_test.cpp
* DESCRIPTION: Owner-thread native context, nesting, and unwind regression tests
* COPYRIGHT  : (C) 2026
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "native_context.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <new>
#include <stdexcept>
#include <thread>
#include <vector>


bool headless_mode= true;
bool is_headless () { return true; }

static void
check (bool ok, const char* expression, int line) {
  if (!ok) {
    std::fprintf (stderr, "FAIL line %d: %s\n", line, expression);
    std::fflush (stderr);
    std::abort ();
  }
}
#define CHECK(condition) check ((condition), #condition, __LINE__)


// Deliberately NON-ATOMIC intrusive reference counts.  Any retain/release or
// dereference on a different thread is a test failure, not something hidden
// by shared_ptr or by a test-only mutex around the native object.
class native_environment {
  struct state {
    std::thread::id owner;
    unsigned refs;
    int identity;
    int* destroyed;
  };
  state* rep;

  void retain () noexcept {
    if (rep == nullptr) return;
    CHECK (rep->owner == std::this_thread::get_id ());
    ++rep->refs;
  }

  void release () noexcept {
    if (rep == nullptr) return;
    CHECK (rep->owner == std::this_thread::get_id ());
    CHECK (rep->refs > 0);
    if (--rep->refs == 0) {
      ++*rep->destroyed;
      delete rep;
    }
  }

public:
  native_environment () noexcept: rep (nullptr) {}
  native_environment (int id, int& destroyed):
    rep (new state {std::this_thread::get_id (), 1, id, &destroyed}) {}
  native_environment (const native_environment& other) noexcept:
    rep (other.rep) { retain (); }
  native_environment& operator= (const native_environment& other) noexcept {
    // Retain before release, including assignment of aliases.
    native_environment next (other);
    release ();
    rep= next.rep;
    next.rep= nullptr;
    return *this;
  }
  ~native_environment () noexcept { release (); }
  int id () const noexcept {
    CHECK (rep != nullptr);
    CHECK (rep->owner == std::this_thread::get_id ());
    return rep->identity;
  }
};
using context= native_context_scope<native_environment>;

static int
callback_environment (int explicit_fallback) {
  const auto* current= context::current ();
  return current == nullptr ? explicit_fallback : current->id ();
}

static void
test_empty_and_fallback () {
  CHECK (context::current () == nullptr);
  CHECK (callback_environment (17) == 17);
  { context registered_but_not_bound; CHECK (context::current () == nullptr); }
  CHECK (context::current () == nullptr);
}

static void
test_nested_lifetime () {
  int destroyed= 0;
  {
    context outer;
    outer.bind (native_environment (11, destroyed));
    CHECK (destroyed == 0); // temporary gone; scope owns the only native ref
    CHECK (callback_environment (17) == 11);
    {
      context inner;
      inner.bind (native_environment (22, destroyed));
      CHECK (callback_environment (17) == 22);
    }
    CHECK (destroyed == 1);
    CHECK (callback_environment (17) == 11);
  }
  CHECK (destroyed == 2);
  CHECK (callback_environment (17) == 17);
}

static void
test_cpp_exception () {
  int destroyed= 0;
  try {
    context outer;
    outer.bind (native_environment (31, destroyed));
    try {
      context inner;
      inner.bind (native_environment (32, destroyed));
      throw std::runtime_error ("inner");
    }
    catch (const std::runtime_error&) {
      CHECK (destroyed == 1);
      CHECK (callback_environment (17) == 31);
    }
    throw std::runtime_error ("outer");
  }
  catch (const std::runtime_error&) {
    CHECK (destroyed == 2);
    CHECK (context::current () == nullptr);
  }
}

static void
test_restore_before_result_processing () {
  int destroyed= 0;
  int result= [&] {
    context local;
    local.bind (native_environment (41, destroyed));
    return callback_environment (17);
  } ();
  CHECK (result == 41);
  CHECK (destroyed == 1);
  // env_exec.cpp must end its binding before the command result is processed.
  CHECK (callback_environment (17) == 17);
}


// Test synchronization controls overlap; it never protects native context.
class two_thread_barrier {
  std::mutex mutex;
  std::condition_variable changed;
  unsigned count= 0;
  unsigned generation= 0;
public:
  void wait () {
    std::unique_lock<std::mutex> lock (mutex);
    const unsigned observed= generation;
    if (++count == 2) {
      count= 0;
      ++generation;
      changed.notify_all ();
      return;
    }
    CHECK (changed.wait_for (lock, std::chrono::seconds (3), [&] {
      return generation != observed;
    }));
  }
};

struct worker_request {
  two_thread_barrier* gate;
  int id;
  unsigned rounds;
};

static void*
worker_body (void* raw) {
  const auto& request= *static_cast<worker_request*> (raw);
  // Main retains a live context while the new thread starts.  A process-global
  // slot fails here deterministically, before competing writes are required.
  CHECK (context::current () == nullptr);
  int destroyed= 0;
  {
    context outer;
    outer.bind (native_environment (request.id, destroyed));
    if (request.gate != nullptr) request.gate->wait ();
    CHECK (callback_environment (-1) == request.id);
    for (unsigned i= 0; i < request.rounds; ++i) {
      {
        context inner;
        inner.bind (native_environment (request.id + 10, destroyed));
        if ((i & 31) == 0) std::this_thread::yield ();
        CHECK (callback_environment (-1) == request.id + 10);
      }
      CHECK (callback_environment (-1) == request.id);
    }
    if (request.gate != nullptr) request.gate->wait ();
  }
  CHECK (destroyed == static_cast<int> (request.rounds) + 1);
  CHECK (context::current () == nullptr);
  return nullptr;
}

static void
run_worker (worker_request* request) {
  worker_body (request);
}

static void
test_parallel_owners () {
  int destroyed= 0;
  {
    context main_context;
    main_context.bind (native_environment (60, destroyed));
    two_thread_barrier gate;
    worker_request requests[2]= {{&gate, 61, 4096}, {&gate, 62, 4096}};
    std::thread first (&run_worker, &requests[0]);
    std::thread second (&run_worker, &requests[1]);
    first.join ();
    second.join ();
    CHECK (callback_environment (-1) == 60);
  }
  CHECK (destroyed == 1);
  CHECK (context::current () == nullptr);
}

static void
test_worker_lifecycle () {
  for (unsigned i= 0; i < 32; ++i) {
    worker_request request= {nullptr, 70, 64};
    std::thread worker (&run_worker, &request);
    worker.join ();
    CHECK (context::current () == nullptr);
  }
}

static void
test_distinct_types () {
  int destroyed= 0;
  {
    context outer;
    outer.bind (native_environment (80, destroyed));
    {
      native_context_scope<int> other;
      other.bind (81);
      CHECK (callback_environment (-1) == 80);
      CHECK (*native_context_scope<int>::current () == 81);
    }
    CHECK (native_context_scope<int>::current () == nullptr);
    CHECK (callback_environment (-1) == 80);
  }
  CHECK (destroyed == 1);
}

int
main () {
  struct test_case { const char* name; void (*run) (); };
  const test_case tests[]= {
    {"empty context and explicit fallback", &test_empty_and_fallback},
    {"nested context and native lifetime", &test_nested_lifetime},
    {"C++ exception restoration", &test_cpp_exception},
    {"restore before result processing", &test_restore_before_result_processing},
    {"parallel independent owners", &test_parallel_owners},
    {"worker lifecycle", &test_worker_lifecycle},
    {"independent native context types", &test_distinct_types},
  };
  for (const auto& test: tests) {
    test.run ();
    std::cout << "PASS " << test.name << std::endl;
  }
  return 0;
}

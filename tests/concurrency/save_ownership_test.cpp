// Compile without Qt/Guile; exercises the exact production lifetime/snapshot types.
#include "actor_lifetime.hpp"
#include "buffer_name_catalog.hpp"

#include <atomic>
#include <cassert>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

static void test_snapshot_publication () {
  buffer_name_catalog catalog;
  assert (catalog.read ()->empty ());
  catalog.publish ({"first", "second"});
  auto retained= catalog.read ();
  catalog.publish ({"renamed", "second"});
  assert ((*retained)[0] == "first");
  assert ((*catalog.read ())[0] == "renamed");
  catalog.publish ({});
  assert (catalog.read ()->empty ());
  assert (retained->size () == 2);

  std::atomic<bool> start {false}, done {false};
  std::atomic<unsigned> observations {0};
  catalog.publish ({"0", "0", "0"});
  std::vector<std::thread> readers;
  for (unsigned i= 0; i < 8; ++i)
    readers.emplace_back ([&] {
      while (!start.load ()) std::this_thread::yield ();
      do {
        auto snapshot= catalog.read ();
        assert (snapshot->size () == 3);
        assert ((*snapshot)[0] == (*snapshot)[1]);
        assert ((*snapshot)[1] == (*snapshot)[2]);
        ++observations;
      } while (!done.load ());
    });
  start= true;
  for (unsigned i= 1; i <= 10000; ++i) {
    auto name= std::to_string (i);
    catalog.publish ({name, name, name});
  }
  done= true;
  for (auto& reader: readers) reader.join ();
  assert (observations >= 8);
  assert ((*retained)[0] == "first");
}

static void test_lifetime_pin () {
  int owner= 42;
  actor_lifetime<int> lifetime (&owner);
  auto first= lifetime.acquire ();
  assert (first && *first.get () == 42);
  // Leases are movable and do not retain a lock while in use.
  auto moved= std::move (first);
  assert (!first && moved);
  auto concurrent= std::async (std::launch::async, [&] {
    auto nested= lifetime.acquire ();
    assert (nested && *nested.get () == 42);
  });
  concurrent.get ();

  std::promise<void> closing;
  auto entered= closing.get_future ();
  std::atomic<bool> closed {false};
  std::thread closer ([&] {
    closing.set_value ();
    lifetime.close ();
    closed= true;
  });
  entered.get ();
  // close has a linearization point at which acquisition is disabled. Keep
  // one old lease alive until that point; close must not finish before release.
  for (;;) {
    auto probe= lifetime.acquire ();
    if (!probe) break;
    std::this_thread::yield ();
  }
  assert (!closed);
  assert (*moved.get () == 42);
  moved= {};
  closer.join ();
  assert (closed && !lifetime.acquire ());
  lifetime.close (); // idempotent
}

static void test_exception_and_racing_close () {
  for (unsigned round= 0; round < 200; ++round) {
    std::atomic<unsigned> owner {0};
    actor_lifetime<std::atomic<unsigned>> lifetime (&owner);
    try {
      auto pinned= lifetime.acquire ();
      ++*pinned.get ();
      throw std::runtime_error ("test");
    } catch (const std::runtime_error&) {}
    std::vector<std::thread> readers;
    for (unsigned i= 0; i < 4; ++i)
      readers.emplace_back ([&] {
        for (unsigned j= 0; j < 100; ++j) {
          auto pinned= lifetime.acquire ();
          if (!pinned) break;
          ++*pinned.get ();
        }
      });
    lifetime.close ();
    const auto after_close= owner.load ();
    for (auto& reader: readers) reader.join ();
    assert (owner.load () == after_close);
    assert (!lifetime.acquire ());
  }
}

int main () {
  test_snapshot_publication ();
  test_lifetime_pin ();
  test_exception_and_racing_close ();
  std::cout << "PASS: immutable catalog publication, retained generations, "
               "eight readers, lifetime pins, stale IDs, close races\n";
}

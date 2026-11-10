// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "Document/State/auxiliary_cache.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <sqlite3.h>

namespace fs= std::filesystem;
using namespace athena::derived;

int main () {
  const auto root= fs::temp_directory_path () / ("athena-auxiliary-test-" +
    std::to_string (std::chrono::steady_clock::now ().time_since_epoch ().count ()));
  fs::create_directories (root);
  const auto source= root / "Document.ath";
  std::ofstream (source) << "source";
  const cache_bytes expected {0, 255, 1, 0, 7};
  {
    auxiliary_cache cache (source, "test");
    std::promise<void> started, release;
    auto gate= release.get_future ().share ();
    cache.request ({"toc", "old", [&] {
      started.set_value (); gate.wait (); return cache_bytes {1};
    }});
    started.get_future ().wait ();
    std::atomic<int> computations= 0;
    for (int i= 0; i < 20; ++i)
      cache.request ({"toc", "superseded", [&] { ++computations; return cache_bytes {2}; }});
    const auto latest= cache.request ({"toc", "current", [&] { ++computations; return expected; }});
    release.set_value ();
    cache.wait (latest);
    assert (computations == 1);
    assert (!cache.lookup ("toc", "old"));
    assert (!cache.lookup ("toc", "superseded"));
    assert (cache.lookup ("toc", "current")->bytes == expected);
    cache.flush ();
  }
  {
    auxiliary_cache cache (source, "test");
    bool computed= false;
    const auto ticket= cache.request ({"toc", "current", [&] {
      computed= true; return cache_bytes {};
    }});
    cache.wait (ticket);
    assert (!computed && cache.lookup ("toc", "current")->bytes == expected);

    // The editor can consume a new result even while storage cannot commit it.
    sqlite3* lock= nullptr;
    assert (sqlite3_open (auxiliary_cache::location (source).string ().c_str (), &lock) == SQLITE_OK);
    assert (sqlite3_exec (lock, "BEGIN EXCLUSIVE", nullptr, nullptr, nullptr) == SQLITE_OK);
    cache.wait (cache.request ({"toc", "edited", [] { return cache_bytes {3}; }}));
    assert (cache.lookup ("toc", "edited")->bytes == cache_bytes {3});
    bool failed= false;
    try { cache.flush (); } catch (const std::runtime_error&) { failed= true; }
    assert (failed);
    assert (sqlite3_exec (lock, "ROLLBACK", nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close (lock);
    cache.flush ();
    assert (cache.error ().empty ());
  }
  {
    auxiliary_cache cache (source, "test");
    cache.wait (0);
    assert (cache.lookup ("toc", "edited")->bytes == cache_bytes {3});
    cache.wait (cache.erase ("toc"));
    assert (!cache.lookup ("toc", "edited"));
    cache.flush ();
  }
  {
    auxiliary_cache cache (source, "test");
    cache.wait (0);
    assert (!cache.lookup ("toc", "edited"));
    cache.wait (cache.request ({"toc", "restored", [] { return cache_bytes {4}; }}));
    cache.flush ();
  }
  {
    auxiliary_cache cache (source, "different-build");
    cache.wait (0);
    assert (!cache.lookup ("toc", "restored"));
    cache.wait (cache.request ({"toc", "new-format", [] { return cache_bytes {5}; }}));
    cache.flush ();
  }
  {
    std::ofstream (auxiliary_cache::location (source), std::ios::trunc) << "damaged cache";
    auxiliary_cache cache (source, "test");
    cache.wait (cache.request ({"toc", "recovered", [] { return cache_bytes {6}; }}));
    cache.flush ();
  }
  {
    auxiliary_cache cache (source, "test");
    cache.wait (0);
    assert (cache.lookup ("toc", "recovered")->bytes == cache_bytes {6});
    std::promise<void> started, release;
    auto gate= release.get_future ().share ();
    auto ticket= cache.request ({"toc", "deleted", [&] {
      started.set_value (); gate.wait (); return cache_bytes {7};
    }});
    started.get_future ().wait ();
    fs::remove (source);
    release.set_value ();
    cache.wait (ticket);
    cache.flush ();
    assert (!fs::exists (auxiliary_cache::location (source)));
    assert (!cache.lookup ("toc", "deleted"));
  }
  {
    std::ofstream (source) << "source";
    auxiliary_cache cache (source, "test");
    std::promise<void> started, release, finished;
    auto gate= release.get_future ().share ();
    cache.request ({"toc", "closing", [&] {
      started.set_value (); gate.wait (); finished.set_value (); return cache_bytes {8};
    }});
    started.get_future ().wait ();
    cache.close ();
    release.set_value ();
    finished.get_future ().wait ();
    auxiliary_cache next (source, "test");
    next.wait (0);
    assert (!cache.lookup ("toc", "closing"));
    fs::remove (source);
    next.flush ();
  }
  fs::remove_all (root);
  std::cout << "auxiliary_cache_test passed\n";
}

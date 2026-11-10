// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace athena::derived {
using cache_bytes= std::vector<std::uint8_t>;
struct cache_record {
  std::string receipt;
  cache_bytes bytes;
};

// The computation runs on the cache worker. Capture detached/frozen inputs,
// never an editor, environment, or a mutable native tree shared with its actor.
struct cache_request {
  std::string key, receipt;
  std::function<cache_bytes()> compute;
  bool cacheable= true;
  bool reuse_existing= true;
};

class auxiliary_cache {
public:
  struct state;
  // ready runs on the worker and may only signal an owner by ID. It is not a
  // callback into editor state, and may already be in flight during close().
  auxiliary_cache (std::filesystem::path source, std::string format,
                   std::function<void()> ready= {});
  ~auxiliary_cache ();
  auxiliary_cache (const auxiliary_cache&)= delete;
  auxiliary_cache& operator= (const auxiliary_cache&)= delete;

  std::shared_ptr<const cache_record> lookup (
    const std::string& key, const std::string& receipt) const;
  std::uint64_t request (cache_request);
  std::uint64_t post (std::string key, std::function<void()>);
  std::uint64_t erase (std::string key);
  bool loaded () const;
  std::string error () const;
  // Only explicit export/drain operations may wait. Ordinary editing uses
  // lookup/readiness publication and never calls this method.
  void wait (std::uint64_t ticket);
  // Explicit persistence barrier, not part of editor publication or closing.
  void flush ();
  void close ();
  static std::filesystem::path location (const std::filesystem::path& source);
  // Called by background source inventory, never by an editing command.
  static void prune_directory (const std::filesystem::path& source_directory);
private:
  std::shared_ptr<state> data;
};
}

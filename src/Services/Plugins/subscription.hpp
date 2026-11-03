/******************************************************************************
* MODULE     : subscription.hpp
* DESCRIPTION: Bounded per-launch plugin command mailboxes and AUDM accessors
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include "resolution.hpp"
#include <deque>

namespace athena::plugins {
using interop::value;

// One instance per process launch. All mutable state is guarded by its mutex;
// no GUI objects, Scheme values or editor trees cross this boundary.
class subscription {
  struct entry {
    std::uint64_t id;
    value request;
    value response;
    std::size_t bytes;
    bool delivered = false;
  };
  const std::string guid_;
  const std::string plugin_;
  const std::size_t capacity_;
  const std::size_t byte_limit_;
  mutable std::mutex mutex_;
  std::deque<entry> entries_;
  std::uint64_t sequence_ = 0;
  std::size_t bytes_ = 0;
  bool active_ = true;
public:
  subscription (std::string guid, std::string plugin,
                std::size_t capacity = 256, std::size_t byte_limit = 32 * 1024 * 1024);
  const std::string& guid () const { return guid_; }
  value properties () const;
  std::uint64_t enqueue (const std::string& command, const value& parameters);
  // Non-destructive polling. A repeated get returns the same command IDs.
  value get (std::uint64_t after = 0, std::size_t limit = 64) const;
  void reply (std::uint64_t id, const std::string& status, const value& result);
  // Host consumption, not a plugin operation. Only consumed replies can expire.
  value take_responses ();
  void close ();
};

// Install only into the registry granted to this launch's authenticated key.
// The GUID is a selector, not an authentication credential.
std::shared_ptr<const interop::resolver> subscription_resolver (
  std::shared_ptr<subscription> channel);
} // namespace athena::plugins

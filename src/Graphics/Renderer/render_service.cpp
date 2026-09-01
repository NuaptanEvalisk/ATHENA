/******************************************************************************
* MODULE     : render_service.cpp
* DESCRIPTION: Shared RenderService worker and per-actor connections
* COPYRIGHT  : (C) 2026  Nuaptan F. Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "render_service.hpp"

#include <cstdio>
#include <exception>
#include <utility>

render_service&
render_service::instance () {
  static render_service service;
  return service;
}

render_service::render_service ():
  worker_thread_ (), wake_generation_ (0), stopping_ (false) {}

render_service::~render_service () {
  shutdown ();
}

std::shared_ptr<render_connection>
render_service::connect (processor process, std::size_t slot_count,
                         std::size_t slot_capacity) {
  auto connection= std::shared_ptr<render_connection> (
    new render_connection (*this, std::move (process), slot_count,
                           slot_capacity));
  {
    std::lock_guard<std::mutex> guard (lock_);
    if (stopping_) return nullptr;
    connections_.push_back (connection);
  }
  ensure_started ();
  notify ();
  return connection;
}

void
render_service::ensure_started () {
  std::lock_guard<std::mutex> guard (lock_);
  if (!worker_.joinable () && !stopping_)
    worker_= std::thread ([this] { run (); });
}

void
render_service::notify () noexcept {
  {
    std::lock_guard<std::mutex> guard (lock_);
    ++wake_generation_;
  }
  changed_.notify_one ();
}

void
render_service::shutdown () {
  std::vector<std::shared_ptr<render_connection>> active;
  {
    std::lock_guard<std::mutex> guard (lock_);
    if (stopping_ && !worker_.joinable ()) return;
    stopping_= true;
    ++wake_generation_;
    for (const auto& weak: connections_)
      if (auto connection= weak.lock ()) active.push_back (connection);
  }
  for (const auto& connection: active) connection->retire ();
  changed_.notify_all ();
  if (worker_.joinable ()) worker_.join ();
}

std::thread::id
render_service::worker_thread () const noexcept {
  std::lock_guard<std::mutex> guard (lock_);
  return worker_thread_;
}

void
render_service::run () {
  {
    std::lock_guard<std::mutex> guard (lock_);
    worker_thread_= std::this_thread::get_id ();
  }

  std::uint64_t observed= 0;
  while (true) {
    {
      std::unique_lock<std::mutex> guard (lock_);
      changed_.wait (guard, [this, observed] {
        return stopping_ || wake_generation_ != observed;
      });
      observed= wake_generation_;
    }
    drain_connections ();
    std::lock_guard<std::mutex> guard (lock_);
    if (stopping_) break;
  }

  std::lock_guard<std::mutex> guard (lock_);
  worker_thread_= std::thread::id ();
  connections_.clear ();
}

void
render_service::drain_connections () {
  std::vector<std::shared_ptr<render_connection>> active;
  {
    std::lock_guard<std::mutex> guard (lock_);
    auto out= connections_.begin ();
    for (auto it= connections_.begin (); it != connections_.end (); ++it) {
      if (auto connection= it->lock ()) {
        active.push_back (connection);
        *out++= *it;
      }
    }
    connections_.erase (out, connections_.end ());
  }
  for (const auto& connection: active) connection->drain ();
}

render_connection::render_connection (
  render_service& service, render_service::processor process,
  std::size_t slot_count, std::size_t slot_capacity):
  service_ (service), process_ (std::move (process)),
  arena_ (slot_count, slot_capacity) {}

render_connection::~render_connection () {
  retire ();
}

bool
render_connection::acquire (render_chunk_arena::writable_chunk& chunk) {
  if (retired_.load (std::memory_order_acquire)) return false;
  return arena_.acquire (chunk);
}

bool
render_connection::discard (std::uint32_t slot) noexcept {
  if (retired_.load (std::memory_order_acquire)) return false;
  return arena_.discard (slot);
}

bool
render_connection::publish (const render_chunk_descriptor& descriptor) {
  if (retired_.load (std::memory_order_acquire)) return false;
  if (!arena_.publish (descriptor)) return false;
  service_.notify ();
  return true;
}

void
render_connection::retire () noexcept {
  bool expected= false;
  if (!retired_.compare_exchange_strong (
        expected, true, std::memory_order_acq_rel))
    return;
  arena_.close ();
  service_.notify ();
}

bool
render_connection::is_retired () const noexcept {
  return retired_.load (std::memory_order_acquire);
}

render_chunk_arena&
render_connection::arena () noexcept {
  return arena_;
}

void
render_connection::drain () {
  render_chunk_descriptor descriptor;
  while (arena_.try_submission (descriptor)) {
    if (!is_retired () && process_) {
      try { process_ (descriptor, arena_.payload (descriptor.slot)); }
      catch (const std::exception& error) {
        std::fprintf (stderr, "ATHENA] RenderService processor failed: %s\n",
                      error.what ());
      }
      catch (...) {
        std::fprintf (stderr, "ATHENA] RenderService processor failed\n");
      }
    }
    (void) arena_.complete (descriptor.slot);
  }
}

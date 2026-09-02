/******************************************************************************
* MODULE     : render_service.hpp
* DESCRIPTION: Shared RenderService worker and per-actor connections
* COPYRIGHT  : (C) 2026  Nuaptan F. Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef RENDER_SERVICE_H
#define RENDER_SERVICE_H

#include "render_chunk_arena.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class render_connection;

class render_processor {
public:
  virtual ~render_processor ()= default;
  virtual void process (const render_chunk_descriptor& descriptor,
                        const std::byte* payload)= 0;
};

class render_service {
public:
  static render_service& instance ();

  std::shared_ptr<render_connection> connect (
    std::shared_ptr<render_processor> process, std::size_t slot_count= 4,
    std::size_t slot_capacity= 4 * 1024 * 1024);
  void shutdown ();
  std::thread::id worker_thread () const noexcept;

private:
  render_service ();
  ~render_service ();
  render_service (const render_service&)= delete;
  render_service& operator = (const render_service&)= delete;

  mutable std::mutex lock_;
  std::condition_variable changed_;
  std::vector<std::weak_ptr<render_connection>> connections_;
  std::thread worker_;
  std::thread::id worker_thread_;
  std::uint64_t wake_generation_;
  bool stopping_;

  void notify () noexcept;
  void ensure_started ();
  void run ();
  void drain_connections ();

  friend class render_connection;
};

class render_connection: public std::enable_shared_from_this<render_connection> {
public:
  ~render_connection ();

  render_connection (const render_connection&)= delete;
  render_connection& operator = (const render_connection&)= delete;

  bool acquire (render_chunk_arena::writable_chunk& chunk);
  bool discard (std::uint32_t slot) noexcept;
  bool publish (const render_chunk_descriptor& descriptor);
  void retire () noexcept;
  bool is_retired () const noexcept;
  render_chunk_arena& arena () noexcept;

private:
  render_connection (render_service& service,
                     std::shared_ptr<render_processor> process,
                     std::size_t slot_count, std::size_t slot_capacity);

  render_service& service_;
  std::shared_ptr<render_processor> process_;
  render_chunk_arena arena_;
  std::atomic<bool> retired_ {false};

  void drain ();

  friend class render_service;
};

#endif // defined RENDER_SERVICE_H

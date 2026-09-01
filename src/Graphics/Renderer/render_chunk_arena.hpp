/******************************************************************************
* MODULE     : render_chunk_arena.hpp
* DESCRIPTION: Zero-copy BufferActor to RenderService transport
* COPYRIGHT  : (C) 2026  Nuaptan F. Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef RENDER_CHUNK_ARENA_H
#define RENDER_CHUNK_ARENA_H

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

struct render_damage {
  std::int64_t x1= 0;
  std::int64_t y1= 0;
  std::int64_t x2= 0;
  std::int64_t y2= 0;
};

struct render_chunk_descriptor {
  std::uint32_t slot= 0;
  std::uint32_t used= 0;
  std::uint64_t buffer_generation= 0;
  std::uint64_t frame_generation= 0;
  render_damage damage;
  bool final_chunk= false;
};

class render_chunk_arena {
public:
  struct writable_chunk {
    std::uint32_t slot= 0;
    std::byte* data= nullptr;
    std::size_t capacity= 0;
  };

  explicit render_chunk_arena (std::size_t slot_count= 4,
                               std::size_t slot_capacity= 4 * 1024 * 1024);
  ~render_chunk_arena ();

  render_chunk_arena (const render_chunk_arena&)= delete;
  render_chunk_arena& operator = (const render_chunk_arena&)= delete;

  // BufferActor producer API.
  bool acquire (writable_chunk& chunk);
  bool discard (std::uint32_t slot) noexcept;
  bool publish (const render_chunk_descriptor& descriptor);

  // RenderService consumer API.
  bool try_submission (render_chunk_descriptor& descriptor);
  bool wait_submission (render_chunk_descriptor& descriptor);
  const std::byte* payload (std::uint32_t slot) const noexcept;
  bool complete (std::uint32_t slot);

  void close () noexcept;
  bool is_closed () const noexcept;
  std::size_t slot_count () const noexcept;
  std::size_t slot_capacity () const noexcept;

private:
  struct alignas(64) sequence_counter {
    std::atomic<std::uint64_t> value {0};
  };

  const std::size_t slot_count_;
  const std::size_t slot_capacity_;
  std::unique_ptr<std::byte[]> payloads_;
  std::vector<render_chunk_descriptor> submissions_;
  std::vector<std::uint32_t> completions_;

  // These counters occupy separate cache lines. Ring entries and payload slots
  // have exactly one writer while their respective descriptor is owned.
  sequence_counter submission_write_;
  sequence_counter submission_read_;
  sequence_counter completion_write_;
  sequence_counter completion_read_;

  std::vector<std::uint32_t> producer_free_slots_;
  std::atomic<bool> closed_ {false};
  mutable std::mutex wait_lock_;
  std::condition_variable submission_ready_;
  std::condition_variable submission_space_;
  std::condition_variable completion_ready_;
  std::condition_variable completion_space_;

  bool reclaim_completion ();
  bool valid_slot (std::uint32_t slot) const noexcept;
};

#endif // defined RENDER_CHUNK_ARENA_H

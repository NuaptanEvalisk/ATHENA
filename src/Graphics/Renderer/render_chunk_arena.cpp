/******************************************************************************
* MODULE     : render_chunk_arena.cpp
* DESCRIPTION: Zero-copy BufferActor to RenderService transport
* COPYRIGHT  : (C) 2026  Nuaptan F. Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "render_chunk_arena.hpp"

#include <limits>
#include <stdexcept>

render_chunk_arena::render_chunk_arena (std::size_t slot_count,
                                        std::size_t slot_capacity):
  slot_count_ (slot_count), slot_capacity_ (slot_capacity),
  payloads_ (), submissions_ (slot_count), completions_ (slot_count) {
  if (slot_count_ < 2)
    throw std::invalid_argument ("render arena needs at least two slots");
  if (slot_count_ > std::numeric_limits<std::uint32_t>::max ())
    throw std::invalid_argument ("render arena has too many slots");
  if (slot_capacity_ == 0 ||
      slot_capacity_ > std::numeric_limits<std::uint32_t>::max ())
    throw std::invalid_argument ("render slot capacity is invalid");
  if (slot_count_ > std::numeric_limits<std::size_t>::max () / slot_capacity_)
    throw std::length_error ("render arena size overflows");

  payloads_= std::make_unique<std::byte[]> (slot_count_ * slot_capacity_);
  producer_free_slots_.reserve (slot_count_);
  for (std::uint32_t slot= static_cast<std::uint32_t> (slot_count_);
       slot != 0; --slot)
    producer_free_slots_.push_back (slot - 1);
}

render_chunk_arena::~render_chunk_arena () {
  close ();
}

bool
render_chunk_arena::reclaim_completion () {
  std::uint32_t slot;
  {
    std::lock_guard<std::mutex> guard (wait_lock_);
    std::uint64_t read=
      completion_read_.value.load (std::memory_order_relaxed);
    std::uint64_t write=
      completion_write_.value.load (std::memory_order_acquire);
    if (read == write) return false;

    slot= completions_[read % slot_count_];
    completion_read_.value.store (read + 1, std::memory_order_release);
  }
  producer_free_slots_.push_back (slot);
  completion_space_.notify_one ();
  return true;
}

bool
render_chunk_arena::acquire (writable_chunk& chunk) {
  if (closed_.load (std::memory_order_acquire)) return false;
  while (producer_free_slots_.empty ()) {
    if (reclaim_completion ()) continue;
    std::unique_lock<std::mutex> guard (wait_lock_);
    completion_ready_.wait (guard, [this] {
      return closed_.load (std::memory_order_acquire) ||
             completion_read_.value.load (std::memory_order_relaxed) !=
             completion_write_.value.load (std::memory_order_acquire);
    });
    if (closed_.load (std::memory_order_acquire)) return false;
  }

  std::uint32_t slot= producer_free_slots_.back ();
  producer_free_slots_.pop_back ();
  chunk.slot= slot;
  chunk.data= payloads_.get () + slot * slot_capacity_;
  chunk.capacity= slot_capacity_;
  return true;
}

bool
render_chunk_arena::discard (std::uint32_t slot) noexcept {
  if (!valid_slot (slot) || closed_.load (std::memory_order_acquire))
    return false;
  producer_free_slots_.push_back (slot);
  return true;
}

bool
render_chunk_arena::publish (const render_chunk_descriptor& descriptor) {
  if (!valid_slot (descriptor.slot) || descriptor.used > slot_capacity_)
    return false;

  {
    std::unique_lock<std::mutex> guard (wait_lock_);
    std::uint64_t write=
      submission_write_.value.load (std::memory_order_relaxed);
    submission_space_.wait (guard, [this, write] {
      return closed_.load (std::memory_order_acquire) ||
             write - submission_read_.value.load (std::memory_order_acquire) <
               slot_count_;
    });
    if (closed_.load (std::memory_order_acquire)) return false;

    submissions_[write % slot_count_]= descriptor;
    submission_write_.value.store (write + 1, std::memory_order_release);
  }
  submission_ready_.notify_one ();
  return true;
}

bool
render_chunk_arena::try_submission (render_chunk_descriptor& descriptor) {
  {
    std::lock_guard<std::mutex> guard (wait_lock_);
    std::uint64_t read=
      submission_read_.value.load (std::memory_order_relaxed);
    if (read == submission_write_.value.load (std::memory_order_acquire))
      return false;

    descriptor= submissions_[read % slot_count_];
    submission_read_.value.store (read + 1, std::memory_order_release);
  }
  submission_space_.notify_one ();
  return true;
}

bool
render_chunk_arena::wait_submission (render_chunk_descriptor& descriptor) {
  {
    std::unique_lock<std::mutex> guard (wait_lock_);
    submission_ready_.wait (guard, [this] {
      return closed_.load (std::memory_order_acquire) ||
             submission_read_.value.load (std::memory_order_relaxed) !=
               submission_write_.value.load (std::memory_order_acquire);
    });
    std::uint64_t read=
      submission_read_.value.load (std::memory_order_relaxed);
    if (read == submission_write_.value.load (std::memory_order_acquire))
      return false;

    descriptor= submissions_[read % slot_count_];
    submission_read_.value.store (read + 1, std::memory_order_release);
  }
  submission_space_.notify_one ();
  return true;
}

const std::byte*
render_chunk_arena::payload (std::uint32_t slot) const noexcept {
  if (!valid_slot (slot)) return nullptr;
  return payloads_.get () + slot * slot_capacity_;
}

bool
render_chunk_arena::complete (std::uint32_t slot) {
  if (!valid_slot (slot)) return false;
  {
    std::unique_lock<std::mutex> guard (wait_lock_);
    std::uint64_t write=
      completion_write_.value.load (std::memory_order_relaxed);
    completion_space_.wait (guard, [this, write] {
      return closed_.load (std::memory_order_acquire) ||
             write - completion_read_.value.load (std::memory_order_acquire) <
               slot_count_;
    });
    if (closed_.load (std::memory_order_acquire)) return false;

    completions_[write % slot_count_]= slot;
    completion_write_.value.store (write + 1, std::memory_order_release);
  }
  completion_ready_.notify_one ();
  return true;
}

void
render_chunk_arena::close () noexcept {
  {
    std::lock_guard<std::mutex> guard (wait_lock_);
    closed_.store (true, std::memory_order_release);
  }
  submission_ready_.notify_all ();
  submission_space_.notify_all ();
  completion_ready_.notify_all ();
  completion_space_.notify_all ();
}

bool
render_chunk_arena::is_closed () const noexcept {
  return closed_.load (std::memory_order_acquire);
}

std::size_t
render_chunk_arena::slot_count () const noexcept {
  return slot_count_;
}

std::size_t
render_chunk_arena::slot_capacity () const noexcept {
  return slot_capacity_;
}

bool
render_chunk_arena::valid_slot (std::uint32_t slot) const noexcept {
  return slot < slot_count_;
}

/******************************************************************************
* MODULE     : actor_transport.cpp
* DESCRIPTION: ID-only zero-copy actor command transport
* COPYRIGHT  : (C) 2026  Nuaptan F. Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "actor_transport.hpp"

#include <limits>
#include <new>
#include <stdexcept>
#include <unordered_map>

actor_command_transport::actor_command_transport (std::size_t slot_count):
  slot_count_ (slot_count), records_ (), submissions_ (slot_count),
  completions_ (slot_count) {
  if (slot_count_ < 2)
    throw std::invalid_argument ("actor transport needs at least two slots");
  if (slot_count_ > std::numeric_limits<athena_slot_id>::max ())
    throw std::invalid_argument ("actor transport has too many slots");

  records_= std::make_unique<actor_command_record[]> (slot_count_);
  producer_free_slots_.reserve (slot_count_);
  for (athena_slot_id slot= static_cast<athena_slot_id> (slot_count_);
       slot != 0; --slot)
    producer_free_slots_.push_back (slot - 1);
}

actor_command_transport::~actor_command_transport () {
  close ();
}

bool
actor_command_transport::reclaim_completion () {
  athena_slot_id slot;
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
  records_[slot]= actor_command_record {};
  producer_free_slots_.push_back (slot);
  completion_space_.notify_one ();
  return true;
}

bool
actor_command_transport::acquire (writable_command& command) {
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

  athena_slot_id slot= producer_free_slots_.back ();
  producer_free_slots_.pop_back ();
  records_[slot]= actor_command_record {};
  command.slot= slot;
  command.record= &records_[slot];
  return true;
}

bool
actor_command_transport::try_acquire (writable_command& command) {
  if (closed_.load (std::memory_order_acquire)) return false;
  while (producer_free_slots_.empty () && reclaim_completion ()) {}
  if (producer_free_slots_.empty ()) return false;

  athena_slot_id slot= producer_free_slots_.back ();
  producer_free_slots_.pop_back ();
  records_[slot]= actor_command_record {};
  command.slot= slot;
  command.record= &records_[slot];
  return true;
}

bool
actor_command_transport::discard (athena_slot_id slot) noexcept {
  if (!valid_slot (slot) || closed_.load (std::memory_order_acquire))
    return false;
  records_[slot]= actor_command_record {};
  producer_free_slots_.push_back (slot);
  return true;
}

bool
actor_command_transport::publish (athena_slot_id slot) {
  if (!valid_slot (slot) ||
      closed_.load (std::memory_order_acquire)) return false;

  {
    std::unique_lock<std::mutex> guard (wait_lock_);
    std::uint64_t write=
      submission_write_.value.load (std::memory_order_relaxed);
    command_space_.wait (guard, [this, write] {
      return closed_.load (std::memory_order_acquire) ||
             write - submission_read_.value.load (std::memory_order_acquire) <
               slot_count_;
    });
    if (closed_.load (std::memory_order_acquire)) return false;

    submissions_[write % slot_count_]= slot;
    submission_write_.value.store (write + 1, std::memory_order_release);
  }
  command_ready_.notify_one ();
  return true;
}

bool
actor_command_transport::try_command (readable_command& command) {
  athena_slot_id slot;
  {
    std::lock_guard<std::mutex> guard (wait_lock_);
    std::uint64_t read=
      submission_read_.value.load (std::memory_order_relaxed);
    if (read == submission_write_.value.load (std::memory_order_acquire))
      return false;

    slot= submissions_[read % slot_count_];
    submission_read_.value.store (read + 1, std::memory_order_release);
  }
  command_space_.notify_one ();
  command.slot= slot;
  command.record= &records_[slot];
  return true;
}

bool
actor_command_transport::wait_command (readable_command& command) {
  athena_slot_id slot;
  {
    std::unique_lock<std::mutex> guard (wait_lock_);
    command_ready_.wait (guard, [this] {
      return closed_.load (std::memory_order_acquire) ||
             submission_read_.value.load (std::memory_order_relaxed) !=
               submission_write_.value.load (std::memory_order_acquire);
    });
    std::uint64_t read=
      submission_read_.value.load (std::memory_order_relaxed);
    if (read == submission_write_.value.load (std::memory_order_acquire))
      return false;

    slot= submissions_[read % slot_count_];
    submission_read_.value.store (read + 1, std::memory_order_release);
  }
  command_space_.notify_one ();
  command.slot= slot;
  command.record= &records_[slot];
  return true;
}

bool
actor_command_transport::complete (athena_slot_id slot) {
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
actor_command_transport::close () noexcept {
  {
    std::lock_guard<std::mutex> guard (wait_lock_);
    closed_.store (true, std::memory_order_release);
  }
  command_ready_.notify_all ();
  command_space_.notify_all ();
  completion_ready_.notify_all ();
  completion_space_.notify_all ();
}

bool
actor_command_transport::is_closed () const noexcept {
  return closed_.load (std::memory_order_acquire);
}

std::size_t
actor_command_transport::slot_count () const noexcept {
  return slot_count_;
}

const actor_command_record*
actor_command_transport::record (athena_slot_id slot) const noexcept {
  return valid_slot (slot) ? &records_[slot] : nullptr;
}

bool
actor_command_transport::valid_slot (athena_slot_id slot) const noexcept {
  return slot < slot_count_;
}

struct owned_actor_blob::storage {
  std::unique_ptr<std::byte[]> bytes;
  std::size_t size;

  explicit storage (std::size_t size2):
    bytes (size2 == 0 ? nullptr : std::make_unique<std::byte[]> (size2)),
    size (size2) {}
};

owned_actor_blob::owned_actor_blob () noexcept= default;
owned_actor_blob::owned_actor_blob (owned_actor_blob&&) noexcept= default;
owned_actor_blob&
owned_actor_blob::operator = (owned_actor_blob&&) noexcept= default;
owned_actor_blob::~owned_actor_blob ()= default;

owned_actor_blob::owned_actor_blob (
  std::unique_ptr<storage> storage2) noexcept:
  storage_ (std::move (storage2)) {}

std::byte*
owned_actor_blob::data () noexcept {
  return storage_ == nullptr ? nullptr : storage_->bytes.get ();
}

const std::byte*
owned_actor_blob::data () const noexcept {
  return storage_ == nullptr ? nullptr : storage_->bytes.get ();
}

std::size_t
owned_actor_blob::size () const noexcept {
  return storage_ == nullptr ? 0 : storage_->size;
}

owned_actor_blob::operator bool () const noexcept {
  return storage_ != nullptr;
}

struct actor_blob_registry::implementation {
  mutable std::mutex lock;
  std::unordered_map<athena_blob_id,
                     std::unique_ptr<owned_actor_blob::storage>> blobs;
  athena_blob_id next_id= 1;
};

actor_blob_reservation::actor_blob_reservation () noexcept:
  id_ (ATHENA_NO_BLOB), data_ (nullptr), size_ (0) {}

actor_blob_reservation::actor_blob_reservation (
  athena_blob_id id, std::byte* data, std::size_t size) noexcept:
  id_ (id), data_ (data), size_ (size) {}

actor_blob_reservation::actor_blob_reservation (
  actor_blob_reservation&& other) noexcept:
  id_ (other.id_), data_ (other.data_), size_ (other.size_) {
  other.id_= ATHENA_NO_BLOB;
  other.data_= nullptr;
  other.size_= 0;
}

actor_blob_reservation&
actor_blob_reservation::operator = (actor_blob_reservation&& other) noexcept {
  if (this == &other) return *this;
  reset ();
  id_= other.id_;
  data_= other.data_;
  size_= other.size_;
  other.id_= ATHENA_NO_BLOB;
  other.data_= nullptr;
  other.size_= 0;
  return *this;
}

actor_blob_reservation::~actor_blob_reservation () {
  reset ();
}

athena_blob_id
actor_blob_reservation::id () const noexcept {
  return id_;
}

std::byte*
actor_blob_reservation::data () noexcept {
  return data_;
}

std::size_t
actor_blob_reservation::size () const noexcept {
  return size_;
}

athena_blob_id
actor_blob_reservation::publish () noexcept {
  athena_blob_id result= id_;
  id_= ATHENA_NO_BLOB;
  data_= nullptr;
  size_= 0;
  return result;
}

actor_blob_reservation::operator bool () const noexcept {
  return id_ != ATHENA_NO_BLOB;
}

void
actor_blob_reservation::reset () noexcept {
  if (id_ != ATHENA_NO_BLOB)
    (void) actor_blob_registry::instance ().discard (id_);
  id_= ATHENA_NO_BLOB;
  data_= nullptr;
  size_= 0;
}

actor_blob_registry&
actor_blob_registry::instance () {
  static actor_blob_registry registry;
  return registry;
}

actor_blob_registry::actor_blob_registry ():
  impl_ (std::make_unique<implementation> ()) {}

actor_blob_registry::~actor_blob_registry ()= default;

actor_blob_reservation
actor_blob_registry::allocate (std::size_t size) {
  auto storage= std::make_unique<owned_actor_blob::storage> (size);
  std::byte* data= storage->bytes.get ();
  std::lock_guard<std::mutex> guard (impl_->lock);
  athena_blob_id id= impl_->next_id++;
  if (id == ATHENA_NO_BLOB)
    throw std::overflow_error ("actor blob id space exhausted");
  impl_->blobs.emplace (id, std::move (storage));
  return actor_blob_reservation (id, data, size);
}

owned_actor_blob
actor_blob_registry::take (athena_blob_id id) noexcept {
  if (id == ATHENA_NO_BLOB) return owned_actor_blob ();
  std::unique_ptr<owned_actor_blob::storage> storage;
  {
    std::lock_guard<std::mutex> guard (impl_->lock);
    auto found= impl_->blobs.find (id);
    if (found == impl_->blobs.end ()) return owned_actor_blob ();
    storage= std::move (found->second);
    impl_->blobs.erase (found);
  }
  return owned_actor_blob (std::move (storage));
}

bool
actor_blob_registry::discard (athena_blob_id id) noexcept {
  if (id == ATHENA_NO_BLOB) return false;
  std::lock_guard<std::mutex> guard (impl_->lock);
  return impl_->blobs.erase (id) != 0;
}

std::size_t
actor_blob_registry::outstanding () const noexcept {
  std::lock_guard<std::mutex> guard (impl_->lock);
  return impl_->blobs.size ();
}

struct actor_text_registry::implementation {
  mutable std::mutex lock;
  std::unordered_map<athena_blob_id, string> texts;
  athena_blob_id next_id= 1;
};

actor_text_reservation::actor_text_reservation () noexcept:
  id_ (ATHENA_NO_BLOB), data_ (nullptr), size_ (0) {}

actor_text_reservation::actor_text_reservation (
  athena_blob_id id, char* data, std::size_t size) noexcept:
  id_ (id), data_ (data), size_ (size) {}

actor_text_reservation::actor_text_reservation (
  actor_text_reservation&& other) noexcept:
  id_ (other.id_), data_ (other.data_), size_ (other.size_) {
  other.id_= ATHENA_NO_BLOB;
  other.data_= nullptr;
  other.size_= 0;
}

actor_text_reservation&
actor_text_reservation::operator = (actor_text_reservation&& other) noexcept {
  if (this == &other) return *this;
  reset ();
  id_= other.id_;
  data_= other.data_;
  size_= other.size_;
  other.id_= ATHENA_NO_BLOB;
  other.data_= nullptr;
  other.size_= 0;
  return *this;
}

actor_text_reservation::~actor_text_reservation () {
  reset ();
}

athena_blob_id
actor_text_reservation::id () const noexcept {
  return id_;
}

char*
actor_text_reservation::data () noexcept {
  return data_;
}

std::size_t
actor_text_reservation::size () const noexcept {
  return size_;
}

athena_blob_id
actor_text_reservation::publish () noexcept {
  athena_blob_id result= id_;
  id_= ATHENA_NO_BLOB;
  data_= nullptr;
  size_= 0;
  return result;
}

actor_text_reservation::operator bool () const noexcept {
  return id_ != ATHENA_NO_BLOB;
}

void
actor_text_reservation::reset () noexcept {
  if (id_ != ATHENA_NO_BLOB)
    (void) actor_text_registry::instance ().discard (id_);
  id_= ATHENA_NO_BLOB;
  data_= nullptr;
  size_= 0;
}

actor_text_registry&
actor_text_registry::instance () {
  static actor_text_registry registry;
  return registry;
}

actor_text_registry::actor_text_registry ():
  impl_ (std::make_unique<implementation> ()) {}

actor_text_registry::~actor_text_registry ()= default;

actor_text_reservation
actor_text_registry::allocate (std::size_t size) {
  if (size > static_cast<std::size_t> (std::numeric_limits<int>::max ()))
    throw std::length_error ("actor text is too large");
  string text= string::transferable (static_cast<int> (size));
  char* data= text.mutable_data ();
  std::lock_guard<std::mutex> guard (impl_->lock);
  athena_blob_id id= impl_->next_id++;
  if (id == ATHENA_NO_BLOB)
    throw std::overflow_error ("actor text id space exhausted");
  impl_->texts.emplace (id, std::move (text));
  return actor_text_reservation (id, data, size);
}

athena_blob_id
actor_text_registry::store (string text) {
  text.ensure_transferable ();
  std::lock_guard<std::mutex> guard (impl_->lock);
  athena_blob_id id= impl_->next_id++;
  if (id == ATHENA_NO_BLOB)
    throw std::overflow_error ("actor text id space exhausted");
  impl_->texts.emplace (id, std::move (text));
  return id;
}

string
actor_text_registry::take (athena_blob_id id) noexcept {
  if (id == ATHENA_NO_BLOB) return string ();
  string result;
  {
    std::lock_guard<std::mutex> guard (impl_->lock);
    auto found= impl_->texts.find (id);
    if (found == impl_->texts.end ()) return result;
    result= std::move (found->second);
    impl_->texts.erase (found);
  }
  return result;
}

bool
actor_text_registry::discard (athena_blob_id id) noexcept {
  if (id == ATHENA_NO_BLOB) return false;
  std::lock_guard<std::mutex> guard (impl_->lock);
  return impl_->texts.erase (id) != 0;
}

struct actor_tree_registry::implementation {
  mutable std::mutex lock;
  std::unordered_map<athena_blob_id, tree> trees;
  athena_blob_id next_id= 1;
};

actor_tree_registry&
actor_tree_registry::instance () {
  static actor_tree_registry registry;
  return registry;
}

actor_tree_registry::actor_tree_registry ():
  impl_ (std::make_unique<implementation> ()) {}

actor_tree_registry::~actor_tree_registry ()= default;

athena_blob_id
actor_tree_registry::store (tree value) {
  std::lock_guard<std::mutex> guard (impl_->lock);
  athena_blob_id id= impl_->next_id++;
  if (id == ATHENA_NO_BLOB)
    throw std::overflow_error ("actor tree id space exhausted");
  impl_->trees.emplace (id, std::move (value));
  return id;
}

tree
actor_tree_registry::take (athena_blob_id id) {
  if (id == ATHENA_NO_BLOB) return tree ();
  std::lock_guard<std::mutex> guard (impl_->lock);
  auto found= impl_->trees.find (id);
  if (found == impl_->trees.end ()) return tree ();
  tree result= std::move (found->second);
  impl_->trees.erase (found);
  return result;
}

bool
actor_tree_registry::discard (athena_blob_id id) noexcept {
  if (id == ATHENA_NO_BLOB) return false;
  std::lock_guard<std::mutex> guard (impl_->lock);
  return impl_->trees.erase (id) != 0;
}

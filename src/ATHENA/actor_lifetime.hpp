/******************************************************************************
* MODULE     : actor_lifetime.hpp
* DESCRIPTION: Pin an actor during ID lookup, without locking its execution
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* See the file LICENSE in the root directory.
******************************************************************************/

#ifndef ACTOR_LIFETIME_HPP
#define ACTOR_LIFETIME_HPP

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <utility>

// The owner calls close() before destroying T. A lease protects only lifetime;
// it does not grant access to T's document or replace its mailbox protocol.
// No mutex remains held during a caller's invocation/submission.
template<typename T>
class actor_lifetime {
  struct state {
    explicit state (T* value): owner (value) {}
    std::mutex mutex;
    std::condition_variable drained;
    T* owner;
    std::size_t users= 0;
  };

public:
  class lease {
  public:
    lease () noexcept= default;
    lease (const lease&)= delete;
    lease& operator = (const lease&)= delete;
    lease (lease&& other) noexcept:
      state_ (std::move (other.state_)), owner_ (other.owner_) {
      other.owner_= nullptr;
    }
    lease& operator = (lease&& other) noexcept {
      if (this != &other) {
        release ();
        state_= std::move (other.state_);
        owner_= other.owner_;
        other.owner_= nullptr;
      }
      return *this;
    }
    ~lease () { release (); }

    explicit operator bool () const noexcept { return owner_ != nullptr; }
    T* get () const noexcept { return owner_; }
    T* operator -> () const noexcept { return owner_; }

  private:
    friend class actor_lifetime;
    lease (std::shared_ptr<state> shared, T* owner) noexcept:
      state_ (std::move (shared)), owner_ (owner) {}

    void release () noexcept {
      if (!state_) return;
      {
        std::lock_guard<std::mutex> guard (state_->mutex);
        --state_->users;
        if (state_->users == 0) state_->drained.notify_all ();
      }
      state_.reset ();
      owner_= nullptr;
    }

    std::shared_ptr<state> state_;
    T* owner_= nullptr;
  };

  explicit actor_lifetime (T* owner): state_ (std::make_shared<state> (owner)) {}
  actor_lifetime (const actor_lifetime&)= delete;
  actor_lifetime& operator = (const actor_lifetime&)= delete;

  lease acquire () const {
    std::lock_guard<std::mutex> guard (state_->mutex);
    if (state_->owner == nullptr) return {};
    ++state_->users;
    return lease (state_, state_->owner);
  }

  // May wait for an already-started submission/invocation, but never for new
  // work. Removing an ID from the registry precedes this call. It is called
  // by the UI owner, not by an actor holding a lease to itself.
  void close () {
    std::unique_lock<std::mutex> guard (state_->mutex);
    state_->owner= nullptr;
    state_->drained.wait (guard, [this] { return state_->users == 0; });
  }

private:
  std::shared_ptr<state> state_;
};

#endif // ACTOR_LIFETIME_HPP

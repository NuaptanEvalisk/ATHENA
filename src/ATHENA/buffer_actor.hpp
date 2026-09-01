/******************************************************************************
* MODULE     : buffer_actor.hpp
* DESCRIPTION: Per-buffer execution owner and FIFO mailbox
* COPYRIGHT  : (C) 2026  Nuaptan F. Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef BUFFER_ACTOR_H
#define BUFFER_ACTOR_H

#include "scheme_execution_context.hpp"
#include "string.hpp"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

class editor_rep;
class tm_buffer_rep;

class buffer_actor {
public:
  using task= std::function<void ()>;

  explicit buffer_actor (tm_buffer_rep* owner);
  ~buffer_actor ();

  buffer_actor (const buffer_actor&)= delete;
  buffer_actor& operator = (const buffer_actor&)= delete;

  bool post_native (
    task work, editor_rep* editor= nullptr, string view_id= string (),
    SchemeCapabilitySet capabilities= SCHEME_CAPABILITY_BUFFER);
  bool post_scheme (
    task work, editor_rep* editor= nullptr, string view_id= string (),
    SchemeCapabilitySet capabilities= SCHEME_CAPABILITY_BUFFER);

  template<typename Function>
  auto invoke_native (Function&& function)
    -> typename std::invoke_result<Function>::type {
    return invoke (false, std::forward<Function> (function));
  }

  template<typename Function>
  auto invoke_scheme (Function&& function)
    -> typename std::invoke_result<Function>::type {
    return invoke (true, std::forward<Function> (function));
  }

  void update_buffer_id (string id);
  void wait_until_idle ();
  void shutdown ();

  bool is_owner_thread () const noexcept;
  std::thread::id owner_thread () const noexcept;
  std::uint64_t completed_commands () const noexcept;

private:
  struct message {
    std::uint64_t command_id;
    bool uses_scheme;
    editor_rep* editor;
    string view_id;
    SchemeCapabilitySet capabilities;
    task work;
  };

  tm_buffer_rep* owner_;
  string buffer_id_;
  mutable std::mutex lock_;
  std::condition_variable changed_;
  std::condition_variable idle_;
  std::condition_variable started_;
  std::deque<message> mailbox_;
  std::thread worker_;
  std::thread::id owner_thread_;
  std::uint64_t next_command_id_;
  std::uint64_t completed_commands_;
  bool accepting_;
  bool stopping_;
  bool executing_;
  bool started_flag_;

  bool post (bool uses_scheme, task work, editor_rep* editor,
             string view_id, SchemeCapabilitySet capabilities);
  bool ensure_started ();
  void run ();
  void execute (message& command);

  template<typename Function>
  auto invoke (bool uses_scheme, Function&& function)
    -> typename std::invoke_result<Function>::type {
    using result_type= typename std::invoke_result<Function>::type;
    if (is_owner_thread ()) return std::forward<Function> (function) ();

    auto promise= std::make_shared<std::promise<result_type>> ();
    std::future<result_type> result= promise->get_future ();
    auto callable= std::make_shared<typename std::decay<Function>::type> (
      std::forward<Function> (function));
    task wrapped= [promise, callable] () {
      try {
        if constexpr (std::is_void<result_type>::value) {
          (*callable) ();
          promise->set_value ();
        }
        else promise->set_value ((*callable) ());
      }
      catch (...) { promise->set_exception (std::current_exception ()); }
    };

    bool accepted= uses_scheme ? post_scheme (std::move (wrapped))
                               : post_native (std::move (wrapped));
    if (!accepted)
      throw std::runtime_error ("buffer actor no longer accepts commands");
    return result.get ();
  }
};

#endif // defined BUFFER_ACTOR_H

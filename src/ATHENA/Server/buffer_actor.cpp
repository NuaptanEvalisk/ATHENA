/******************************************************************************
* MODULE     : buffer_actor.cpp
* DESCRIPTION: Per-buffer execution owner and FIFO mailbox
* COPYRIGHT  : (C) 2026  Nuaptan F. Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "buffer_actor.hpp"

#include "editor.hpp"
#include "guile_tm.hpp"
#include "tm_buffer.hpp"

#include <cstdio>

namespace {

struct scheme_task_request {
  buffer_actor::task* work;
  std::exception_ptr failure;
};

tmscm
execute_scheme_task (void* raw) {
  scheme_task_request* request= static_cast<scheme_task_request*> (raw);
  try { (*request->work) (); }
  catch (...) { request->failure= std::current_exception (); }
  return TMSCM_UNSPECIFIED;
}

struct guile_task_request {
  const SchemeExecutionContext* context;
  buffer_actor::task* work;
  std::exception_ptr failure;
};

void*
execute_task_with_guile (void* raw) {
  guile_task_request* request= static_cast<guile_task_request*> (raw);
  scheme_task_request task_request= { request->work, nullptr };
  try {
    (void) scheme_with_execution_context (
      *request->context, execute_scheme_task, &task_request);
    scheme_runtime_safe_point ();
  }
  catch (...) { request->failure= std::current_exception (); }
  if (task_request.failure != nullptr)
    request->failure= task_request.failure;
  return nullptr;
}

void
report_unhandled_actor_exception (std::exception_ptr failure) noexcept {
  try {
    if (failure != nullptr) std::rethrow_exception (failure);
  }
  catch (const std::exception& error) {
    std::fprintf (stderr, "ATHENA] buffer actor command failed: %s\n",
                  error.what ());
  }
  catch (...) {
    std::fprintf (stderr, "ATHENA] buffer actor command failed\n");
  }
}

} // namespace

buffer_actor::buffer_actor (tm_buffer_rep* owner):
  owner_ (owner), buffer_id_ (as_string (owner->buf->name)),
  owner_thread_ (), next_command_id_ (1), completed_commands_ (0),
  accepting_ (true), stopping_ (false), executing_ (false),
  started_flag_ (false) {}

buffer_actor::~buffer_actor () {
  shutdown ();
}

bool
buffer_actor::post_native (task work, editor_rep* editor, string view_id,
                           SchemeCapabilitySet capabilities) {
  return post (false, std::move (work), editor, std::move (view_id),
               capabilities);
}

bool
buffer_actor::post_scheme (task work, editor_rep* editor, string view_id,
                           SchemeCapabilitySet capabilities) {
  if (!scheme_runtime_is_initialized ()) return false;
  return post (true, std::move (work), editor, std::move (view_id),
               capabilities);
}

bool
buffer_actor::post (bool uses_scheme, task work, editor_rep* editor,
                    string view_id, SchemeCapabilitySet capabilities) {
  if (!work) return false;
  if (!ensure_started ()) return false;
  std::lock_guard<std::mutex> guard (lock_);
  if (!accepting_) return false;
  mailbox_.push_back ({ next_command_id_++, uses_scheme, editor,
                        std::move (view_id), capabilities,
                        std::move (work) });
  changed_.notify_one ();
  return true;
}

bool
buffer_actor::ensure_started () {
  std::unique_lock<std::mutex> guard (lock_);
  if (!accepting_) return false;
  if (!worker_.joinable ())
    worker_= std::thread ([this] { run (); });
  started_.wait (guard, [this] { return started_flag_ || !accepting_; });
  return started_flag_ && accepting_;
}

void
buffer_actor::update_buffer_id (string id) {
  invoke_native ([this, id= std::move (id)] () mutable {
    buffer_id_= std::move (id);
  });
}

void
buffer_actor::wait_until_idle () {
  if (is_owner_thread ()) return;
  std::unique_lock<std::mutex> guard (lock_);
  idle_.wait (guard, [this] { return mailbox_.empty () && !executing_; });
}

void
buffer_actor::shutdown () {
  if (is_owner_thread ()) {
    std::fprintf (stderr,
      "ATHENA] refusing to join a buffer actor from its own thread\n");
    std::terminate ();
  }
  bool has_worker;
  {
    std::lock_guard<std::mutex> guard (lock_);
    if (!accepting_ && !worker_.joinable ()) return;
    accepting_= false;
    stopping_= true;
    has_worker= worker_.joinable ();
  }
  if (!has_worker) return;
  changed_.notify_one ();
  worker_.join ();
}

bool
buffer_actor::is_owner_thread () const noexcept {
  std::lock_guard<std::mutex> guard (lock_);
  return started_flag_ && owner_thread_ == std::this_thread::get_id ();
}

std::thread::id
buffer_actor::owner_thread () const noexcept {
  std::lock_guard<std::mutex> guard (lock_);
  return owner_thread_;
}

std::uint64_t
buffer_actor::completed_commands () const noexcept {
  std::lock_guard<std::mutex> guard (lock_);
  return completed_commands_;
}

void
buffer_actor::run () {
  {
    std::lock_guard<std::mutex> guard (lock_);
    owner_thread_= std::this_thread::get_id ();
    started_flag_= true;
  }
  started_.notify_all ();

  while (true) {
    message command;
    {
      std::unique_lock<std::mutex> guard (lock_);
      changed_.wait (guard,
        [this] { return stopping_ || !mailbox_.empty (); });
      if (mailbox_.empty ()) {
        if (stopping_) break;
        continue;
      }
      command= std::move (mailbox_.front ());
      mailbox_.pop_front ();
      executing_= true;
    }

    execute (command);

    {
      std::lock_guard<std::mutex> guard (lock_);
      executing_= false;
      ++completed_commands_;
      if (mailbox_.empty ()) idle_.notify_all ();
    }
  }

  {
    std::lock_guard<std::mutex> guard (lock_);
    executing_= false;
    idle_.notify_all ();
  }
}

void
buffer_actor::execute (message& command) {
  drd_info* drd= command.editor == nullptr ? nullptr : &command.editor->drd;
  SchemeExecutionContext context (
    this, command.editor, drd, &owner_->document, url (buffer_id_),
    command.view_id == "" ? url_none () : url (command.view_id),
    command.command_id, command.capabilities);

  std::exception_ptr failure;
  if (command.uses_scheme) {
    guile_task_request request= { &context, &command.work, nullptr };
    scm_with_guile (execute_task_with_guile, &request);
    failure= request.failure;
  }
  else {
    try {
      SchemeExecutionScope scope (context);
      command.work ();
    }
    catch (...) { failure= std::current_exception (); }
  }
  report_unhandled_actor_exception (failure);
}

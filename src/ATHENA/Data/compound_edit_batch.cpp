/******************************************************************************
* MODULE     : compound_edit_batch.cpp
* DESCRIPTION: Nonblocking admission and Guile-safe multi-actor edit barriers
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "compound_edit_batch.hpp"
#include "buffer_actor.hpp"
#include "editor.hpp"
#include "scheme.hpp"
#include "guile_tm.hpp"
#include <chrono>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>

namespace athena::avd {
namespace {

std::mutex reservation_mutex;
std::set<athena_actor_id> reservations;

struct callback_call {
  const std::function<void(editor_rep&)>& function;
  editor_rep& editor;
  std::exception_ptr failure;
  bool scheme_failure= false;
};

SCM call_body (void* data) {
  auto& call= *static_cast<callback_call*> (data);
  try { call.function (call.editor); }
  catch (...) { call.failure= std::current_exception (); }
  return SCM_UNSPECIFIED;
}

SCM call_handler (void* data, SCM, SCM) {
  static_cast<callback_call*> (data)->scheme_failure= true;
  return SCM_UNSPECIFIED;
}

void call (const std::function<void(editor_rep&)>& function, editor_rep& editor) {
  // A Scheme throw must be caught inside the rendezvous, not by the actor's
  // outer command handler after it has abandoned the other participants.
  callback_call invocation {function, editor, {}, false};
  scm_c_catch (SCM_BOOL_T, call_body, &invocation, call_handler, &invocation,
               nullptr, nullptr);
  if (invocation.failure) std::rethrow_exception (invocation.failure);
  if (invocation.scheme_failure)
    throw std::runtime_error ("Scheme exception in compound edit callback");
}

std::string exception_message () {
  try { throw; }
  catch (const std::exception& error) { return error.what (); }
  catch (const string& error) { return std::string (error.c_str (), N(error)); }
  catch (...) { return "Unknown compound edit failure"; }
}

struct batch {
  std::vector<edit_participant> members;
  std::function<void(std::string)> completion;
  std::mutex mutex;
  std::condition_variable changed;
  const std::chrono::steady_clock::time_point deadline=
    std::chrono::steady_clock::now () + std::chrono::seconds (5);
  std::size_t prepared= 0, applied= 0, finished= 0;
  bool admitted= false, preparation_failed= false, apply_failed= false;
  bool completed= false, completion_finished= false;
  std::string error;

  void fail (std::string message, bool preparing) {
    {
      std::lock_guard<std::mutex> lock (mutex);
      if (error.empty ()) error= std::move (message);
      if (preparing) preparation_failed= true;
      else apply_failed= true;
    }
    changed.notify_all ();
  }

  void complete () {
    std::string result;
    {
      std::lock_guard<std::mutex> lock (mutex);
      if (completed) return;
      completed= true;
      result= error;
    }
    {
      std::lock_guard<std::mutex> lock (reservation_mutex);
      for (const auto& member: members) reservations.erase (member.actor);
    }
    try { completion (std::move (result)); }
    catch (...) {
      std_error << "Compound edit completion: " << string (exception_message ().c_str ()) << LF;
    }
    {
      std::lock_guard<std::mutex> lock (mutex);
      completion_finished= true;
    }
    changed.notify_all ();
  }

  struct wait_request { batch* group; unsigned stage; };
  static void* wait_without_guile (void* data) {
    auto& request= *static_cast<wait_request*> (data);
    auto& group= *request.group;
    std::unique_lock<std::mutex> lock (group.mutex);
    if (request.stage == 0) {
      if (!group.changed.wait_until (lock, group.deadline, [&] {
            return group.preparation_failed ||
              (group.admitted && group.prepared == group.members.size ());
          })) {
        group.preparation_failed= true;
        if (group.error.empty ())
          group.error= "Source actors are busy; no compound edit was performed";
        group.changed.notify_all ();
      }
    }
    else if (request.stage == 3) group.changed.wait (lock, [&] {
      return group.completion_finished;
    });
    else group.changed.wait (lock, [&] {
      return (request.stage == 1 ? group.applied : group.finished) ==
             group.members.size ();
    });
    return nullptr;
  }

  void wait (unsigned stage) {
    wait_request request {this, stage};
    scm_without_guile (wait_without_guile, &request);
  }

  void run (std::size_t index) {
    auto& member= members[index];
    editor_rep* editor= nullptr;
    try {
      {
        std::lock_guard<std::mutex> lock (mutex);
        if (preparation_failed) return;
      }
      const auto* context= current_scheme_execution_context ();
      editor= context ? context->editor : nullptr;
      if (editor == nullptr) throw std::runtime_error ("Compound source view was closed");
      call (member.prepare, *editor);
    }
    catch (...) { fail (exception_message (), true); }
    {
      std::lock_guard<std::mutex> lock (mutex);
      ++prepared;
    }
    changed.notify_all ();
    wait (0);
    bool cancelled;
    {
      std::lock_guard<std::mutex> lock (mutex);
      cancelled= preparation_failed;
    }
    if (cancelled) { complete (); return; }

    // All participants now occupy their owner actors. Later edit/save/read
    // commands remain queued until every member has committed or rolled back.
    try { call (member.apply, *editor); }
    catch (...) { fail (exception_message (), false); }
    {
      std::lock_guard<std::mutex> lock (mutex);
      ++applied;
    }
    changed.notify_all ();
    wait (1);
    {
      std::lock_guard<std::mutex> lock (mutex);
      cancelled= apply_failed;
    }
    try {
      if (cancelled) call (member.rollback, *editor);
      else call (member.commit, *editor);
    }
    catch (...) {
      // Finalizers are required to be nonthrowing. Still release every owner
      // if that contract is broken: an exception must not strand the peers.
      const auto failure= std::string (cancelled ? "Compound rollback failed: " :
                                                "Compound confirmation failed: ") +
                          exception_message ();
      {
        std::lock_guard<std::mutex> lock (mutex);
        error= failure;
      }
    }
    {
      std::lock_guard<std::mutex> lock (mutex);
      ++finished;
    }
    changed.notify_all ();
    wait (2);
    complete ();
    // Publish group history before any owner admits a following undo command.
    wait (3);
  }
};
} // namespace

void submit_edit_batch (std::vector<edit_participant> members,
                       std::function<void(std::string)> completion) {
  if (members.empty ()) { completion ("Compound edit has no participants"); return; }
  std::set<athena_actor_id> actors;
  for (const auto& member: members)
    if (member.actor == ATHENA_NO_ACTOR || member.view == ATHENA_NO_VIEW ||
        !member.prepare || !member.apply || !member.rollback || !member.commit ||
        !actors.insert (member.actor).second) {
      completion ("Compound edit requires distinct live source actors and complete callbacks");
      return;
    }
  bool busy= false;
  {
    std::lock_guard<std::mutex> lock (reservation_mutex);
    for (auto actor: actors) busy= busy || reservations.count (actor) != 0;
    if (!busy) reservations.insert (actors.begin (), actors.end ());
  }
  if (busy) { completion ("A compound edit already owns one of these sources"); return; }
  auto group= std::make_shared<batch> ();
  group->members= std::move (members);
  group->completion= std::move (completion);
  for (std::size_t i= 0; i < group->members.size (); ++i) {
    const auto& member= group->members[i];
    const auto continuation= actor_continuation_registry::instance ().store (
      [group, i] { group->run (i); });
    if (!buffer_actor::try_submit_to (member.actor,
          actor_command_kind::run_native_continuation, member.view,
          ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER, continuation)) {
      actor_continuation_registry::instance ().discard (continuation);
      group->fail ("Could not queue every source; no compound edit was performed", true);
      group->complete ();
      return;
    }
  }
  {
    std::lock_guard<std::mutex> lock (group->mutex);
    group->admitted= true;
  }
  group->changed.notify_all ();
}
} // namespace athena::avd

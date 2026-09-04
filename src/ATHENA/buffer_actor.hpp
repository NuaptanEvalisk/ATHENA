/******************************************************************************
* MODULE     : buffer_actor.hpp
* DESCRIPTION: Per-buffer execution owner and ID-only command mailbox
* COPYRIGHT  : (C) 2026  Nuaptan F. Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef BUFFER_ACTOR_H
#define BUFFER_ACTOR_H

#include "actor_transport.hpp"
#include "scheme_execution_context.hpp"
#include "url.hpp"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <memory>
#include <thread>
#include <unordered_map>

class editor_rep;
class tm_buffer_rep;
struct buffer_document_state;

struct actor_command_ticket {
  std::uint64_t command_id= 0;
  athena_response_id response_id= ATHENA_NO_RESPONSE;

  explicit operator bool () const noexcept { return command_id != 0; }
};

class buffer_actor {
public:
  explicit buffer_actor (tm_buffer_rep* owner);
  ~buffer_actor ();

  buffer_actor (const buffer_actor&)= delete;
  buffer_actor& operator = (const buffer_actor&)= delete;

  athena_actor_id id () const noexcept;

  actor_command_ticket submit (
    actor_command_kind kind, athena_view_id view_id= ATHENA_NO_VIEW,
    athena_blob_id payload0= ATHENA_NO_BLOB,
    athena_blob_id payload1= ATHENA_NO_BLOB,
    SchemeCapabilitySet capabilities= SCHEME_CAPABILITY_BUFFER,
    std::uint64_t argument0= 0, std::uint64_t argument1= 0,
    std::uint64_t argument2= 0, std::uint64_t argument3= 0,
    std::uint64_t argument4= 0, std::uint64_t argument5= 0,
    std::uint64_t argument6= 0, std::uint64_t argument7= 0);

  actor_command_ticket try_submit (
    actor_command_kind kind, athena_view_id view_id= ATHENA_NO_VIEW,
    athena_blob_id payload0= ATHENA_NO_BLOB,
    athena_blob_id payload1= ATHENA_NO_BLOB,
    SchemeCapabilitySet capabilities= SCHEME_CAPABILITY_BUFFER,
    std::uint64_t argument0= 0, std::uint64_t argument1= 0,
    std::uint64_t argument2= 0, std::uint64_t argument3= 0,
    std::uint64_t argument4= 0, std::uint64_t argument5= 0,
    std::uint64_t argument6= 0, std::uint64_t argument7= 0);

  static actor_command_ticket submit_to (
    athena_actor_id actor_id, actor_command_kind kind,
    athena_view_id view_id= ATHENA_NO_VIEW,
    athena_blob_id payload0= ATHENA_NO_BLOB,
    athena_blob_id payload1= ATHENA_NO_BLOB,
    SchemeCapabilitySet capabilities= SCHEME_CAPABILITY_BUFFER,
    std::uint64_t argument0= 0, std::uint64_t argument1= 0,
    std::uint64_t argument2= 0, std::uint64_t argument3= 0,
    std::uint64_t argument4= 0, std::uint64_t argument5= 0,
    std::uint64_t argument6= 0, std::uint64_t argument7= 0);

  static bool try_submit_coalesced_to (
    athena_actor_id actor_id, actor_command_kind kind,
    athena_view_id view_id, std::uint64_t argument0= 0,
    std::uint64_t argument1= 0, std::uint64_t argument2= 0,
    std::uint64_t argument3= 0);

  static bool invoke_on (
    athena_actor_id actor_id, actor_command_kind kind,
    athena_view_id view_id= ATHENA_NO_VIEW,
    athena_blob_id payload0= ATHENA_NO_BLOB,
    athena_blob_id payload1= ATHENA_NO_BLOB,
    actor_command_record* result= nullptr,
    SchemeCapabilitySet capabilities= SCHEME_CAPABILITY_BUFFER,
    std::uint64_t argument0= 0, std::uint64_t argument1= 0,
    std::uint64_t argument2= 0, std::uint64_t argument3= 0,
    std::uint64_t argument4= 0, std::uint64_t argument5= 0,
    std::uint64_t argument6= 0, std::uint64_t argument7= 0);

  bool invoke (
    actor_command_kind kind, athena_view_id view_id= ATHENA_NO_VIEW,
    athena_blob_id payload0= ATHENA_NO_BLOB,
    athena_blob_id payload1= ATHENA_NO_BLOB,
    actor_command_record* result= nullptr,
    SchemeCapabilitySet capabilities= SCHEME_CAPABILITY_BUFFER,
    std::uint64_t argument0= 0, std::uint64_t argument1= 0,
    std::uint64_t argument2= 0, std::uint64_t argument3= 0,
    std::uint64_t argument4= 0, std::uint64_t argument5= 0,
    std::uint64_t argument6= 0, std::uint64_t argument7= 0);
  bool wait_until_idle ();
  void shutdown ();

  bool is_owner_thread () const noexcept;
  std::thread::id owner_thread () const noexcept;
  std::uint64_t completed_commands () const noexcept;

  // Actor-thread accessors used by Scheme compatibility glue.
  url current_buffer_url () const;
  url current_view_url (athena_view_id view_id) const;
  editor_rep* current_editor (athena_view_id view_id) const noexcept;
  buffer_document_state* current_state () const noexcept;

  // Fixed dispatch is public only for the Guile C trampoline.  Callers submit
  // command ids; they never invoke this directly.
  void dispatch (actor_command_record& command);

private:
  const athena_actor_id id_;
  athena_blob_id initial_name_;
  athena_blob_id initial_master_;
  athena_blob_id initial_title_;
  bool initial_read_only_;
  actor_command_transport commands_;

  // The transport itself remains SPSC. This lock is the one producer gate for
  // callers from Qt and other Guile actors; it never protects actor state.
  std::mutex submit_lock_;
  mutable std::mutex state_lock_;
  std::condition_variable started_condition_;
  std::condition_variable completed_condition_;
  std::thread worker_;
  std::thread::id owner_thread_;
  std::uint64_t next_command_id_;
  athena_response_id next_response_id_;
  std::uint64_t completed_command_id_;
  std::uint64_t completed_commands_;
  bool accepting_;
  bool started_;

  struct response_state {
    bool ready= false;
    actor_command_record record;
  };
  std::unordered_map<athena_response_id, response_state> responses_;

  struct implementation;
  std::unique_ptr<implementation> impl_;

  bool ensure_started ();
  actor_command_ticket submit_with_response (
    actor_command_kind kind, athena_view_id view_id,
    athena_blob_id payload0, athena_blob_id payload1,
    SchemeCapabilitySet capabilities, athena_response_id response_id,
    std::uint64_t argument0, std::uint64_t argument1,
    std::uint64_t argument2, std::uint64_t argument3,
    std::uint64_t argument4, std::uint64_t argument5,
    std::uint64_t argument6, std::uint64_t argument7,
    bool wait_for_slot);
  bool wait (actor_command_ticket ticket,
             actor_command_record* result= nullptr);
  void run_in_guile ();
  void execute (actor_command_record& command);
  void publish_tmfs_title (editor_rep* preferred_editor);

  static void thread_entry (athena_actor_id id);
  static void* guile_entry (void* raw_id);
  static void* wait_without_guile (void* raw_request);
  static buffer_actor* lookup (athena_actor_id id) noexcept;
};

athena_blob_id actor_text_from_string (string text);

#endif // defined BUFFER_ACTOR_H

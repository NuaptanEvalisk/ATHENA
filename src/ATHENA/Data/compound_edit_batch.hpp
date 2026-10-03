/******************************************************************************
* MODULE     : compound_edit_batch.hpp
* DESCRIPTION: Owner-actor rendezvous for reversible multi-document edits
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once

#include "actor_transport.hpp"
#include <functional>
#include <string>
#include <vector>

class editor_rep;

namespace athena::avd {

// The callbacks execute on the member's owner, never on the caller thread.
// prepare is read-only; apply leaves a cancellable editor transaction open.
// rollback must handle a partially executed apply. commit only confirms the
// already validated transaction, and must not perform I/O or peer-actor calls.
// rollback and commit must not throw. Capture only IDs/plain detached data;
// create and destroy native tree/history values inside their owning callback.
// No callback may synchronously wait on another participant or on the GUI.
struct edit_participant {
  athena_actor_id actor= ATHENA_NO_ACTOR;
  athena_view_id view= ATHENA_NO_VIEW;
  std::function<void(editor_rep&)> prepare;
  std::function<void(editor_rep&)> apply;
  std::function<void(editor_rep&)> rollback;
  std::function<void(editor_rep&)> commit;
};

// Nonblocking admission. Completion runs once, on an arbitrary participant or
// on the caller if admission fails; GUI clients must marshal its plain data.
// Reservations cover source actors, including their other views.
void submit_edit_batch (std::vector<edit_participant>,
                       std::function<void(std::string)> completion);

} // namespace athena::avd

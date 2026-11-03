/******************************************************************************
* MODULE     : execution_context.cpp
* DESCRIPTION: Dynamic ownership context for native editor execution
* COPYRIGHT  : (C) 2026  Nuaptan F. Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "execution_context.hpp"
#include "Data/NodeContracts/execution_state.hpp"
#include "System/Misc/crash_report.hpp"

static thread_local const ExecutionContext* execution_context= nullptr;

static athena::contracts::evaluation_state*
checked_context_state (const ExecutionContext& context) {
  ASSERT (context.owner_thread == std::this_thread::get_id (),
          "Execution context entered from a foreign thread");
  return context.contract_state;
}

ExecutionContext::ExecutionContext (
  buffer_actor* actor2, editor_rep* editor2, athena::contracts::evaluation_state* contract_state2,
  tree* document2, athena_actor_id actor_id2, athena_view_id view_id2,
  std::uint64_t command_id2, ExecutionCapabilitySet capabilities2):
    actor (actor2), editor (editor2), contract_state (contract_state2), document (document2),
    actor_id (actor_id2), view_id (view_id2), command_id (command_id2),
    capabilities (capabilities2), owner_thread (std::this_thread::get_id ()) {}

bool
ExecutionContext::has (ExecutionCapability capability) const noexcept {
  return (capabilities & static_cast<ExecutionCapabilitySet> (capability)) != 0;
}

ExecutionScope::ExecutionScope (
  const ExecutionContext& context):
    previous_context (nullptr), evaluation_scope (checked_context_state (context)),
    document_scope (context.document) {
  previous_context= execution_context;
  execution_context= &context;
  athena_crash_set_execution (context.actor_id, context.view_id, context.command_id);
}

ExecutionScope::~ExecutionScope () {
  execution_context= previous_context;
  athena_crash_set_execution (
    previous_context ? previous_context->actor_id : 0,
    previous_context ? previous_context->view_id : 0,
    previous_context ? previous_context->command_id : 0);
}

const ExecutionContext*
current_execution_context () noexcept {
  return execution_context;
}

bool
execution_has_capability (
  ExecutionCapability capability) noexcept {
  const ExecutionContext* context= current_execution_context ();
  return context != nullptr && context->has (capability);
}

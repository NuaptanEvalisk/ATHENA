/******************************************************************************
* MODULE     : execution_context.hpp
* DESCRIPTION: Dynamic ownership context for native editor execution
* COPYRIGHT  : (C) 2026  Nuaptan F. Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef ATHENA_EXECUTION_CONTEXT_H
#define ATHENA_EXECUTION_CONTEXT_H

#include "App/Runtime/actor_transport.hpp"
#include "Document/NodeContracts/execution_state.hpp"
#include "new_document.hpp"

#include <cstdint>
#include <thread>

class buffer_actor;
class editor_rep;

enum ExecutionCapability: std::uint32_t {
  EXECUTION_CAPABILITY_NONE   = 0,
  EXECUTION_CAPABILITY_BUFFER = 1U << 0,
  EXECUTION_CAPABILITY_UI     = 1U << 1,
  EXECUTION_CAPABILITY_GLOBAL = 1U << 2
};

using ExecutionCapabilitySet= std::uint32_t;

class ExecutionContext {
public:
  buffer_actor* const actor;
  editor_rep* const editor;
  athena::contracts::evaluation_state* const contract_state;
  tree* const document;
  const athena_actor_id actor_id;
  const athena_view_id view_id;
  const std::uint64_t command_id;
  const ExecutionCapabilitySet capabilities;
  const std::thread::id owner_thread;

  ExecutionContext (buffer_actor* actor, editor_rep* editor,
                          athena::contracts::evaluation_state* contract_state, tree* document,
                          athena_actor_id actor_id, athena_view_id view_id,
                          std::uint64_t command_id,
                          ExecutionCapabilitySet capabilities);

  ExecutionContext (const ExecutionContext&)= delete;
  ExecutionContext& operator = (const ExecutionContext&)= delete;

  bool has (ExecutionCapability capability) const noexcept;
};

class ExecutionScope {
  const ExecutionContext* previous_context;
  athena::contracts::evaluation_state_scope evaluation_scope;
  with_document_tree document_scope;

public:
  explicit ExecutionScope (const ExecutionContext& context);
  ~ExecutionScope ();

  ExecutionScope (const ExecutionScope&)= delete;
  ExecutionScope& operator = (const ExecutionScope&)= delete;
};

const ExecutionContext*
current_execution_context () noexcept;

bool execution_has_capability (
  ExecutionCapability capability) noexcept;

#endif // defined ATHENA_EXECUTION_CONTEXT_H

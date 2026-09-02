/******************************************************************************
* MODULE     : scheme_execution_context.cpp
* DESCRIPTION: Dynamic ownership context for native and Scheme execution
* COPYRIGHT  : (C) 2026  Nuaptan F. Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "scheme_execution_context.hpp"
#include "drd_std.hpp"

static thread_local const SchemeExecutionContext* execution_context= nullptr;

static drd_info*
checked_context_drd (const SchemeExecutionContext& context) {
  ASSERT (context.owner_thread == std::this_thread::get_id (),
          "Scheme execution context entered from a foreign thread");
  return context.drd;
}

SchemeExecutionContext::SchemeExecutionContext (
  buffer_actor* actor2, editor_rep* editor2, drd_info* drd2,
  tree* document2, athena_actor_id actor_id2, athena_view_id view_id2,
  std::uint64_t command_id2, SchemeCapabilitySet capabilities2):
    actor (actor2), editor (editor2), drd (drd2), document (document2),
    actor_id (actor_id2), view_id (view_id2), command_id (command_id2),
    capabilities (capabilities2), owner_thread (std::this_thread::get_id ()) {}

bool
SchemeExecutionContext::has (SchemeExecutionCapability capability) const noexcept {
  return (capabilities & static_cast<SchemeCapabilitySet> (capability)) != 0;
}

SchemeExecutionScope::SchemeExecutionScope (
  const SchemeExecutionContext& context):
    previous_context (nullptr), drd_scope (checked_context_drd (context)),
    document_scope (context.document) {
  previous_context= execution_context;
  execution_context= &context;
}

SchemeExecutionScope::~SchemeExecutionScope () {
  execution_context= previous_context;
}

const SchemeExecutionContext*
current_scheme_execution_context () noexcept {
  return execution_context;
}

bool
scheme_execution_has_capability (
  SchemeExecutionCapability capability) noexcept {
  const SchemeExecutionContext* context= current_scheme_execution_context ();
  return context != nullptr && context->has (capability);
}

/******************************************************************************
* MODULE     : scheme_execution_context.hpp
* DESCRIPTION: Dynamic ownership context for native and Scheme execution
* COPYRIGHT  : (C) 2026  Nuaptan F. Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef SCHEME_EXECUTION_CONTEXT_H
#define SCHEME_EXECUTION_CONTEXT_H

#include "actor_transport.hpp"
#include "drd_std.hpp"
#include "new_document.hpp"

#include <cstdint>
#include <thread>

class buffer_actor;
class editor_rep;

enum SchemeExecutionCapability: std::uint32_t {
  SCHEME_CAPABILITY_NONE   = 0,
  SCHEME_CAPABILITY_BUFFER = 1U << 0,
  SCHEME_CAPABILITY_UI     = 1U << 1,
  SCHEME_CAPABILITY_GLOBAL = 1U << 2
};

using SchemeCapabilitySet= std::uint32_t;

class SchemeExecutionContext {
public:
  buffer_actor* const actor;
  editor_rep* const editor;
  drd_info* const drd;
  tree* const document;
  const athena_actor_id actor_id;
  const athena_view_id view_id;
  const std::uint64_t command_id;
  const SchemeCapabilitySet capabilities;
  const std::thread::id owner_thread;

  SchemeExecutionContext (buffer_actor* actor, editor_rep* editor,
                          drd_info* drd, tree* document,
                          athena_actor_id actor_id, athena_view_id view_id,
                          std::uint64_t command_id,
                          SchemeCapabilitySet capabilities);

  SchemeExecutionContext (const SchemeExecutionContext&)= delete;
  SchemeExecutionContext& operator = (const SchemeExecutionContext&)= delete;

  bool has (SchemeExecutionCapability capability) const noexcept;
};

class SchemeExecutionScope {
  const SchemeExecutionContext* previous_context;
  with_borrowed_drd drd_scope;
  with_document_tree document_scope;

public:
  explicit SchemeExecutionScope (const SchemeExecutionContext& context);
  ~SchemeExecutionScope ();

  SchemeExecutionScope (const SchemeExecutionScope&)= delete;
  SchemeExecutionScope& operator = (const SchemeExecutionScope&)= delete;
};

const SchemeExecutionContext*
current_scheme_execution_context () noexcept;

bool scheme_execution_has_capability (
  SchemeExecutionCapability capability) noexcept;

#endif // defined SCHEME_EXECUTION_CONTEXT_H

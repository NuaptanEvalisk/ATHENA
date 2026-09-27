/******************************************************************************
* MODULE     : document_node_copy.hpp
* DESCRIPTION: New source-object copies and internal persistent-reference remapping
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include "node_metadata.hpp"
#include "ATHENA/actor_transport.hpp"
#include <optional>
#include <string>

namespace athena::document_node {
// role -> artifact UUID strings. These bindings identify extracted artifacts,
// not source-node references; new objects must acquire their own bindings.
inline constexpr const char* artifact_bindings_property= "athena:artifact-bindings";

// Snapshot copy() is deliberately different: it preserves all stored identity.
// This operation is for paste/import as new source objects, not an authorized
// move. It also remaps native HLINK targets and TRANSCLUDE(TUPLE(uuid,...)).
// Old four-argument anchor transclusions are not the new source-node protocol.
tree duplicate_source_nodes (const tree&, node::identity_map* = nullptr);

struct source_move_endpoint {
  athena_actor_id actor= ATHENA_NO_ACTOR;
  athena_view_id view= ATHENA_NO_VIEW;
  bool operator == (const source_move_endpoint& other) const {
    return actor == other.actor && view == other.view;
  }
};

struct source_move_ticket {
  std::string token;
  double marker= 0.0;
};

struct source_move_claim {
  std::string token;
  double marker= 0.0;
  source_move_endpoint source;
  source_move_endpoint target;
};

enum class source_move_state { pending, reserved, active, undone };

// Process-local capability: clipboard bytes alone never authorize a move.
// The exact native selection binds the opaque token to one cut snapshot.
source_move_ticket issue_source_move (
  const tree& selection, std::string vault_key, source_move_endpoint source);
std::optional<source_move_claim> reserve_source_move (
  const std::string& token, const tree& selection, const std::string& vault_key,
  source_move_endpoint target);
bool activate_source_move (const std::string& token);
void release_source_move (const std::string& token);
void invalidate_source_move (const std::string& token);

struct source_move_history {
  double marker= 0.0;
  source_move_endpoint source;
  source_move_endpoint target;
  source_move_state state= source_move_state::pending;
};
std::optional<source_move_history> source_move_for_history (
  double marker, source_move_endpoint endpoint);
bool set_source_move_history_state (double marker, source_move_state expected,
                                    source_move_state next);
}

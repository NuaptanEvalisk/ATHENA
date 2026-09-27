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

namespace athena::document_node {
// role -> artifact UUID strings. These bindings identify extracted artifacts,
// not source-node references; new objects must acquire their own bindings.
inline constexpr const char* artifact_bindings_property= "athena:artifact-bindings";

// Snapshot copy() is deliberately different: it preserves all stored identity.
// This operation is for paste/import as new source objects, not an authorized
// move. It also remaps native HLINK targets and TRANSCLUDE(TUPLE(uuid,...)).
// Old four-argument anchor transclusions are not the new source-node protocol.
tree duplicate_source_nodes (const tree&, node::identity_map* = nullptr);
}

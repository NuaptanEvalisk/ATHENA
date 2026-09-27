/******************************************************************************
* MODULE     : node_reference.hpp
* DESCRIPTION: Nonblocking native reference snapshots for document presentation
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include "node_location.hpp"

namespace athena::node_reference {
struct view {
  node_location::snapshot snapshot;
  std::uint64_t revision= 0;
};
view get (std::vector<std::string> ids, std::vector<std::string> ancestry= {});
// Only real source changes, not typesetting invalidations. O(1), no tree copy.
void source_changed ();
// Canonical TRANSCLUDE has one TUPLE child containing an ordered UUID set.
bool canonical (const tree&);
std::vector<std::string> targets (const tree&);
tree display (const view&);
inline constexpr const char* ancestry_variable= "athena-node-reference-ancestry";
std::vector<std::string> ancestry (const tree& environment_value);
std::string target_id (const string& url);
} // namespace athena::node_reference

// Executes only on the target editor's BufferActor; no GUI or document mutation.
void athena_refresh_node_reference_view ();

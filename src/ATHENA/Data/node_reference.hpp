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
#include "url.hpp"

namespace athena::node_reference {
struct view {
  node_location::snapshot snapshot;
  std::uint64_t revision= 0;
};
// Presentation may retain its last snapshot during refresh. Inserters require
// a snapshot verified in the current source epoch before accepting a target.
view get (std::vector<std::string> ids, std::vector<std::string> ancestry= {},
          bool require_current= false);
// Only real source changes, not typesetting invalidations. O(1), no tree copy.
void source_changed ();
std::uint64_t source_epoch ();
// Canonical TRANSCLUDE has one TUPLE child containing an ordered UUID set.
bool canonical (const tree&);
std::vector<std::string> targets (const tree&);
tree display (const view&);
// Independent rendering copy retaining semantic properties, not source identity.
tree presentation_copy (const tree&, const url& base= url_none ());
// A single-target preview inherits its source style, initial values and preamble.
tree preview_document (const view&, url& source);
// Transclusion source peeks show nearby source context instead of repeating the
// transcluded node itself.
tree preview_context_document (const view&, url& source);
inline constexpr const char* ancestry_variable= "athena-node-reference-ancestry";
std::vector<std::string> ancestry (const tree& environment_value);
std::string target_id (const string& url);
} // namespace athena::node_reference

// Executes only on the target editor's BufferActor; no GUI or document mutation.
void athena_refresh_node_reference_view ();

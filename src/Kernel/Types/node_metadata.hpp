/******************************************************************************
* MODULE     : node_metadata.hpp
* DESCRIPTION: Optional persistent node identities and typed source properties
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once

#include "tree.hpp"
#include <atomic>
#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>
#include <utility>

namespace athena::node {

struct reference { std::string id; };
struct rich_text { tree content; };

struct property {
  using list= std::vector<property>;
  using dictionary= std::map<std::string, property>;
  using value= std::variant<std::string, bool, std::int64_t, double,
                           list, dictionary, reference, rich_text>;
  value data;
  property (): data (std::string ()) {}
  explicit property (value input): data (std::move (input)) {}
};

struct metadata {
  std::string id;
  property::dictionary properties;
  bool empty () const { return id.empty () && properties.empty (); }
};

// Immutable copy-on-write storage. Most source nodes carry only an identity
// (or scalar properties), so ordinary tree copies can share this immutable
// payload instead of cloning maps and strings at every node. Metadata which
// embeds rich-text trees remains unshared because those legacy tree handles can
// be mutated after being obtained through a const property value.
struct metadata_rep {
  std::atomic<std::uint32_t> references {1};
  metadata value;
  bool shareable;
  // Zero means uncached.  Cached hashes carry a validity bit in bit 32, so all
  // 32-bit hash values remain representable without a second synchronization
  // primitive.
  mutable std::atomic<std::uint64_t> hash_state {0};

  metadata_rep (metadata input, bool can_share):
    value (std::move (input)), shareable (can_share) {}

  void retain () noexcept {
    references.fetch_add (1, std::memory_order_relaxed);
  }
  void release () noexcept {
    if (references.fetch_sub (1, std::memory_order_acq_rel) == 1)
      tm_delete (this);
  }
};

bool valid_id (const std::string& id);
std::string new_id ();
const metadata* get (const tree& node);
// Internal mutation boundary. Metadata storage is immutable once published to
// a tree: callers always receive a private clone before changing it. This keeps
// cross-owner copies safe even when the underlying storage is shared.
metadata* edit (tree& node);
// Includes descendants; metadata itself implies true, so rich values need no scan.
bool contains_metadata (const tree& node);
std::string id (const tree& node);
// Detached-tree primitives. Live edits must go through modification/observers.
void set (tree& node, const metadata& value);
void clear (tree& node);
void copy_metadata (const tree& source, tree& target);
property copy_property (const property& value);
bool equal (const property& a, const property& b);
bool equal_metadata (const tree& a, const tree& b);
int hash_metadata (const tree& node);

// Structural/value comparison without persistent identity, recursively.
bool content_equal (const tree& a, const tree& b);
// New document objects, unlike copy(), must not inherit source identity.
using identity_map= std::map<std::string, std::string>;
tree duplicate (const tree& source, identity_map* replacements= nullptr);
// Content projection preserves properties but removes source identities,
// including those inside structured text. References retain their targets.
tree content_projection (const tree& source);

} // namespace athena::node

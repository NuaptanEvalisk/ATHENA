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

bool valid_id (const std::string& id);
std::string new_id ();
const metadata* get (const tree& node);
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

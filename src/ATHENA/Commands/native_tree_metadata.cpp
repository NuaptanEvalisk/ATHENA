/******************************************************************************
* MODULE     : native_tree_metadata.cpp
* DESCRIPTION: Lossless owner-local tree reconstruction for Scheme source edits
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "native_interfaces.hpp"
#include "node_metadata.hpp"
#include "ATHENA/Data/document_node_copy.hpp"

tree tree_rebuild (tree source, array<tree> children) {
  ASSERT (is_compound (source), "tree-rebuild requires a compound source");
  tree result (L(source), children);
  athena::node::copy_metadata (source, result);
  return result;
}

string tree_node_id (tree source) {
  const std::string id= athena::node::id (source);
  return string (id.data (), static_cast<int> (id.size ()));
}

tree tree_duplicate_source (tree source) {
  return athena::document_node::duplicate_source_nodes (source);
}

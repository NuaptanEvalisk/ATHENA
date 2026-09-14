/******************************************************************************
* MODULE     : interop_document_nodes.hpp
* DESCRIPTION: Owner-thread native document nodes with transferable AUDM identities
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include "../Interop/value.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

class tree;

namespace athena::interop {

// A lease contains no TeXmacs objects. Its last reference may die on any worker.
class document_node_identity {
  friend class document_nodes;
  friend class document_node_transfer;
  explicit document_node_identity (std::uint64_t value): id (value) {}
public:
  const std::uint64_t id;
};
using document_node = std::shared_ptr<const document_node_identity>;
using document_node_path = std::vector<int>;

// An immutable handoff record, not a path-based identity recovery heuristic.
// It contains only wire values and leases, never native tree/observer objects.
class document_node_transfer {
  friend class document_nodes;
  value source;
  std::vector<std::pair<document_node, document_node_path>> nodes;
public:
  const value& source_value () const { return source; }
  // Lazy identities for an immutable wire snapshot, without native tree
  // allocation on the receiving worker. The caller serializes registration.
  document_node track (const document_node_path& path);
  document_node_path locate (const document_node& node) const;
};

class document_nodes {
  struct impl;
  std::unique_ptr<impl> state;
public:
  document_nodes ();
  ~document_nodes ();
  document_nodes (const document_nodes&) = delete;
  document_nodes& operator= (const document_nodes&) = delete;

  // All methods, including destruction, run on the document's owner thread.
  document_node track (const tree& root, const document_node_path& path);
  std::vector<document_node> track_many (
    const tree& root, const std::vector<document_node_path>& paths);
  document_node_path locate (const tree& root, const document_node& node);
  value properties (const tree& root, const document_node& node);
  value read (const tree& root, const document_node& node);
  std::vector<document_node> children (const tree& root, const document_node& node);
  document_node replace (tree& root, const document_node& node, const value& source);
  std::vector<document_node> insert_children (tree& root, const document_node& parent,
                                             std::size_t index, const value& children);
  void insert_siblings (tree& root, const document_node& node,
                        bool after, const value& siblings);
  void erase (tree& root, const document_node& node);
  void set_tag (tree& root, const document_node& node, const value& tag);
  void collect ();

  // The caller serializes the source transition: freeze the old owner until
  // the new owner has imported, then retire the old registry. Import requires
  // an empty registry and an exactly equivalent full source tree.
  document_node_transfer export_nodes (const tree& root);
  void import_nodes (const tree& root, const document_node_transfer&);

  // Destination index is a gap in the parent's children before the move.
  void move (tree& root, const document_node& node,
             const document_node& parent, std::size_t index);
};

} // namespace athena::interop

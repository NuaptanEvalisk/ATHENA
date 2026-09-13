/******************************************************************************
* MODULE     : interop_document_nodes.cpp
* DESCRIPTION: Strict native node observers for document resolver handles
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "interop_document_nodes.hpp"
#include "interop_document_codec.hpp"
#include "tree.hpp"
#include <algorithm>
#include <atomic>
#include <limits>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace athena::interop {
namespace {
std::atomic<std::uint64_t> next_node_id {1};

std::uint64_t allocate_node_id () {
  auto id= next_node_id.fetch_add (1, std::memory_order_relaxed);
  if (id == 0 || id == std::numeric_limits<std::uint64_t>::max ()) std::terminate ();
  return id;
}

tree& at (tree& root, const document_node_path& path) {
  tree* node= &root;
  for (int index: path) {
    if (!is_compound (*node) || index < 0 || index >= N (*node))
      throw std::invalid_argument ("Invalid document node path");
    node= &(*node)[index];
  }
  return *node;
}

// Iterative traversal avoids native-stack growth on externally supplied trees.
template<class Visit>
void walk (const tree& root, Visit visit) {
  struct frame { const tree* node; int next= 0; };
  std::vector<frame> stack {{&root}};
  document_node_path path;
  visit (root, path);
  while (!stack.empty ()) {
    auto& current= stack.back ();
    if (!is_compound (*current.node) || current.next == N (*current.node)) {
      stack.pop_back ();
      if (!path.empty ()) path.pop_back ();
    }
    else {
      int index= current.next++;
      const tree* child= &(*current.node)[index];
      path.push_back (index);
      visit (*child, path);
      stack.push_back ({child});
    }
  }
}

document_node_path unique_path (const tree& root, tree_rep* sought) {
  document_node_path found;
  unsigned matches= 0;
  walk (root, [&] (const tree& node, const document_node_path& path) {
    if (inside (node) != sought) return;
    if (++matches > 1)
      throw std::runtime_error ("STALE: document node has ambiguous occurrences");
    found= path;
  });
  if (matches == 0)
    throw std::runtime_error ("STALE: document node no longer exists");
  return found;
}

struct node_slot {
  const std::thread::id owner= std::this_thread::get_id ();
  std::weak_ptr<const document_node_identity> lease;
  tree held;
  observer listener;
  bool live= true;
  bool moving= false;
  explicit node_slot (const tree& node): held (node) {}
  ~node_slot () {
    if (owner != std::this_thread::get_id ()) std::terminate ();
    if (!is_nil (listener)) detach_observer (held, listener);
  }
};

class node_observer final: public observer_rep {
  const std::weak_ptr<node_slot> slot;
  void invalidate (bool detach= false) {
    if (auto locked= slot.lock ()) {
      if (locked->owner != std::this_thread::get_id ()) std::terminate ();
      if (!detach || !locked->moving) locked->live= false;
    }
  }
public:
  explicit node_observer (const std::shared_ptr<node_slot>& value): slot (value) {}
  void notify_assign (tree&, tree) override { invalidate (); }
  void notify_remove_node (tree&, int) override { invalidate (); }
  void notify_var_split (tree&, tree, tree) override { invalidate (); }
  void notify_var_join (tree&, tree, int) override { invalidate (); }
  void notify_detach (tree&, tree, bool) override { invalidate (true); }
  // Insert/remove children, relabel, and wrapping preserve the original node.
  // Callbacks only mark state: detach/destruction happens after notifications.
};
} // namespace

struct document_nodes::impl {
  const std::thread::id owner= std::this_thread::get_id ();
  std::unordered_map<std::uint64_t, std::shared_ptr<node_slot>> slots;
  std::unordered_map<tree_rep*, std::weak_ptr<node_slot>> nodes;
  void check_owner () const {
    if (owner != std::this_thread::get_id ())
      throw std::logic_error ("Document node registry accessed outside its owner");
  }
  std::shared_ptr<node_slot> get (const document_node& node) {
    check_owner ();
    auto found= node ? slots.find (node->id) : slots.end ();
    if (found == slots.end () || !found->second->live ||
        found->second->lease.lock () != node)
      throw std::runtime_error ("STALE: document node handle expired");
    return found->second;
  }
  void bind (const tree& node, const document_node& lease) {
    auto slot= std::make_shared<node_slot> (node);
    slot->lease= lease;
    slot->listener= observer (tm_new<node_observer> (slot));
    attach_observer (slot->held, slot->listener);
    slots.emplace (lease->id, slot);
    nodes.emplace (inside (node), slot);
  }
};

document_nodes::document_nodes (): state (std::make_unique<impl> ()) {}
document_nodes::~document_nodes () {
  if (state->owner != std::this_thread::get_id ()) std::terminate ();
}

void document_nodes::collect () {
  state->check_owner ();
  for (auto it= state->slots.begin (); it != state->slots.end ();) {
    const auto& slot= it->second;
    if (slot->live && !slot->lease.expired ()) { ++it; continue; }
    state->nodes.erase (inside (slot->held));
    it= state->slots.erase (it);
  }
}

document_node document_nodes::track (const tree& root,
                                     const document_node_path& path) {
  return track_many (root, {path}).front ();
}

std::vector<document_node> document_nodes::track_many (
  const tree& root, const std::vector<document_node_path>& paths) {
  state->check_owner ();
  collect ();
  tree view= root;
  std::unordered_map<tree_rep*, unsigned> occurrences;
  for (const auto& path: paths) occurrences.emplace (inside (at (view, path)), 0);
  if (occurrences.empty ()) return {};
  // Resolving many candidates scans membership once, not once per emitted handle.
  walk (root, [&] (const tree& child, const document_node_path&) {
    auto found= occurrences.find (inside (child));
    if (found != occurrences.end () && ++found->second > 1)
      throw std::runtime_error ("STALE: document node has ambiguous occurrences");
  });
  std::vector<document_node> result;
  result.reserve (paths.size ());
  for (const auto& path: paths) {
    const tree& node= at (view, path);
    auto existing= state->nodes.find (inside (node));
    if (existing != state->nodes.end ())
      if (auto slot= existing->second.lock ())
        if (auto lease= slot->lease.lock ()) {
          result.push_back (std::move (lease));
          continue;
        }
    document_node lease (new document_node_identity (allocate_node_id ()));
    state->bind (node, lease);
    result.push_back (std::move (lease));
  }
  return result;
}

document_node document_node_transfer::track (const document_node_path& path) {
  const value* current= &source;
  for (int index: path) {
    if (index < 0 || !current->contains ("children") ||
        std::size_t (index) >= current->at ("children").size ())
      throw std::invalid_argument ("Invalid document node path");
    current= &current->at ("children")[index];
  }
  if (!current->is_object ()) throw std::invalid_argument ("Document source is unavailable");
  for (const auto& entry: nodes) if (entry.second == path) return entry.first;
  document_node lease (new document_node_identity (allocate_node_id ()));
  nodes.emplace_back (lease, path);
  return lease;
}

document_node_path document_node_transfer::locate (const document_node& node) const {
  for (const auto& entry: nodes) if (entry.first == node) return entry.second;
  throw std::runtime_error ("STALE: document node handle expired");
}

document_node_transfer document_nodes::export_nodes (const tree& root) {
  state->check_owner ();
  collect ();
  document_node_transfer result;
  result.source= document_node_to_value (root);
  struct occurrence { document_node_path path; unsigned count= 0; };
  std::unordered_map<tree_rep*, occurrence> occurrences;
  for (const auto& item: state->slots)
    occurrences.emplace (inside (item.second->held), occurrence {});
  walk (root, [&] (const tree& node, const document_node_path& path) {
    auto found= occurrences.find (inside (node));
    if (found == occurrences.end ()) return;
    if (++found->second.count == 1) found->second.path= path;
  });
  for (const auto& item: state->slots) {
    const auto& location= occurrences.at (inside (item.second->held));
    if (location.count != 1) { item.second->live= false; continue; }
    if (auto lease= item.second->lease.lock ())
      result.nodes.emplace_back (std::move (lease), location.path);
  }
  return result;
}

void document_nodes::import_nodes (const tree& root,
                                   const document_node_transfer& transfer) {
  state->check_owner ();
  collect ();
  if (!state->slots.empty ())
    throw std::logic_error ("Node identity handoff requires an empty registry");
  if (document_node_to_value (root) != transfer.source)
    throw std::runtime_error ("STALE: source changed during document identity handoff");
  // Validate occurrence uniqueness before attaching anything. A source value
  // alone does not distinguish a copied tree from one with shared occurrences.
  tree view= root;
  std::unordered_map<tree_rep*, unsigned> counts;
  for (const auto& item: transfer.nodes) counts.emplace (inside (at (view, item.second)), 0);
  walk (root, [&] (const tree& node, const document_node_path&) {
    auto found= counts.find (inside (node));
    if (found != counts.end () && ++found->second > 1)
      throw std::runtime_error ("STALE: document node has ambiguous occurrences during handoff");
  });
  document_nodes replacement;
  for (const auto& item: transfer.nodes)
    replacement.state->bind (at (view, item.second), item.first);
  state.swap (replacement.state);
}

document_node_path document_nodes::locate (const tree& root,
                                          const document_node& node) {
  auto slot= state->get (node);
  try { return unique_path (root, inside (slot->held)); }
  catch (...) { slot->live= false; throw; }
}

value document_nodes::properties (const tree& root, const document_node& node) {
  const auto location= locate (root, node);
  tree view= root;
  const tree& target= at (view, location);
  // Encode only this node's scalar data, never its descendants for a predicate.
  tree scalar= is_atomic (target) ? target : tree (L (target));
  value result= document_node_to_value (scalar);
  result.erase ("children");
  result["type"]= is_atomic (target) ? "text" : "compound";
  result["arity"]= is_atomic (target) ? 0 : N (target);
  result["path"]= location;
  if (result.contains ("tag")) result["name"]= result.at ("tag");
  else if (result.contains ("text")) result["name"]= result.at ("text");
  return result;
}

value document_nodes::read (const tree& root, const document_node& node) {
  const auto location= locate (root, node);
  tree view= root;
  return document_node_to_value (at (view, location));
}

std::vector<document_node> document_nodes::children (
    const tree& root, const document_node& node) {
  auto location= locate (root, node);
  tree view= root;
  const tree& parent= at (view, location);
  if (is_atomic (parent)) return {};
  std::vector<document_node_path> paths;
  paths.reserve (N (parent));
  location.push_back (0);
  for (int i= 0; i < N (parent); ++i) {
    location.back ()= i;
    paths.push_back (location);
  }
  return track_many (root, paths);
}

document_node document_nodes::replace (tree& root, const document_node& node,
                                       const value& source) {
  const auto location= locate (root, node);
  tree replacement= document_node_from_value (source);
  assign (at (root, location), replacement);
  return track (root, location);
}

std::vector<document_node> document_nodes::insert_children (
    tree& root, const document_node& parent, std::size_t index, const value& children) {
  auto location= locate (root, parent);
  tree target= at (root, location);
  if (!is_compound (target) || index > std::size_t (N (target)))
    throw std::invalid_argument ("Invalid insertion child index");
  // Decode the entire batch first, with one shared codec budget. Malformed
  // later children cannot leave a partially inserted batch in the editor.
  tree insertion= document_node_from_value (value {{"tag", "tuple"}, {"children", children}});
  if (N (insertion) > std::numeric_limits<int>::max () - N (target))
    throw std::length_error ("Document child count overflow");
  if (N (insertion) == 0) return {};
  insert (target, int (index), insertion);
  std::vector<document_node_path> paths;
  paths.reserve (N (insertion));
  location.push_back (0);
  for (int i= 0; i < N (insertion); ++i) {
    location.back ()= int (index) + i;
    paths.push_back (location);
  }
  return track_many (root, paths);
}

void document_nodes::erase (tree& root, const document_node& node) {
  auto location= locate (root, node);
  if (location.empty ()) throw std::invalid_argument ("Cannot erase the document root");
  int index= location.back ();
  location.pop_back ();
  remove (at (root, location), index, 1);
  collect ();
}

void document_nodes::set_tag (tree& root, const document_node& node, const value& tag) {
  const auto location= locate (root, node);
  tree target= at (root, location);
  if (!is_compound (target)) throw std::invalid_argument ("A text node has no tag");
  tree decoded= document_node_from_value (value {{"tag", tag}, {"children", value::array ()}});
  assign_node (target, L (decoded));
}

void document_nodes::move (tree& root, const document_node& node,
                           const document_node& parent, std::size_t index) {
  const auto source_path= locate (root, node);
  const auto parent_path= locate (root, parent);
  if (source_path.empty ())
    throw std::invalid_argument ("Cannot move the document root");
  if (parent_path.size () >= source_path.size () &&
      std::equal (source_path.begin (), source_path.end (), parent_path.begin ()))
    throw std::invalid_argument ("Cannot move a node into itself or its descendants");
  tree destination= at (root, parent_path);
  if (!is_compound (destination) || index > std::size_t (N (destination)))
    throw std::invalid_argument ("Invalid destination child index");
  document_node_path source_parent_path= source_path;
  int source_index= source_parent_path.back ();
  source_parent_path.pop_back ();
  tree source_parent= at (root, source_parent_path);
  const bool same_parent= inside (destination) == inside (source_parent);
  if (same_parent && (index == std::size_t (source_index) ||
                      index == std::size_t (source_index + 1))) return;
  tree moving= at (root, source_path);
  tree insertion (TUPLE, moving);
  std::unordered_set<tree_rep*> subtree;
  walk (moving, [&] (const tree& child, const document_node_path&) {
    subtree.insert (inside (child));
  });
  struct move_guard {
    std::vector<std::shared_ptr<node_slot>> slots;
    ~move_guard () { for (const auto& slot: slots) slot->moving= false; }
  } guard;
  for (const auto& item: state->slots)
    if (item.second->live && subtree.count (inside (item.second->held)))
      guard.slots.push_back (item.second);
  for (const auto& slot: guard.slots) slot->moving= true;
  // Retain parent/node references; removing an earlier sibling changes paths.
  try {
    remove (source_parent, source_index, 1);
    if (same_parent && index > std::size_t (source_index)) --index;
    insert (destination, int (index), insertion);
  }
  catch (...) {
    // A notification can throw after insertion; do not insert a second copy.
    try {
      bool present= false;
      walk (root, [&] (const tree& child, const document_node_path&) {
        present= present || inside (child) == inside (moving);
      });
      if (!present) {
        unique_path (root, inside (source_parent));
        insert (source_parent, source_index, insertion);
      }
    }
    catch (...) {
      for (const auto& slot: guard.slots) slot->live= false;
      throw;
    }
    throw;
  }
}

} // namespace athena::interop

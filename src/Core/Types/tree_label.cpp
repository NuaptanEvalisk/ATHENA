
/******************************************************************************
* MODULE     : tree_label.cpp
* DESCRIPTION: labels of trees
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "Core/Types/tree_label.hpp"
#include <atomic>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace {

struct tree_label_string_hash {
  std::size_t operator () (const string& value) const noexcept {
    return static_cast<std::size_t> (hash (value));
  }
};

std::shared_mutex constructor_lock;
std::vector<std::optional<string>> constructor_names;
std::unordered_map<string, int, tree_label_string_hash> constructor_codes;
std::atomic<std::uint64_t> constructor_revision {0};
tree_label next_tree_label= START_EXTENSIONS;

// Successful extension registrations are immutable. Only explicit registration
// can rebind an existing name or label; missing names must not be cached.
struct label_cache {
  std::uint64_t revision= 0;
  std::vector<std::optional<string>> names;
  std::unordered_map<string, int, tree_label_string_hash> codes;
};

label_cache&
local_labels () {
  static thread_local label_cache cache;
  const auto revision= constructor_revision.load (std::memory_order_acquire);
  if (cache.revision != revision) {
    cache.names.clear ();
    cache.codes.clear ();
    cache.revision= revision;
  }
  return cache;
}

void
store_tree_label (tree_label label, const string& name) {
  std::size_t index= static_cast<std::size_t> (label);
  if (constructor_names.size () <= index)
    constructor_names.resize (index + 1);
  constructor_names[index]= name;
  constructor_codes[name]= static_cast<int> (label);
}

} // namespace

/******************************************************************************
* Setting up the conversion tables
******************************************************************************/

void
make_tree_label (tree_label l, string s) {
  std::unique_lock<std::shared_mutex> guard (constructor_lock);
  store_tree_label (l, s);
  constructor_revision.fetch_add (1, std::memory_order_release);
}

tree_label
make_tree_label (string s) {
  auto& cache= local_labels ();
  const auto cached= cache.codes.find (s);
  if (cached != cache.codes.end ()) return static_cast<tree_label> (cached->second);
  std::unique_lock<std::shared_mutex> guard (constructor_lock);
  auto found= constructor_codes.find (s);
  if (found != constructor_codes.end ()) {
    cache.codes.emplace (s, found->second);
    return static_cast<tree_label> (found->second);
  }
  tree_label l= next_tree_label;
  next_tree_label= (tree_label) (((int) next_tree_label) + 1);
  store_tree_label (l, s);
  cache.codes.emplace (s, static_cast<int> (l));
  return l;
}

/******************************************************************************
* Conversions between tree_labels and strings
******************************************************************************/

string
as_string (tree_label l) {
  auto& cache= local_labels ();
  const std::size_t index= static_cast<std::size_t> (l);
  if (index < cache.names.size () && cache.names[index])
    return *cache.names[index];
  std::shared_lock<std::shared_mutex> guard (constructor_lock);
  if (index >= constructor_names.size () || !constructor_names[index]) return "?";
  if (cache.names.size () <= index) cache.names.resize (index + 1);
  cache.names[index]= constructor_names[index];
  return *cache.names[index];
}

tree_label
as_tree_label (string s) {
  auto& cache= local_labels ();
  const auto cached= cache.codes.find (s);
  if (cached != cache.codes.end ()) return static_cast<tree_label> (cached->second);
  std::shared_lock<std::shared_mutex> guard (constructor_lock);
  auto found= constructor_codes.find (s);
  if (found == constructor_codes.end ()) return UNKNOWN;
  cache.codes.emplace (s, found->second);
  return static_cast<tree_label> (found->second);
}

bool
existing_tree_label (string s) {
  auto& cache= local_labels ();
  if (cache.codes.find (s) != cache.codes.end ()) return true;
  std::shared_lock<std::shared_mutex> guard (constructor_lock);
  auto found= constructor_codes.find (s);
  if (found == constructor_codes.end ()) return false;
  cache.codes.emplace (s, found->second);
  return true;
}

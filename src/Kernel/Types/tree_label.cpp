
/******************************************************************************
* MODULE     : tree_label.cpp
* DESCRIPTION: labels of trees
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "tree_label.hpp"
#include <mutex>
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
std::vector<string> constructor_names;
std::unordered_map<string, int, tree_label_string_hash> constructor_codes;
tree_label next_tree_label= START_EXTENSIONS;

void
store_tree_label (tree_label label, const string& name) {
  std::size_t index= static_cast<std::size_t> (label);
  if (constructor_names.size () <= index)
    constructor_names.resize (index + 1, string ("?"));
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
}

tree_label
make_tree_label (string s) {
  std::unique_lock<std::shared_mutex> guard (constructor_lock);
  auto found= constructor_codes.find (s);
  if (found != constructor_codes.end ())
    return static_cast<tree_label> (found->second);
  tree_label l= next_tree_label;
  next_tree_label= (tree_label) (((int) next_tree_label) + 1);
  store_tree_label (l, s);
  return l;
}

/******************************************************************************
* Conversions between tree_labels and strings
******************************************************************************/

string
as_string (tree_label l) {
  std::shared_lock<std::shared_mutex> guard (constructor_lock);
  std::size_t index= static_cast<std::size_t> (l);
  return index < constructor_names.size () ? constructor_names[index] :
    string ("?");
}

tree_label
as_tree_label (string s) {
  std::shared_lock<std::shared_mutex> guard (constructor_lock);
  auto found= constructor_codes.find (s);
  return found == constructor_codes.end () ? UNKNOWN :
    static_cast<tree_label> (found->second);
}

bool
existing_tree_label (string s) {
  std::shared_lock<std::shared_mutex> guard (constructor_lock);
  return constructor_codes.find (s) != constructor_codes.end ();
}

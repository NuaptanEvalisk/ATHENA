#include "native_node_groups.hpp"
#include "Data/NodeContracts/node_groups.hpp"
#include "Data/NodeContracts/node_group_scope.hpp"
#include "drd_std.hpp"

#include <algorithm>

namespace {
using athena::contracts::standard_node_groups;
std::string bytes (const string& value) {
  return std::string (value.data (), N (value));
}
string text (const std::string& value) {
  return string (value.data (), value.size ());
}
object symbols (const std::vector<std::string>& values) {
  array<object> result;
  for (const auto& value: values) result << symbol_object (text (value));
  return as_list_object (result);
}
std::vector<std::string> members (const std::string& group) {
  return athena::contracts::group_members (the_drd->contracts, group);
}
}

void initialize_node_groups () { (void) standard_node_groups (); }

object native_node_group_members (string group) { return symbols (members (bytes (group))); }

bool native_node_group_contains (string group, object tag) {
  if (!is_symbol (tag)) return false;
  return athena::contracts::group_contains (the_drd->contracts, bytes (group), bytes (as_symbol (tag)));
}

object native_node_group_find (object tag, string group) {
  if (!is_symbol (tag)) return object (false);
  auto found= athena::contracts::group_find (the_drd->contracts, bytes (group), bytes (as_symbol (tag)));
  return found.empty () ? object (false) : symbol_object (text (found));
}

namespace {
object variants (object tag, bool numbered) {
  if (!is_symbol (tag)) return object (false);
  const auto name= bytes (as_symbol (tag));
  auto values= standard_node_groups ().variants (name, numbered);
  if (values.size () == 1 && values.front () == name) {
    auto group= athena::contracts::group_find (the_drd->contracts,
      numbered ? "similar-tag" : "variant-tag", name);
    if (!group.empty ()) values= members (group);
  }
  return symbols (values);
}
}

object native_node_variants (object tag) { return variants (tag, false); }
object native_node_similar (object tag) { return variants (tag, true); }
object native_node_alternate (object tag) {
  if (!is_symbol (tag)) return object (false);
  const auto name= standard_node_groups ().alternate (bytes (as_symbol (tag)));
  return name.empty () ? object (false) : symbol_object (text (name));
}

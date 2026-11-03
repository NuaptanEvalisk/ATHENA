#include "App/Commands/native_node_groups.hpp"
#include "Document/NodeContracts/node_groups.hpp"
#include "Document/NodeContracts/node_group_scope.hpp"
#include "Document/NodeContracts/execution_state.hpp"

namespace {
using athena::contracts::standard_node_groups;
std::string bytes (const string& value) {
  return std::string (value.data (), N (value));
}
string text (const std::string& value) {
  return string (value.data (), value.size ());
}
std::vector<std::string> members (const std::string& group) {
  return athena::contracts::group_members (athena::contracts::current_evaluation_state ().contracts, group);
}
}

void initialize_node_groups () { (void) standard_node_groups (); }

bool native_node_group_contains (string group, string tag) {
  return athena::contracts::group_contains (athena::contracts::current_evaluation_state ().contracts, bytes (group), bytes (tag));
}

namespace {
std::vector<std::string> variants (string tag, bool numbered) {
  const auto name= bytes (tag);
  auto values= standard_node_groups ().variants (name, numbered);
  if (values.size () == 1 && values.front () == name) {
    auto group= athena::contracts::group_find (athena::contracts::current_evaluation_state ().contracts,
      numbered ? "similar-tag" : "variant-tag", name);
    if (!group.empty ()) values= members (group);
  }
  return values;
}
}

array<string> native_node_variants (string tag) {
  array<string> result;
  for (const auto& value: variants (tag, false)) result << text (value);
  return result;
}
array<string> native_node_similar (string tag) {
  array<string> result;
  for (const auto& value: variants (tag, true)) result << text (value);
  return result;
}

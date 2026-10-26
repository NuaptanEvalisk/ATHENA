#include "node_group_scope.hpp"
#include "node_groups.hpp"
#include "contract_declaration.hpp"
#include <algorithm>

namespace athena::contracts {
namespace {
bool member (const std::vector<std::string>& values, std::string_view tag) {
  return std::find (values.begin (), values.end (), tag) != values.end ();
}
}

std::vector<std::string> group_members (const contract_scope& scope, std::string_view group) {
  auto result= group_members (scope.groups (), group);
  observe_group_members (scope, group, result);
  return result;
}

bool group_contains (const contract_scope& scope, std::string_view group, std::string_view tag) {
  const bool result= group_contains (scope.groups (), group, tag);
  observe_group_contains (scope, group, tag, result);
  return result;
}

std::string group_find (const contract_scope& scope, std::string_view group, std::string_view tag) {
  auto result= group_find (scope.groups (), group, tag);
  observe_group_find (scope, group, tag, result);
  return result;
}

std::vector<std::string> group_members (const group_extensions& scope, std::string_view group) {
  const auto& registry= standard_node_groups ();
  auto result= registry.members (group);
  for (const auto& [extension, members]: scope)
    if (registry.includes (group, extension))
      for (const auto& name: members) {
        if (std::find (result.begin (), result.end (), name) == result.end ())
          result.push_back (name);
      }
  return result;
}

bool group_contains (const group_extensions& scope, std::string_view group, std::string_view tag) {
  const auto& registry= standard_node_groups ();
  if (registry.contains (group, tag)) return true;
  for (const auto& [extension, members]: scope)
    if (registry.includes (group, extension) && member (members, tag)) return true;
  return false;
}

std::string group_find (const group_extensions& scope, std::string_view group, std::string_view tag) {
  const auto& registry= standard_node_groups ();
  const auto found= registry.find (group, tag);
  if (!found.empty ()) return found;
  for (const auto& [extension, members]: scope)
    if (registry.includes (group, extension) && member (members, tag)) return extension;
  return {};
}

void declare_group_member (group_extensions& scope, std::string_view group, std::string_view tag) {
  if (group_contains (scope, group, tag)) return;
  auto found= std::find_if (scope.begin (), scope.end (),
    [&] (const auto& entry) { return entry.first == group; });
  if (found == scope.end ()) scope.push_back ({std::string (group), {std::string (tag)}});
  else found->second.emplace_back (tag);
}
}

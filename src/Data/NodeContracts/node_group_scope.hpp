#ifndef ATHENA_NODE_GROUP_SCOPE_HPP
#define ATHENA_NODE_GROUP_SCOPE_HPP

#include "contract_scope.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace athena::contracts {
// Scoped reads participate in execution admission. Raw projections below are
// used by declaration materialization and replay, not by evaluator consumers.
std::vector<std::string> group_members (const contract_scope&, std::string_view group);
bool group_contains (const contract_scope&, std::string_view group, std::string_view tag);
std::string group_find (const contract_scope&, std::string_view group, std::string_view tag);
std::vector<std::string> group_members (const group_extensions&, std::string_view group);
bool group_contains (const group_extensions&, std::string_view group, std::string_view tag);
std::string group_find (const group_extensions&, std::string_view group, std::string_view tag);
void declare_group_member (group_extensions&, std::string_view group, std::string_view tag);
}
#endif

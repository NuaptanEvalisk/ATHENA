#ifndef ATHENA_NATIVE_NODE_GROUPS_HPP
#define ATHENA_NATIVE_NODE_GROUPS_HPP
#include "array.hpp"
#include "string.hpp"

void initialize_node_groups ();
bool native_node_group_contains (string group, string tag);
array<string> native_node_variants (string tag);
array<string> native_node_similar (string tag);

#endif

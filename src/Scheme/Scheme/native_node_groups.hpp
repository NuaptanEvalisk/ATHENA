#ifndef ATHENA_NATIVE_NODE_GROUPS_HPP
#define ATHENA_NATIVE_NODE_GROUPS_HPP
#include "scheme.hpp"

void initialize_node_groups ();
object native_node_group_members (string group);
bool native_node_group_contains (string group, object tag);
object native_node_group_find (object tag, string group);
object native_node_variants (object tag);
object native_node_similar (object tag);
object native_node_alternate (object tag);

#endif

// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once
#include "Core/Types/node_metadata.hpp"
#include "Core/Containers/hashmap.hpp"
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace athena::derived {
struct contribution_block;
using contribution_log= std::shared_ptr<const contribution_block>;
struct contribution {
  std::string channel;
  node::frozen_tree content;
  bool operator== (const contribution& other) const {
    return channel == other.channel && content == other.content;
  }
};
using contribution_entry= std::variant<contribution, contribution_log>;
// Reused bridges retain child handles rather than copying all descendants.
// Leaves are frozen, so a published log is also a valid worker input.
struct contribution_block {
  std::vector<contribution_entry> entries;
};
hashmap<string,tree> contribution_channels (const contribution_log&);
}

// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "Document/State/auxiliary_contributions.hpp"

namespace athena::derived {
hashmap<string,tree> contribution_channels (const contribution_log& root) {
  hashmap<string,tree> result (UNINIT);
  struct cursor { const contribution_block* block; std::size_t next; };
  std::vector<cursor> stack;
  if (root) stack.push_back ({root.get (), 0});
  while (!stack.empty ()) {
    auto& frame= stack.back ();
    if (frame.next == frame.block->entries.size ()) { stack.pop_back (); continue; }
    const auto& entry= frame.block->entries[frame.next++];
    if (const auto* value= std::get_if<contribution> (&entry)) {
      string channel (value->channel.data (), value->channel.size ());
      if (!result->contains (channel)) result (channel)= tree (DOCUMENT);
      result (channel) << tree (value->content);
    }
    else {
      const auto& child= std::get<contribution_log> (entry);
      stack.push_back ({child.get (), 0});
    }
  }
  return result;
}
}

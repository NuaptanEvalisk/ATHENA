// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "Document/State/auxiliary_contributions.hpp"
#include <set>

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
    else if (const auto* child= std::get_if<contribution_log> (&entry)) {
      stack.push_back ({child->get (), 0});
    }
  }
  return result;
}

std::string directory_request::key () const {
  std::string result= "derived-directory:";
  for (const auto* value: {&kind, &channel, &language, &break_style})
    result += std::to_string (value->size ()) + ":" + *value;
  return result;
}

std::vector<directory_request> contribution_directories (const contribution_log& root) {
  std::vector<directory_request> result;
  std::set<std::string> seen;
  std::vector<contribution_log> pending;
  if (root) pending.push_back (root);
  while (!pending.empty ()) {
    auto block= std::move (pending.back ()); pending.pop_back ();
    for (const auto& entry: block->entries)
      if (const auto* request= std::get_if<directory_request> (&entry)) {
        if (seen.insert (request->key ()).second) result.push_back (*request);
      }
      else if (const auto* child= std::get_if<contribution_log> (&entry)) pending.push_back (*child);
  }
  return result;
}
}

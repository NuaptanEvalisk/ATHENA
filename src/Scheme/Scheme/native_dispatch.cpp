// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "native_dispatch.hpp"

#include <string>
#include <unordered_map>

namespace {
auto& entries () {
  static std::unordered_map<std::string, native_dispatch_entry> registry;
  return registry;
}
}

void native_dispatch_register (const char* name, native_dispatch_entry entry) {
  entries ().emplace (name, entry);
}

bool native_dispatch_call (string name, const array<object>& arguments, object& result) {
  const auto& registry= entries ();
  const auto found= registry.find (std::string (name.data (), N(name)));
  if (found == registry.end ()) return false;
  result= found->second (arguments);
  return true;
}

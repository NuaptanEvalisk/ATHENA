#pragma once
#include "node_metadata.hpp"
#include <vector>

namespace athena::slides {
inline std::string kind (const tree& t) {
  if (!is_func (t, SLIDE_PAGE, 1)) return {};
  const auto* meta= node::get (t);
  if (!meta) return {};
  auto it= meta->properties.find ("kind");
  if (it == meta->properties.end ()) return {};
  const auto* value= std::get_if<std::string> (&it->second.data);
  return value ? *value : std::string ();
}
inline std::vector<std::string> fields (const tree& t) {
  const auto k= kind (t);
  if (k == "title") return {"title", "subtitle", "author", "date"};
  if (k == "section") return {"title", "subtitle"};
  if (k == "content") return {"title"};
  return {};
}
}

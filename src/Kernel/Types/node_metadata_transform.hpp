/******************************************************************************
* MODULE     : node_metadata_transform.hpp
* DESCRIPTION: Detached metadata copies with explicit rich-tree transforms
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/
#pragma once
#include "node_metadata.hpp"

namespace athena::node {

// Transform only rich content, never string/identity/reference-valued fields.
// The callback owns recursion through each rich tree and its nested metadata.
template<class F>
property transform_rich_property (const property& value, const F& transform) {
  if (const auto* rich= std::get_if<rich_text> (&value.data))
    return property (rich_text {transform (rich->content)});
  if (const auto* list= std::get_if<property::list> (&value.data)) {
    property::list result;
    for (const auto& item: *list)
      result.push_back (transform_rich_property (item, transform));
    return property (std::move (result));
  }
  if (const auto* dictionary= std::get_if<property::dictionary> (&value.data)) {
    property::dictionary result;
    for (const auto& item: *dictionary)
      result.emplace (item.first, transform_rich_property (item.second, transform));
    return property (std::move (result));
  }
  return copy_property (value);
}

template<class F>
void transform_rich_metadata (const tree& source, tree& target, const F& transform) {
  const auto* original= get (source);
  if (!original) { clear (target); return; }
  metadata result;
  result.id= original->id;
  for (const auto& item: original->properties)
    result.properties.emplace (
      item.first, transform_rich_property (item.second, transform));
  set (target, result);
}

} // namespace athena::node

/******************************************************************************
* Immutable native conversion graph. GNU GPL version 3 or later; see LICENSE.
******************************************************************************/
#pragma once
#include "tree.hpp"
#include <optional>
#include <string>
#include <vector>

struct conversion_edge {
  std::string from, to, executor;
};

std::optional<std::vector<conversion_edge>> conversion_graph_path (
  const std::string& from, const std::string& to);
tree conversion_graph_search (string from, string to);
tree conversion_graph_reachable (tree sources, bool backwards);
tree conversion_menu_formats (string source, string suffix,
                              bool include_texmacs, bool backwards);

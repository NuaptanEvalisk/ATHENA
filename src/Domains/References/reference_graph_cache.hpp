/******************************************************************************
* MODULE     : reference_graph_cache.hpp
* DESCRIPTION: Incremental Vault document-reference graph cache
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#ifndef ATHENA_REFERENCE_GRAPH_CACHE_HPP
#define ATHENA_REFERENCE_GRAPH_CACHE_HPP

#include "tree.hpp"

#include <functional>
#include <string>
#include <vector>

struct AthenaDocumentReference {
  std::string uuid;
  std::string kind;
};

struct AthenaReferenceGraphEdge {
  std::string referenced_path;
  std::string referencing_path;
  std::string kind;
};

std::vector<AthenaDocumentReference>
athena_collect_document_references (tree document);

// A depth of 1 returns direct references; zero means no depth limit.
bool athena_reference_graph_query (
  const std::string& source_relative_path, int max_depth,
  std::vector<AthenaReferenceGraphEdge>& edges,
  const std::function<void(size_t,size_t)>& progress,
  std::string& error);

#endif // ATHENA_REFERENCE_GRAPH_CACHE_HPP

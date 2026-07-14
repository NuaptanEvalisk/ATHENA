/******************************************************************************
* MODULE     : QTMGraphTopology.cpp
* DESCRIPTION: Topological invariants of finite graphs
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "QTMGraphTopology.hpp"

#include <QHash>
#include <algorithm>

namespace {

class DisjointSet {
public:
  int add () {
    int index= (int) parent.size ();
    parent.push_back (index);
    size.push_back (1);
    return index;
  }

  int find (int value) {
    if (parent[value] != value) parent[value]= find (parent[value]);
    return parent[value];
  }

  void unite (int left, int right) {
    left= find (left);
    right= find (right);
    if (left == right) return;
    if (size[left] < size[right]) std::swap (left, right);
    parent[right]= left;
    size[left] += size[right];
  }

private:
  std::vector<int> parent;
  std::vector<int> size;
};

} // namespace

QTMGraphTopologySummary
qtm_graph_topology (
  const std::vector<QString>& vertices,
  const std::vector<std::pair<QString,QString>>& edges) {
  QTMGraphTopologySummary result;
  QHash<QString,int> indices;
  DisjointSet sets;
  auto ensureVertex= [&] (const QString& id) {
    auto existing= indices.constFind (id);
    if (existing != indices.constEnd ()) return existing.value ();
    int index= sets.add ();
    indices.insert (id, index);
    return index;
  };

  for (const QString& vertex: vertices) ensureVertex (vertex);
  std::vector<std::pair<int,int>> indexedEdges;
  indexedEdges.reserve (edges.size ());
  for (const auto& edge: edges) {
    int left= ensureVertex (edge.first);
    int right= ensureVertex (edge.second);
    indexedEdges.push_back ({ left, right });
    sets.unite (left, right);
  }

  result.vertices= indices.size ();
  result.edges= (int) indexedEdges.size ();
  QHash<int,int> componentVertices;
  QHash<int,int> componentEdges;
  for (int i=0; i<result.vertices; i++) componentVertices[sets.find (i)]++;
  for (const auto& edge: indexedEdges) componentEdges[sets.find (edge.first)]++;

  QList<int> roots= componentVertices.keys ();
  std::sort (roots.begin (), roots.end ());
  result.components= roots.size ();
  for (int root: roots) {
    int rank= componentEdges.value (root) - componentVertices.value (root) + 1;
    result.componentRanks.push_back (std::max (0, rank));
    result.firstBettiNumber += std::max (0, rank);
  }
  return result;
}

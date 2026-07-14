/******************************************************************************
* MODULE     : QTMGraphTopology.hpp
* DESCRIPTION: Topological invariants of finite graphs
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#ifndef QTMGRAPHTOPOLOGY_HPP
#define QTMGRAPHTOPOLOGY_HPP

#include <QString>
#include <utility>
#include <vector>

struct QTMGraphTopologySummary {
  int vertices= 0;
  int edges= 0;
  int components= 0;
  int firstBettiNumber= 0;
  std::vector<int> componentRanks;
};

QTMGraphTopologySummary qtm_graph_topology (
  const std::vector<QString>& vertices,
  const std::vector<std::pair<QString,QString>>& edges);

#endif // QTMGRAPHTOPOLOGY_HPP

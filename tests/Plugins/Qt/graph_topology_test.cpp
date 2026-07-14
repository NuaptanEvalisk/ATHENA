/******************************************************************************
* MODULE     : graph_topology_test.cpp
* DESCRIPTION: Tests for finite graph topological invariants
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************/
/* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include <QtTest/QtTest>
#include "Qt/QTMGraphTopology.hpp"

class TestGraphTopology: public QObject {
  Q_OBJECT

private slots:
  void treeIsContractible ();
  void cycleHasOneGenerator ();
  void disconnectedGraphIsComponentwise ();
  void oppositeArrowsRemainDistinctOneCells ();
};

void
TestGraphTopology::treeIsContractible () {
  QTMGraphTopologySummary summary= qtm_graph_topology (
    { "a", "b", "c" }, { { "a", "b" }, { "b", "c" } });
  QCOMPARE (summary.vertices, 3);
  QCOMPARE (summary.edges, 2);
  QCOMPARE (summary.components, 1);
  QCOMPARE (summary.firstBettiNumber, 0);
  QCOMPARE (summary.componentRanks, std::vector<int> ({ 0 }));
}

void
TestGraphTopology::cycleHasOneGenerator () {
  QTMGraphTopologySummary summary= qtm_graph_topology (
    { "a", "b", "c" },
    { { "a", "b" }, { "b", "c" }, { "c", "a" } });
  QCOMPARE (summary.firstBettiNumber, 1);
  QCOMPARE (summary.componentRanks, std::vector<int> ({ 1 }));
}

void
TestGraphTopology::disconnectedGraphIsComponentwise () {
  QTMGraphTopologySummary summary= qtm_graph_topology (
    { "a", "b", "c", "d" }, { { "a", "b" }, { "c", "c" } });
  QCOMPARE (summary.components, 3);
  QCOMPARE (summary.firstBettiNumber, 1);
  QCOMPARE (summary.componentRanks, std::vector<int> ({ 0, 1, 0 }));
}

void
TestGraphTopology::oppositeArrowsRemainDistinctOneCells () {
  QTMGraphTopologySummary summary= qtm_graph_topology (
    { "a", "b" }, { { "a", "b" }, { "b", "a" } });
  QCOMPARE (summary.firstBettiNumber, 1);
}

QTEST_MAIN(TestGraphTopology)
#include "graph_topology_test.moc"

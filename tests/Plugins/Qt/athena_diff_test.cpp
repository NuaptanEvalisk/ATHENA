/******************************************************************************
* MODULE     : athena_diff_test.cpp
* DESCRIPTION: Tests for structural ATHENA document comparison
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include <QtTest/QtTest>

#include "athena_diff.hpp"
#include "node_metadata.hpp"

class TestAthenaDiff: public QObject {
  Q_OBJECT

private slots:
  void alignsInsertedDocumentNodes ();
  void comparesAtomicTextWithinMatchedStructure ();
  void marksStructurallyDifferentNodesOnBothSides ();
  void detectsSourceMetadataChanges ();
};

void
TestAthenaDiff::alignsInsertedDocumentNodes () {
  tree left (DOCUMENT);
  left << tree ("first") << tree ("last");
  tree right (DOCUMENT);
  right << tree ("first") << tree ("inserted") << tree ("last");

  AthenaTreeDiff diff= athena_diff_trees (left, right);
  QCOMPARE (N(diff.left), 0);
  QCOMPARE (N(diff.right), 2);
  QVERIFY (diff.right[0] == path (1) * 0);
  QVERIFY (diff.right[1] == path (1) * 8);
}

void
TestAthenaDiff::comparesAtomicTextWithinMatchedStructure () {
  tree left (DOCUMENT);
  left << tree ("alpha beta omega");
  tree right (DOCUMENT);
  right << tree ("alpha WXYZ omega");

  AthenaTreeDiff diff= athena_diff_trees (left, right);
  QCOMPARE (N(diff.left), 2);
  QCOMPARE (N(diff.right), 2);
  QVERIFY (diff.left[0] == path (0) * 6);
  QVERIFY (diff.left[1] == path (0) * 10);
  QVERIFY (diff.right[0] == path (0) * 6);
  QVERIFY (diff.right[1] == path (0) * 10);
}

void
TestAthenaDiff::marksStructurallyDifferentNodesOnBothSides () {
  tree left (DOCUMENT);
  left << tree (make_tree_label ("strong"), tree ("text"));
  tree right (DOCUMENT);
  right << tree (make_tree_label ("em"), tree ("text"));

  AthenaTreeDiff diff= athena_diff_trees (left, right);
  QCOMPARE (N(diff.left), 2);
  QCOMPARE (N(diff.right), 2);
  QVERIFY (diff.left[0] == path (0) * 0);
  QVERIFY (diff.left[1] == path (0) * 1);
  QVERIFY (diff.right[0] == path (0) * 0);
  QVERIFY (diff.right[1] == path (0) * 1);
}

void
TestAthenaDiff::detectsSourceMetadataChanges () {
  namespace node= athena::node;
  tree left ("same text"), right ("same text");
  node::metadata first, second;
  first.id= "00000000-0000-4000-8000-000000000001";
  second.id= "00000000-0000-4000-8000-000000000002";
  node::set (left, first); node::set (right, second);
  auto diff= athena_diff_trees (left, right);
  QCOMPARE (diff.hunks, size_t (1));
  QCOMPARE (N(diff.left), 2);
  QCOMPARE (N(diff.right), 2);

  left= tree (make_tree_label ("enunciation"), tree (DOCUMENT, "unchanged"));
  right= copy (left);
  first.properties["kind"]= node::property (std::string ("theorem"));
  second= first;
  second.properties["kind"]= node::property (std::string ("lemma"));
  node::set (left, first); node::set (right, second);
  diff= athena_diff_trees (tree (DOCUMENT, left), tree (DOCUMENT, right));
  QVERIFY (diff.hunks > 0);
  QCOMPARE (N(diff.left), 2);
  QCOMPARE (N(diff.right), 2);

  left= tree (""); right= tree ("");
  node::set (left, first); node::set (right, second);
  QCOMPARE (athena_diff_trees (left, right).hunks, size_t (1));
  node::set (right, first);
  QCOMPARE (athena_diff_trees (left, right).hunks, size_t (0));
}

QTEST_MAIN(TestAthenaDiff)
#include "athena_diff_test.moc"

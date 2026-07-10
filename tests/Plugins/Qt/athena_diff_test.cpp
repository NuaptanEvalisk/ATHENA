/******************************************************************************
* MODULE     : athena_diff_test.cpp
* DESCRIPTION: Tests for structural ATHENA document comparison
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include <QtTest/QtTest>

#include "athena_diff.hpp"

class TestAthenaDiff: public QObject {
  Q_OBJECT

private slots:
  void alignsInsertedDocumentNodes ();
  void comparesAtomicTextWithinMatchedStructure ();
  void marksStructurallyDifferentNodesOnBothSides ();
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

QTEST_MAIN(TestAthenaDiff)
#include "athena_diff_test.moc"

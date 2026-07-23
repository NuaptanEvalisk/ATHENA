/******************************************************************************
* MODULE     : person_names_test.cpp
* DESCRIPTION: tests for semantic person-name normalization
* COPYRIGHT  : (C) 2026  Nuaptan
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include <QtTest/QtTest>

#include "ATHENA/Data/person_names.hpp"
#include "drd_std.hpp"

class TestPersonNames: public QObject {
  Q_OBJECT

private slots:
  void initTestCase ();
  void wrapsLongestWholeName ();
  void preservesExistingPersonTags ();
  void learnsExistingPersonTags ();
  void doesNotLeakContextualNames ();
  void respectsWordBoundaries ();
};

void
TestPersonNames::initTestCase () {
  init_std_drd ();
}

void
TestPersonNames::wrapsLongestWholeName () {
  tree source (DOCUMENT, "Leonhard Euler and Emmy Noether");
  int wrapped= 0;
  tree result= athena_normalize_person_names (source, wrapped);

  QCOMPARE (wrapped, 2);
  QVERIFY (is_func (result, DOCUMENT, 1));
  QVERIFY (is_func (result[0], CONCAT, 3));
  QVERIFY (is_compound (result[0][0], "person", 1));
  QCOMPARE (tree_as_string (result[0][0][0]), "Leonhard Euler");
  QVERIFY (is_compound (result[0][2], "person", 1));
  QCOMPARE (tree_as_string (result[0][2][0]), "Emmy Noether");
}

void
TestPersonNames::preservesExistingPersonTags () {
  tree source (DOCUMENT);
  source << compound ("person", "Leonhard Euler");
  int wrapped= 0;
  tree result= athena_normalize_person_names (source, wrapped);

  QCOMPARE (wrapped, 0);
  QCOMPARE (result, source);
}

void
TestPersonNames::learnsExistingPersonTags () {
  tree source (DOCUMENT);
  source << compound ("person", "Ada Lovelace")
         << tree (" and Ada Lovelace");
  int wrapped= 0;
  tree result= athena_normalize_person_names (source, wrapped);

  QCOMPARE (wrapped, 1);
  QCOMPARE (athena_collect_person_occurrences (result).size (), (size_t) 2);
}

void
TestPersonNames::doesNotLeakContextualNames () {
  tree source (DOCUMENT);
  source << compound ("person", "Zyxwvu Exampleperson")
         << tree (" and Zyxwvu Exampleperson");
  int wrapped= 0;
  (void) athena_normalize_person_names (source, wrapped);
  QCOMPARE (wrapped, 1);

  tree unrelated (DOCUMENT, "Zyxwvu Exampleperson");
  wrapped= 0;
  tree result= athena_normalize_person_names (unrelated, wrapped);
  QCOMPARE (wrapped, 0);
  QCOMPARE (result, unrelated);
}

void
TestPersonNames::respectsWordBoundaries () {
  tree source (DOCUMENT, "Eulerian methods and Euler's formula");
  int wrapped= 0;
  tree result= athena_normalize_person_names (source, wrapped);

  QCOMPARE (wrapped, 1);
  QVERIFY (athena_tree_contains_person_text (result, "Euler"));
  QVERIFY (!athena_tree_contains_person_text (
    tree (DOCUMENT, "Eulerian methods"), "Euler"));
}

QTEST_MAIN (TestPersonNames)
#include "person_names_test.moc"

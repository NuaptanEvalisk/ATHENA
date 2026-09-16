/******************************************************************************
* MODULE     : hlink_cursor_locus_test.cpp
* DESCRIPTION: Hyperlink loci preserve child cursor boundaries
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include <QtTest/QtTest>

#include "Typeset/Boxes/construct.hpp"

class HlinkCursorLocusTest: public QObject {
  Q_OBJECT

private slots:
  void transparent_locus_uses_child_cursor_boundaries ();
};

void
HlinkCursorLocusTest::transparent_locus_uses_child_cursor_boundaries () {
  list<string> ids ("source-id");
  box opaque_child= empty_box (path (3), 0, -10, 100, 10);
  box transparent_child= empty_box (path (3), 0, -10, 100, 10);

  box opaque= locus_box (
    path (7), opaque_child, ids, 1, "tmfs://wikilink/example", "");
  box transparent= locus_box (
    path (7), transparent_child, ids, 1, "tmfs://wikilink/example", "", true);

  QCOMPARE (opaque->find_left_box_path (), path (0));
  QCOMPARE (opaque->find_right_box_path (), path (1));
  QCOMPARE (transparent->find_left_box_path (), path (0, path (0)));
  QCOMPARE (transparent->find_right_box_path (), path (0, path (1)));

  QCOMPARE (
    transparent->find_tree_path (transparent->find_left_box_path ()),
    transparent_child->find_tree_path (transparent_child->find_left_box_path ()));
  QCOMPARE (
    transparent->find_tree_path (transparent->find_right_box_path ()),
    transparent_child->find_tree_path (transparent_child->find_right_box_path ()));
}

QTEST_MAIN (HlinkCursorLocusTest)
#include "hlink_cursor_locus_test.moc"

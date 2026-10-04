/******************************************************************************
* MODULE     : heading_structure_test.cpp
* DESCRIPTION: Native section title/text projection regressions
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* See the file LICENSE in the root directory.
******************************************************************************/

#include <QtTest/QtTest>

#include "ATHENA/Data/heading_word_count.hpp"

class TestHeadingStructure: public QObject {
  Q_OBJECT

private slots:
  void projectsPlainText () {
    tree title (CONCAT, "Alpha ", compound ("strong", "Beta"),
                compound ("label", "ignored"));
    QCOMPARE (athena_plain_text_projection (title), string ("Alpha  Beta"));
  }

  void sectionTitlesAndIndentation () {
    QCOMPARE (athena_section_title (compound ("chapter", "Intro"), true, false),
              string ("Intro"));
    QCOMPARE (athena_section_title (compound ("section", "Methods"), true, false),
              string ("   Methods"));
    QCOMPARE (athena_section_title (
                compound ("subsection", "Details"), true, false),
              string ("      Details"));
    QCOMPARE (athena_section_title (
                compound ("subsection", "Details"), true, true),
              string ("   Details"));
    QCOMPARE (athena_section_title (compound ("the-index"), false, false),
              string ("Index"));
    QCOMPARE (athena_section_title (compound ("list-of-figures"), false, false),
              string ("List of figures"));
    QCOMPARE (athena_section_title (compound ("prologue", "discarded"), false, false),
              string ("Prologue"));
  }

  void findsNestedSectionTitle () {
    tree nested (CONCAT, "prefix", compound ("section", "Nested"), "suffix");
    QCOMPARE (athena_section_title (nested, false, false), string ("Nested"));
    tree shared= compound (
      "shared", "key", "meta",
      tree (DOCUMENT, compound ("subsection", "Shared title")));
    QCOMPARE (athena_section_title (shared, false, false),
              string ("Shared title"));
    QCOMPARE (athena_section_title (tree ("plain"), false, false),
              string ("no title"));
  }
};

QTEST_MAIN (TestHeadingStructure)
#include "heading_structure_test.moc"

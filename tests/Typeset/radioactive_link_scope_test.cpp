/******************************************************************************
* MODULE     : radioactive_link_scope_test.cpp
* DESCRIPTION: Tests for structural radioactive-link exclusions
* COPYRIGHT  : (C) 2026  Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "radioactive_link_scope.hpp"

#include <QtTest/QtTest>

class TestRadioactiveLinkScope: public QObject {
  Q_OBJECT

private slots:
  void suppressesDocumentAndHeadingTitles ();
  void suppressesTableOfContents ();
  void suppressesEntireDefinitions ();
  void preservesOrdinaryContent ();
  void permitsSyntheticTransclusionContent ();
};

void
TestRadioactiveLinkScope::suppressesDocumentAndHeadingTitles () {
  QVERIFY (athena_suppresses_radioactive_links ("doc-title"));
  QVERIFY (athena_suppresses_radioactive_links ("heading-fold-title"));
}

void
TestRadioactiveLinkScope::suppressesTableOfContents () {
  QVERIFY (athena_suppresses_radioactive_links ("table-of-contents"));
  QVERIFY (athena_suppresses_radioactive_links ("table-of-contents*"));
  QVERIFY (
    athena_suppresses_radioactive_links ("screen-folded-table-of-contents"));
  QVERIFY (
    athena_suppresses_radioactive_links ("screen-unfolded-table-of-contents*"));
  QVERIFY (athena_suppresses_radioactive_links (
    "render-unfolded-table-of-contents"));
}

void
TestRadioactiveLinkScope::suppressesEntireDefinitions () {
  QVERIFY (athena_suppresses_radioactive_links ("definition"));
}

void
TestRadioactiveLinkScope::preservesOrdinaryContent () {
  QVERIFY (!athena_suppresses_radioactive_links ("document"));
  QVERIFY (!athena_suppresses_radioactive_links ("strong"));
  QVERIFY (!athena_suppresses_radioactive_links ("theorem"));
}

void
TestRadioactiveLinkScope::permitsSyntheticTransclusionContent () {
  QVERIFY (athena_allows_radioactive_link_path (true, false));
  QVERIFY (!athena_allows_radioactive_link_path (false, false));
  QVERIFY (athena_allows_radioactive_link_path (false, true));
}

QTEST_MAIN (TestRadioactiveLinkScope)
#include "radioactive_link_scope_test.moc"

/******************************************************************************
* MODULE     : new_buffer_test.cpp
* DESCRIPTION: Tests for ATHENA buffer loading
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include <QtTest/QtTest>

#include "new_buffer.hpp"
#include "new_data.hpp"

class TestNewBuffer: public QObject {
  Q_OBJECT

private slots:
  void importsEmptyAthenaFileAsNewDocument ();
};

void
TestNewBuffer::importsEmptyAthenaFileAsNewDocument () {
  tree document= import_loaded_tree ("", url ("empty.ath"), "texmacs");
  tree expected (DOCUMENT);
  expected << compound ("TeXmacs", TEXMACS_COMPAT_VERSION)
           << compound ("style", "generic")
           << compound ("body", tree (DOCUMENT, ""));

  QVERIFY (document == expected);
  QVERIFY (!is_func (document, _ERROR));
}

QTEST_MAIN (TestNewBuffer)
#include "new_buffer_test.moc"

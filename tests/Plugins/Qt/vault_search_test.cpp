/******************************************************************************
* MODULE     : vault_search_test.cpp
* DESCRIPTION: Tests for vault content fuzzy matching
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include <QtTest/QtTest>
#include "Qt/QTMVaultSearch.hpp"
#include "Qt/qt_utilities.hpp"

class TestVaultSearch: public QObject {
  Q_OBJECT

private slots:
  void fuzzyInsertion ();
  void fuzzySubstitution ();
  void shortQueryIsExactOnly ();
  void exactPrecedesAndDoesNotOverlapFuzzy ();
  void caseInsensitiveExactMatch ();
  void unicodeOffsetsMapToTeXmacsBytes ();
};

static std::vector<VaultContentMatch>
matchesFor (const QString& text, const QString& query,
            bool caseInsensitive= false, bool fuzzy= true) {
  std::vector<VaultContentMatch> matches;
  append_content_matches (matches, tree (from_qstring (text)),
                          tree (from_qstring (query)), path (), 200,
                          caseInsensitive, fuzzy);
  return matches;
}

void
TestVaultSearch::fuzzyInsertion () {
  std::vector<VaultContentMatch> matches=
    matchesFor ("The hello world example", "helo world");
  QVERIFY (!matches.empty ());
  QVERIFY (!matches[0].exact);
  QVERIFY (matches[0].score >= 80.0);
}

void
TestVaultSearch::fuzzySubstitution () {
  std::vector<VaultContentMatch> matches=
    matchesFor ("A compact mathematical world", "mathematical worle");
  QVERIFY (!matches.empty ());
  QVERIFY (!matches[0].exact);
  QVERIFY (matches[0].score >= 80.0);
}

void
TestVaultSearch::shortQueryIsExactOnly () {
  QVERIFY (matchesFor ("abcdef", "abx").empty ());
  std::vector<VaultContentMatch> exact= matchesFor ("abcdef", "abc");
  QCOMPARE ((int) exact.size (), 1);
  QVERIFY (exact[0].exact);
}

void
TestVaultSearch::exactPrecedesAndDoesNotOverlapFuzzy () {
  std::vector<VaultContentMatch> matches=
    matchesFor ("hello world; helo world", "hello world");
  QCOMPARE ((int) matches.size (), 2);
  QVERIFY (matches[0].exact);
  QVERIFY (!matches[1].exact);
  QVERIFY (path_less_eq (matches[0].end, matches[1].start));
}

void
TestVaultSearch::caseInsensitiveExactMatch () {
  std::vector<VaultContentMatch> matches=
    matchesFor ("ATHENA Knowledge", "athena knowledge", true);
  QCOMPARE ((int) matches.size (), 1);
  QVERIFY (matches[0].exact);
}

void
TestVaultSearch::unicodeOffsetsMapToTeXmacsBytes () {
  QString text= QString::fromUtf8 ("数学知识组织 and more");
  std::vector<VaultContentMatch> matches=
    matchesFor (text, QString::fromUtf8 ("数学知织"));
  QVERIFY (!matches.empty ());
  QVERIFY (!matches[0].exact);

  string source= from_qstring (text);
  int start= last_item (matches[0].start);
  int end= last_item (matches[0].end);
  QCOMPARE (to_qstring (source (start, end)), QString::fromUtf8 ("数学知识"));
}

QTEST_MAIN(TestVaultSearch)
#include "vault_search_test.moc"

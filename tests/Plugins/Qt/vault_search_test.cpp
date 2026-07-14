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
#include <QTemporaryFile>
#include "Qt/QTMVaultAnchorModel.hpp"
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
  void listFilteringRespectsOptions ();
  void recognizesEnunciationAnchorPairs ();
  void findsInnermostEnclosingAnchorPair ();
  void rawPrefilterRejectsUnrelatedFiles ();
  void rawPrefilterRespectsCaseOption ();
  void rawPrefilterIsConservative ();
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

void
TestVaultSearch::listFilteringRespectsOptions () {
  QCOMPARE (list_filter_score ("Definition: Compactness", "definition",
                               false, false), -1);
  QVERIFY (list_filter_score ("Definition: Compactness", "definition",
                              true, false) >= 0);
  QCOMPARE (list_filter_score ("Definition: Compactness", "Defnition",
                               false, false), -1);
  QVERIFY (list_filter_score ("Definition: Compactness", "Defnition",
                              false, true) >= 0);
}

void
TestVaultSearch::recognizesEnunciationAnchorPairs () {
  TransclusionAnchorPair theorem;
  theorem.upper= "theorem:Banach fixed point {";
  theorem.lower= "theorem:Banach fixed point }";
  QVERIFY (anchor_pair_is_enunciation (theorem));

  TransclusionAnchorPair paragraph;
  paragraph.upper= "A paragraph anchor {";
  paragraph.lower= "A paragraph anchor }";
  QVERIFY (!anchor_pair_is_enunciation (paragraph));
}

void
TestVaultSearch::findsInnermostEnclosingAnchorPair () {
  TransclusionAnchorPair outer;
  outer.upperWhere= path (1);
  outer.lowerWhere= path (8);
  TransclusionAnchorPair inner;
  inner.upperWhere= path (3);
  inner.lowerWhere= path (6);
  std::vector<TransclusionAnchorPair> pairs= { outer, inner };

  QCOMPARE (enclosing_anchor_pair_index (pairs, path (4)), 1);
  QCOMPARE (enclosing_anchor_pair_index (pairs, path (7)), 0);
  QCOMPARE (enclosing_anchor_pair_index (pairs, path (9)), -1);
}

static url
temporarySource (QTemporaryFile& file, const QByteArray& source) {
  if (!file.open ()) qFatal ("Unable to open temporary source file");
  if (file.write (source) != source.size ())
    qFatal ("Unable to write temporary source file");
  if (!file.flush ()) qFatal ("Unable to flush temporary source file");
  return url_system (from_qstring (file.fileName ()));
}

void
TestVaultSearch::rawPrefilterRejectsUnrelatedFiles () {
  QTemporaryFile matching;
  QTemporaryFile unrelated;
  url matchingUrl= temporarySource (
    matching, "<\\body>The Banach fixed point theorem</body>");
  url unrelatedUrl= temporarySource (
    unrelated, "<\\body>A compactness argument</body>");
  VaultRawSearchPrefilter filter ("Banach fixed point", false, false);
  QVERIFY (filter.isEffective ());
  QVERIFY (filter.fileMayMatch (matchingUrl));
  QVERIFY (!filter.fileMayMatch (unrelatedUrl));
}

void
TestVaultSearch::rawPrefilterRespectsCaseOption () {
  QTemporaryFile file;
  url source= temporarySource (file, "<\\body>BANACH theorem</body>");
  VaultRawSearchPrefilter sensitive ("banach", false, false);
  VaultRawSearchPrefilter insensitive ("banach", true, false);
  QVERIFY (!sensitive.fileMayMatch (source));
  QVERIFY (insensitive.fileMayMatch (source));
}

void
TestVaultSearch::rawPrefilterIsConservative () {
  QTemporaryFile file;
  url source= temporarySource (file, "<\\body>unrelated source</body>");
  VaultRawSearchPrefilter fuzzy ("misspeled query", false, true);
  VaultRawSearchPrefilter unicode (QString::fromUtf8 ("数学知识"), false,
                                  false);
  QVERIFY (!fuzzy.isEffective ());
  QVERIFY (fuzzy.fileMayMatch (source));
  QVERIFY (!unicode.isEffective ());
  QVERIFY (unicode.fileMayMatch (source));
  VaultRawSearchPrefilter exact ("definitely absent", false, false);
  QVERIFY (exact.fileMayMatch (
    url_system (from_qstring (file.fileName () + ".missing"))));
}

QTEST_MAIN(TestVaultSearch)
#include "vault_search_test.moc"

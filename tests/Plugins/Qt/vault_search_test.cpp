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
#include "drd_std.hpp"
#include "drd_mode.hpp"
#include <atomic>
#include <thread>

class TestVaultSearch: public QObject {
  Q_OBJECT

private slots:
  void initTestCase ();
  void findsStructuredMathematicalExpressions ();
  void keepsConcurrentSearchOptionsIndependent ();
  void fuzzyInsertion ();
  void fuzzySubstitution ();
  void shortQueryIsExactOnly ();
  void exactPrecedesAndDoesNotOverlapFuzzy ();
  void caseInsensitiveExactMatch ();
  void unicodeOffsetsMapToTeXmacsBytes ();
  void listFilteringRespectsOptions ();
  void recognizesEnunciationAnchorPairs ();
  void findsInnermostEnclosingAnchorPair ();
  void recognizesHeadingAnchorTargets ();
  void prefersEnunciationNamesAndHeadings ();
  void rawPrefilterRejectsUnrelatedFiles ();
  void rawPrefilterRespectsCaseOption ();
  void rawPrefilterIsConservative ();
};

void
TestVaultSearch::initTestCase () {
  init_std_drd ();
}

void
TestVaultSearch::keepsConcurrentSearchOptionsIndependent () {
  std::atomic<int> ready {0}, errors {0};
  auto worker= [&] (bool insensitive) {
    tree source ("AAA AAA AAA AAA AAA AAA AAA AAA"), query ("aaa");
    ready.fetch_add (1);
    while (ready.load () != 2) std::this_thread::yield ();
    for (int i=0; i<2000; ++i) {
      auto ranges= search (source, query, path (), insensitive, 200);
      if (N(ranges) != (insensitive ? 16 : 0)) errors.fetch_add (1);
    }
  };
  std::thread sensitive (worker, false), insensitive (worker, true);
  sensitive.join ();
  insensitive.join ();
  QCOMPARE (errors.load (), 0);
}

void
TestVaultSearch::findsStructuredMathematicalExpressions () {
  tree expression= tree (CONCAT, "x", tree (RSUP, "2"), "+1");
  tree math_query= compound ("math", expression);
  tree longer= compound ("math",
    tree (CONCAT, "f=x", tree (RSUP, "2"), "+1+y"));
  std::vector<VaultContentMatch> matches;
  int previous= set_access_mode (DRD_ACCESS_SOURCE);
  append_content_matches (matches, longer, math_query, path (), 200, false, false);
  set_access_mode (previous);
  QCOMPARE (matches.size (), (size_t) 1);
  QVERIFY (matches[0].exact);
  QCOMPARE (matches[0].start, path (0) * 0 * 2);
  QCOMPARE (matches[0].end, path (0) * 2 * 2);

  for (tree different: {
         tree ("f=x2+1+y"),
         compound ("math", tree (CONCAT, "f=x", tree (RSUB, "2"), "+1+y")),
         compound ("math", tree (CONCAT, "f=x", tree (RSUP, "20"), "+1+y")),
         compound ("math", tree (CONCAT, "f=x", tree (RSUP, "2"), "+10+y"))}) {
    matches.clear ();
    append_content_matches (matches, different, math_query, path (), 200, false, false);
    QVERIFY (matches.empty ());
  }

  tree fraction= tree (FRAC, tree (CONCAT, "x", tree (RSUP, "2")), "y");
  tree fraction_source= compound ("math", tree (CONCAT, "f=", fraction, "+z"));
  matches.clear ();
  append_content_matches (matches, fraction_source, compound ("math", fraction),
                          path (), 200, false, true);
  QCOMPARE (matches.size (), (size_t) 1);
  QVERIFY (matches[0].exact);
  matches.clear ();
  append_content_matches (matches, fraction_source,
                          compound ("math", tree (FRAC, "y", "x")),
                          path (), 200, false, true);
  QVERIFY (matches.empty ());

  tree mixed_query= tree (CONCAT, "where ", compound ("math", "x"), " is");
  tree mixed_source= tree (CONCAT, "Observe where ", compound ("math", "x"),
                          " is defined.");
  matches.clear ();
  append_content_matches (matches, mixed_source, mixed_query, path (), 200, false, false);
  QCOMPARE (matches.size (), (size_t) 1);
  matches.clear ();
  append_content_matches (matches, tree ("Observe where x is defined."),
                          mixed_query, path (), 200, false, false);
  QVERIFY (matches.empty ());
  matches.clear ();
  append_content_matches (matches, compound ("math", "sinh"),
                          compound ("math", "sin"), path (), 200, false, false);
  QVERIFY (matches.empty ());
}

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

void
TestVaultSearch::recognizesHeadingAnchorTargets () {
  tree body (DOCUMENT);
  body << tree (LABEL, "H1 Overview");
  body << compound ("section", "Overview");
  body << tree ("Body text");
  body << tree (LABEL, "theorem:Result {");
  body << compound ("theorem", "Result");
  body << tree (LABEL, "theorem:Result }");

  std::vector<TransclusionAnchorPair> headings=
    collect_heading_anchor_targets (body, path ());
  QCOMPARE ((int) headings.size (), 1);
  QCOMPARE (headings[0].upper, QString ("H1 Overview"));
  QCOMPARE (headings[0].lower, QString ("H1 Overview"));
  QCOMPARE (headings[0].upperWhere, path (0));
  QCOMPARE (headings[0].lowerWhere, path (1));
  QCOMPARE (heading_anchor_target_index (headings, path (1, 0)), 0);
  QCOMPARE (heading_anchor_target_index (headings, path (2, 0)), -1);
  QVERIFY (is_wikilink_anchor ("H1 Overview"));

  std::vector<VaultContentMatch> matches;
  append_content_matches (matches, body, tree ("Overview"), path (), 200,
                          false, false);
  append_heading_matches (matches, body, tree ("Overview"), path (),
                          200 - (int) matches.size (), false, false);
  bool foundHeadingText= false;
  for (const VaultContentMatch& match: matches)
    if (heading_anchor_target_index (headings, match.start) == 0)
      foundHeadingText= true;
  QVERIFY (foundHeadingText);
}

void
TestVaultSearch::prefersEnunciationNamesAndHeadings () {
  tree theoremBody (CONCAT);
  theoremBody << compound ("strong", "Named result");
  theoremBody << tree ("Body result");
  tree body (DOCUMENT);
  body << tree (LABEL, "theorem:Named result {");
  body << compound ("theorem", theoremBody);
  body << tree (LABEL, "theorem:Named result }");
  body << tree (LABEL, "H1 Named result section");
  body << compound ("section", "Named result section");

  std::vector<VaultContentMatch> matches (4);
  for (VaultContentMatch& match: matches) {
    match.exact= true;
    match.score= 100.0;
  }
  matches[0].start= path () * 0 * 0 * 0;
  matches[1].start= path () * 1 * 0 * 0 * 0;
  matches[2].start= path () * 1 * 0 * 1 * 0;
  matches[3].start= path () * 4 * 0 * 0;
  score_search_match_titles (body, tree ("Named result"), path (), matches,
                             false, false);

  QCOMPARE (matches[0].titleMatchScore, 100000);
  QCOMPARE (matches[1].titleMatchScore, 100000);
  QCOMPARE (matches[2].titleMatchScore, -1);
  QVERIFY (matches[3].titleMatchScore >= 0);
  QVERIFY (vault_search_match_precedes (matches[0], matches[2]));

  tree names (DOCUMENT);
  names << tree (LABEL, "definition:Lie group {");
  names << tree (LABEL, "definition:Lie groupoid {");
  std::vector<VaultContentMatch> nameMatches (2);
  for (VaultContentMatch& match: nameMatches) {
    match.exact= true;
    match.score= 100.0;
  }
  nameMatches[0].start= path () * 0 * 0 * 0;
  nameMatches[1].start= path () * 1 * 0 * 0;
  score_search_match_titles (names, tree ("Lie group"), path (),
                             nameMatches, false, false);
  QCOMPARE (nameMatches[0].titleMatchScore, 100000);
  QVERIFY (nameMatches[1].titleMatchScore < nameMatches[0].titleMatchScore);
  QVERIFY (vault_search_match_precedes (nameMatches[0], nameMatches[1]));
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

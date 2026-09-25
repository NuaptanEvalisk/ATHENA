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
#include <QTemporaryDir>
#include "Qt/QTMVaultAnchorModel.hpp"
#include "Qt/QTMVaultAvailableEnunciations.hpp"
#include "Qt/QTMVaultAvailablePage.hpp"
#include "Qt/QTMChoiceNavigation.hpp"
#include <QLineEdit>
#include <QRadioButton>
#include <QVBoxLayout>
#include "ATHENA/Data/transclusion_cache.hpp"
#include "convert.hpp"
#include "Qt/QTMVaultSearch.hpp"
#include "Qt/QTMVaultSearchWorker.hpp"
#include <QSemaphore>
#include "Qt/qt_utilities.hpp"
#include "drd_std.hpp"
#include "drd_mode.hpp"
#include "namespaces.hpp"
#include "vault.hpp"
#include "vaultfile_json.hpp"
#include <filesystem>
#include <fstream>
#include <set>
#include <atomic>
#include <thread>

bool headless_mode= true;

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
  void fileRankingLetsQueryBeatCurrent ();
  void recognizesEnunciationAnchorPairs ();
  void findsInnermostEnclosingAnchorPair ();
  void recognizesHeadingAnchorTargets ();
  void prefersEnunciationNamesAndHeadings ();
  void rawPrefilterRejectsUnrelatedFiles ();
  void rawPrefilterRespectsCaseOption ();
  void rawPrefilterIsConservative ();
  void parallelSearchPreservesOrder ();
  void parallelSearchCancelsOnOwnerDestruction ();
  void rawPrefilterMatchesAcrossReadBoundary ();
  void neighborhoodCandidateScope ();
  void availableEnunciationsFollowOnlyReferencedRanges ();
  void availableEnunciationsKeepUnsavedSourceAndCancel ();
  void availablePreviewHasAnEmbeddingLayout ();
  void modeChoicesCycle ();
  void availableArrowKeysKeepInputFocus ();
};

void TestVaultSearch::fileRankingLetsQueryBeatCurrent () {
  WikilinkFileEntry lectureOne;
  lectureOne.relPath= "Lecture Notes I.ath";
  lectureOne.stem= "Lecture Notes I";
  lectureOne.searchPath= "Lecture Notes I";
  lectureOne.searchStem= "Lecture Notes I";
  lectureOne.mtime= 10;
  lectureOne.isCurrent= false;

  WikilinkFileEntry lectureTwo;
  lectureTwo.relPath= "Lecture Notes II.ath";
  lectureTwo.stem= "Lecture Notes II";
  lectureTwo.searchPath= "Lecture Notes II";
  lectureTwo.searchStem= "Lecture Notes II";
  lectureTwo.mtime= 20;
  lectureTwo.isCurrent= true;

  const std::vector<WikilinkFileEntry> files {lectureOne, lectureTwo};
  const auto initial= rank_vault_link_files (files, "");
  QCOMPARE (initial.size (), std::size_t (2));
  QCOMPARE (initial[0].index, 1);

  const auto filtered=
    rank_vault_link_files (files, "Lecture Notes I");
  QCOMPARE (filtered.size (), std::size_t (2));
  QCOMPARE (filtered[0].index, 0);
  QVERIFY (filtered[0].score > filtered[1].score);
}

void TestVaultSearch::modeChoicesCycle () {
  for (int count: {3, 4}) {
    QWidget page;
    QVBoxLayout layout (&page);
    QRadioButton first ("File"), second ("Search"), third ("Artifact"), fourth ("Available");
    layout.addWidget (&first); layout.addWidget (&second); layout.addWidget (&third);
    if (count == 4) layout.addWidget (&fourth);
    if (count == 3) new QTMRadioChoiceNavigation (&page, {&first, &second, &third});
    else new QTMRadioChoiceNavigation (&page, {&first, &second, &third, &fourth});
    page.show (); page.activateWindow (); first.setChecked (true); first.setFocus ();
    QApplication::processEvents ();
    auto* last= count == 3 ? &third : &fourth;
    QTest::keyClick (&first, Qt::Key_Up);
    QVERIFY (last->isChecked ()); QVERIFY (last->hasFocus ());
    QTest::keyClick (last, Qt::Key_Down);
    QVERIFY (first.isChecked ());
    second.setEnabled (false);
    QTest::keyClick (&first, Qt::Key_Down);
    QVERIFY (third.isChecked ());
  }
}

void TestVaultSearch::availableArrowKeysKeepInputFocus () {
  QTMVaultAvailablePage page;
  auto* list= page.findChild<QListWidget*> ();
  QVERIFY (list);
  // Only exercise navigation here; these rows do not represent preview trees.
  QSignalBlocker blocker (list);
  list->addItems ({"First", "Second", "Third"}); list->setCurrentRow (0);
  page.show (); page.activateWindow ();
  for (auto* input: page.findChildren<QLineEdit*> ()) {
    input->setFocus (); QApplication::processEvents ();
    list->setCurrentRow (0);
    QTest::keyClick (input, Qt::Key_Up);
    QCOMPARE (list->currentRow (), 2); QVERIFY (input->hasFocus ());
    QTest::keyClick (input, Qt::Key_Down);
    QCOMPARE (list->currentRow (), 0); QVERIFY (input->hasFocus ());
  }
  list->clear ();
  QTest::keyClick (page.findChild<QLineEdit*> (), Qt::Key_Down);
  QCOMPARE (list->currentRow (), -1);
}

void TestVaultSearch::availablePreviewHasAnEmbeddingLayout () {
  QTMVaultAvailablePage page;
  QWidget* host= page.findChild<QWidget*> ("availableEnunciationPreview");
  QVERIFY (host != nullptr);
  QVERIFY (host->layout () != nullptr);
  QVERIFY (!page.isComplete ());
}

namespace {
std::shared_ptr<const std::string> availableSnapshot (tree body) {
  string bytes= tree_to_scheme (body);
  return std::make_shared<const std::string> (bytes.data (), N (bytes));
}
tree enunciationBody (string name, tree content) {
  return tree (DOCUMENT, tree (LABEL, name * " {"),
    compound ("theorem", content), tree (LABEL, name * " }"));
}
tree transclusionTo (string uuid) {
  return tree (TRANSCLUDE, uuid, "obsolete path hint", "obsolete begin", "obsolete end");
}
}

void TestVaultSearch::availableEnunciationsFollowOnlyReferencedRanges () {
  tree local= enunciationBody ("theorem:Local", tree (CONCAT,
    transclusionTo ("b"), transclusionTo ("b"), transclusionTo ("missing")));
  tree b= enunciationBody ("theorem:Included", transclusionTo ("c"));
  b << enunciationBody ("theorem:Excluded", "Not part of the selected range");
  tree c= enunciationBody ("lemma:Nested", tree (CONCAT, "nested", transclusionTo ("b")));
  std::map<QString, std::shared_ptr<const std::string>> files {
    {"B.ath", availableSnapshot (b)}, {"C.ath", availableSnapshot (c)}};
  std::map<QString, int> reads;
  auto locate= [] (const std::string& uuid, AthenaVaultMapNode& target) {
    if (uuid == "b") target= {uuid, "B.ath", "theorem:Included {", "theorem:Included }"};
    else if (uuid == "c") target= {uuid, "C.ath", "", ""};
    else return false;
    return true;
  };
  auto load= [&] (const QString& file) {
    ++reads[file];
    const auto& bytes= files.at (file);
    return scheme_to_tree (string (bytes->data (), bytes->size ()));
  };
  std::atomic<bool> cancelled {false};
  auto result= collect_available_enunciations (availableSnapshot (local), "A.ath", locate, load, cancelled);
  QCOMPARE (result.entries.size (), std::size_t (3));
  QCOMPARE (reads["B.ath"], 1);
  QCOMPARE (reads["C.ath"], 1);
  QCOMPARE (result.warnings.size (), 1);
  const auto& nested= result.entries[2];
  QCOMPARE (nested.relative_path, QString ("C.ath"));
  QCOMPARE (nested.upper, QString ("lemma:Nested {"));
  QVERIFY (nested.title != "theorem:Excluded");
  tree source= scheme_to_tree (string (nested.source_body->data (), nested.source_body->size ()));
  tree range= athena_transclusion_source_range (source, from_qstring (nested.upper), from_qstring (nested.lower));
  QVERIFY (range != UNINIT);
  std::vector<WikilinkAnchorEntry> anchors;
  collect_anchors (range, path (), anchors);
  QCOMPARE (anchors.size (), std::size_t (2));
  QCOMPARE (anchors[0].anchor, nested.upper);
}

void TestVaultSearch::availableEnunciationsKeepUnsavedSourceAndCancel () {
  tree source= enunciationBody ("definition:Unsaved", "not on disk");
  source << transclusionTo ("self");
  auto bytes= availableSnapshot (source);
  auto locate= [] (const std::string& uuid, AthenaVaultMapNode& target) {
    target= {uuid, "A.ath", "", ""}; return true;
  };
  int reads= 0;
  auto load= [&] (const QString&) { ++reads; return tree (DOCUMENT, "old disk content"); };
  std::atomic<bool> cancelled {false};
  auto result= collect_available_enunciations (bytes, "A.ath", locate, load, cancelled);
  QCOMPARE (result.entries.size (), std::size_t (1));
  QCOMPARE (result.entries[0].title, QString ("definition:Unsaved"));
  QCOMPARE (result.entries[0].source_body, bytes);
  QCOMPARE (reads, 0);
  cancelled= true;
  result= collect_available_enunciations (bytes, "A.ath", locate, load, cancelled);
  QVERIFY (result.entries.empty ());
  QCOMPARE (reads, 0);
  cancelled= false;
  result= collect_available_enunciations (availableSnapshot (
    enunciationBody ("definition:Unsaved", "no persistent file")), "", locate, load, cancelled);
  QVERIFY (result.entries.empty ());
}

void
TestVaultSearch::initTestCase () {
  init_std_drd ();
}

void
TestVaultSearch::neighborhoodCandidateScope () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  struct CloseVault { ~CloseVault () { if (vault_active ()) vault_close (); } } close;
  namespace fs= std::filesystem;
  const fs::path root (temporary.path ().toStdString ());
  fs::create_directories (root / "a");
  fs::create_directories (root / "b");
  for (const auto* name: {"a/Note alpha.ath", "a/Other.ath", "b/Note beta.ath",
                          "b/Note two words.ath", "b/Else omega.ath"}) {
    std::ofstream file (root / name);
    file << "<TeXmacs|2.1.4>\n\n<\\body>\nhello\n</body>\n";
  }
  std::string configuration_error;
  QVERIFY (athena_vaultfile_write (root, AthenaVaultfileInfo {}, configuration_error));
  QCOMPARE (vault_load (url_system (from_qstring (temporary.path ())),
                        "Search test", "map.sqlite", "ns.sqlite"), string (""));
  const auto context= vault_capture_context ();
  string error;
  athena_namespace_definition parent;
  parent.name= "Broad"; parent.kind= "semi-concrete";
  parent.templ= "Note %s"; parent.sorter_trivial= true;
  QVERIFY (athena_namespace_create (context, parent, error));
  athena_namespace_definition child;
  child.name= "Specific"; child.kind= "semi-concrete";
  child.templ= "Note %w"; child.sorter_trivial= true;
  child.parents.push_back (parent.name);
  QVERIFY (athena_namespace_create (context, child, error));
  athena_namespace_definition other;
  other.name= "Else"; other.kind= "semi-concrete";
  other.templ= "Else %w"; other.sorter_trivial= true;
  QVERIFY (athena_namespace_create (context, other, error));
  const url origin= url_system (from_qstring (temporary.path () + "/a/Note alpha.ath"));
  std::vector<url> files;
  auto names= [&] () {
    std::set<std::string> result;
    for (const auto& file: files)
      result.insert (fs::path (to_qstring (concretize (file)).toStdString ()).filename ().string ());
    return result;
  };
  QVERIFY (vault_search_candidate_files (origin, "", false, files, error));
  QCOMPARE (files.size (), std::size_t (5));
  QVERIFY2 (vault_search_candidate_files (origin, "", true, files, error), as_charp (error));
  QVERIFY ((names () == std::set<std::string> {"Note alpha.ath", "Note beta.ath", "Other.ath"}));
  QCOMPARE (files.size (), std::size_t (3));
  QVERIFY (vault_search_candidate_files (origin, "Broad", true, files, error));
  QVERIFY ((names () == std::set<std::string> {"Note alpha.ath", "Note beta.ath"}));
  QVERIFY (vault_search_candidate_files (origin, "Broad", false, files, error));
  QCOMPARE (files.size (), std::size_t (3));
  QVERIFY (vault_search_candidate_files (origin, "Else", true, files, error));
  QVERIFY (files.empty ());
  QVERIFY (!vault_search_candidate_files (origin, "Missing", false, files, error));
  QVERIFY (error != "");
  QVERIFY (files.empty ());
  QVERIFY (!vault_search_candidate_files (url_none (), "", true, files, error));
  QVERIFY (error != "");
  QVERIFY (files.empty ());
  QVERIFY (vault_search_candidate_files (url_none (), "", false, files, error));
  QCOMPARE (files.size (), std::size_t (5));

  QVERIFY (vault_search_candidate_files (origin, "", true, files, error));
  std::vector<std::string> paths;
  for (const auto& file: files) paths.push_back (to_qstring (concretize (file)).toStdString ());
  VaultSearchOptions options;
  options.query= "hello";
  bool complete= false;
  int inspected= -1;
  std::vector<QString> searched;
  const auto task= start_vault_search<QString> (this, std::move (paths), options,
    [] (tree, url file, const tree&, const VaultSearchOptions&, std::vector<QString>& hits) {
      hits.push_back (to_qstring (as_system_string (tail (file))));
    }, [] (const VaultSearchProgress&) {},
    [&] (std::vector<QString> result, const VaultSearchProgress& progress, bool cancelled) {
      inspected= cancelled ? -1 : progress.completed;
      searched= std::move (result);
      complete= true;
    });
  QTRY_VERIFY (complete);
  QCOMPARE (inspected, 3);
  QCOMPARE (searched.size (), std::size_t (3));
  QVERIFY (std::find (searched.begin (), searched.end (), "Other.ath") != searched.end ());
  QVERIFY (std::find (searched.begin (), searched.end (), "Note beta.ath") != searched.end ());
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

void TestVaultSearch::parallelSearchPreservesOrder () {
  QTemporaryFile first, second;
  temporarySource (first, "<TeXmacs|2.1.4>\n\n<style|generic>\n\n<\\body>\nsubgroup\n</body>\n");
  temporarySource (second, "<TeXmacs|2.1.4>\n\n<style|generic>\n\n<\\body>\nunrelated\n</body>\n");
  QVERIFY (vault_search_read_body (first.fileName ()) == tree (DOCUMENT, "subgroup"));
  for (bool fuzzy: {false, true}) {
    QObject owner;
    VaultSearchOptions options;
    options.query= "subgroup";
    options.fuzzy= fuzzy;
    bool done= false;
    int scanned= -1;
    std::vector<std::string> results;
    auto task= start_vault_search<std::string> (&owner,
      {first.fileName ().toStdString (), second.fileName ().toStdString ()},
      options,
      [] (tree body, url, const tree& query, const VaultSearchOptions& options,
          std::vector<std::string>& out) {
        std::vector<VaultContentMatch> matches;
        int previous= set_access_mode (DRD_ACCESS_SOURCE);
        append_content_matches (matches, body, query, path (), 200,
                                options.caseInsensitive, options.fuzzy);
        set_access_mode (previous);
        if (!matches.empty ()) out.push_back ("subgroup");
      }, [] (const VaultSearchProgress&) {},
      [&] (std::vector<std::string> out, const VaultSearchProgress& p, bool stopped) {
        QVERIFY (!stopped);
        results= std::move (out);
        scanned= p.completed;
        done= true;
      });
    QTRY_VERIFY_WITH_TIMEOUT (done, 5000);
    QCOMPARE (scanned, fuzzy ? 2 : 1);
    QCOMPARE (results.size (), size_t (1));
    QCOMPARE (results[0], std::string ("subgroup"));
  }
}

void TestVaultSearch::parallelSearchCancelsOnOwnerDestruction () {
  QTemporaryFile source;
  temporarySource (source, "<TeXmacs|2.1.4>\n\n<style|generic>\n\n<\\body>\nsubgroup\n</body>\n");
  auto gate= std::make_shared<QSemaphore> ();
  auto entered= std::make_shared<std::atomic<bool>> (false);
  auto* owner= new QObject;
  VaultSearchOptions options;
  options.query= "subgroup";
  options.fuzzy= true;
  bool delivered= false;
  auto task= start_vault_search<int> (owner,
    {source.fileName ().toStdString ()}, options,
    [gate, entered] (tree, url, const tree&, const VaultSearchOptions&, std::vector<int>& out) {
      *entered= true;
      gate->acquire ();
      out.push_back (1);
    }, [] (const VaultSearchProgress&) {},
    [&] (std::vector<int>, const VaultSearchProgress&, bool) { delivered= true; });
  QTRY_VERIFY_WITH_TIMEOUT (entered->load (), 5000);
  delete owner;
  bool cancelled= task->cancelled.load ();
  gate->release ();
  QVERIFY (vault_search_workers ().waitForDone (5000));
  QVERIFY (cancelled);
  QVERIFY (!delivered);
}

void TestVaultSearch::rawPrefilterMatchesAcrossReadBoundary () {
  QTemporaryFile source;
  QByteArray text (256 * 1024 - 3, ' ');
  text += "Subgroup";
  temporarySource (source, text);
  VaultRawSearchPrefilter filter ("subgroup", true, false);
  QVERIFY (filter.fileMayMatch (source.fileName ()));
}

QTEST_MAIN(TestVaultSearch)
#include "vault_search_test.moc"

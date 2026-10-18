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
#include "ATHENA/Data/enunciation_model.hpp"
#include "convert.hpp"
#include "Qt/QTMVaultSearch.hpp"
#include "Qt/QTMVaultSearchWorker.hpp"
#include "Qt/QTMVaultPreviewBuilder.hpp"
#include "ATHENA/Data/node_reference.hpp"
#include "utf8_edit.hpp"
#include "Data/Convert/Xml/athena_document_xml.hpp"
#include "Data/Convert/Xml/document_file_codec.hpp"
#include "Data/Convert/Xml/document_upgrade_file.hpp"
#include "Subsystems/RAG/rag_index.hpp"
#include "node_metadata.hpp"
#include "math_tree_v3.hpp"
#include "boxes.hpp"
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
#include <algorithm>
#include <atomic>
#include <thread>

bool headless_mode= true;

static QByteArray
currentDocumentBytes (const char* text) {
  const tree source (DOCUMENT,
    compound ("style", tree (TUPLE, "generic")),
    compound ("body", tree (DOCUMENT, string (text))));
  const std::string bytes= athena::document::write_xml_v2 (source);
  return QByteArray (bytes.data (), static_cast<qsizetype> (bytes.size ()));
}

class TestVaultSearch: public QObject {
  Q_OBJECT

private slots:
  void initTestCase ();
  void semanticVectorsUseReadOnlyIndex ();
  void semanticSearchUsesStoredVersion ();
  void structuredQueryNativeRoundTrip ();
  void structuredQueryRejectsInvalidTransport ();
  void mixedStructuredQueriesStayIndependent ();
  void sourceSelectionsUsePersistentObjects ();
  void sourcePreviewsKeepPropertiesNotIdentities ();
  void sourcePreviewsDispatchNativeFragments ();
  void sourceLookupFollowsExternalRenameAndCancellation ();
  void availableSourceEnunciationsFollowUuidSelections_data ();
  void availableSourceEnunciationsFollowUuidSelections ();
  void findsStructuredMathematicalExpressions ();
  void nativeV3StructuredSearchHonorsPolicies ();
  void nativeV3SearchExcludesMathDescriptorsFromTextAndFuzzy ();
  void emptyNativeSearchPatternDoesNotLoopOrIndexMissingChildren ();
  void filtersCanonicalAndLegacyEnunciations ();
  void keepsConcurrentSearchOptionsIndependent ();
  void fuzzyInsertion ();
  void fuzzySubstitution ();
  void shortQueryIsExactOnly ();
  void exactPrecedesAndDoesNotOverlapFuzzy ();
  void caseInsensitiveExactMatch ();
  void unicodeOffsetsMapToUtf8Bytes ();
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

void TestVaultSearch::sourceSelectionsUsePersistentObjects () {
  using namespace athena::node;
  tree theorem (athena::enunciation::label (), tree (DOCUMENT, "Body"));
  metadata properties;
  properties.id= new_id ();
  properties.properties["kind"]= athena::node::property (std::string ("theorem"));
  properties.properties["numbered"]= athena::node::property (true);
  properties.properties["name"]= athena::node::property (rich_text {tree ("Structured name")});
  set (theorem, properties);
  tree paragraph ("Second object");
  metadata second; second.id= new_id (); set (paragraph, second);
  tree body (DOCUMENT, theorem, paragraph);
  metadata root; root.id= new_id (); set (body, root);
  auto targets= vault_source_targets (body);
  QCOMPARE (targets.size (), std::size_t (3));
  QVERIFY (targets[1].title.contains ("Structured name"));
  QCOMPARE (targets[1].where, path (0));
  QString error;
  std::vector<VaultSourceTarget> selected;
  const QString a= QString::fromStdString (properties.id), b= QString::fromStdString (second.id);
  QVERIFY (vault_source_selection (body, {b, a, b}, selected, error));
  QCOMPARE (selected.size (), std::size_t (2));
  QCOMPARE (selected[0].uuid, b);
  QCOMPARE (selected[1].uuid, a);
  QVERIFY (!vault_source_selection (body, {QString::fromStdString (root.id), a}, selected, error));
  QVERIFY (!vault_source_selection (body, {QString::fromStdString (new_id ())}, selected, error));
  body << copy (theorem);
  QVERIFY (!vault_source_selection (body, {a}, selected, error));
  QVERIFY (error.contains ("duplicated"));
}

void TestVaultSearch::sourcePreviewsKeepPropertiesNotIdentities () {
  using namespace athena::node;
  tree source (athena::enunciation::label (), tree (DOCUMENT, "Body"));
  metadata properties; properties.id= new_id ();
  properties.properties["kind"]= athena::node::property (std::string ("theorem"));
  properties.properties["numbered"]= athena::node::property (true);
  properties.properties["name"]= athena::node::property (rich_text {tree ("")});
  property::dictionary bindings;
  bindings["role"]= athena::node::property (new_id ());
  properties.properties["athena:artifact-bindings"]= athena::node::property (bindings);
  set (source, properties);
  tree rebased= rebase_preview_images (source, url_system ("/tmp"));
  QVERIFY (equal_metadata (source, rebased));
  tree preview= athena::node_reference::presentation_copy (rebased);
  QVERIFY (id (preview).empty ());
  QVERIFY (athena::enunciation::is_canonical (preview));
  QVERIFY (get (preview)->properties.count ("athena:artifact-bindings") == 0);
  tree attached= attach_dip (preview, path (0));
  QVERIFY (athena::enunciation::is_canonical (attached));
  QVERIFY (equal_metadata (preview, attached));
  QVERIFY (id (source) == properties.id);
}

void TestVaultSearch::sourceLookupFollowsExternalRenameAndCancellation () {
  namespace fs= std::filesystem;
  using namespace athena;
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  struct CloseVault { ~CloseVault () { if (vault_active ()) vault_close (); } } close;
  const fs::path root (temporary.path ().toStdString ());
  AthenaVaultfileInfo info;
  info.node_model_version= 1;
  std::string error;
  QVERIFY2 (athena_vaultfile_write (root, info, error), error.c_str ());
  tree paragraph ("Selected source");
  node::metadata metadata; metadata.id= node::new_id ();
  node::set (paragraph, metadata);
  tree document (DOCUMENT, compound ("body", tree (DOCUMENT, paragraph)));
  const auto xml= document::write_xml_v2 (document);
  { std::ofstream out (root / "Original.ath"); out << xml; }
  QCOMPARE (vault_load (url_system (from_qstring (temporary.path ())),
                        "Source lookup", info.map_path.c_str (), "ns.sqlite"), string (""));
  const QString id= QString::fromStdString (metadata.id);
  node_location::snapshot answer;
  QString diagnostic;
  int callbacks= 0;
  auto completed= [&] (auto result, QString failure) {
    answer= result; diagnostic= failure; ++callbacks;
  };
  vault_resolve_source_ids (this, {id}, completed);
  QTRY_COMPARE_WITH_TIMEOUT (callbacks, 1, 10000);
  QVERIFY2 (diagnostic.isEmpty (), qPrintable (diagnostic));
  QCOMPARE (answer->items.front ().candidates.front ().file, std::string ("Original.ath"));
  fs::rename (root / "Original.ath", root / "Renamed.ath");
  vault_resolve_source_ids (this, {id}, completed);
  QTRY_COMPARE_WITH_TIMEOUT (callbacks, 2, 10000);
  QVERIFY2 (diagnostic.isEmpty (), qPrintable (diagnostic));
  QCOMPARE (answer->items.front ().candidates.front ().file, std::string ("Renamed.ath"));
  QCOMPARE (vault_source_preview (*answer)[0], paragraph);
  auto* owner= new QObject;
  vault_resolve_source_ids (owner, {id}, completed);
  delete owner;
  QTest::qWait (80);
  QCOMPARE (callbacks, 2);
  vault_close ();
  vault_resolve_source_ids (this, {id}, completed);
  QTRY_COMPARE_WITH_TIMEOUT (callbacks, 3, 1000);
  QVERIFY (!diagnostic.isEmpty ());
}

void TestVaultSearch::sourcePreviewsDispatchNativeFragments () {
  using namespace athena;
  node_location::result located;
  for (auto version: {document::xml_format_version::v2, document::xml_format_version::v3}) {
    tree fragment= version == document::xml_format_version::v3 ?
      tree (MATH_SCRIPTS, "x", "", "", "i", "2") : tree (FRAC, "x", "y");
    node::metadata meta;
    meta.id= node::new_id ();
    meta.properties["test:enabled"]= node::property (true);
    if (version == document::xml_format_version::v3)
      meta.properties["math:scripts-policy"]= node::property (std::string ("r"));
    node::set (fragment, meta);
    node_location::item item;
    item.state= node_location::status::resolved;
    item.source_directory= "/tmp";
    item.fragment_xml= document::encode_native_xml_bytes (fragment, version, document::xml_kind::fragment);
    located.items.push_back (item);
  }
  const tree preview= vault_source_preview (located);
  QCOMPARE (N(preview), 2);
  for (int i=0; i<2; ++i) {
    const auto decoded= document::decode_native_xml_bytes (
      located.items[i].fragment_xml, document::xml_kind::fragment);
    QCOMPARE (document::encode_native_xml_bytes (preview[i], decoded.version, document::xml_kind::fragment),
              located.items[i].fragment_xml);
  }
  const std::vector<std::string> invalid {
    "<a v=\"3\" k=\"t\"><n", "<a v=\"99\" k=\"t\"><t>x</t></a>",
    document::write_xml_v3 (tree (DOCUMENT, compound ("body", tree (DOCUMENT, "x"))))};
  for (const auto& bytes: invalid) {
    located.items[1].fragment_xml= bytes;
    QVERIFY_EXCEPTION_THROWN (vault_source_preview (located), document::codec_exception);
    std::atomic<bool> cancelled {false};
    auto resolve= [] (const std::vector<std::string>&, const std::vector<std::string>&) {
      return node_location::snapshot ();
    };
    const auto available= collect_available_source_enunciations (
      std::make_shared<const std::string> (bytes), "invalid.ath", resolve, cancelled);
    QVERIFY (available.entries.empty ());
    QCOMPARE (available.warnings.size (), qsizetype (1));
    QVERIFY (available.warnings[0].startsWith ("invalid.ath: "));
  }
}

void TestVaultSearch::availableSourceEnunciationsFollowUuidSelections_data () {
  QTest::addColumn<int> ("sourceVersion");
  QTest::addColumn<int> ("remoteVersion");
  QTest::newRow ("v2-v2") << 2 << 2;
  QTest::newRow ("v2-v3") << 2 << 3;
  QTest::newRow ("v3-v2") << 3 << 2;
  QTest::newRow ("v3-v3") << 3 << 3;
}

void TestVaultSearch::availableSourceEnunciationsFollowUuidSelections () {
  using namespace athena;
  QFETCH (int, sourceVersion);
  QFETCH (int, remoteVersion);
  const auto source_format= sourceVersion == 3 ? document::xml_format_version::v3 : document::xml_format_version::v2;
  const auto remote_format= remoteVersion == 3 ? document::xml_format_version::v3 : document::xml_format_version::v2;
  const std::string local_id= node::new_id (), remote_id= node::new_id (),
                    missing_id= node::new_id ();
  auto enunciation= [] (const std::string& id, const char* text) {
    tree value (athena::enunciation::label (), tree (DOCUMENT, text));
    node::metadata metadata; metadata.id= id;
    metadata.properties["kind"]= node::property (std::string ("theorem"));
    metadata.properties["numbered"]= node::property (true);
    metadata.properties["name"]= node::property (node::rich_text {tree ("")});
    node::set (value, metadata);
    return value;
  };
  auto reference= [] (const std::string& id) {
    return tree (TRANSCLUDE, tree (TUPLE, string (id.data (), id.size ())));
  };
  tree local= enunciation (local_id, "Unsaved local theorem");
  tree remote= enunciation (remote_id, "Remote theorem");
  remote[0] << reference (remote_id);
  if (remoteVersion == 3) {
    tree scripts (MATH_SCRIPTS, "x", "", "", "i", "2");
    node::metadata meta;
    meta.properties["math:scripts-policy"]= node::property (std::string ("r"));
    node::set (scripts, meta);
    remote[0] << scripts;
  }
  tree body (DOCUMENT, local, reference (remote_id), reference (missing_id));
  auto snapshot= std::make_shared<const std::string> (
    document::encode_native_xml_bytes (body, source_format, document::xml_kind::fragment));
  int calls= 0;
  auto resolve= [&] (const std::vector<std::string>& ids,
                     const std::vector<std::string>& ancestry) -> node_location::snapshot {
    ++calls;
    auto answer= std::make_shared<node_location::result> ();
    for (const auto& id: ids) {
      node_location::item item; item.id= id;
      if (std::find (ancestry.begin (), ancestry.end (), id) != ancestry.end ()) {
        item.state= node_location::status::cycle;
        item.diagnostic= "cycle";
      }
      else if (id == remote_id) {
        item.state= node_location::status::resolved;
        node_location::location location; location.file= "Remote.ath";
        item.candidates.push_back (location);
        item.fragment_xml= document::encode_native_xml_bytes (remote, remote_format, document::xml_kind::fragment);
      }
      else { item.state= node_location::status::missing; item.diagnostic= "missing"; }
      answer->items.push_back (std::move (item));
    }
    return answer;
  };
  std::atomic<bool> cancelled {false};
  const auto found= collect_available_source_enunciations (
    snapshot, "Local.ath", resolve, cancelled);
  QCOMPARE (found.entries.size (), std::size_t (2));
  QCOMPARE (found.entries[0].source_uuid, QString::fromStdString (local_id));
  QCOMPARE (found.entries[1].source_uuid, QString::fromStdString (remote_id));
  QCOMPARE (document::inspect_xml_version (*found.entries[0].source_body), source_format);
  QCOMPARE (document::inspect_xml_version (*found.entries[1].source_body), remote_format);
  const auto retained= document::decode_native_xml_bytes (
    *found.entries[1].source_body, document::xml_kind::fragment);
  QCOMPARE (document::encode_native_xml_bytes (retained.document, retained.version, document::xml_kind::fragment),
            document::encode_native_xml_bytes (remote, remote_format, document::xml_kind::fragment));
  QVERIFY (found.entries[0].title.contains ("Unsaved local theorem"));
  QCOMPARE (found.warnings.size (), qsizetype (2));
  QCOMPARE (calls, 3);
  cancelled= true;
  QVERIFY (collect_available_source_enunciations (
    snapshot, "Local.ath", resolve, cancelled).entries.empty ());
  QCOMPARE (calls, 3);
}

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
    QVERIFY (last->isChecked ());
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
    QCOMPARE (list->currentRow (), 2);
    QTest::keyClick (input, Qt::Key_Down);
    QCOMPARE (list->currentRow (), 0);
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
std::shared_ptr<const std::string> availableSnapshot (tree body,
    athena::document::xml_format_version version= athena::document::xml_format_version::v2) {
  return std::make_shared<const std::string> (athena::document::encode_native_xml_bytes (
    body, version, athena::document::xml_kind::fragment));
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
    {"B.ath", availableSnapshot (b)},
    {"C.ath", availableSnapshot (c, athena::document::xml_format_version::v3)}};
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
    return athena::document::decode_native_xml_bytes (*bytes, athena::document::xml_kind::fragment);
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
  const auto decoded= athena::document::decode_native_xml_bytes (*nested.source_body,
    athena::document::xml_kind::fragment);
  QCOMPARE (decoded.version, athena::document::xml_format_version::v3);
  tree source= decoded.document;
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
  auto load= [&] (const QString&) {
    ++reads;
    return athena::document::native_xml_read_result {
      athena::document::xml_format_version::v2, tree (DOCUMENT, "old disk content")};
  };
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
TestVaultSearch::structuredQueryNativeRoundTrip () {
  using namespace athena::document;
  for (const auto version: {xml_format_version::v2, xml_format_version::v3}) {
    tree query= version == xml_format_version::v3 ?
      tree (MATH_SCRIPTS, "x", "", "", "i", "2") :
      tree (CONCAT, "x", tree (RSUP, "2"));
    athena::node::metadata meta;
    meta.id= athena::node::new_id ();
    meta.properties["test:enabled"]= athena::node::property (true);
    meta.properties["test:title"]= athena::node::property (
      athena::node::rich_text {tree (CONCAT, "typed", " title")});
    if (version == xml_format_version::v3)
      meta.properties["math:scripts-policy"]= athena::node::property (std::string ("r"));
    athena::node::set (query, meta);
    VaultSearchOptions options;
    vault_search_set_query (options, query, version);
    QVERIFY (options.queryFormat.has_value ());
    QCOMPARE (*options.queryFormat, version);
    QCOMPARE (inspect_xml_version (options.serializedQuery), version);
    const auto decoded= vault_search_decode_query (options);
    QCOMPARE (encode_native_xml_bytes (decoded, version, xml_kind::fragment),
              options.serializedQuery);
    const auto* preserved= athena::node::get (decoded);
    QVERIFY (preserved != nullptr);
    QCOMPARE (preserved->id, meta.id);
    QVERIFY (std::get<bool> (preserved->properties.at ("test:enabled").data));
    QVERIFY (std::get<athena::node::rich_text> (
      preserved->properties.at ("test:title").data).content == tree (CONCAT, "typed", " title"));

    tree atom ("typed text");
    // Keep only general properties on a text node.
    meta.properties.erase ("math:scripts-policy");
    athena::node::set (atom, meta);
    vault_search_set_query (options, atom, version);
    QVERIFY (!options.serializedQuery.empty ());
    QCOMPARE (encode_native_xml_bytes (vault_search_decode_query (options), version,
                                      xml_kind::fragment), options.serializedQuery);
    const auto before= options.serializedQuery;
    QVERIFY_EXCEPTION_THROWN (vault_search_set_query (options, atom, xml_format_version::v1),
                              codec_exception);
    QCOMPARE (options.serializedQuery, before);
    QCOMPARE (*options.queryFormat, version);

    vault_search_set_query (options, tree ("plain text"), version);
    QVERIFY (options.serializedQuery.empty ());
    QVERIFY (!options.queryFormat);
    QVERIFY (vault_search_decode_query (options) == tree ("plain text"));
  }
}

void
TestVaultSearch::structuredQueryRejectsInvalidTransport () {
  using namespace athena::document;
  VaultSearchOptions good;
  vault_search_set_query (good, tree (FRAC, "x", "y"), xml_format_version::v3);
  std::vector<VaultSearchOptions> rejected;
  auto malformed= good;
  malformed.serializedQuery.resize (malformed.serializedQuery.size () / 2);
  rejected.push_back (malformed);
  auto mismatch= good;
  mismatch.queryFormat= xml_format_version::v2;
  rejected.push_back (mismatch);
  auto missing_format= good;
  missing_format.queryFormat.reset ();
  rejected.push_back (missing_format);
  auto missing_payload= good;
  missing_payload.serializedQuery.clear ();
  rejected.push_back (missing_payload);
  auto document= good;
  document.serializedQuery= write_xml_v3 (tree (DOCUMENT, compound ("body", tree (DOCUMENT, "x"))));
  rejected.push_back (document);
  auto old_fragment= good;
  old_fragment.serializedQuery= write_xml_v2 (tree (FRAC, "x", "y"), xml_kind::fragment);
  rejected.push_back (old_fragment);
  auto unsupported= good;
  unsupported.serializedQuery= "<a v=\"99\" k=\"t\"><t>x</t></a>";
  rejected.push_back (unsupported);
  for (auto options: rejected) {
    options.query= "must not fall back to this text";
    std::string reason;
    try { (void) vault_search_decode_query (options); }
    catch (const codec_exception& error) { reason= error.what (); }
    QVERIFY (!reason.empty ());
    if (options.serializedQuery == mismatch.serializedQuery &&
        options.queryFormat == mismatch.queryFormat)
      QCOMPARE (reason, std::string ("Search query fragment version does not match its source format"));

    QObject owner;
    bool semantic_done= false;
    vault_search_semantic_candidates (&owner, {}, options,
      std::make_shared<VaultSearchControl> (),
      [&] (std::shared_ptr<const VaultSemanticCandidates> candidates, QString error) {
        QVERIFY (!candidates);
        QCOMPARE (error, QString::fromStdString (reason));
        semantic_done= true;
      });
    QVERIFY (semantic_done);

    bool done= false;
    std::atomic<int> scans {0};
    auto control= start_vault_search<int> (&owner, {}, options,
      [&] (tree, url, const tree&, const VaultSearchOptions&, std::vector<int>&) {
        ++scans;
      }, [] (const VaultSearchProgress&) {},
      [&] (std::vector<int> hits, const VaultSearchProgress& progress, bool cancelled) {
        QVERIFY (cancelled);
        QVERIFY (hits.empty ());
        QCOMPARE (progress.diagnostic, QString::fromStdString (reason));
        done= true;
      });
    QTRY_VERIFY_WITH_TIMEOUT (done, 5000);
    QCOMPARE (scans.load (), 0);
    QVERIFY (control->cancelled.load ());
  }
}

void
TestVaultSearch::mixedStructuredQueriesStayIndependent () {
  using namespace athena::document;
  QTemporaryFile source;
  QVERIFY (source.open ());
  const auto bytes= currentDocumentBytes ("search source");
  QCOMPARE (source.write (bytes), qint64 (bytes.size ()));
  QVERIFY (source.flush ());
  QObject owner;
  int completed= 0;
  std::vector<std::shared_ptr<VaultSearchControl>> controls;
  for (int i=0; i<4; ++i) {
    const auto version= i % 2 ? xml_format_version::v3 : xml_format_version::v2;
    tree query (FRAC, "x", as_string (i));
    athena::node::metadata meta;
    meta.properties["test:enabled"]= athena::node::property (i % 2 == 0);
    athena::node::set (query, std::move (meta));
    VaultSearchOptions options;
    vault_search_set_query (options, query, version);
    const auto expected= options.serializedQuery;
    controls.push_back (start_vault_search<std::string> (&owner,
      {source.fileName ().toStdString ()}, options,
      [] (tree, url, const tree& query, const VaultSearchOptions& options,
          std::vector<std::string>& out) {
        out.push_back (encode_native_xml_bytes (query, *options.queryFormat, xml_kind::fragment));
      }, [] (const VaultSearchProgress&) {},
      [&, expected] (std::vector<std::string> hits, const VaultSearchProgress& progress, bool cancelled) {
        QVERIFY (!cancelled);
        QVERIFY (progress.diagnostic.isEmpty ());
        QCOMPARE (hits.size (), std::size_t (1));
        QCOMPARE (hits[0], expected);
        ++completed;
      }));
  }
  QTRY_COMPARE_WITH_TIMEOUT (completed, 4, 5000);
}

void
TestVaultSearch::semanticSearchUsesStoredVersion () {
  using namespace athena::document;
  for (int fixture= 0; fixture<4; ++fixture) {
    const auto version= fixture == 0 ? xml_format_version::v1 :
      fixture == 1 ? xml_format_version::v2 : xml_format_version::v3;
    tree content ("snapshot");
    if (fixture == 3) {
      content= tree (MATH_SCRIPTS, "x", "", "", "i", "2");
      athena::node::metadata meta;
      meta.properties["math:scripts-policy"]= athena::node::property (std::string ("r"));
      athena::node::set (content, std::move (meta));
    }
    const tree document (DOCUMENT, compound ("body", tree (DOCUMENT, content)));
    const auto bytes= encode_native_xml_bytes (document, version);
    QTemporaryFile source;
    QVERIFY (source.open ());
    QCOMPARE (source.write (bytes.data (), qint64 (bytes.size ())), qint64 (bytes.size ()));
    QVERIFY (source.flush ());
    const url file= url_system (from_qstring (source.fileName ()));
    const auto revision= semantic_document_fingerprint (document, version);
    auto candidates= std::make_shared<VaultSemanticCandidates> ();
    (*candidates)[source.fileName ()].push_back ({"0", revision, 1.0});
    VaultSearchOptions options;
    options.semanticCandidates= candidates;
    xml_format_version actual_version;
    const tree body= vault_search_read_body (source.fileName (), true, &actual_version);
    QCOMPARE (actual_version, version);
    QVERIFY (body == tree (DOCUMENT, content));
    if (fixture == 3) {
      const auto* meta= athena::node::get (body[0]);
      QVERIFY (meta != nullptr);
      QCOMPARE (std::get<std::string> (meta->properties.at ("math:scripts-policy").data),
                std::string ("r"));
    }
    std::vector<VaultContentMatch> matches;
    vault_search_semantic_matches (body, file, options, matches);
    QCOMPARE (matches.size (), std::size_t (1));
    QCOMPARE (options.staleCandidates->load (), 0);

    // A metadata-free v3 tree is indistinguishable by shape, but not by revision.
    if (fixture == 2) {
      const auto old_revision= semantic_document_fingerprint (document, xml_format_version::v2);
      QVERIFY (revision != old_revision);
      (*candidates)[source.fileName ()][0].semanticRevision= old_revision;
    }
    else (*candidates)[source.fileName ()][0].semanticRevision= "stale";
    matches.clear ();
    vault_search_semantic_matches (body, file, options, matches);
    QVERIFY (matches.empty ());
    QCOMPARE (options.staleCandidates->load (), 1);
  }
}

void
TestVaultSearch::semanticVectorsUseReadOnlyIndex () {
  using namespace athena::rag;
  namespace fs= std::filesystem;
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  const fs::path root (temporary.path ().toStdString ());
  const auto source= currentDocumentBytes ("A geometric proof about triangles and angles.");
  {
    std::ofstream file (root / "Geometry.ath", std::ios::binary);
    file.write (source.constData (), source.size ());
    QVERIFY (file.good ());
  }
  RagConfig config;
  config.vault_root= root;
  config.db_path= root / "rag.sqlite";
  config.load_embedding_model= false;
  config.progress= false;
  RagPreparedDocument prepared;
  {
    RagIndex writer;
    QVERIFY2 (writer.open (config), writer.status ().last_error.c_str ());
    QVERIFY2 (writer.prepare_document ("Geometry.ath", "", "test-space", prepared),
              writer.status ().last_error.c_str ());
    QVERIFY (!prepared.missing_embedding_indices.empty ());
    std::vector<std::vector<float>> vectors (
      prepared.missing_embedding_indices.size (), std::vector<float>{1.0f, 0.0f});
    QVERIFY2 (writer.commit_document (prepared, vectors), writer.status ().last_error.c_str ());
  }
  QFile database (QString::fromStdString (config.db_path.string ()));
  QVERIFY (database.open (QIODevice::ReadOnly));
  const auto before= database.readAll ();
  database.close ();
  const auto modified= fs::last_write_time (config.db_path);
  config.read_only_query= true;
  config.force_reindex= true; // Read-only opening must ignore writer-only flags.
  config.load_embedding_model= true;
  config.embedding_model= root / "must-not-load.gguf";
  {
    RagIndex reader;
    QVERIFY2 (reader.open (config), reader.status ().last_error.c_str ());
    QVERIFY (!reader.status ().embeddings_enabled);
    QVERIFY (reader.status ().embedding_warning.empty ());
    // The external query embedding can match even with no lexical candidates.
    QVERIFY (reader.search ("nonlexicalquery", 10).empty ());
    const auto hits= reader.search_vector ({2.0f, 0.0f}, "test-space", {"Geometry.ath"});
    QVERIFY2 (!hits.empty (), reader.status ().last_error.c_str ());
    QCOMPARE (hits.size (), prepared.missing_embedding_indices.size ());
    const std::string revision= athena::document::storage_bytes_fingerprint (
      std::string (source.constData (), std::size_t (source.size ())));
    for (const auto& hit: hits) {
      QCOMPARE (hit.rel_path, std::string ("Geometry.ath"));
      QCOMPARE (hit.storage_revision, revision);
      QCOMPARE (hit.vector_similarity, 1.0);
      QCOMPARE (hit.score, hit.vector_similarity);
      const auto original= std::find_if (prepared.chunks.begin (), prepared.chunks.end (),
        [&] (const RagPreparedChunk& chunk) { return chunk.chunk.chunk_id == hit.chunk_id; });
      QVERIFY (original != prepared.chunks.end ());
      QCOMPARE (hit.tree_path, original->chunk.tree_path);
    }
    QVERIFY (reader.search_vector ({1.0f, 0.0f}, "test-space", {"Excluded.ath"}).empty ());
    QVERIFY (reader.search_vector ({1.0f, 0.0f}, "incompatible-space", {"Geometry.ath"}).empty ());
    QVERIFY (reader.status ().last_error.find ("embedding space") != std::string::npos);
    QVERIFY (!reader.delete_document ("Geometry.ath"));
  }
  QVERIFY (database.open (QIODevice::ReadOnly));
  QCOMPARE (database.readAll (), before);
  QVERIFY (fs::last_write_time (config.db_path) == modified);
  config.db_path= root / "missing" / "rag.sqlite";
  RagIndex missing;
  QVERIFY (!missing.open (config));
  QVERIFY (!missing.status ().last_error.empty ());
  QVERIFY (!fs::exists (config.db_path.parent_path ()));
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
    std::ofstream file (root / name, std::ios::binary);
    const auto bytes= currentDocumentBytes ("hello");
    file.write (bytes.constData (), bytes.size ());
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

void TestVaultSearch::nativeV3StructuredSearchHonorsPolicies () {
  using namespace athena;
  struct SourceAccess {
    int previous= set_access_mode (DRD_ACCESS_SOURCE);
    ~SourceAccess () { set_access_mode (previous); }
  } sourceAccess;

  auto count= [] (tree source, tree query, bool fuzzy= true) {
    std::vector<VaultContentMatch> matches;
    append_content_matches (matches, compound ("math", source),
                            compound ("math", query), path (), 200,
                            false, fuzzy);
    return matches.size ();
  };
  tree script= math_v3::scripts ("x", "", "", "i", "2", 'r');
  tree same_script= math_v3::scripts ("x", "", "", "i", "2", 'r');
  // Source UUIDs do not constrain an otherwise identical structured query.
  auto source_meta= *node::get (same_script);
  source_meta.id= node::new_id ();
  node::set (script, source_meta);
  QCOMPARE (count (script, same_script), std::size_t (1));
  QCOMPARE (count (script, math_v3::scripts ("x", "", "", "i", "2", 'o')),
            std::size_t (0));
  QCOMPARE (count (script, math_v3::scripts ("x", "", "", "i", "3", 'r')),
            std::size_t (0));
  QCOMPARE (count (math_v3::scripts ("x", "j", "", "i", "2", 'r'),
                   same_script), std::size_t (0));

  tree delimiter= math_v3::delimited ("(", ")", 'a', "x");
  QCOMPARE (count (delimiter, math_v3::delimited ("(", ")", 'a', "x")),
            std::size_t (1));
  QCOMPARE (count (delimiter, math_v3::delimited ("(", "]", 'a', "x")),
            std::size_t (0));
  QCOMPARE (count (delimiter, math_v3::delimited ("(", ")", 'f', "x")),
            std::size_t (0));

  auto grid= [] (const char* kind, math_v3::row_numbering policy,
                 const tree& tag) {
    tree row (ROW, tree (CELL, "x"));
    math_v3::set_row_numbering (row, policy);
    math_v3::set_row_tag (row, tag);
    tree result (MATH_GRID, row);
    node::metadata meta;
    meta.properties["math:grid-kind"]= node::property (std::string (kind));
    node::set (result, meta);
    return result;
  };
  tree source_grid= grid ("align", math_v3::row_numbering::unnumbered,
                           tree ("A"));
  tree query_grid= grid ("align", math_v3::row_numbering::unnumbered,
                          tree ("A"));
  auto row_meta= *node::get (source_grid[0]);
  row_meta.id= node::new_id ();
  node::set (source_grid[0], row_meta);
  QCOMPARE (count (source_grid, query_grid), std::size_t (1));
  QCOMPARE (count (source_grid, grid ("eqnarray",
            math_v3::row_numbering::unnumbered, tree ("A"))), std::size_t (0));
  QCOMPARE (count (source_grid, grid ("align",
            math_v3::row_numbering::numbered, tree ("A"))), std::size_t (0));
  QCOMPARE (count (source_grid, grid ("align",
            math_v3::row_numbering::unnumbered, tree ("B"))), std::size_t (0));
  tree inherited= grid ("align", math_v3::row_numbering::inherit, tree ("A"));
  QCOMPARE (count (source_grid, inherited), std::size_t (0));
  tree extra_cell= copy (source_grid);
  extra_cell[0] << tree (CELL, "y");
  QCOMPARE (count (extra_cell, query_grid), std::size_t (0));
  tree empty_cell_query= copy (query_grid);
  empty_cell_query[0][0]= tree (CELL, "");
  QCOMPARE (count (source_grid, empty_cell_query), std::size_t (0));
  tree empty_cell_source= copy (source_grid);
  empty_cell_source[0][0]= tree (CELL, "");
  QCOMPARE (count (empty_cell_source, empty_cell_query), std::size_t (1));

  // The Vault math route adds an outer select-region to an expression query.
  // Keep an already nested selector active while enforcing the complete grid
  // shape; a partial-column grid must not yield a selected cell.
  tree selection_source= copy (source_grid);
  selection_source[0] << tree (CELL, "y");
  tree selected_grid= copy (query_grid);
  selected_grid[0] << tree (CELL, "y");
  selected_grid[0][0]= tree (CELL, compound ("select-region", "x"));
  std::vector<VaultContentMatch> selections;
  append_content_matches (selections, compound ("math", selection_source),
                          compound ("math", selected_grid), path (),
                          200, false, false);
  QCOMPARE (selections.size (), std::size_t (1));
  tree short_selected_grid= copy (selected_grid);
  short_selected_grid[0]= tree (ROW, selected_grid[0][0]);
  node::copy_metadata (selected_grid[0], short_selected_grid[0]);
  QCOMPARE (N(selection_source[0]), 2);
  QCOMPARE (N(short_selected_grid[0]), 1);
  selections.clear ();
  append_content_matches (selections, compound ("math", selection_source),
                          compound ("math", short_selected_grid), path (),
                          200, false, false);
  QVERIFY (selections.empty ());

  tree selected_delimiter= math_v3::delimited (
    "(", ")", 'a', compound ("select-region", "x"));
  selections.clear ();
  append_content_matches (selections, compound ("math", delimiter),
                          compound ("math", selected_delimiter), path (),
                          200, false, false);
  QCOMPARE (selections.size (), std::size_t (1));
  selected_delimiter= math_v3::delimited (
    "", ")", 'a', compound ("select-region", "x"));
  selections.clear ();
  append_content_matches (selections, compound ("math", delimiter),
                          compound ("math", selected_delimiter), path (),
                          200, false, false);
  QVERIFY (selections.empty ());

  // Explicit metadata on the outer math wrapper must survive the temporary
  // select-region wrapper used by the Vault's mathematical search.
  tree outer= compound ("math", same_script);
  tree constrained= compound ("math", same_script);
  node::metadata marker;
  marker.properties["query:scope"]= node::property (std::string ("proof"));
  node::set (constrained, marker);
  std::vector<VaultContentMatch> matches;
  append_content_matches (matches, outer, constrained, path (), 200, false, true);
  QVERIFY (matches.empty ());
  node::set (outer, marker);
  append_content_matches (matches, outer, constrained, path (), 200, false, true);
  QCOMPARE (matches.size (), std::size_t (1));

  tree word ("properties");
  tree word_query ("properties");
  node::metadata word_meta;
  word_meta.properties["role"]= node::property (std::string ("definition"));
  node::set (word_query, word_meta);
  matches.clear ();
  append_content_matches (matches, word, word_query, path (), 200, false, true);
  QVERIFY (matches.empty ());
  node::set (word, word_meta);
  append_content_matches (matches, word, word_query, path (), 200, false, true);
  QCOMPARE (matches.size (), std::size_t (1));
}

void TestVaultSearch::emptyNativeSearchPatternDoesNotLoopOrIndexMissingChildren () {
  struct SourceAccess {
    int previous= set_access_mode (DRD_ACCESS_SOURCE);
    ~SourceAccess () { set_access_mode (previous); }
  } sourceAccess;
  const tree empty_concat (CONCAT);
  const auto from_atom= search (tree ("abc"), empty_concat,
                                path (), false, 10);
  QCOMPARE (N(from_atom), 0);
  const auto from_concat= search (tree (CONCAT, "a", "b"), empty_concat,
                                  path (), false, 10);
  QCOMPARE (N(from_concat), 0);
  // A zero-length atomic query cannot repeatedly revisit the same byte.
  const auto empty_atom= search (tree ("abc"), tree (""),
                                 path (), false, 10);
  QVERIFY (N(empty_atom) <= 20);
}

void TestVaultSearch::nativeV3SearchExcludesMathDescriptorsFromTextAndFuzzy () {
  struct SourceAccess {
    int previous= set_access_mode (DRD_ACCESS_SOURCE);
    ~SourceAccess () { set_access_mode (previous); }
  } sourceAccess;
  tree manual= athena::math_v3::delimited ("(", ")", 'm', "x", "1024", "1024");
  tree source= compound ("math", manual);
  std::vector<VaultContentMatch> matches;
  append_content_matches (matches, source, tree ("1024"), path (),
                          200, false, false);
  QVERIFY (matches.empty ());
  append_content_matches (matches, source, tree ("1023"), path (),
                          200, false, true);
  QVERIFY (matches.empty ());
  append_content_matches (matches, source, tree ("x"), path (),
                          200, false, false);
  QCOMPARE (matches.size (), std::size_t (1));

  matches.clear ();
  tree visible= compound ("math", athena::math_v3::delimited (
    "(", ")", 'm', "1024", "1024", "1024"));
  append_content_matches (matches, visible, tree ("1023"), path (),
                          200, false, true);
  QVERIFY (!matches.empty ());
  for (const auto& match: matches)
    QVERIFY (match.start == path (0, 5, 0) || match.start == path (0, 5, 1) ||
             match.start == path (0, 5, 2) || match.start == path (0, 5, 3));
}

void TestVaultSearch::filtersCanonicalAndLegacyEnunciations () {
  struct SourceAccess {
    int previous= set_access_mode (DRD_ACCESS_SOURCE);
    ~SourceAccess () { set_access_mode (previous); }
  } sourceAccess;
  tree legacy= compound ("theorem", tree (DOCUMENT, "Needle"));
  tree canonical= athena::enunciation::convert_detached_source (legacy).source;
  tree other= compound ("definition", tree (DOCUMENT, "Needle"));
  tree source (DOCUMENT, legacy, canonical, other);
  std::vector<VaultContentMatch> matches;
  collect_enunciation_matches (matches, source, tree ("Needle"), "theorem",
                               path (), 200, false, false);
  QCOMPARE (matches.size (), std::size_t (2));
  QCOMPARE (matches[0].start, path (0, 0, 0, 0));
  QCOMPARE (matches[1].start, path (1, 0, 0, 0));
  matches.clear ();
  collect_enunciation_matches (matches, source, tree ("Needle"), "definition",
                               path (), 200, false, false);
  QCOMPARE (matches.size (), std::size_t (1));
  QCOMPARE (matches[0].start, path (2, 0, 0, 0));
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
TestVaultSearch::unicodeOffsetsMapToUtf8Bytes () {
  QString text= QString::fromUtf8 ("数学知识组织 and more");
  std::vector<VaultContentMatch> matches=
    matchesFor (text, QString::fromUtf8 ("数学知织"));
  QVERIFY (!matches.empty ());
  QVERIFY (!matches[0].exact);

  string source= from_qstring (text);
  int start= last_item (matches[0].start);
  int end= last_item (matches[0].end);
  QCOMPARE (to_qstring (utf8_byte_slice (source, start, end)),
            QString::fromUtf8 ("数学知识"));
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
  temporarySource (first, currentDocumentBytes ("subgroup"));
  temporarySource (second, currentDocumentBytes ("unrelated"));
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
  temporarySource (source, currentDocumentBytes ("subgroup"));
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
